"""Replay the WPA legacy timer lifetime using the linked RV32 timer list."""
import argparse
import hashlib
import json
from pathlib import Path

from elftools.elf.elffile import ELFFile
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


def verify_fixed(elf):
    probe = OutputProbe(elf)
    cpu, symbols = probe.uc, probe.symbols
    record = symbols["g_c3wpatmr"]
    with elf.open("rb") as stream:
        obj, = ELFFile(stream).get_section_by_name(".symtab").get_symbol_by_name("g_c3wpatmr")
        assert obj["st_size"] == 32 and obj["st_value"] == record
    callback = symbols["c3wpatmr_callback"]
    deliveries = []
    now = 100

    def reject(*_):
        raise AssertionError("fixed WPA timer reached native allocation or deletion")

    def clock(*_):
        cpu.reg_write(UC_RISCV_REG_A1, 0)
        probe.finish(now)

    def deliver(*_):
        deliveries.append(now)
        probe.finish(0)

    for name in ("esp_timer_create", "esp_timer_delete", "heap_caps_calloc", "free"):
        probe.hook(symbols[name], reject)
    for name in ("timer_list_lock", "timer_list_unlock", "esp_timer_impl_set_alarm_id"):
        probe.hook(symbols[name], lambda *_: probe.finish(0))
    probe.hook(symbols["esp_timer_get_time"], clock)
    probe.hook(callback, deliver)
    cpu.mem_write(symbols["s_timer_task"], (1).to_bytes(4, "little"))
    probe.run("eloop_reset_state_locked")
    assert int.from_bytes(cpu.mem_read(record + 16, 4), "little") == callback
    # Put one pending entry on the event-loop list and take its real 100 ms retry path.
    pending = SSL + 0x19000
    head = symbols["eloop"]
    cpu.mem_write(head, pending.to_bytes(4, "little") * 2)
    cpu.mem_write(pending, head.to_bytes(4, "little") * 2)
    probe.hook(symbols["eloop_get_reltime_now"], lambda *_: probe.finish(-1))
    probe.run("eloop_arm_next_locked")
    assert int.from_bytes(cpu.mem_read(record, 8), "little") == now + 100000
    # Reset with an outstanding deadline, then cross its old expiry.
    probe.run("eloop_reset_state_locked")
    now = 100100
    probe.run("timer_process_alarm", 0)
    assert not deliveries
    assert probe.run("esp_timer_start_once", record, 20, 0) == 0
    now = 100120
    probe.run("timer_process_alarm", 0)
    probe.run("timer_process_alarm", 0)
    assert deliveries == [100120]
    assert probe.run("esp_timer_start_once", record, 1, 0) == 0
    assert probe.run("esp_timer_stop", record) == 0
    probe.run("eloop_reset_state_locked")
    now = 100121
    probe.run("timer_process_alarm", 0)
    assert deliveries == [100120]
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                fixed_address=record, callbacks=len(deliveries), general_heap_calls=0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--fixed", action="store_true", help="Verify the permanent station timer")
    args = parser.parse_args()
    print(json.dumps((verify_fixed if args.fixed else verify)(args.elf), indent=2))
