"""Replay the demo TLS client's record-key allocation and retirement on RV32."""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn import UC_HOOK_CODE

from .tls_certificate_probe import layouts
from .tls_fragment_probe import SSL
from .tls_prf_key_probe import KeyProbe, expected


class TrafficProbe(KeyProbe):
    def free(self, *args):
        address = self.args()[0]
        if address:
            assert not any(self.uc.mem_read(address, self.live[address]))
            self.freed.append((self.destroying, address))
        super().free(*args)


def verify(elf, suite):
    probe = TrafficProbe(elf, fixed=True, require_import=True)
    probe.freed = []
    probe.destroying = None

    def destroying(*_):
        probe.destroying = probe.args()[0]

    pc = probe.symbols['psa_destroy_key']
    probe.uc.hook_add(UC_HOOK_CODE, destroying, begin=pc, end=pc)
    types = layouts(elf, ('mbedtls_ssl_transform',))
    handshake = probe.symbols['g_handshakes']
    session = probe.symbols['g_sessions']
    first = probe.symbols['g_transforms']
    transform = types['mbedtls_ssl_transform']
    second = first + transform['size']
    hs = types['mbedtls_ssl_handshake_params']['fields']
    sess = types['mbedtls_ssl_session']['fields']
    suite_info = probe.run('mbedtls_ssl_ciphersuite_from_id', suite)
    assert suite_info and not probe.live
    probe.write_field('handshake', handshake)
    probe.write_field('session_negotiate', session)
    probe.write_field('tls_version', 0x0303)

    def word(address, value):
        probe.uc.mem_write(address, value.to_bytes(4, 'little'))

    word(handshake + hs['ciphersuite_info'], suite_info)
    word(session + sess['ciphersuite'], suite)

    def derive(target, generation, fail_second=False):
        material = bytes([generation]) * 32
        randoms = bytes(range(64))
        word(handshake + hs['pmslen'], len(material))
        probe.uc.mem_write(handshake + hs['premaster'], material)
        probe.uc.mem_write(handshake + hs['randbytes'], randoms)
        probe.write_field('transform_negotiate', target)
        before = len(probe.allocations)
        held = dict(probe.live)
        if fail_second:
            probe.fail_call = before + 2
        result = probe.run('mbedtls_ssl_derive_keys', SSL)
        probe.fail_call = None
        assert (result < 0 if fail_second else result == 0)
        assert probe.allocations[before:] == [16, 16]
        assert all(probe.live.get(addr) == size for addr, size in held.items())
        # Compare the actual retained allocations to an independent TLS 1.2 PRF.
        master = expected('sha256', material, b'master secret', randoms, 48)
        keys = expected('sha256', master, b'key expansion', randoms[32:] + randoms[:32], 32)
        new = list(probe.live.keys() - held.keys())
        handles = []
        for index, field in enumerate(('psa_key_enc', 'psa_key_dec')):
            handle = int.from_bytes(
                probe.uc.mem_read(target + transform['fields'][field], 4), 'little')
            if index == 1 and fail_second:
                assert handle == 0
                continue
            key = keys[index * 16:(index + 1) * 16]
            address, = [addr for addr in new if bytes(probe.uc.mem_read(addr, 16)) == key]
            assert handle and probe.live[address] == 16
            handles.append((handle, address))
        assert len(new) == len(handles)
        return handles

    def retire(target, owned):
        # Follow the native transform destructor while checking which live key
        # each handle releases and that the synthetic material was wiped first.
        before = len(probe.freed)
        probe.run('mbedtls_ssl_transform_free', target)
        assert probe.freed[before:] == owned
        assert not any(probe.uc.mem_read(target, transform['size']))

    active = derive(first, 1)
    original = {addr: bytes(probe.uc.mem_read(addr, 16)) for _, addr in active}

    def check_original():
        assert all(addr in probe.live and bytes(probe.uc.mem_read(addr, 16)) == key
                   for addr, key in original.items())

    partial = derive(second, 2, fail_second=True)
    check_original()
    retire(second, partial)
    check_original()
    replacement = derive(second, 3)
    check_original()
    peak = sum(probe.live.values())
    assert peak == 64 and len({h for h, _ in active + replacement}) == 4
    retire(first, active)
    assert probe.live == {addr: 16 for _, addr in replacement}
    retire(second, replacement)
    assert not probe.live
    return dict(retained_bytes_per_transform=32, overlapping_key_bytes=peak,
                previous_keys_survived_failed_replacement=True, all_keys_wiped=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__ or args.output.exists():
        parser.error('Enable assertions and choose a new report path')
    report = dict(elf_sha256=hashlib.sha256(args.elf.read_bytes()).hexdigest(),
                  suites={name: verify(args.elf, suite) for name, suite in
                          [('ECDHE-ECDSA-AES128-GCM-SHA256', 0xC02B),
                           ('ECDHE-RSA-AES128-GCM-SHA256', 0xC02F)]})
    with args.output.open('x', encoding='utf-8') as out:
        out.write(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
