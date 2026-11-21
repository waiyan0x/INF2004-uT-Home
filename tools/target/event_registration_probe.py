"""Replay default-loop registration and legacy handler replacement on RV32."""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A4

from .tcp_packet_probe import target_layout
from .tls_fragment_probe import Probe, SSL
from .tls_output_probe import OutputProbe


def verify(elf, gdb):
    cpu = Probe(elf)
    loop_size = "sizeof(esp_event_loop_instance_t)"
    layout = target_layout(elf, gdb, ("esp_event_handler_context",), (loop_size,))
    # Start with empty loop storage and simulate a successful native lock.
    # Base identities and callback values stand in for the observed tree.
    loop = SSL + 0x8000
    first, second, third = (loop + n for n in (64, 68, 72))
    cpu.uc.mem_write(loop, bytes(layout[loop_size]))
    live = {}
    next_address = SSL + 0x10000
    requests = []
    peak = 0
    fail_at = None

    def calloc(*_):
        nonlocal next_address, peak
        count = cpu.uc.reg_read(UC_RISCV_REG_A0)
        size = cpu.uc.reg_read(UC_RISCV_REG_A1)
        requests.append(size)
        assert count == 1 and 0 < size <= 64
        if fail_at == len(requests):
            cpu.finish(0)
            return
        ptr = next_address
        next_address += 64
        live[ptr] = size
        peak = max(peak, sum(live.values()))
        cpu.uc.mem_write(ptr, bytes(size))
        cpu.finish(ptr)

    def release(*_):
        ptr = cpu.uc.reg_read(UC_RISCV_REG_A0)
        if ptr:
            del live[ptr]
        cpu.finish(0)

    cpu.hook(cpu.symbols["heap_caps_calloc"], calloc)
    cpu.hook(cpu.symbols["free"], release)
    for name in ("c3nq_take_recursive", "c3nq_give_recursive"):
        cpu.hook(cpu.symbols[name], lambda *_: cpu.finish(1))

    def register(base, event_id, handler, argument):
        cpu.uc.reg_write(UC_RISCV_REG_A4, argument)
        return OutputProbe.run(cpu, "esp_event_handler_register_with", loop, base, event_id, handler)

    # The board had four IDs on one base, two on another (one with two
    # handlers), and a base-wide listener on the third. Choose concrete IDs
    # only to reproduce those relationships.
    registrations = [(first, n) for n in range(4)] + [
        (second, 0), (second, 0), (second, 1), (third, 0xffffffff)]
    for index, (base, event_id) in enumerate(registrations):
        assert register(base, event_id, 0x1000 + index, 1) == 0
    retained = live.copy()
    assert len(retained) == 26 and sum(retained.values()) == 292
    # Fail each provisional request. The original tree must remain reachable
    # at the same addresses, with its original callback argument.
    def snapshot():
        return {ptr: bytes(cpu.uc.mem_read(ptr, size)) for ptr, size in
                {**retained, loop: layout[loop_size]}.items()}

    def context_field(ptr, name):
        offset, width = layout["esp_event_handler_context"][name]
        return int.from_bytes(cpu.uc.mem_read(ptr + offset, width), "little")

    before = snapshot()
    context, = [ptr for ptr, size in retained.items()
                if size == 8 and context_field(ptr, "handler") == 0x1000]
    for ordinal in (1, 2, None):
        requests.clear()
        fail_at = ordinal
        result = register(first, 0, 0x1000, 2)
        assert (result == 0) == (ordinal is None) and live == retained
        assert requests == ([12] if ordinal == 1 else [12, 8])
        if ordinal is not None:
            assert snapshot() == before
        else:
            assert context_field(context, "arg") == 2
            assert all(content == before[ptr] for ptr, content in snapshot().items() if ptr != context)
    assert peak == 312
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                retained_bytes_including_loop=sum(retained.values()) + layout[loop_size],
                peak_bytes_during_replacement_including_loop=peak + layout[loop_size],
                temporary_requests=requests, replacement_failures_checked=2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--gdb", default="riscv32-esp-elf-gdb")
    args = parser.parse_args()
    print(json.dumps(verify(args.elf, args.gdb), indent=2))
