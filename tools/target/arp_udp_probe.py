"""Run the station's compiled UDP lifecycle and ARP frame/queue paths on RV32."""
import argparse
import hashlib
import json
from pathlib import Path

from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1
from .tcp_packet_probe import target_layout
from .tls_fragment_probe import Probe, SSL, STOP
from .tls_output_probe import OutputProbe


def verify(elf, gdb):
    layout = target_layout(elf, gdb, ("netif", "pbuf"),
                           ("PBUF_RAM", "PBUF_LINK", "PBUF_TRANSPORT", "PBUF_IP", "MEMP_ARP_QUEUE", "ERR_MEM"))
    cpu = Probe(elf)
    netif = SSL + 0x8000
    peer = SSL + 0x9000
    link = STOP + 0x240
    local_ip = bytes([192, 0, 2, 1])
    peer_ip = bytes([192, 0, 2, 2])
    local_mac = bytes.fromhex("020000000001")
    peer_mac = bytes.fromhex("020000000002")
    frames = []

    def run(name, *args):
        return OutputProbe.run(cpu, name, *args)

    def reject_heap(*_):
        raise AssertionError("ARP/UDP exercise reached the general allocator")

    def field(kind, base, name):
        offset, width = layout[kind][name]
        return int.from_bytes(cpu.uc.mem_read(base + offset, width), "little")

    def set_netif(name, value):
        offset, width = layout["netif"][name]
        assert len(value) == width
        cpu.uc.mem_write(netif + offset, value)

    def transmit(*_):
        assert cpu.uc.reg_read(UC_RISCV_REG_A0) == netif
        packet = cpu.uc.reg_read(UC_RISCV_REG_A1)
        frames.append(bytes(cpu.uc.mem_read(field("pbuf", packet, "payload"), field("pbuf", packet, "len"))))
        cpu.finish(0)

    def packet(layer, size):
        return run("c3pkt_alloc", layout[layer], size, layout["PBUF_RAM"])

    for name in ("malloc", "free", "mem_malloc", "mem_free"):
        cpu.hook(cpu.symbols[name], reject_heap)
    for name in ("sys_arch_protect", "sys_arch_unprotect", "c3bport_enter", "c3bport_leave"):
        cpu.hook(cpu.symbols[name], lambda *_: cpu.finish(0))
    cpu.hook(link, transmit)
    set_netif("linkoutput", link.to_bytes(4, "little"))
    set_netif("hwaddr_len", bytes([6]))
    set_netif("hwaddr", local_mac)
    set_netif("ip_addr", local_ip)
    set_netif("netmask", bytes.fromhex("ffffff00"))
    cpu.uc.mem_write(peer, peer_ip + bytes([192, 0, 2, 3, 192, 0, 2, 4]))

    pcbs = [run("udp_new") for _ in range(2)]
    assert all(pcbs) and len(set(pcbs)) == 2 and run("udp_new") == 0
    for pcb, port in zip(pcbs, (68, 7000)):
        assert run("udp_bind", pcb, 0, port) == 0
    run("udp_remove", pcbs[0])
    assert run("udp_new") == pcbs[0] and run("udp_bind", pcbs[0], 0, 68) == 0

    out_of_memory = layout["ERR_MEM"] & 255
    held = packet("PBUF_LINK", 28)
    assert held and packet("PBUF_LINK", 29) == 0
    run("pbuf_ref", held)
    assert run("pbuf_free", held) == 0
    assert run("etharp_request", netif, peer) & 255 == out_of_memory and not frames
    assert run("pbuf_free", held) == 1 and run("etharp_request", netif, peer) == 0
    arp_header = bytes.fromhex("000108000604")
    assert frames == [b"\xff"*6 + local_mac + b"\x08\x06" + arp_header + b"\x00\x01" +
                      local_mac + local_ip + bytes(6) + peer_ip]

    # Fill two unresolved queues. The SDK takes its own reference on each packet.
    packets = [packet(layer, 4) for layer in ["PBUF_TRANSPORT"] * 4 + ["PBUF_IP"] * 2]
    assert all(packets) and len(set(packets)) == 6
    for i, item in enumerate(packets):
        cpu.uc.mem_write(field("pbuf", item, "payload"), bytes([i])*4)
        assert run("etharp_query", netif, peer + (0 if i < 3 else 4), item) == 0
        assert field("pbuf", item, "ref") == 2
    assert run("etharp_query", netif, peer, packets[0]) & 255 == out_of_memory
    spare = run("c3lrec_alloc", layout["MEMP_ARP_QUEUE"])
    assert spare and not run("c3lrec_alloc", layout["MEMP_ARP_QUEUE"])
    assert run("etharp_query", netif, peer + 8, packets[0]) & 255 == out_of_memory
    assert all(field("pbuf", item, "ref") == 2 for item in packets)
    run("c3lrec_free", layout["MEMP_ARP_QUEUE"], spare)

    # Resolving the first peer flushes exactly its three original frames in order.
    reply = packet("PBUF_LINK", 28)
    assert reply
    cpu.uc.mem_write(field("pbuf", reply, "payload"),
                     arp_header + b"\x00\x02" + peer_mac + peer_ip + local_mac + local_ip)
    start = len(frames)
    run("etharp_input", reply, netif)
    assert frames[start:] == [peer_mac + local_mac + b"\x08\x00" + bytes([i])*4 for i in range(3)]
    assert [field("pbuf", item, "ref") for item in packets] == [1, 1, 1, 2, 2, 2]
    run("etharp_cleanup_netif", netif)
    for item in packets:
        assert field("pbuf", item, "ref") == 1 and run("pbuf_free", item) == 1

    # Feed a request from a peer and check the complete outgoing reply.
    request = packet("PBUF_TRANSPORT", 28)
    assert request
    cpu.uc.mem_write(field("pbuf", request, "payload"),
                     arp_header + b"\x00\x01" + peer_mac + peer_ip + bytes(6) + local_ip)
    run("etharp_input", request, netif)
    assert frames[-1] == (peer_mac + local_mac + b"\x08\x06" + arp_header + b"\x00\x02" +
                          local_mac + local_ip + peer_mac + peer_ip)
    records = [run("c3lrec_alloc", layout["MEMP_ARP_QUEUE"]) for _ in range(7)]
    assert all(records) and len(set(records)) == 7 and not run("c3lrec_alloc", layout["MEMP_ARP_QUEUE"])
    for record in records:
        run("c3lrec_free", layout["MEMP_ARP_QUEUE"], record)
    for pcb in pcbs:
        run("udp_remove", pcb)
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), udp_pcbs=len(pcbs),
                queued_packets=len(packets), recovered_queue_slots=len(records), arp_frame_bytes=42,
                general_heap_calls=0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--gdb", default="riscv32-esp-elf-gdb")
    args = parser.parse_args()
    print(json.dumps(verify(args.elf, args.gdb), indent=2))
