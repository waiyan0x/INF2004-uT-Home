"""Replay the pinned TLS 1.2 X25519 client step with host-only test entropy."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_CODE
from unicorn.riscv_const import (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2,
                                  UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC)

from .tls_certificate_probe import layouts
from .tls_fragment_probe import SSL, STACK, STACK_SIZE, STOP
from .tls_x509_parse_probe import ParserProbe


def scratch_ranges(elf):
    """Use the target layout to check the key and integer storage after use."""
    with elf.open('rb') as stream:
        for unit in ELFFile(stream).get_dwarf_info().iter_CUs():
            for die in unit.iter_DIEs():
                name = die.attributes.get('DW_AT_name')
                if die.tag == 'DW_TAG_typedef' and name and name.value == b'c3peer_math_t':
                    members = die.get_DIE_from_attribute('DW_AT_type').iter_children()
                    offsets = {m.attributes['DW_AT_name'].value.decode():
                               m.attributes['DW_AT_data_member_location'].value for m in members}
                    return [(offsets[a], offsets[b] - offsets[a]) for a, b in
                            [('words', 'used'), ('inverse', 'b_inverse_used'),
                             ('key', 'b_key_used'), ('exchange_key', 'b_exchange_key_free')]]
    raise ValueError('missing workspace DWARF layout')


def measure(elf, fields, ranges, *, busy=False, bad_peer=False):
    probe = ParserProbe(elf)
    cpu = probe.uc
    base = SSL + 0x90000
    cpu.mem_map(base, 0x10000)
    msg = base + 0x8000
    assert fields['size'] < msg - base
    offsets = fields['fields']
    put = lambda name, value: cpu.mem_write(base + offsets[name], struct.pack('<I', value))
    word = lambda address: int.from_bytes(cpu.mem_read(address, 4), 'little')
    public = lambda key: key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    peer = X25519PrivateKey.from_private_bytes(bytes(range(32, 64)))
    entropy = bytes(range(1, 33))
    # The pinned MPI generator reads a big-endian integer and drops its top spare bit by shifting.
    scalar = (int.from_bytes(entropy, 'big') >> 1).to_bytes(32, 'little')
    client = X25519PrivateKey.from_private_bytes(scalar)
    draws = 0

    def random_bytes(*_):
        nonlocal draws
        dst, size = (cpu.reg_read(reg) for reg in (UC_RISCV_REG_A1, UC_RISCV_REG_A2))
        if draws == 0:
            assert size == len(entropy)
        data = entropy if draws == 0 else hashlib.shake_256(struct.pack('<I', draws)).digest(size)
        draws += 1
        cpu.mem_write(dst, data)
        probe.finish(0)

    probe.hook(probe.symbols['mbedtls_psa_get_random'], random_bytes)
    probe.hook(probe.symbols['mbedtls_ssl_write_handshake_msg_ext'], lambda *_: probe.finish(0))
    probe.hook(probe.symbols['esp_mpi_mult_mpi_failover_mod_mult_hw_op'], probe.multiply)
    sem = probe.symbols['op_sem_buf.1']
    probe.hook(probe.symbols['c3nq_static'], lambda *_: probe.finish(sem))
    probe.hook(probe.symbols['c3nq_take'], lambda *_: probe.finish(1))
    for name in ('esp_intr_alloc', 'mpi_hal_clear_interrupt', 'mpi_hal_interrupt_enable'):
        probe.hook(probe.symbols[name], lambda *_: probe.finish(0))
    assert probe.run('psa_initialize_key_slots') == 0
    suite = probe.run('mbedtls_ssl_ciphersuite_from_id', 0xc02b)  # ECDHE-ECDSA-AES128-GCM-SHA256
    assert suite
    put('ciphersuite_info', suite)
    cpu.mem_write(base + offsets['xxdh_psa_type'], struct.pack('<H', 0x7141))  # Montgomery keypair
    put('xxdh_psa_bits', 255)
    put('xxdh_psa_peerkey_len', 32)
    cpu.mem_write(base + offsets['xxdh_psa_peerkey'], bytes(32) if bad_peer else public(peer))
    probe.write_field('handshake', base)
    probe.write_field('out_msg', msg)
    probe.write_field('state', 8)  # MBEDTLS_SSL_CLIENT_KEY_EXCHANGE in the pinned SDK

    phases = {}
    def boundary(name):
        assert name not in phases
        phases[name] = (len(probe.allocations), sum(probe.allocations), sum(probe.live.values()))
    for name in ('psa_generate_key', 'psa_export_public_key', 'psa_raw_key_agreement', 'psa_destroy_key'):
        addr = probe.symbols[name]
        cpu.hook_add(UC_HOOK_CODE, lambda *_, n=name: boundary(n), begin=addr, end=addr)

    marker = bytes(range(256)) * 16
    if busy:
        scratch = probe.run('c3peer_views')
        assert scratch
        cpu.mem_write(scratch, marker)
    probe.allocations.clear()
    probe.peak = 0
    assert not probe.live
    entry = probe.symbols.get('c3peer_client_step', probe.symbols['mbedtls_ssl_handshake_client_step'])
    cpu.reg_write(UC_RISCV_REG_A0, SSL)
    cpu.reg_write(UC_RISCV_REG_RA, STOP)
    cpu.reg_write(UC_RISCV_REG_SP, STACK + STACK_SIZE - 16)
    cpu.emu_start(entry, STOP, count=100000000)
    assert cpu.reg_read(UC_RISCV_REG_PC) == STOP
    result = struct.unpack('<i', struct.pack('<I', cpu.reg_read(UC_RISCV_REG_A0)))[0]
    boundary('end')
    assert list(phases) == ['psa_generate_key', 'psa_export_public_key', 'psa_raw_key_agreement',
                            'psa_destroy_key', 'end']
    assert not probe.live and not word(base + offsets['xxdh_psa_privkey'])
    if bad_peer:
        assert result < 0 and probe.read_field('state') == 8
    else:
        assert result == 0 and probe.read_field('state') == 9
        assert probe.read_field('out_msglen') == 37
        assert bytes(cpu.mem_read(msg + 4, 33)) == bytes([32]) + public(client)
        assert word(base + offsets['pmslen']) == 32
        assert bytes(cpu.mem_read(base + offsets['premaster'], 32)) == client.exchange(peer.public_key())
    if busy:
        assert bytes(cpu.mem_read(scratch, 4096)) == marker
        probe.run('c3peer_close')
    scratch = probe.run('c3peer_views')
    assert scratch
    if not busy:
        for offset, size in ranges:
            assert size > 0 and not any(cpu.mem_read(scratch + offset, size))
    probe.run('c3peer_close')
    counts = {}
    for (name, start), end in zip(phases.items(), list(phases.values())[1:]):
        counts[name] = dict(requests=end[0]-start[0], requested=end[1]-start[1], live_after=end[2])
    return dict(requests=len(probe.allocations), peak=probe.peak, result=result, operations=counts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--expect', choices=('native', 'workspace'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__ or args.output.exists():
        parser.error('enable assertions and choose a new output path')
    fields = layouts(args.elf)['mbedtls_ssl_handshake_params']
    ranges = scratch_ranges(args.elf) if args.expect == 'workspace' else []
    cases = {name: measure(args.elf, fields, ranges, busy=busy, bad_peer=bad) for name, busy, bad in
             [('available', False, False), ('usb_busy', True, False), ('invalid_peer', False, True)]}
    assert cases['usb_busy']['requests'] > 1
    if args.expect == 'native':
        assert cases['available'] == cases['usb_busy']
    else:
        assert cases['available']['requests'] == cases['invalid_peer']['requests'] == 1
        assert cases['available']['peak'] == 32
    report = dict(elf_sha256=hashlib.sha256(args.elf.read_bytes()).hexdigest(), cases=cases)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2)
    print('Compiled X25519 client exchange, USB ownership and rejection controls passed.')


if __name__ == '__main__':
    main()
