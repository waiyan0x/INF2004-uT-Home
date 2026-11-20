"""Run ICMP replies and outbound IPv4 fragmentation from the station's RV32 ELF."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1
from .tcp_packet_probe import target_layout
from .tls_fragment_probe import Probe, RECV, SSL
from .tls_output_probe import OutputProbe


def checksum(data):
    padded = data + bytes(len(data) % 2)
    total = sum(value for value, in struct.iter_unpack("!H", padded))
    while total >> 16:
        total = (total & 0xffff) + (total >> 16)
    return total ^ 0xffff


def datagram(src, dst, protocol, payload):
    header = bytearray(struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(payload),
                                   1234, 0, 64, protocol, 0, src, dst))
    struct.pack_into("!H", header, 10, checksum(header))
    return bytes(header) + payload


def verify(elf, gdb, case):
    layout = target_layout(elf, gdb, ("netif", "pbuf"), ("PBUF_IP", "PBUF_RAM", "ERR_MEM"))
    cpu = Probe(elf)
    station, dest, storage = SSL + 0x8000, SSL + 0x9000, SSL + 0xa000
    local, peer = bytes([192, 0, 2, 1]), bytes([192, 0, 2, 2])
    frames, returned, held = [], [], []
    retain = False

    def run(name, *args):
        return OutputProbe.run(cpu, name, *args)

    def field(kind, ptr, name, value=None):
        offset, width = layout[kind][name]
        if value is not None:
            cpu.uc.mem_write(ptr + offset, value.to_bytes(width, "little"))
        return int.from_bytes(cpu.uc.mem_read(ptr + offset, width), "little")

    def output(*_):
        assert cpu.uc.reg_read(UC_RISCV_REG_A0) == station
        pkt = cpu.uc.reg_read(UC_RISCV_REG_A1)
        assert not field("pbuf", pkt, "next")
        addr, size = field("pbuf", pkt, "payload"), field("pbuf", pkt, "tot_len")
        frame = bytes(cpu.uc.mem_read(addr, size))
        assert size == int.from_bytes(frame[2:4], "big") and checksum(frame[:20]) == 0
        frames.append(frame)
        if retain:
            field("pbuf", pkt, "ref", field("pbuf", pkt, "ref") + 1)
            held.append(pkt)
        cpu.finish(0)

    def release(*_):
        assert cpu.uc.reg_read(UC_RISCV_REG_A0) == station
        returned.append(cpu.uc.reg_read(UC_RISCV_REG_A1))
        cpu.finish(0)

    def reject(*_):
        raise AssertionError("IPv4 packet path reached the general allocator")

    for name in ("malloc", "free", "mem_malloc", "mem_free"):
        cpu.hook(cpu.symbols[name], reject)
    for name in ("c3bport_enter", "c3bport_leave", "sys_arch_protect", "sys_arch_unprotect"):
        cpu.hook(cpu.symbols[name], lambda *_: cpu.finish(0))
    cpu.hook(RECV + 4, output)
    cpu.hook(cpu.symbols["esp_netif_free_rx_buffer"], release)
    for key, value in (("flags", 15), ("mtu", 1500), ("output", RECV + 4),
                       ("ip_addr", int.from_bytes(local, "little")), ("netmask", 0xffffff)):
        field("netif", station, key, value)
    for name in ("netif_default", "netif_list"):
        cpu.uc.mem_write(cpu.symbols[name], struct.pack("<I", station))
    cpu.uc.mem_write(dest, peer)

    def borrow(data, token):
        cpu.uc.mem_write(storage, data)
        packet = run("c3rx_alloc", station, storage, len(data), token)
        assert packet
        return packet

    def slot(length):
        return run("c3pkt_frame", layout["PBUF_IP"], length, layout["PBUF_RAM"])

    if case == "icmp":
        echo = bytearray(struct.pack("!BBHHH", 8, 0, 0, 0x2345, 7) + bytes(range(256)) * 5 + bytes(range(192)))
        struct.pack_into("!H", echo, 2, checksum(echo))
        request = datagram(peer, local, 1, echo)
        assert len(request) == 1500
        run("ip4_input", borrow(request, 1), station)
        assert len(frames) == 1 and returned == [1]
        reply = frames[0]
        assert len(reply) == 1500 and reply[9] == 1 and reply[12:20] == local + peer
        assert reply[20:22] == b"\0\0" and checksum(reply[20:]) == 0 and reply[24:] == echo[4:]
        # A closed UDP port produces a native ICMP error quoting the original IP header and first eight bytes.
        udp = struct.pack("!HHHH", 7000, 7001, 16, 0) + b"abcdefgh"
        original = datagram(peer, local, 17, udp)
        blocker = slot(1480)
        assert blocker
        run("ip4_input", borrow(request, 2), station)
        run("ip4_input", borrow(original, 3), station)
        assert len(frames) == 1 and returned == [1, 2, 3]
        assert run("pbuf_free", blocker) == 1
        run("ip4_input", borrow(original, 4), station)
        assert len(frames) == 2 and returned[-1] == 4
        error = frames[-1]
        assert error[9] == 1 and error[12:20] == local + peer and checksum(error[20:]) == 0
        assert error[20:22] == bytes([3, 3]) and error[28:] == original[:28]
    else:
        data = struct.pack("!HHHH", 7000, 7001, 2968, 0) + bytes(i % 251 for i in range(2960))
        original = datagram(local, peer, 17, data)
        packet = borrow(original, 1)

        def fragment():
            return run("ip4_frag", packet, station, dest) & 255

        # The fragment tail is eight bytes and must use the frame even with the independent UDP header busy.
        udp_header = run("c3pkt_aux", layout["PBUF_IP"], 8, layout["PBUF_RAM"])
        assert udp_header
        assert fragment() == 0 and len(frames) == 3
        assert [len(f) for f in frames] == [1500, 1500, 28]
        assert [int.from_bytes(f[6:8], "big") for f in frames] == [0x2000, 0x2000 | 185, 370]
        assert all(f[4:6] == original[4:6] and f[8:10] == original[8:10] and f[12:20] == local + peer for f in frames)
        assert b"".join(f[20:] for f in frames) == data
        reference = frames[:]
        frames.clear()
        retain = True
        failure = layout["ERR_MEM"] & 255
        assert fragment() == failure and frames == reference[:1] and len(held) == 1
        assert fragment() == failure and len(frames) == 1
        assert bytes(cpu.uc.mem_read(field("pbuf", held[0], "payload"), 1500)) == reference[0]
        assert field("pbuf", packet, "ref") == 1 and not returned
        assert run("pbuf_free", held.pop()) == 1
        retain = False
        frames.clear()
        assert fragment() == 0 and frames == reference
        assert not slot(1481)
        assert run("pbuf_free", udp_header) == run("pbuf_free", packet) == 1 and returned == [1]
    return dict(case=case, elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                output_lengths=[len(f) for f in frames], general_heap_calls=0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--gdb", default="riscv32-esp-elf-gdb")
    parser.add_argument("--case", required=True, choices=("icmp", "fragment"))
    args = parser.parse_args()
    print(json.dumps(verify(args.elf, args.gdb, args.case), indent=2))
