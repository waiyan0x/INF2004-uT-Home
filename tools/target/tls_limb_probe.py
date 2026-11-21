"""Exercise the linked USB/TLS workspace lease on a single P-256 certificate."""
import argparse
import hashlib
import json
from pathlib import Path
import ssl
import struct

from unicorn import UC_HOOK_CODE
from unicorn.riscv_const import (
    UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3,
    UC_RISCV_REG_A4, UC_RISCV_REG_A5, UC_RISCV_REG_A6, UC_RISCV_REG_A7,
    UC_RISCV_REG_PC, UC_RISCV_REG_RA, UC_RISCV_REG_SP,
)

from .tls_fragment_probe import SSL, STACK, STACK_SIZE, STOP
from .tls_x509_parse_probe import ParserProbe


def check_public_copy(probe, chain, source, size):
    """Exercise native PSA slot creation and destruction under a real parser lease."""
    cpu, run = probe.uc, probe.run
    workspace = run('c3peer_views')
    assert workspace
    run('c3peer_close')
    entry = probe.symbols['c3peer_import']
    waiting = [True]

    def stop_at_import(uc, address, length, user):
        if waiting[0]:
            waiting[0] = False
            uc.emu_stop()

    cpu.hook_add(UC_HOOK_CODE, stop_at_import, begin=entry, end=entry)
    for reg, value in zip((UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2),
                          (chain, source, size)):
        cpu.reg_write(reg, value)
    cpu.reg_write(UC_RISCV_REG_SP, STACK + STACK_SIZE - 16)
    cpu.reg_write(UC_RISCV_REG_RA, STOP)
    cpu.emu_start(probe.symbols['c3peer_der'], STOP, count=2000000)
    assert cpu.reg_read(UC_RISCV_REG_PC) == entry
    attrs, data = (cpu.reg_read(reg) for reg in (UC_RISCV_REG_A0, UC_RISCV_REG_A1))
    assert cpu.reg_read(UC_RISCV_REG_A2) == 65
    point = bytes(cpu.mem_read(data, 65))
    assert point[0] == 4
    saved = cpu.context_save()
    stack = bytes(cpu.mem_read(STACK, STACK_SIZE))
    handles = SSL + 0x80000
    cpu.mem_map(handles, 4096)
    live = dict(probe.live)
    # Nested calls use a separate stack so the paused parser's attributes stay live.
    def nested(name, *args):
        for reg, arg in zip((UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3), args):
            cpu.reg_write(reg, arg)
        cpu.reg_write(UC_RISCV_REG_SP, STACK + STACK_SIZE // 2)
        cpu.reg_write(UC_RISCV_REG_RA, STOP)
        cpu.emu_start(probe.symbols[name], STOP, count=2000000)
        assert cpu.reg_read(UC_RISCV_REG_PC) == STOP
        return struct.unpack('<i', struct.pack('<I', cpu.reg_read(UC_RISCV_REG_A0)))[0]

    def import_point(out):
        return nested('c3peer_import', attrs, data, 65, out)

    # Fail each integer allocation during the native P-256 import, then retry
    # in the same parser lease. An error must discard the partial PSA key.
    limb_calls = 0
    fail_at = 0

    def trace_limb(uc, address, length, user):
        nonlocal limb_calls
        limb_calls += 1
        if limb_calls == fail_at:
            uc.reg_write(UC_RISCV_REG_A0, 0)
            uc.reg_write(UC_RISCV_REG_PC, uc.reg_read(UC_RISCV_REG_RA))

    limb_entry = probe.symbols['c3peer_limbs']
    hook = cpu.hook_add(UC_HOOK_CODE, trace_limb, begin=limb_entry, end=limb_entry)
    # Both coordinates zero encode an invalid P-256 point with the right shape.
    invalid = handles + 16
    cpu.mem_write(invalid, bytes([4]) + bytes(64))
    assert nested('c3peer_import', attrs, invalid, 65, handles) < 0 and probe.live == live
    limb_calls = 0
    start = len(probe.allocations)
    assert import_point(handles) == 0 and probe.live == live
    heap_requests = len(probe.allocations) - start
    limb_requests = limb_calls
    assert limb_requests > 0
    for index in range(1, limb_requests + 1):
        assert nested('psa_destroy_key', int.from_bytes(cpu.mem_read(handles, 4), 'little')) == 0
        fail_at = index
        limb_calls = 0
        assert import_point(handles) < 0 and limb_calls >= index and probe.live == live
        fail_at = 0
        limb_calls = 0
        assert import_point(handles) == 0 and limb_calls == limb_requests and probe.live == live
    cpu.hook_del(hook)
    offset = bytes(cpu.mem_read(workspace, 4096)).index(point)
    assert import_point(handles + 4) < 0 and probe.live == live
    probe.task_id = 2
    assert import_point(handles + 4) == 0 and sum(probe.live.values()) == sum(live.values()) + 65
    assert nested('psa_destroy_key', int.from_bytes(cpu.mem_read(handles + 4, 4), 'little')) == 0
    probe.task_id = 1
    assert cpu.mem_read(workspace + offset, 65) == point
    assert nested('psa_destroy_key', int.from_bytes(cpu.mem_read(handles, 4), 'little')) == 0
    assert not any(cpu.mem_read(workspace + offset, 65)) and probe.live == live
    cpu.context_restore(saved)
    cpu.mem_write(STACK, stack)
    cpu.emu_start(entry, STOP, count=2000000)
    assert cpu.reg_read(UC_RISCV_REG_PC) == STOP and cpu.reg_read(UC_RISCV_REG_A0) == 0
    return dict(heap_requests=heap_requests, limb_failures_retried=limb_requests)


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
    import_result = None
    if 'c3peer_import' in probe.symbols:
        import_result = check_public_copy(probe, chain, source, len(data))
    else:
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

    # The typed EC key remains separate from the inverse and all ordinary slots.
    key_bytes = 0
    if 'c3peer_key' in probe.symbols:
        key_bytes = 128
        key = run('c3peer_key', 1, key_bytes)
        assert workspace <= key <= workspace + 4096 - key_bytes and key % 4 == 0
        for start, size in [(p, 96) for p in slots] + [(inverse, 160)]:
            assert start + size <= key or key + key_bytes <= start
        assert not any(cpu.mem_read(key, key_bytes))
        cpu.mem_write(key, bytes(range(key_bytes)))
        assert run('c3peer_key', 1, key_bytes) == 0 and probe.live == retained
        for task, count, size in ((1, 2, 64), (2, 1, key_bytes)):
            probe.task_id = task
            other = run('c3peer_key', count, size)
            assert other != key and probe.live[other] == key_bytes
            run('c3peer_key_free', other)
        probe.task_id = 1
        assert cpu.mem_read(key, key_bytes) == bytes(range(key_bytes))
        run('c3peer_key_free', key)
        assert not any(cpu.mem_read(key, key_bytes))
        assert run('c3peer_key', 1, key_bytes) == key
        run('c3peer_key_free', key)

    if 'c3peer_points' in probe.symbols:
        table = run('c3peer_points', 2, 24)
        assert workspace <= table <= workspace + 4096 - 48 and table not in probe.live
        assert not any(cpu.mem_read(table, 48)) and not run('c3peer_points', 2, 24)
        marker_bytes_table = bytes(range(48))
        cpu.mem_write(table, marker_bytes_table)
        other = run('c3peer_points', 1, 48)
        assert other != table and probe.live[other] == 48
        run('c3peer_points_free', other)
        probe.task_id = 2
        other = run('c3peer_points', 2, 24)
        assert other != table and probe.live[other] == 48
        run('c3peer_points_free', other)
        probe.task_id = 1
        assert cpu.mem_read(table, 48) == marker_bytes_table
        run('c3peer_points_free', table)
        assert not any(cpu.mem_read(table, 48))
        assert run('c3peer_points', 2, 24) == table
        run('c3peer_points_free', table)

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
    return dict(parser_key_import=import_result, elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), slots=len(slots),
                point_table_bytes=48 if 'c3peer_points' in probe.symbols else 0,
                slot_bytes=96, inverse_bytes=160, key_bytes=key_bytes,
                public_data_bytes=65 if 'c3peer_import' in probe.symbols else 0, exhaustion_verified=True, foreign_task_verified=True,
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
