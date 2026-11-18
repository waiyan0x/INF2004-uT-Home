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
    def __init__(self, path, fixed):
        super().__init__(path)
        self.fixed = fixed
        self.mac = {}
        self.key_inputs = 0
        self.secret_address = self.symbols.get('g_prf_secret')
        assert bool(self.secret_address) == fixed
        if fixed:
            with path.open('rb') as stream:
                symbol, = ELFFile(stream).get_section_by_name('.symtab').get_symbol_by_name('g_prf_secret')
                assert symbol['st_size'] == 66
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
        assert self.run('psa_initialize_key_slots') == 0

    def args(self):
        return tuple(self.uc.reg_read(reg) for reg in (UC_RISCV_REG_A0, UC_RISCV_REG_A1,
                                                       UC_RISCV_REG_A2, UC_RISCV_REG_A3))

    def strlen(self, *_):
        self.finish(bytes(self.uc.mem_read(self.args()[0], 64)).index(0))

    def count_key(self, *_):
        self.key_inputs += 1

    def mac_start(self, *_):
        operation, alg, key, size = self.args()
        assert operation not in self.mac
        if self.fixed:
            assert key == self.secret_address
        else:
            assert self.live[key] == size
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


def expected(alg, key, label, seed, length):
    # RFC 5246 section 5, evaluated independently of the compiled PRF.
    source = label + seed
    chain = source
    output = bytearray()
    while len(output) < length:
        chain = hmac.digest(key, chain, alg)
        output.extend(hmac.digest(key, chain + source, alg))
    return bytes(output[:length])


def verify(path, fixed):
    probe = KeyProbe(path, fixed)
    cases = [(bytes(range(32)), b'master secret', bytes(range(64)), 48),
             (bytes(range(66)), b'extended master secret', None, 48),
             (bytes(range(48)), b'client finished', None, 12),
             (bytes(range(48)), b'key expansion', bytes(range(64)), 256)]
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
                else:
                    assert probe.live == active
                    assert probe.allocations[before:] == [len(key)] * (1 if fixed else 2)
                if fixed:
                    assert bytes(probe.uc.mem_read(probe.secret_address, 66)) == bytes(66)
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
    probe.uc.mem_write(SSL, bytes(layouts(path)['psa_key_derivation_s']['size']))
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
        assert probe.allocations[before:] == [32] and probe.live == active
        assert bytes(probe.uc.mem_read(probe.secret_address, 66)) == b'X' * 66
        probe.run('c3tout_free', probe.secret_address)
    probe.fail_call = len(probe.allocations) + 1
    calls = probe.key_inputs
    status, output = probe.derive('sha256', *cases[0])
    assert status != 0 and output == b'\xA5' * len(output)
    assert probe.key_inputs == calls and probe.live == active
    probe.fail_call = None
    assert probe.derive('sha256', *cases[0]) == (0, expected('sha256', *cases[0]))
    probe.run('psa_wipe_all_key_slots')
    assert not probe.live
    return dict(passed=True, elf_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                fixed_key_copy=fixed, matched_derivations=checked, known_answer_vectors=2,
                invalid_key_aborted=True, recovered_after_failure=True,
                heap_requests_per_warm_derivation=1 if fixed else 2,
                remaining_request='PSA imported-key material',
                doubles=['HMAC primitive', 'hash setup/abort', 'heap', 'ROM byte/string routines',
                         'mutexes, native task identity and interrupt exclusion'],
                scope='Compiled client PRF, key import/lookup, output, abort and destruction',
                authenticated_tls_tested=False, hardware_tested=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--expect', choices=('heap', 'fixed'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__ or args.output.exists():
        parser.error('Enable assertions and choose a fresh report path')
    result = verify(args.elf, args.expect == 'fixed')
    with args.output.open('x', encoding='utf-8') as report:
        report.write(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
