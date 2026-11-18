"""Exercise the TLS client's compiled key-ID PRF path with synthetic keys."""
import argparse
import hashlib
import hmac
import json
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_CODE
from unicorn.riscv_const import (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2,
                                 UC_RISCV_REG_A3, UC_RISCV_REG_A4, UC_RISCV_REG_A5,
                                 UC_RISCV_REG_A6)

from .tls_fragment_probe import SSL
from .tls_output_probe import OutputProbe
from .tls_prf_probe import ALGORITHMS, SECRET, SEED, layouts


class KeyProbe(OutputProbe):
    def __init__(self, path, fixed, require_import=False):
        super().__init__(path)
        self.fixed = fixed
        self.mac = {}
        self.key_inputs = 0
        self.secret_address = self.symbols.get('g_prf_secret')
        self.import_address = self.symbols.get('g_import_key')
        self.types = layouts(path, ('psa_key_attributes_s',))
        self.attributes = None
        assert bool(self.secret_address) == fixed
        assert bool(self.import_address) == ('c3tout_prf_import' in self.symbols)
        assert not require_import or self.import_address, 'ELF lacks fixed PRF import storage'
        if fixed:
            with path.open('rb') as stream:
                symbols = ELFFile(stream).get_section_by_name('.symtab')
                for name in ('g_prf_secret', 'g_import_key'):
                    if name not in self.symbols:
                        continue
                    symbol, = symbols.get_symbol_by_name(name)
                    assert symbol['st_size'] == 66
                if self.import_address:
                    assert abs(self.import_address - self.secret_address) >= 66
        for name in ('mbedtls_mutex_lock', 'mbedtls_mutex_unlock',
                     'c3bport_enter', 'c3bport_leave'):
            self.hook(self.symbols[name], lambda *_: self.finish(0))
        self.hook(self.symbols['c3bport_task'], lambda *_: self.finish(1))
        self.hook(self.symbols['strlen'], self.strlen)
        for name, callback in (('psa_key_derivation_start_hmac', self.mac_start),
                               ('psa_mac_update', self.mac_update),
                               ('psa_mac_sign_finish', self.mac_finish),
                               ('psa_mac_abort', self.mac_abort)):
            self.hook(self.symbols[name], callback)
        entry = self.symbols['psa_key_derivation_input_key']
        self.uc.hook_add(UC_HOOK_CODE, self.count_key, begin=entry, end=entry)
        entry = self.symbols['psa_import_key']
        self.uc.hook_add(UC_HOOK_CODE, self.read_attributes, begin=entry, end=entry)
        assert self.run('psa_initialize_key_slots') == 0

    def args(self):
        return tuple(self.uc.reg_read(reg) for reg in (UC_RISCV_REG_A0, UC_RISCV_REG_A1,
                                                       UC_RISCV_REG_A2, UC_RISCV_REG_A3))

    def strlen(self, *_):
        self.finish(bytes(self.uc.mem_read(self.args()[0], 64)).index(0))

    def count_key(self, *_):
        self.key_inputs += 1

    def read_attributes(self, *_):
        if self.attributes is None:
            size = self.types['psa_key_attributes_s']['size']
            self.attributes = bytes(self.uc.mem_read(self.args()[0], size))

    def mac_start(self, *_):
        operation, alg, key, size = self.args()
        assert operation not in self.mac
        if self.fixed:
            assert key == self.secret_address
        else:
            assert self.live[key] == size
        if self.import_address:
            assert self.uc.mem_read(self.import_address, size) == self.uc.mem_read(key, size)
        self.mac[operation] = hmac.new(bytes(self.uc.mem_read(key, size)),
                                       digestmod={0x02000009: 'sha256', 0x0200000A: 'sha384'}[alg])
        self.finish(0)

    def mac_update(self, *_):
        operation, data, size, _ = self.args()
        self.mac[operation].update(bytes(self.uc.mem_read(data, size)))
        self.finish(0)

    def mac_finish(self, *_):
        operation, dest, capacity, length = self.args()
        digest = self.mac.pop(operation).digest()
        assert len(digest) == capacity
        self.uc.mem_write(dest, digest)
        self.uc.mem_write(length, struct.pack('<I', len(digest)))
        self.finish(0)

    def mac_abort(self, *_):
        self.mac.pop(self.args()[0], None)
        self.finish(0)

    def derive(self, alg, key, label, seed, length):
        data = SSL + 0x1000
        self.uc.mem_write(data, key)
        self.uc.mem_write(data + 128, label + b'\0')
        self.uc.mem_write(data + 256, seed)
        self.uc.mem_write(data + 384, b'\xA5' * length)
        for reg, value in ((UC_RISCV_REG_A4, len(seed)), (UC_RISCV_REG_A5, data + 384),
                           (UC_RISCV_REG_A6, length)):
            self.uc.reg_write(reg, value)
        status = self.run('tls_prf_' + alg, data, len(key), data + 128, data + 256)
        assert not self.mac
        return status, bytes(self.uc.mem_read(data + 384, length))

    def import_key(self, value, *, wrong_bits=False, native=False):
        # Start from the exact volatile derive attributes issued by ssl_tls.c.
        attrs, data, handle = SSL + 0xC00, SSL + 0xD00, SSL + 0xE00
        raw = bytearray(self.attributes)
        if wrong_bits:
            offset = self.types['psa_key_attributes_s']['fields']['bits']
            assert raw[offset] == 0
            raw[offset] = 8  # Deliberately contradict the supplied 32-byte synthetic key.
        self.uc.mem_write(attrs, bytes(raw))
        self.uc.mem_write(data, value or b'\0')
        self.uc.mem_write(handle, b'\xA5' * 4)
        name = 'c3tout_prf_import' if self.import_address and not native else 'psa_import_key'
        status = self.run(name, attrs, data, len(value), handle)
        return status, int.from_bytes(self.uc.mem_read(handle, 4), 'little')


def expected(alg, key, label, seed, length):
    # RFC 5246 section 5, evaluated independently of the compiled PRF.
    source = label + seed
    chain = source
    output = bytearray()
    while len(output) < length:
        chain = hmac.digest(key, chain, alg)
        output.extend(hmac.digest(key, chain + source, alg))
    return bytes(output[:length])


def check_import_lifetime(probe, retained):
    value = bytes(range(1, 33))
    before = len(probe.allocations)
    status, key = probe.import_key(value)
    assert status == 0 and key != 0
    address = probe.import_address
    if address:
        assert bytes(probe.uc.mem_read(address, len(value))) == value
        assert probe.import_key(b'x') == (-141, 0)
        assert bytes(probe.uc.mem_read(address, len(value))) == value
        probe.uc.mem_write(address + len(value), b'X' * (66 - len(value)))
    assert probe.run('psa_destroy_key', key) == 0 and probe.live == retained
    assert probe.allocations[before:] == ([] if address else [len(value)])
    if address:
        assert bytes(probe.uc.mem_read(address, 66)) == bytes(66)
        assert probe.import_key(b'x' * 67) == (-141, 0)
        assert len(probe.allocations) == before
    assert probe.import_key(b'') == (-135, 0)

    # Observe the original SDK failing after it has copied the key. Its native
    # cleanup must receive the populated buffer and erase the entire lease.
    wiped = []
    def observe_wipe(*_):
        pointer, size, _, _ = probe.args()
        assert (pointer == address if address else pointer in probe.live)
        wiped.append(bytes(probe.uc.mem_read(pointer, size)))

    entry = probe.symbols['c3tout_wipe']
    hook = probe.uc.hook_add(UC_HOOK_CODE, observe_wipe, begin=entry, end=entry)
    assert probe.import_key(value, wrong_bits=True) == (-135, 0)
    probe.uc.hook_del(hook)
    assert wiped == [value] and probe.live == retained
    if address:
        assert bytes(probe.uc.mem_read(address, 66)) == bytes(66)
        assert probe.uc.mem_read(probe.symbols['gb_import_used'], 1) == b'\0'
    # A different PSA caller uses identical attributes and its native allocator.
    before = len(probe.allocations)
    status, key = probe.import_key(value, native=True)
    assert status == 0 and key != 0 and probe.allocations[before:] == [len(value)]
    assert probe.run('psa_destroy_key', key) == 0 and probe.live == retained


def verify(path, fixed, require_import=False):
    probe = KeyProbe(path, fixed, require_import)
    init_requests = list(probe.allocations)
    warm_count = sum(address is None for address in (probe.secret_address, probe.import_address))
    cases = [(bytes(range(32)), b'master secret', bytes(range(64)), 48),
             (bytes(range(66)), b'extended master secret', None, 48),
             (bytes(range(48)), b'client finished', None, 12),
             (bytes(range(48)), b'key expansion', bytes(range(64)), 256)]
    # A cold volatile-key table can fail before PSA requests key material.
    initial_live = dict(probe.live)
    probe.fail_call = len(init_requests) + 1
    status, output = probe.derive('sha256', *cases[0])
    assert status != 0 and output == b'\xA5' * len(output)
    failed_requests = probe.allocations[len(init_requests):]
    assert len(failed_requests) == 1 and probe.key_inputs == 0 and probe.live == initial_live
    if probe.import_address:
        assert probe.uc.mem_read(probe.symbols['gb_import_used'], 1) == b'\0'
    probe.fail_call = None
    checked = 0
    active = None
    for alg, hash_size in (('sha256', 32), ('sha384', 48)):
        for key, label, seed, length in cases:
            seed = bytes(range(hash_size)) if seed is None else seed
            for _ in range(10):
                before = len(probe.allocations)
                assert probe.derive(alg, key, label, seed, length) == (
                    0, expected(alg, key, label, seed, length))
                if active is None:
                    # The pinned PSA key store can retain its first volatile slot slice.
                    active = dict(probe.live)
                    first_requests = probe.allocations[before:]
                else:
                    assert probe.live == active
                    assert probe.allocations[before:] == [len(key)] * warm_count
                if fixed:
                    assert bytes(probe.uc.mem_read(probe.secret_address, 66)) == bytes(66)
                if probe.import_address:
                    assert bytes(probe.uc.mem_read(probe.import_address, 66)) == bytes(66)
                checked += 1
    assert probe.key_inputs == checked
    # First complete block of each published TLS 1.2 "test label" PRF vector.
    for alg, key_hex, seed_hex, digest_hex in (
            ('sha256', '9bbe436ba940f017b17652849a71db35', 'a0ba9f936cda311827a6f796ffd5198c',
             'e3f229ba727be17b8d122620557cd453c2aab21d07c3d495329b52d4e61edb5a'),
            ('sha384', 'b80b733d6ceefcdc71566ea48e5567df', 'cd665cf6a8447dd6ff8b27555edb7465',
             '7b0c18e9ced410ed1804f2cfa34a336a1c14dffb4900bb5fd7942107e81c83cd'
             'e9ca0faa60be9fe34f82b1233c9146a0')):
        digest = bytes.fromhex(digest_hex)
        assert probe.derive(alg, bytes.fromhex(key_hex), b'test label', bytes.fromhex(seed_hex),
                            len(digest)) == (0, digest)
    # Key zero is invalid for input_key. PSA aborts and releases the preceding seed.
    probe.uc.mem_write(SSL, bytes(probe.types['psa_key_derivation_s']['size']))
    assert probe.run('psa_key_derivation_setup', SSL, ALGORITHMS[0]) == 0
    assert probe.run('c3tout_prf_input', SSL, SEED, SSL + 0x1000, 32) == 0
    key_input = 'c3tout_prf_key' if fixed else 'psa_key_derivation_input_key'
    assert probe.run(key_input, SSL, SECRET, 0) == -136
    assert probe.run('psa_key_derivation_abort', SSL) == 0 and probe.live == active
    if fixed:
        # An outstanding lease must keep its contents on a second request.
        occupied = probe.symbols['gb_secret_used']
        probe.uc.mem_write(occupied, b'\1')
        probe.uc.mem_write(probe.secret_address, b'X' * 66)
        before = len(probe.allocations)
        status, output = probe.derive('sha256', *cases[0])
        assert status != 0 and output == b'\xA5' * len(output)
        assert probe.allocations[before:] == ([] if probe.import_address else [32])
        assert probe.live == active
        assert bytes(probe.uc.mem_read(probe.secret_address, 66)) == b'X' * 66
        probe.run('c3tout_free', probe.secret_address)
    check_import_lifetime(probe, active)
    if probe.import_address:
        probe.uc.mem_write(probe.symbols['gb_import_used'], b'\1')
        probe.uc.mem_write(probe.import_address, b'X' * 66)
    else:
        probe.fail_call = len(probe.allocations) + 1
    calls = probe.key_inputs
    before = len(probe.allocations)
    status, output = probe.derive('sha256', *cases[0])
    assert status != 0 and output == b'\xA5' * len(output)
    assert probe.key_inputs == calls and probe.live == active
    if probe.import_address:
        assert len(probe.allocations) == before
        assert bytes(probe.uc.mem_read(probe.import_address, 66)) == b'X' * 66
        probe.run('c3tout_free', probe.import_address)
    probe.fail_call = None
    assert probe.derive('sha256', *cases[0]) == (0, expected('sha256', *cases[0]))
    probe.run('psa_wipe_all_key_slots')
    assert not probe.live
    return dict(passed=True, elf_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                fixed_key_copy=fixed, matched_derivations=checked, known_answer_vectors=2,
                invalid_key_aborted=True, recovered_after_failure=True,
                fixed_import=bool(probe.import_address), post_copy_failure_cleaned=True,
                heap_requests_per_warm_derivation=warm_count,
                remaining_request=None if probe.import_address else 'PSA imported-key material',
                key_store_initialization_requests=init_requests, first_derivation_requests=first_requests,
                cold_failure_requests=failed_requests,
                retained_key_store_allocations=list(active.values()),
                doubles=['HMAC primitive', 'hash setup/abort', 'heap', 'ROM byte/string routines',
                         'mutexes, native task identity and interrupt exclusion'],
                scope='Compiled client PRF, key import/lookup, output, abort and destruction',
                authenticated_tls_tested=False, hardware_tested=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--expect', choices=('heap', 'fixed', 'fixed-import'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__ or args.output.exists():
        parser.error('Enable assertions and choose a fresh report path')
    result = verify(args.elf, args.expect != 'heap', args.expect == 'fixed-import')
    with args.output.open('x', encoding='utf-8') as report:
        report.write(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
