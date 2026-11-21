"""Execute native TLS certificate ownership paths with parsing/crypto doubled.

The SDK handles message framing, renegotiation identity decisions, allocation,
promotion and cleanup. DER parsing, trust verification, digest calculation and
public-key import are explicit doubles, so this is not authentication proof.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3
from .tls_fragment_probe import SSL, CONFIG, WANT_READ
from .tls_output_probe import OutputProbe


def layouts(path, additional=()):
    wanted = {'mbedtls_ssl_session', 'mbedtls_ssl_handshake_params',
              'mbedtls_x509_crt', 'mbedtls_asn1_buf', *additional}
    result = {}
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        for unit in elf.get_dwarf_info().iter_CUs():
            for die in unit.iter_DIEs():
                name = die.attributes.get('DW_AT_name')
                if (die.tag != 'DW_TAG_structure_type' or not name
                        or name.value.decode() not in wanted or 'DW_AT_byte_size' not in die.attributes):
                    continue
                key = name.value.decode()
                result[key] = dict(size=die.attributes['DW_AT_byte_size'].value,
                                  fields={c.attributes['DW_AT_name'].value.decode():
                                          c.attributes['DW_AT_data_member_location'].value
                                          for c in die.iter_children() if c.tag == 'DW_TAG_member'})
            if set(result) == wanted:
                break
    assert set(result) == wanted
    assert result['mbedtls_x509_crt']['size'] == 952
    return result


def fixed_storage(path):
    expected = {'g_certificate': 952, 'g_peer_digest': 32,
                'gb_certificate_used': 1, 'gb_digest_used': 1}
    result = {}
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        if elf.get_section_by_name('.symtab').get_symbol_by_name('g_peer_used'):
            expected['g_peer_used'] = expected.pop('gb_digest_used')
        for unit in elf.get_dwarf_info().iter_CUs():
            source = unit.get_top_DIE().attributes['DW_AT_name'].value.decode()
            if Path(source.replace('\\', '/')).name != 'c3tout.c':
                continue
            for die in unit.iter_DIEs():
                name = die.attributes.get('DW_AT_name')
                if die.tag != 'DW_TAG_variable' or not name or name.value.decode() not in expected:
                    continue
                name = name.value.decode()
                location = die.attributes['DW_AT_location'].value
                assert len(location) == 5 and location[0] == 3 and elf.elfclass == 32
                address = int.from_bytes(location[1:], 'little')
                kind = die.get_DIE_from_attribute('DW_AT_type')
                while kind.tag == 'DW_TAG_typedef':
                    kind = kind.get_DIE_from_attribute('DW_AT_type')
                if name == 'g_certificate':
                    assert kind.tag == 'DW_TAG_structure_type'
                    assert kind.attributes['DW_AT_name'].value == b'mbedtls_x509_crt'
                    assert kind.attributes['DW_AT_byte_size'].value == 952 and address % 4 == 0
                elif name == 'g_peer_digest':
                    if 'g_peer_used' in expected:
                        assert kind.tag == 'DW_TAG_union_type' and kind.attributes['DW_AT_byte_size'].value == 32
                        members = {m.attributes['DW_AT_name'].value: m for m in kind.iter_children()}
                        nodes = members[b'names'].get_DIE_from_attribute('DW_AT_type')
                        count = next(c for c in nodes.iter_children() if c.tag == 'DW_TAG_subrange_type')
                        assert count.attributes['DW_AT_upper_bound'].value + 1 == 2 and address % 4 == 0
                        kind = members[b'bytes'].get_DIE_from_attribute('DW_AT_type')
                    assert kind.tag == 'DW_TAG_array_type'
                    bound = next(c for c in kind.iter_children() if c.tag == 'DW_TAG_subrange_type')
                    assert bound.attributes['DW_AT_upper_bound'].value + 1 == 32
                symbols = elf.get_section_by_name('.symtab').get_symbol_by_name(name)
                assert any(s['st_value'] == address and s['st_size'] == expected[name] for s in symbols)
                result[name] = dict(address=address, size=expected[name])
    assert set(result) == set(expected), 'ELF lacks fixed certificate/digest stores'
    ranges = sorted((v['address'], v['size']) for v in result.values())
    assert all(a + n <= b for (a, n), (b, _) in zip(ranges, ranges[1:]))
    return result


class CertificateProbe(OutputProbe):
    def __init__(self, path, types, fixed):
        self.types = types
        self.fixed = fixed
        self.read_result = self.der_result = self.verify_result = self.digest_result = self.key_result = 0
        self.alerts = []
        self.operations = Counter()
        self.storage = fixed_storage(path) if fixed else {}
        super().__init__(path)
        for name, callback in (
                ('mbedtls_ssl_read_record', self.read_record),
                ('mbedtls_x509_crt_parse_der_nocopy', self.parse_der),
                ('mbedtls_ssl_verify_certificate', self.verify_certificate),
                ('mbedtls_md', self.digest), ('mbedtls_pk_parse_subpubkey', self.public_key),
                ('mbedtls_ssl_send_alert_message', self.alert), ('memcmp', self.compare)):
            self.hook(self.symbols[name], callback)
        self.hook(self.symbols['mbedtls_ssl_handshake'], lambda *args: self.finish(WANT_READ))

    def calloc(self, *args):
        size = self.uc.reg_read(UC_RISCV_REG_A0) * self.uc.reg_read(UC_RISCV_REG_A1)
        if size == 32:
            assert 32 not in self.live.values(), 'Old and new digest heap lifetimes overlap'
        super().calloc(*args)

    def get(self, address, kind, field):
        return int.from_bytes(self.uc.mem_read(address + self.types[kind]['fields'][field], 4), 'little')

    def put(self, address, kind, field, value):
        self.uc.mem_write(address + self.types[kind]['fields'][field], struct.pack('<I', value))

    def session(self, field, active=False):
        return self.get(self.field(SSL, 'session' if active else 'session_negotiate'), 'mbedtls_ssl_session', field)

    def read_record(self, *args):
        self.operations['read'] += 1
        self.finish(self.read_result)

    def parse_der(self, *args):
        chain, source, size = (self.uc.reg_read(r) for r in (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2))
        if self.fixed:
            assert chain == self.storage['g_certificate']['address']
        self.operations['der'] += 1
        if not self.der_result:
            for field in ('raw', 'pk_raw'):
                buf = chain + self.types['mbedtls_x509_crt']['fields'][field]
                self.put(buf, 'mbedtls_asn1_buf', 'p', source)
                self.put(buf, 'mbedtls_asn1_buf', 'len', size)
        self.finish(self.der_result)

    def verify_certificate(self, *args):
        self.operations['verify'] += 1
        session = self.field(SSL, 'session_negotiate')
        self.put(session, 'mbedtls_ssl_session', 'verify_result', 0 if not self.verify_result else 8)
        self.finish(self.verify_result)

    def digest(self, *args):
        source, size, target = (self.uc.reg_read(r) for r in (UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3))
        self.operations['digest'] += 1
        # Fail the replacement hash only, after the old-identity comparison.
        result = self.digest_result if target == self.session('peer_cert_digest') else 0
        if not result:
            self.uc.mem_write(target, hashlib.sha256(bytes(self.uc.mem_read(source, size))).digest())
        self.finish(result)

    def public_key(self, *args):
        self.operations['key'] += 1
        self.finish(self.key_result)

    def alert(self, *args):
        self.alerts.append(self.uc.reg_read(UC_RISCV_REG_A2))
        self.finish(0)

    def compare(self, *args):
        left, right, size = (self.uc.reg_read(r) for r in (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2))
        a, b = bytes(self.uc.mem_read(left, size)), bytes(self.uc.mem_read(right, size))
        self.finish((a > b) - (a < b))

    def message(self, data):
        body = len(data).to_bytes(3, 'big') + data
        frame = b'\x0b' + (len(body) + 3).to_bytes(3, 'big') + len(body).to_bytes(3, 'big') + body
        self.uc.mem_write(self.field(SSL, 'in_msg'), frame)
        self.write_field('in_msgtype', 22)
        self.write_field('in_hslen', len(frame))

    def parse(self):
        result = self.run('c3tout_certificate' if self.fixed else 'mbedtls_ssl_parse_certificate', SSL)
        if self.fixed:
            assert int.from_bytes(self.uc.mem_read(self.symbols['g_cert_phase'], 4), 'little') == 0
            assert int.from_bytes(self.uc.mem_read(self.symbols['gp_certificate'], 4), 'little') == 0
        return result

    def prepare_cipher(self):
        cipher = self.run('mbedtls_ssl_ciphersuite_from_id', 0xC02F)
        assert cipher
        self.put(self.field(SSL, 'handshake'), 'mbedtls_ssl_handshake_params', 'ciphersuite_info', cipher)

    def clean(self):
        self.run('mbedtls_ssl_free', SSL)
        assert not self.live
        if self.fixed:
            for item in self.storage.values():
                assert bytes(self.uc.mem_read(item['address'], item['size'])) == bytes(item['size'])

    def certificate_free(self):
        if self.fixed:
            for name in ('g_certificate', 'gb_certificate_used'):
                item = self.storage[name]
                assert bytes(self.uc.mem_read(item['address'], item['size'])) == bytes(item['size'])
            assert not self.live
        else:
            assert 952 not in self.live.values()

    def start_renegotiation(self):
        assert self.run('c3tout_start', SSL) == WANT_READ
        self.prepare_cipher()


def initialize(path, types, fixed):
    probe = CertificateProbe(path, types, fixed)
    probe.run('mbedtls_ssl_init', SSL)
    assert probe.run('mbedtls_ssl_config_defaults', CONFIG, 0, 0, 0) == 0
    assert probe.run('c3tout_setup', SSL, CONFIG) == 0
    probe.prepare_cipher()
    return probe


def verify(path, fixed):
    types = layouts(path)
    data = bytes(range(128))
    probe = initialize(path, types, fixed)
    probe.message(data)
    assert probe.parse() == 0
    pointer = probe.session('peer_cert_digest')
    if fixed:
        assert pointer == probe.storage['g_peer_digest']['address']
    assert probe.session('peer_cert_digest_len') == 32
    assert bytes(probe.uc.mem_read(pointer, 32)) == hashlib.sha256(data).digest()
    assert probe.operations == dict(read=1, der=1, verify=1, digest=1, key=1)
    if fixed:
        assert not probe.allocations
    else:
        assert probe.allocations == [952, 32]
    probe.certificate_free()
    probe.run('mbedtls_ssl_handshake_wrapup', SSL)
    assert probe.session('peer_cert_digest', active=True) == pointer
    for _ in range(100):
        probe.start_renegotiation()
        probe.message(data)
        assert probe.parse() == 0
        assert probe.session('peer_cert_digest', active=True) == 0
        assert probe.session('peer_cert_digest') == pointer
        assert bytes(probe.uc.mem_read(pointer, 32)) == hashlib.sha256(data).digest()
        probe.certificate_free()
        probe.run('mbedtls_ssl_handshake_wrapup', SSL)
    cycles = dict(probe.operations)
    assert probe.allocations == ([] if fixed else [952, 32] * 101)
    probe.clean()

    failures = []
    for case in ('read_retry', 'malformed', 'changed_certificate', 'der', 'verify', 'digest', 'key'):
        probe = initialize(path, types, fixed)
        probe.message(data)
        assert probe.parse() == 0
        old_digest = probe.session('peer_cert_digest')
        probe.run('mbedtls_ssl_handshake_wrapup', SSL)
        probe.start_renegotiation()
        probe.message(data)
        if case == 'read_retry':
            probe.read_result = WANT_READ
        elif case == 'malformed':
            probe.write_field('in_msgtype', 23)
        elif case == 'changed_certificate':
            probe.message(bytes(reversed(data)))
        else:
            setattr(probe, case + '_result', -0x2700 if case == 'verify' else -0x2180)
        result = probe.parse()
        assert result < 0
        before_match = case in ('read_retry', 'malformed', 'changed_certificate')
        assert probe.session('peer_cert_digest', active=True) == (old_digest if before_match else 0), case
        if before_match:
            assert bytes(probe.uc.mem_read(old_digest, 32)) == hashlib.sha256(data).digest()
        digest_retained = bool(probe.session('peer_cert_digest'))
        assert digest_retained == (case in ('digest', 'key'))
        probe.certificate_free()
        if fixed:
            assert not probe.allocations
        failures.append(dict(case=case, result=result, old_digest_retained=before_match,
                             negotiating_digest_retained=digest_retained))
        if case == 'read_retry':
            probe.read_result = 0
            assert probe.parse() == 0
            assert probe.session('peer_cert_digest', active=True) == 0
            assert probe.session('peer_cert_digest') == old_digest
            assert bytes(probe.uc.mem_read(old_digest, 32)) == hashlib.sha256(data).digest()
            probe.certificate_free()
            if fixed:
                assert not probe.allocations
            failures[-1]['retry_recovered'] = True
        probe.clean()

    exhausted = []
    for kind, allocation in (('certificate', 1), ('digest', 2)):
        probe = initialize(path, types, fixed)
        if fixed:
            name = 'g_certificate' if kind == 'certificate' else 'g_peer_digest'
            lease = 'gb_certificate_used' if kind == 'certificate' else 'gb_digest_used'
            if kind == 'digest' and 'g_peer_used' in probe.storage:
                lease = 'g_peer_used'
            item = probe.storage[name]
            probe.uc.mem_write(item['address'], b'\xa5' * item['size'])
            probe.uc.mem_write(probe.storage[lease]['address'], b'\1')
        else:
            probe.fail_call = len(probe.allocations) + allocation
        probe.message(data)
        assert probe.parse() == -141
        assert probe.session('peer_cert_digest') == 0
        if fixed:
            assert not probe.allocations
            assert bytes(probe.uc.mem_read(item['address'], item['size'])) == b'\xa5' * item['size']
            probe.run('c3tout_free', item['address'])
        probe.clean()
        exhausted.append(kind)

    return dict(passed=True, elf_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                fixed_storage=fixed, fixed_store_layout=probe.storage,
                renegotiation_cycles=100, lifecycle_operations=cycles, error_cases=failures,
                exhaustion_cases=exhausted,
                lifecycle_outer_heap_requests=0 if fixed else 202,
                scope='Native certificate framing/ownership with DER, trust, crypto and transport doubled',
                doubles=['SDK heap', 'ROM byte operations', 'record reading', 'DER parsing',
                         'trust verification', 'Python SHA256', 'public-key import', 'alerts',
                         'PSA hash setup/abort', 'handshake returning WANT_READ'],
                authenticated_tls_tested=False, hardware_tested=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--expect', choices=('heap', 'fixed'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        parser.error('Assertions must remain enabled')
    if args.output.exists():
        parser.error('Report already exists; choose a new path')
    result = verify(args.elf, args.expect == 'fixed')
    with args.output.open('x', encoding='utf-8') as report:
        report.write(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
