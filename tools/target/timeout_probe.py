"""Exercise the station's linked lwIP timeout list and fixed record pool on RV32."""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn.riscv_const import UC_RISCV_REG_A0
from .tcp_packet_probe import target_layout
from .tls_fragment_probe import Probe, SSL, STOP
from .tls_output_probe import OutputProbe


def verify(elf, gdb):
    layout = target_layout(elf, gdb, ("sys_timeo", "lwip_cyclic_timer"), ("MEMP_SYS_TIMEOUT",))
    cpu = Probe(elf)
    start = 0xffffff00
    now = start
    callback = STOP + 0x240
    fired = []
    cyclic = []
    kind = layout["MEMP_SYS_TIMEOUT"]

    def run(name, *args):
        return OutputProbe.run(cpu, name, *args)

    def word(address):
        return int.from_bytes(cpu.uc.mem_read(address, 4), "little")

    def field(record, address, name):
        offset, width = layout[record][name]
        assert width == 4
        return word(address + offset)

    def pending():
        entries = {}
        address = word(cpu.symbols["next_timeout"])
        while address:
            assert address not in entries and len(entries) < 12
            entries[address] = tuple(field("sys_timeo", address, n) for n in ("h", "arg"))
            address = field("sys_timeo", address, "next")
        return entries

    def dispatch(*_):
        fired.append(cpu.uc.reg_read(UC_RISCV_REG_A0))
        cpu.finish(0)

    def peer_timer(name):
        def handler(*_):
            cyclic.append(name)
            cpu.finish(0)
        return handler

    def reject_heap(*_):
        raise AssertionError("native timeout list reached the general heap")

    for name in ("malloc", "free", "mem_malloc", "mem_free", "memp_malloc", "memp_free"):
        cpu.hook(cpu.symbols[name], reject_heap)
    for name in ("c3bport_enter", "c3bport_leave"):
        cpu.hook(cpu.symbols[name], lambda *_: cpu.finish(0))
    cpu.hook(cpu.symbols["sys_now"], lambda *_: cpu.finish(now))
    cpu.hook(callback, dispatch)
    for name in ("etharp_tmr", "dhcp_coarse_tmr"):
        cpu.hook(cpu.symbols[name], peer_timer(name))

    run("sys_timeouts_init")
    periodic = pending()
    assert len(periodic) == 2
    assert {field("lwip_cyclic_timer", arg, "handler") for _, arg in periodic.values()} == {
        cpu.symbols["etharp_tmr"], cpu.symbols["dhcp_coarse_tmr"]}
    # Ask twice while a TCP connection exists. Clear it before the timer fires.
    tcp_list = cpu.symbols["tcp_active_pcbs"]
    cpu.uc.mem_write(tcp_list, (SSL + 0xa000).to_bytes(4, "little"))
    run("tcp_timer_needed")
    run("tcp_timer_needed")
    cpu.uc.mem_write(tcp_list, bytes(4))
    assert len(pending()) == 3

    for delay, token in ((20, 1), (10, 2), (10, 3), (40, 4), (60, 4),
                         (1200, 5), (1200, 6), (1200, 7), (1200, 8)):
        run("sys_timeout", delay, callback, token)
    full = set(pending())
    assert len(full) == 12 and not run("c3lrec_alloc", kind)
    run("sys_untimeout", callback, 4)
    assert len(pending()) == 11
    run("sys_timeout", 1200, callback, 9)
    assert set(pending()) == full and run("sys_timeouts_sleeptime") == 10
    now = (start + 10) & 0xffffffff
    run("sys_check_timeouts")
    assert fired == [2, 3]
    now = (start + 60) & 0xffffffff
    run("sys_check_timeouts")
    assert fired == [2, 3, 1, 4] and run("sys_timeouts_sleeptime") == 190
    now = (start + 250) & 0xffffffff
    run("sys_check_timeouts")
    assert len(pending()) == 7  # The TCP timer stopped after the last connection closed.

    for token in range(10, 15):
        run("sys_timeout", 5000, callback, token)
    assert set(pending()) == full
    now = (start + 1000) & 0xffffffff  # Cross the 32-bit clock rollover.
    run("sys_check_timeouts")
    assert sorted(cyclic) == ["dhcp_coarse_tmr", "etharp_tmr"]
    assert set(pending()) == full and run("sys_timeouts_sleeptime") == 200
    now = (start + 1200) & 0xffffffff
    run("sys_check_timeouts")
    assert fired == [2, 3, 1, 4, 5, 6, 7, 8, 9]
    for handler, arg in list(pending().values()):
        run("sys_untimeout", handler, arg)
    assert not pending()
    assert run("sys_timeouts_sleeptime") & 0xffffffff == 0xffffffff
    recovered = [run("c3lrec_alloc", kind) for _ in range(12)]
    assert set(recovered) == full and not run("c3lrec_alloc", kind)
    for record in recovered:
        run("c3lrec_free", kind, record)
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), slots=len(recovered),
                dispatched=fired, rescheduled_at_capacity=cyclic, timeout_heap_calls=0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--gdb", default="riscv32-esp-elf-gdb")
    args = parser.parse_args()
    print(json.dumps(verify(args.elf, args.gdb), indent=2))
