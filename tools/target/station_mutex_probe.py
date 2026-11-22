"""Replay the station's linked pthread mutex allocation, rollback and destruction."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile
from unicorn.riscv_const import UC_RISCV_REG_A0

from .tls_fragment_probe import SSL
from .tls_output_probe import OutputProbe


def verify(elf):
    probe = OutputProbe(elf)
    cpu, symbols = probe.uc, probe.symbols
    handles = SSL + 0x18000
    attr = handles + 64
    with elf.open("rb") as stream:
        table = ELFFile(stream).get_section_by_name(".symtab")
        arrays = {sym["st_value"] for sym in table.get_symbol_by_name("g_mutexes")
                  if sym["st_size"] == 5 * 8}

    def word(at):
        return int.from_bytes(cpu.mem_read(at, 4), "little")

    def reject(*_):
        raise AssertionError("native mutex path called the general heap")

    # Double the interrupt and queue boundaries; execute the actual slot and SDK code.
    masked = False
    accept_create = False
    accept_take = True
    leased = set()

    def enter(*_):
        nonlocal masked
        assert not masked
        masked = True
        probe.finish(8)

    def leave(*_):
        nonlocal masked
        assert masked and cpu.reg_read(UC_RISCV_REG_A0) == 8
        masked = False
        probe.finish(0)

    def create(*_):
        assert not masked
        handle = 0
        if accept_create:
            handle = next(i for i in range(1, 6) if i not in leased)
            leased.add(handle)
        probe.finish(handle)

    def delete(*_):
        leased.remove(cpu.reg_read(UC_RISCV_REG_A0))
        probe.finish(0)

    def queue(success):
        def op(*_):
            assert cpu.reg_read(UC_RISCV_REG_A0) in leased
            probe.finish(int(success()))
        return op

    for name in ("malloc", "calloc", "free"):
        probe.hook(symbols[name], reject)
    probe.hook(symbols["c3bport_enter"], enter)
    probe.hook(symbols["c3bport_leave"], leave)
    probe.hook(symbols["c3nq_mutex"], create)
    probe.hook(symbols["c3nq_delete"], delete)
    for name in ("c3nq_take", "c3nq_take_recursive"):
        probe.hook(symbols[name], queue(lambda: accept_take))
    for name in ("c3nq_send", "c3nq_give_recursive"):
        probe.hook(symbols[name], queue(lambda: True))

    # Native validation and failed semaphore creation leave the output untouched.
    sentinel = 0x12345678
    cpu.mem_write(handles, struct.pack("<6I", *([sentinel] * 6)))
    assert probe.run("pthread_mutex_init", 0, 0) == 22
    assert probe.run("pthread_mutex_init", handles, attr) == 22
    cpu.mem_write(attr, struct.pack("<II", 1, 9))
    assert probe.run("pthread_mutex_init", handles, attr) == 22
    assert probe.run("pthread_mutex_init", handles, 0) == 11
    assert word(handles) == sentinel and not leased

    accept_create = True
    for i in range(5):
        assert probe.run("pthread_mutex_init", handles + 4 * i, 0) == 0
    records = [word(handles + 4 * i) for i in range(5)]
    assert records[0] in arrays and records == list(range(records[0], records[0] + 40, 8))
    before = bytes(cpu.mem_read(records[0], 40))
    assert probe.run("pthread_mutex_init", handles + 20, 0) == 12
    assert word(handles + 20) == sentinel and bytes(cpu.mem_read(records[0], 40)) == before

    accept_take = False
    assert probe.run("pthread_mutex_destroy", handles + 8) == 16
    assert probe.run("pthread_mutex_init", handles + 20, 0) == 12 and len(leased) == 5
    accept_take = True
    assert probe.run("pthread_mutex_destroy", handles + 8) == 0 and len(leased) == 4
    # The SDK reads is_initialized and type; 1 selects the recursive path.
    cpu.mem_write(attr, struct.pack("<II", 1, 1))
    assert probe.run("pthread_mutex_init", handles + 8, attr) == 0
    assert word(handles + 8) == records[2] and word(records[2] + 4) == 1
    for i in range(5):
        assert probe.run("pthread_mutex_destroy", handles + 4 * i) == 0
    assert not leased and not masked
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                fixed_records=5, record_bytes=8, general_heap_calls=0,
                semaphore_failure_rolled_back=True, busy_destroy_preserved=True,
                exhausted_without_heap=True, recursive_reuse=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    print(json.dumps(verify(parser.parse_args().elf), indent=2))
