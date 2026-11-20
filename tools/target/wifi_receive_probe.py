"""Exercise the compiled station receive path with a simulated driver and input queue."""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1
from .tcp_packet_probe import target_layout
from .tls_fragment_probe import Probe, SSL, STOP
from .tls_output_probe import OutputProbe


def verify(elf, gdb):
    layout = target_layout(elf, gdb, ("netif", "pbuf"), ())
    cpu = Probe(elf)
    netif = SSL + 0x8000
    interface = SSL + 0x9000
    input_callback = STOP + 0x200
    accepted = []
    returned = []
    refuse_input = False
    depth = 0

    def run(name, *args):
        return OutputProbe.run(cpu, name, *args)

    def field(kind, base, name, value=None):
        offset, size = layout[kind][name]
        if value is not None:
            cpu.uc.mem_write(base + offset, value.to_bytes(size, "little"))
        return int.from_bytes(cpu.uc.mem_read(base + offset, size), "little")

    def reject_heap(*_):
        raise AssertionError("station receive reached the general allocator")

    def enter(*_):
        nonlocal depth
        depth += 1
        cpu.finish(depth - 1)

    def leave(*_):
        nonlocal depth
        depth -= 1
        assert depth == cpu.uc.reg_read(UC_RISCV_REG_A0)
        cpu.finish(0)

    def upstream(*_):
        assert not depth and cpu.uc.reg_read(UC_RISCV_REG_A1) == netif
        if not refuse_input:
            accepted.append(cpu.uc.reg_read(UC_RISCV_REG_A0))
        cpu.finish(-1 if refuse_input else 0)

    def driver_free(*_):
        assert not depth
        returned.append((cpu.uc.reg_read(UC_RISCV_REG_A0), cpu.uc.reg_read(UC_RISCV_REG_A1)))
        cpu.finish(0)

    for name in ("malloc", "free", "mem_malloc", "mem_free"):
        cpu.hook(cpu.symbols[name], reject_heap)
    for name in ("sys_arch_protect", "c3bport_enter"):
        cpu.hook(cpu.symbols[name], enter)
    for name in ("sys_arch_unprotect", "c3bport_leave"):
        cpu.hook(cpu.symbols[name], leave)
    cpu.hook(cpu.symbols["esp_netif_free_rx_buffer"], driver_free)
    cpu.hook(input_callback, upstream)
    field("netif", netif, "flags", 1)  # NETIF_FLAG_UP in the pinned SDK.
    field("netif", netif, "state", interface)
    field("netif", netif, "input", input_callback)

    def deliver(index, length=32):
        payload = SSL + 0x10000 + index * 64
        token = SSL + 0x18000 + index * 8
        result = run("wlanif_input", netif, payload, length, token)
        if result == 0:
            packet = accepted[-1]
            assert field("pbuf", packet, "payload") == payload
            assert field("pbuf", packet, "len") == field("pbuf", packet, "tot_len") == length
        return result, (interface, token)

    leases = []
    for index in range(8):
        status, lease = deliver(index)
        assert status == 0
        leases.append(lease)
    assert len(set(accepted)) == 8 and not returned
    first = accepted[0]
    run("pbuf_ref", first)
    assert run("pbuf_free", first) == 0 and not returned
    status, ninth = deliver(8)
    assert status == 0x101 and returned == [ninth] and len(accepted) == 8  # ESP_ERR_NO_MEM
    assert run("pbuf_free", first) == 1 and returned[-1] == leases[0]
    status, replacement = deliver(9)
    assert status == 0 and accepted[-1] == first
    for packet in accepted[1:]:
        assert run("pbuf_free", packet) == 1
    assert returned == [ninth, leases[0], *leases[1:], replacement]

    refuse_input = True
    status, refused = deliver(10)
    assert status == -1 and returned[-1] == refused and len(accepted) == 9
    refuse_input = False
    status, oversize = deliver(11, 65536)
    assert status == 0x101 and returned[-1] == oversize and len(accepted) == 9
    status, final = deliver(12, 0)
    assert status == 0 and run("pbuf_free", accepted[-1]) == 1
    assert returned == [ninth, *leases, replacement, refused, oversize, final] and not depth
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), held_frames=len(leases),
                distinct_driver_returns=len(set(returned)), general_heap_calls=0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--gdb", default="riscv32-esp-elf-gdb")
    arguments = parser.parse_args()
    print(json.dumps(verify(arguments.elf, arguments.gdb), indent=2))
