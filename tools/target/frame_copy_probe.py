"""Run native UDP, IPv4, ARP and Wi-Fi output with full-sized borrowed datagrams."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from unicorn.riscv_const import (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2,
                                 UC_RISCV_REG_A3, UC_RISCV_REG_A4)
from .tcp_packet_probe import target_layout
from .tls_fragment_probe import Probe, SSL
from .tls_output_probe import OutputProbe


def verify(elf, gdb):
    layout = target_layout(elf, gdb, ("netif", "pbuf"),
                           ("PBUF_RAM", "PBUF_IP", "PBUF_LINK", "PBUF_RAW_TX", "ERR_MEM"))
    cpu = Probe(elf)
    station, addresses, source = SSL + 0x8000, SSL + 0x9000, SSL + 0xa000
    driver = SSL + 0xb000
    local, known, unknown = (bytes([192, 0, 2, n]) for n in (1, 2, 3))
    mac, peer = bytes.fromhex("020000000001"), bytes.fromhex("020000000002")
    payload = bytes(range(256)) * 5 + bytes(range(192))
    assert len(payload) == 1472
    frames, retained, returned = [], [], []
    retain = False

    def run(name, *args):
        return OutputProbe.run(cpu, name, *args)

    def field(kind, pointer, name, value=None):
        offset, width = layout[kind][name]
        if value is not None:
            cpu.uc.mem_write(pointer + offset, value.to_bytes(width, "little"))
        return int.from_bytes(cpu.uc.mem_read(pointer + offset, width), "little")

    def handoff(*_):
        handle, data, size, packet = (cpu.uc.reg_read(r) for r in
                                       (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3))
        assert handle == driver and not field("pbuf", packet, "next")
        assert data == field("pbuf", packet, "payload") and size == field("pbuf", packet, "tot_len")
        frames.append(bytes(cpu.uc.mem_read(data, size)))
        if retain and frames[-1][12:14] == b"\x08\x00":
            field("pbuf", packet, "ref", field("pbuf", packet, "ref") + 1)
            retained.append(packet)
        cpu.finish(0)

    def driver_free(*_):
        assert cpu.uc.reg_read(UC_RISCV_REG_A0) == driver
        returned.append(cpu.uc.reg_read(UC_RISCV_REG_A1))
        cpu.finish(0)

    def reject_heap(*_):
        raise AssertionError("packet-copy path reached the general allocator")

    for name in ("malloc", "free", "mem_malloc", "mem_free"):
        cpu.hook(cpu.symbols[name], reject_heap)
    for name in ("c3bport_enter", "c3bport_leave", "sys_arch_protect", "sys_arch_unprotect"):
        cpu.hook(cpu.symbols[name], lambda *_: cpu.finish(0))
    cpu.hook(cpu.symbols["esp_netif_transmit_wrap"], handoff)
    cpu.hook(cpu.symbols["esp_netif_free_rx_buffer"], driver_free)
    for name, value in (("state", driver), ("output", cpu.symbols["etharp_output"]),
                        ("linkoutput", cpu.symbols["low_level_output"]), ("mtu", 1500),
                        ("flags", 15), ("hwaddr_len", 6), ("hwaddr", int.from_bytes(mac, "little")),
                        ("ip_addr", int.from_bytes(local, "little")), ("netmask", 0xffffff)):
        field("netif", station, name, value)
    cpu.uc.mem_write(addresses, known + unknown)

    def resolve(address):
        packet = run("c3pkt_alloc", layout["PBUF_LINK"], 28, layout["PBUF_RAM"])
        assert packet
        cpu.uc.mem_write(field("pbuf", packet, "payload"),
                         bytes.fromhex("0001080006040002") + peer + address + mac + local)
        run("etharp_input", packet, station)

    def borrowed(token):
        cpu.uc.mem_write(source, payload)
        packet = run("c3rx_alloc", driver, source, len(payload), token)
        assert packet
        return packet

    def send(packet, address):
        cpu.uc.reg_write(UC_RISCV_REG_A4, station)
        return run("udp_sendto_if", pcb, packet, address, 7001) & 255

    def check_frame(frame, address):
        assert len(frame) == 1514 and frame[:14] == peer + mac + b"\x08\x00"
        assert frame[14] == 0x45 and frame[23] == 17 and frame[26:34] == local + address
        assert struct.unpack_from("!H", frame, 16)[0] == 1500
        assert struct.unpack_from("!HHH", frame, 34) == (7000, 7001, 1480)
        assert frame[42:] == payload

    pcb = run("udp_new")
    assert pcb and run("udp_bind", pcb, 0, 7000) == 0
    exhausted = layout["ERR_MEM"] & 255
    resolve(known)
    first = borrowed(1)
    retain = True
    assert send(first, addresses) == 0
    check_frame(frames[-1], known)
    assert field("pbuf", first, "ref") == 1 and len(retained) == 1
    count = len(frames)
    assert send(first, addresses) == exhausted and len(frames) == count
    assert run("pbuf_free", retained.pop()) == 1
    retain = False
    assert send(first, addresses) == 0
    check_frame(frames[-1], known)

    # The unresolved peer retains a private copy of the entire chain.
    assert send(first, addresses + 4) == 0 and frames[-1][12:14] == b"\x08\x06"
    assert field("pbuf", first, "ref") == 1 and run("pbuf_free", first) == 1 and returned == [1]
    cpu.uc.mem_write(source, b"\xff" * len(payload))
    other = run("c3rx_alloc", driver, source, len(payload), 2)
    assert other
    assert send(other, addresses + 4) == exhausted and send(other, addresses) == exhausted
    assert field("pbuf", other, "ref") == 1
    retain = True
    resolve(unknown)
    check_frame(frames[-1], unknown)
    assert len(retained) == 1 and send(other, addresses) == exhausted
    assert run("pbuf_free", retained.pop()) == 1
    retain = False

    # A held UDP header does not consume the frame copy or affect caller ownership.
    header = run("c3pkt_aux", layout["PBUF_IP"], 8, layout["PBUF_RAM"])
    copy = run("c3pkt_aux", layout["PBUF_RAW_TX"], 1514, layout["PBUF_RAM"])
    assert header and copy and copy != header
    assert send(other, addresses) == exhausted and field("pbuf", other, "ref") == 1
    for packet in (copy, header):
        assert run("pbuf_free", packet) == 1
    for layer, length in (("PBUF_LINK", 1501), ("PBUF_RAW_TX", 1515), ("PBUF_IP", 9)):
        assert not run("c3pkt_aux", layout[layer], length, layout["PBUF_RAM"])
    cpu.uc.mem_write(source, payload)
    assert send(other, addresses) == 0
    check_frame(frames[-1], known)
    assert run("pbuf_free", other) == 1 and returned == [1, 2]
    run("etharp_cleanup_netif", station)
    run("udp_remove", pcb)
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), udp_payload_bytes=len(payload),
                ethernet_bytes=1514, volatile_inputs_returned=len(returned), general_heap_calls=0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--gdb", default="riscv32-esp-elf-gdb")
    args = parser.parse_args()
    print(json.dumps(verify(args.elf, args.gdb), indent=2))
