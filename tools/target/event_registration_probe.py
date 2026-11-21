"""Replay default-loop registration and legacy handler replacement on RV32."""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn import UC_HOOK_CODE
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A4, UC_RISCV_REG_A5

from .tcp_packet_probe import target_layout
from .tls_fragment_probe import Probe, SSL
from .tls_output_probe import OutputProbe


def demo_handlers(first, second, third):
    return [(first, n) for n in range(4)] + [
        (second, 0), (second, 0), (second, 1), (third, 0xffffffff)]


def verify(elf, gdb):
    cpu = Probe(elf)
    if "c3ereg_alloc" in cpu.symbols:
        return verify_fixed(cpu, elf, gdb)
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
    registrations = demo_handlers(first, second, third)
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


def verify_fixed(cpu, elf, gdb):
    pools = {"loop": (1, 24), "bases": (3, 16), "nodes": (16, 12), "contexts": (17, 8)}
    def symbol(name):
        return "(unsigned)&'c3ereg.c'::" + name

    post_size = "sizeof(esp_event_post_instance_t)"
    fields = [symbol(prefix + name) for name in pools for prefix in ("g_", "gb_")]
    layout = target_layout(elf, gdb, ("esp_event_handler_context",), (post_size, *fields))
    def call(name, *args):
        return OutputProbe.run(cpu, name, *args)

    def snapshot():
        records = {}
        for name, (count, size) in pools.items():
            flags = bytes(cpu.uc.mem_read(layout[symbol("gb_" + name)], count))
            for i, used in enumerate(flags):
                if used:
                    address = layout[symbol("g_" + name)] + i * size
                    records[address] = bytes(cpu.uc.mem_read(address, size))
        return records

    def reject(*_):
        raise AssertionError("general record allocator reached")

    def constant(name, value):
        cpu.hook(cpu.symbols[name], lambda *_: cpu.finish(value))

    cpu.hook(cpu.symbols["heap_caps_calloc"], reject)
    for name in ("c3bport_enter", "c3bport_leave", "c3nq_delete", "__wrap_xTaskGetTickCount"):
        constant(name, 0)
    for name in ("c3nq_create", "c3nq_take_recursive", "c3nq_give_recursive", "__wrap_xTaskGetCurrentTaskHandle"):
        constant(name, 1)
    mutex_ready = False
    cpu.hook(cpu.symbols["c3nq_mutex"], lambda *_: cpu.finish(int(mutex_ready)))
    args, output = SSL + 0x8000, SSL + 0x8100
    cpu.uc.mem_write(args, (32).to_bytes(4, "little") + bytes(28))
    assert call("esp_event_loop_create", args, output) != 0 and not snapshot()
    mutex_ready = True
    assert call("esp_event_loop_create", args, output) == 0
    loop = int.from_bytes(cpu.uc.mem_read(output, 4), "little")
    assert loop == layout[symbol("g_loop")] and call("esp_event_loop_create", args, output) != 0
    only_loop = snapshot()

    handlers = demo_handlers(args + 64, args + 68, args + 72)
    def register(index, argument):
        base, event_id = handlers[index]
        cpu.uc.reg_write(UC_RISCV_REG_A4, argument)
        return call("esp_event_handler_register_with", loop, base, event_id, 0x1000 + index)

    def register_all():
        for index in range(len(handlers)):
            assert register(index, 1) == 0
        live = snapshot()
        assert len(live) == 27 and sum(map(len, live.values())) == 316
        return live

    live = register_all()
    # Fill each provisional allocation class independently. Every failure must
    # preserve the complete tree and release any already-acquired record.
    caps = 1 << 12  # MALLOC_CAP_DEFAULT in the pinned SDK
    assert not call("c3ereg_alloc", 2, 12, caps) and not call("c3ereg_alloc", 1, 12, 0)
    for size, spare in ((12, 1), (8, 9)):
        held = [call("c3ereg_alloc", 1, size, caps) for _ in range(spare)]
        assert all(held) and not call("c3ereg_alloc", 1, size, caps)
        assert register(0, 2) != 0
        for ptr in held:
            call("c3ereg_free", ptr)
        assert snapshot() == live
    assert register(0, 2) == 0
    replaced = snapshot()
    changed, = [ptr for ptr in live if live[ptr] != replaced[ptr]]
    offset, width = layout["esp_event_handler_context"]["arg"]
    assert len(live[changed]) == 8 and int.from_bytes(replaced[changed][offset:offset+width], "little") == 2

    # Defer all eight removals, then drive the real SDK loop. Only the queued
    # event bodies are supplied by libc. Native queue and mutex calls are simulated.
    queue, payloads, sizes = [], {}, []
    def payload(*_):
        size = cpu.uc.reg_read(UC_RISCV_REG_A1)
        assert cpu.uc.reg_read(UC_RISCV_REG_A0) == 1 and size <= 64
        ptr = SSL + 0x10000 + 64 * len(sizes)
        payloads[ptr] = size
        sizes.append(size)
        cpu.uc.mem_write(ptr, bytes(size))
        cpu.finish(ptr)

    def release(*_):
        ptr = cpu.uc.reg_read(UC_RISCV_REG_A0)
        if ptr:
            del payloads[ptr]
        cpu.finish(0)

    accept_send = False
    def send(*_):
        if not accept_send:
            cpu.finish(0)
            return
        queue.append(bytes(cpu.uc.mem_read(cpu.uc.reg_read(UC_RISCV_REG_A1), layout[post_size])))
        cpu.finish(1)

    def receive(*_):
        if queue:
            cpu.uc.mem_write(cpu.uc.reg_read(UC_RISCV_REG_A1), queue.pop(0))
            cpu.finish(1)
        else:
            cpu.finish(0)

    cpu.hook(cpu.symbols["calloc"], payload)
    cpu.hook(cpu.symbols["free"], release)
    cpu.hook(cpu.symbols["c3nq_send"], send)
    cpu.hook(cpu.symbols["c3nq_receive"], receive)
    # A 16-byte posted body is the same size as a base record. A failed
    # queue send must return it to libc without disturbing any fixed record.
    cpu.uc.reg_write(UC_RISCV_REG_A4, 16)
    cpu.uc.reg_write(UC_RISCV_REG_A5, 0)
    assert call("esp_event_post_to", loop, handlers[0][0], 0, args) != 0
    assert sizes == [16] and not payloads and snapshot() == replaced
    accept_send = True
    constant("c3nq_take", 0)
    for index, (base, event_id) in enumerate(handlers):
        assert call("esp_event_handler_unregister_with", loop, base, event_id, 0x1000 + index) == 0
    pending = snapshot()
    assert sizes == [16] + [20] * 8 and len(payloads) == len(queue) == 8
    assert len(pending) == 35 and sum(map(len, pending.values())) == 380
    # Even with all eight removals pending, an update can still obtain
    # its provisional handler and context and release both.
    peaks = []
    release_entry = cpu.symbols["c3ereg_free"]
    observer = cpu.uc.hook_add(UC_HOOK_CODE, lambda *_: peaks.append(sum(map(len, snapshot().values()))),
                               begin=release_entry, end=release_entry)
    assert register(0, 2) == 0 and snapshot() == pending
    cpu.uc.hook_del(observer)
    assert max(peaks) == 400
    deferred_bytes = sum(payloads.values())
    # Post a station-sized event while all eight cleanup bodies still own
    # their payloads. Use a distinct event base with no registered handler.
    body = bytes(range(48))
    source = args + 256
    cpu.uc.mem_write(source, body)
    cpu.uc.reg_write(UC_RISCV_REG_A4, len(body))
    cpu.uc.reg_write(UC_RISCV_REG_A5, 0)
    assert call("esp_event_post_to", loop, args + 76, 4, source) == 0
    cpu.uc.mem_write(source, bytes(len(body)))
    station_copy, = [ptr for ptr, length in payloads.items() if length == len(body)]
    assert bytes(cpu.uc.mem_read(station_copy, len(body))) == body
    combined_bytes = sum(payloads.values())
    assert len(queue) == len(payloads) == 9 and combined_bytes == 208
    assert call("esp_event_loop_run", loop, 1) == 0
    assert not queue and not payloads and snapshot().keys() == only_loop.keys()
    assert register_all().keys() == live.keys()
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                fixed_registration_records=27, fixed_live_bytes=316, general_record_requests=0,
                replacement_failures_checked=2, simultaneous_deferred_removals=8,
                fixed_bytes_pending_cleanup=380, peak_fixed_record_bytes=max(peaks),
                deferred_payload_bytes=deferred_bytes,
                cleanup_plus_station_payload_bytes=combined_bytes,
                failed_post_payload_returned=True,
                cleanup_and_reuse_checked=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--gdb", default="riscv32-esp-elf-gdb")
    args = parser.parse_args()
    print(json.dumps(verify(args.elf, args.gdb), indent=2))
