"""Replay the WPA legacy timer lifetime using the linked RV32 timer list."""
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
    wrapper = SSL + 0x18000
    first = wrapper + 256
    second = first + 32
    created = []
    released = []

    def allocate(*_):
        assert cpu.reg_read(UC_RISCV_REG_A0) * cpu.reg_read(UC_RISCV_REG_A1) == 32
        address = (first, second)[len(created)]
        created.append(address)
        cpu.mem_write(address, bytes(32))
        probe.finish(address)

    def release(*_):
        address = cpu.reg_read(UC_RISCV_REG_A0)
        assert address in created and address not in released
        released.append(address)
        probe.finish(0)

    def clock(*_):
        cpu.reg_write(UC_RISCV_REG_A1, 0)
        probe.finish(100)

    probe.hook(symbols["heap_caps_calloc"], allocate)
    probe.hook(symbols["free"], release)
    probe.hook(symbols["esp_timer_get_time"], clock)
    for name in ("timer_list_lock", "timer_list_unlock", "esp_timer_impl_set_alarm_id"):
        probe.hook(symbols[name], lambda *_: probe.finish(0))
    cpu.mem_write(symbols["s_timer_task"], (1).to_bytes(4, "little"))
    callback = symbols["eloop_run_timer"]
    probe.run("ets_timer_setfn", wrapper, callback, 0)
    probe.run("ets_timer_setfn", wrapper, callback, 0)
    assert created == [first]
    probe.run("ets_timer_done", wrapper)
    assert not released
    probe.run("ets_timer_setfn", wrapper, callback, 0)
    assert created == [first, second] and not released
    probe.run("timer_process_alarm", 0)
    assert released == [first]
    probe.run("ets_timer_done", wrapper)
    assert released == [first]
    probe.run("timer_process_alarm", 0)
    assert released == [first, second]
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                peak_live_bytes=64, created=len(created), released=len(released))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    print(json.dumps(verify(parser.parse_args().elf), indent=2))
