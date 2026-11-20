"""Exercise the linked station DHCP client, message slots and final releases on RV32."""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn.riscv_const import UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3
from .tcp_packet_probe import target_layout
from .tls_fragment_probe import Probe, SSL
from .tls_output_probe import OutputProbe


def verify(elf, gdb):
    layout = target_layout(elf, gdb, ("netif", "dhcp", "pbuf"), (
        "PBUF_TRANSPORT", "PBUF_RAM", "ERR_MEM", "LWIP_NETIF_CLIENT_DATA_INDEX_DHCP",
        "DHCP_STATE_BOUND", "DHCP_STATE_RENEWING", "DHCP_STATE_REBINDING"))
    cpu = Probe(elf)
    station = SSL + 0x8000
    other = SSL + 0x9000
    mac = bytes.fromhex("020000000001")
    address = bytes([192, 0, 2, 7])
    server = bytes([192, 0, 2, 1])
    broadcast = bytes([255]) * 4
    frames = []
    retain = False

    def run(name, *args):
        return OutputProbe.run(cpu, name, *args)

    def field(kind, base, name, value=None):
        offset, width = layout[kind][name]
        if value is not None:
            cpu.uc.mem_write(base + offset, value.to_bytes(width, "little"))
        return int.from_bytes(cpu.uc.mem_read(base + offset, width), "little")

    def client(netif):
        offset = layout["netif"]["client_data"][0] + 4 * layout["LWIP_NETIF_CLIENT_DATA_INDEX_DHCP"]
        return int.from_bytes(cpu.uc.mem_read(netif + offset, 4), "little")

    def reject_heap(*_):
        raise AssertionError("DHCP state or message used the general heap")

    def send(*_):
        packet = cpu.uc.reg_read(UC_RISCV_REG_A1)
        dest = bytes(cpu.uc.mem_read(cpu.uc.reg_read(UC_RISCV_REG_A2), 4))
        assert cpu.uc.reg_read(UC_RISCV_REG_A3) == 67
        data = bytes(cpu.uc.mem_read(field("pbuf", packet, "payload"), field("pbuf", packet, "len")))
        assert 240 < len(data) < 309 and data[:3] == bytes([1, 1, 6]) and data[28:34] == mac
        assert data[236:242] == bytes.fromhex("638253633501")
        if retain:
            assert field("pbuf", packet, "ref") == 1
            field("pbuf", packet, "ref", 2)  # Model a downstream owner retaining this datagram.
        frames.append((packet, dest, data))
        cpu.finish(0)

    for name in ("malloc", "free", "mem_malloc", "mem_free"):
        cpu.hook(cpu.symbols[name], reject_heap)
    # The clock, RNG, interrupt guard, UDP transmission and address-change notifications
    # are outside this state/message test. The DHCP, timeout-list and pbuf operations run on RV32.
    for name in ("sys_now", "sys_arch_protect", "sys_arch_unprotect",
                 "c3bport_enter", "c3bport_leave", "netif_set_addr"):
        cpu.hook(cpu.symbols[name], lambda *_: cpu.finish(0))
    cpu.hook(cpu.symbols["esp_random"], lambda *_: cpu.finish(0x12345678))
    for name in ("udp_sendto_if", "udp_sendto_if_src"):
        cpu.hook(cpu.symbols[name], send)

    for netif in (station, other):
        for name, value in (("flags", 5), ("mtu", 1500), ("hwaddr_len", 6),
                            ("hwaddr", int.from_bytes(mac, "little"))):
            field("netif", netif, name, value)
    exhausted = layout["ERR_MEM"] & 255
    assert run("dhcp_start", station) == 0
    state = client(station)
    assert state and len(frames) == 1 and frames[0][1] == broadcast and frames[0][2][242] == 1
    assert run("dhcp_start", other) & 255 == exhausted and not client(other)
    tcp = [run("c3pkt_alloc", layout["PBUF_TRANSPORT"], 4, layout["PBUF_RAM"]) for _ in range(4)]
    assert all(tcp) and len(set(tcp)) == 4
    assert not run("c3pkt_alloc", layout["PBUF_TRANSPORT"], 4, layout["PBUF_RAM"])

    # Supply an acquired lease. Verify unicast renewal, then broadcast rebind, on the wire boundary.
    field("netif", station, "ip_addr", int.from_bytes(address, "little"))
    field("dhcp", state, "server_ip_addr", int.from_bytes(server, "little"))
    field("dhcp", state, "state", layout["DHCP_STATE_BOUND"])
    retain = True
    assert run("dhcp_renew", station) == 0 and field("dhcp", state, "state") == layout["DHCP_STATE_RENEWING"]
    assert run("dhcp_rebind", station) == 0 and field("dhcp", state, "state") == layout["DHCP_STATE_REBINDING"]
    first, second = frames[1:]
    assert first[0] != second[0] and (first[1], second[1]) == (server, broadcast)
    assert all(f[2][242] == 3 and f[2][12:16] == address for f in (first, second))
    assert run("dhcp_renew", station) & 255 == exhausted and len(frames) == 3
    for packet, _, data in (first, second):
        assert field("pbuf", packet, "ref") == 1
        assert bytes(cpu.uc.mem_read(field("pbuf", packet, "payload"), len(data))) == data
    assert run("pbuf_free", first[0]) == 1
    retain = False
    assert run("dhcp_renew", station) == 0 and frames[-1][0] == first[0]
    assert run("pbuf_free", second[0]) == 1
    for packet in tcp:
        assert run("pbuf_free", packet) == 1
    run("dhcp_release_and_stop", station)
    assert frames[-1][2][242] == 7 and frames[-1][1] == server
    run("dhcp_cleanup", station)
    assert not client(station) and run("dhcp_start", other) == 0 and client(other) == state
    run("dhcp_release_and_stop", other)
    run("dhcp_cleanup", other)
    assert not client(other)
    assert bytes(cpu.uc.mem_read(cpu.symbols["next_timeout"], 4)) == bytes(4)
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), client_slots=1,
                held_tcp_packets=len(tcp), held_dhcp_messages=2, messages=[f[2][242] for f in frames],
                state_and_message_heap_calls=0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--gdb", default="riscv32-esp-elf-gdb")
    args = parser.parse_args()
    print(json.dumps(verify(args.elf, args.gdb), indent=2))
