"""Replay station libc startup with heap and SDK lock boundaries observed."""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_PC

from .tls_fragment_probe import SSL, STOP
from .tls_output_probe import OutputProbe


def check_startup(elf, environment_linked, fail_allocation):
    probe = OutputProbe(elf)
    symbols = probe.symbols
    assert ("setenv" in symbols) == environment_linked
    pointer = symbols["syscall_table_ptr"]
    if not any(start <= pointer <= end for start, end, _ in probe.uc.mem_regions()):
        probe.uc.mem_map(pointer & ~4095, 4096)
    table = SSL + 0x18000
    probe.uc.mem_write(table, b"XXXX")
    requests = []
    locks = []

    aborted = False

    def allocate(*_args):
        requests.append(probe.uc.reg_read(UC_RISCV_REG_A0))
        probe.finish(0 if fail_allocation else table)

    def abort(*_args):
        nonlocal aborted
        aborted = True
        probe.uc.reg_write(UC_RISCV_REG_PC, STOP)

    def initialize_locks(*_args):
        locks.append(True)
        probe.finish(0)

    probe.hook(symbols["malloc"], allocate)
    probe.hook(symbols["abort"], abort)
    probe.hook(symbols["esp_libc_locks_init"], initialize_locks)
    probe.run("esp_libc_init")
    assert requests == ([4] if environment_linked else [])
    assert aborted == (environment_linked and fail_allocation)
    assert locks == ([] if aborted else [True])
    assert int.from_bytes(probe.uc.mem_read(pointer, 4), "little") == symbols["s_stub_table"]
    if not environment_linked:
        fixed = next(value for name, value in symbols.items() if name.startswith("empty_environment."))
        assert int.from_bytes(probe.uc.mem_read(symbols["environ"], 4), "little") == fixed
        assert bytes(probe.uc.mem_read(fixed, 4)) == bytes(4)
    else:
        assert int.from_bytes(probe.uc.mem_read(symbols["environ"], 4), "little") == (0 if aborted else table)
        if not aborted:
            assert bytes(probe.uc.mem_read(table, 4)) == bytes(4)
    return dict(requests=requests, aborted=aborted, locks_initialized=bool(locks))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--environment-linked", action="store_true")
    args = parser.parse_args()
    print(json.dumps(dict(
        elf_sha256=hashlib.sha256(args.elf.read_bytes()).hexdigest(),
        available=check_startup(args.elf, args.environment_linked, False),
        exhausted=check_startup(args.elf, args.environment_linked, True),
    ), indent=2))


if __name__ == "__main__":
    main()
