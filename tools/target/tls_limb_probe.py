"""Exercise the linked USB/TLS workspace lease on a single P-256 certificate."""
import argparse
import hashlib
import json
from pathlib import Path
import ssl
import struct

from unicorn.riscv_const import (
    UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3,
    UC_RISCV_REG_A4, UC_RISCV_REG_A5, UC_RISCV_REG_A6, UC_RISCV_REG_A7,
    UC_RISCV_REG_PC, UC_RISCV_REG_RA, UC_RISCV_REG_SP,
)

from .tls_fragment_probe import SSL, STACK, STACK_SIZE, STOP
from .tls_x509_parse_probe import ParserProbe


def measure(elf, pem):
    probe = ParserProbe(elf)
    run = probe.run
    cpu = probe.uc
    assert run('psa_initialize_key_slots') == 0
    chain = probe.symbols['g_certificate']
    data = ssl.PEM_cert_to_DER_cert(pem.read_text(encoding='ascii'))
    source = SSL + 0x40000
    cpu.mem_map(source, (len(data) + 4095) & ~4095)
    cpu.mem_write(source, data)
    run('mbedtls_x509_crt_init', chain)
    assert run('c3peer_der', chain, source, len(data)) == 0
    retained = dict(probe.live)
    workspace = run('c3peer_views')
    assert workspace
    cpu.mem_write(workspace, bytes([0xa5]) * 4096)
    run('c3peer_close')

    # Stop at entry to the SDK verifier with the real wrapper's lease active.
    # The parsed leaf doubles as the issuer here. Authentication is a separate test.
    top = STACK + STACK_SIZE - 16
    regs = (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3,
            UC_RISCV_REG_A4, UC_RISCV_REG_A5, UC_RISCV_REG_A6, UC_RISCV_REG_A7)
    for reg, value in zip(regs, (chain, chain, 0, probe.symbols['mbedtls_x509_crt_profile_default'],
                                  0, source + 1024, 0, 0)):
        cpu.reg_write(reg, value)
    cpu.mem_write(top, struct.pack('<I', 0))
    cpu.reg_write(UC_RISCV_REG_SP, top)
    cpu.reg_write(UC_RISCV_REG_RA, STOP)
    native = probe.symbols['mbedtls_x509_crt_verify_restartable']
    cpu.emu_start(probe.symbols['c3peer_verify'], native, count=50000)
    assert cpu.reg_read(UC_RISCV_REG_PC) == native
    saved = cpu.context_save()
    stack = bytes(cpu.mem_read(STACK, STACK_SIZE))
    assert run('c3peer_views') == 0 and run('c3peer_der', chain, source, len(data)) < 0

    assert run('c3peer_limbs', 25, 4) == 0
    assert run('c3peer_limbs', 0, 4) == 0
    slots = []
    for marker in range(1, 33):
        pointer = run('c3peer_limbs', 24, 4)
        assert workspace <= pointer <= workspace + 4096 - 96 and pointer % 4 == 0
        assert not any(cpu.mem_read(pointer, 96))
        cpu.mem_write(pointer, bytes([marker]) * 96)
        slots.append(pointer)
    assert len(set(slots)) == 32 and probe.live == retained
    assert run('c3peer_limbs', 1, 4) == 0

    # The inverse scratch can coexist with all 32 regular limb slots.
    inverse = run('c3peer_limbs', 32, 5)
    assert workspace <= inverse <= workspace + 4096 - 160 and inverse % 4 == 0
    assert all(pointer + 96 <= inverse or inverse + 160 <= pointer for pointer in slots)
    assert not any(cpu.mem_read(inverse, 160))
    marker_bytes = bytes(range(160))
    cpu.mem_write(inverse, marker_bytes)
    assert run('c3peer_limbs', 32, 5) == 0 and probe.live == retained

    # A different factor uses the SDK even if the product is also 160 bytes.
    other = run('c3peer_limbs', 16, 10)
    assert other != inverse and probe.live[other] == 160
    run('c3peer_drop', other)

    # A different native task still owns its own allocations during verification.
    probe.task_id = 2
    foreign = run('c3peer_limbs', 24, 4)
    assert probe.live[foreign] == 96 and foreign not in slots
    run('c3peer_wipe', foreign, 96)
    other = run('c3peer_limbs', 32, 5)
    assert other != inverse and probe.live[other] == 160
    run('c3peer_wipe', other, 160)
    probe.task_id = 1
    assert cpu.mem_read(inverse, 160) == marker_bytes
    run('c3peer_drop', inverse)
    assert not any(cpu.mem_read(inverse, 160))
    assert run('c3peer_limbs', 32, 5) == inverse
    cpu.mem_write(inverse, marker_bytes)
    run('c3peer_wipe', inverse, 160)
    assert not any(cpu.mem_read(inverse, 160))
    for marker, pointer in enumerate(slots, 1):
        assert cpu.mem_read(pointer, 96) == bytes([marker]) * 96
    run('c3peer_drop', slots[0])
    assert run('c3peer_limbs', 1, 4) == slots[0] and not any(cpu.mem_read(slots[0], 96))
    for pointer in slots:
        run('c3peer_wipe', pointer, 4)
        assert not any(cpu.mem_read(pointer, 96))
    assert probe.live == retained

    # Resume the wrapper with a simulated SDK error and check it returns both
    # the error and workspace. Nested probe calls reused the emulated stack.
    cpu.context_restore(saved)
    cpu.mem_write(STACK, stack)
    cpu.reg_write(UC_RISCV_REG_A0, 0xffffd900)
    cpu.emu_start(cpu.reg_read(UC_RISCV_REG_RA), STOP, count=50000)
    assert cpu.reg_read(UC_RISCV_REG_PC) == STOP and cpu.reg_read(UC_RISCV_REG_A0) == 0xffffd900
    assert run('c3peer_views') == workspace
    run('c3peer_close')
    run('mbedtls_x509_crt_free', chain)
    assert not probe.live
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), slots=len(slots),
                slot_bytes=96, inverse_bytes=160, exhaustion_verified=True, foreign_task_verified=True,
                release_verified=True, simulated_verifier_return=-0x2700)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('pem', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        parser.error('Assertions must be enabled')
    result = measure(args.elf, args.pem)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print('Fixed limb storage and task isolation verified')
