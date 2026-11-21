"""Check the compiled station semaphore's lifetime and thread isolation."""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn.riscv_const import UC_RISCV_REG_A0

from .tls_fragment_probe import SSL
from .tls_output_probe import OutputProbe


def verify_station_failure(elf):
    """An exhausted fixed queue must stop the station before SDK network setup."""
    probe = OutputProbe(elf)
    address = SSL + 0x18000
    probe.uc.mem_write(address, b'station\0' + b'password\0')
    calls = []

    def reject(*_):
        calls.append('capacity')
        probe.finish(0xffffffff)

    def unexpected(*_):
        raise AssertionError('Station entered SDK network setup after exhaustion')

    def length(*_):
        ptr = probe.uc.reg_read(UC_RISCV_REG_A0)
        probe.finish(len(bytes(probe.uc.mem_read(ptr, 33)).split(bytes(1), 1)[0]))

    for name in ('strlen', '__rom_ets_strlen'):
        if name in probe.symbols:
            probe.hook(probe.symbols[name], length)
    probe.hook(probe.symbols['c3lsem_start'], reject)
    probe.hook(probe.symbols['esp_netif_init'], unexpected)
    assert probe.run('c3wsta_start', address, address + 8, 0) == -1
    assert calls == ['capacity']


def verify(elf):
    probe = OutputProbe(elf)
    cpu, symbols = probe.uc, probe.symbols
    current = 0
    creates, drains, foreign = [], [], []

    def create(*_):
        args = [cpu.reg_read(UC_RISCV_REG_A0 + index) for index in range(5)]
        assert args[:3] == [1, 0, 0] and args[4] == 3
        creates.append(args[3])
        probe.finish(0 if len(creates) == 1 else args[3])

    def drain(*_):
        drains.append((cpu.reg_read(UC_RISCV_REG_A0), cpu.reg_read(UC_RISCV_REG_A0 + 1)))
        probe.finish(1 if len(drains) == 1 else 0)

    def delegate(operation, result):
        def record(*_):
            foreign.append(operation)
            probe.finish(result)
        return record

    probe.hook(symbols['c3bport_task'], lambda *_: probe.finish(current))
    probe.hook(symbols['c3nq_static'], create)
    probe.hook(symbols['c3nq_take'], drain)
    for operation, result in [('get', 19), ('init', 37), ('deinit', 0)]:
        assert symbols['sys_thread_sem_' + operation] == symbols['c3lsem_' + operation]
        probe.hook(symbols['c3lsem_native_' + operation], delegate(operation, result))

    assert probe.run('c3lsem_start') == -1 and not creates
    current = 2
    assert probe.run('c3lsem_start') == -1
    assert probe.run('sys_thread_sem_get') == 19
    assert probe.run('c3lsem_start') == 0
    record = probe.run('sys_thread_sem_get')
    assert record and creates == [record, record]
    pattern = bytes(range(1, 85))
    cpu.mem_write(record, pattern)
    assert probe.run('c3lsem_start') == 0
    assert probe.run('sys_thread_sem_init') == record
    current = 5
    assert probe.run('c3lsem_start') == -1
    assert probe.run('sys_thread_sem_get') == 19 and probe.run('sys_thread_sem_init') == 37
    probe.run('sys_thread_sem_deinit')
    assert foreign == ['get', 'get', 'init', 'deinit'] and not drains
    assert bytes(cpu.mem_read(record, 84)) == pattern
    current = 2
    probe.run('sys_thread_sem_deinit')
    probe.run('sys_thread_sem_deinit')
    assert drains == [(record, 0), (record, 0)]
    assert probe.run('sys_thread_sem_get') == record and creates == [record, record]
    assert not probe.allocations
    verify_station_failure(elf)
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                passed=True, record_bytes=84, observed_allocations=0,
                doubles=['native task identity', 'binary queue creation and nonblocking take',
                         'foreign per-thread SDK functions', 'ROM byte operations'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        parser.error('Assertions must be enabled')
    result = verify(args.elf)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print('Compiled controller semaphore isolation, exhaustion and reuse passed.')
