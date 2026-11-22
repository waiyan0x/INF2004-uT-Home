"""Check the linked WPA timeout allocation, refusal and last-reference release."""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1

from .tls_fragment_probe import SSL
from .tls_output_probe import OutputProbe


def verify(elf):
    probe = OutputProbe(elf)
    cpu, symbols = probe.uc, probe.symbols
    record = SSL + 0x18000
    requests = []
    released = []

    def allocate(*_):
        requests.append((cpu.reg_read(UC_RISCV_REG_A0), cpu.reg_read(UC_RISCV_REG_A1)))
        probe.finish(record if len(requests) == 1 else 0)

    def release(*_):
        released.append(cpu.reg_read(UC_RISCV_REG_A0))
        probe.finish(0)

    probe.hook(symbols["calloc"], allocate)
    probe.hook(symbols["free"], release)
    for name in ("xPortSetInterruptMaskFromISR", "vPortClearInterruptMaskFromISR"):
        probe.hook(symbols[name], lambda *_: probe.finish(0))
    cpu.mem_write(record, bytes(64))
    assert probe.run("eloop_timeout_alloc") == record
    ref_count = record + 48
    assert int.from_bytes(cpu.mem_read(ref_count, 4), "little") == 1

    # A second owner still holds the record when the first one releases it.
    cpu.mem_write(ref_count, (2).to_bytes(4, "little"))
    probe.run("eloop_timeout_put", record)
    assert not released and int.from_bytes(cpu.mem_read(ref_count, 4), "little") == 1
    probe.run("eloop_timeout_put", record)
    assert released == [record]

    # ELOOP_STATE_RUNNING, then refuse the public registration's allocation.
    cpu.mem_write(symbols["eloop"] + 8, (1).to_bytes(4, "little"))
    assert probe.run("eloop_register_timeout", 1, 0, 0, 0) == -1
    assert requests == [(1, 64), (1, 64)] and released == [record]
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                record_bytes=64, allocations_requested=len(requests), final_frees=len(released))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    print(json.dumps(verify(parser.parse_args().elf), indent=2))
