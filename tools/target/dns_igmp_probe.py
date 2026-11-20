"""Measure native lwIP allocations for a numeric MQTT endpoint and IGMP membership."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from unicorn import UC_HOOK_CODE
from unicorn.riscv_const import (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2,
                                 UC_RISCV_REG_A4, UC_RISCV_REG_A5, UC_RISCV_REG_PC, UC_RISCV_REG_RA)
from .tcp_packet_probe import target_layout
from .tls_fragment_probe import Probe, RECV, SSL
from .tls_output_probe import OutputProbe


def verify(elf, gdb):
    cpu = Probe(elf)
    layout = target_layout(elf, gdb, ("netif", "igmp_group", "addrinfo", "sockaddr_in", "dns_table_entry"),
                           ("LWIP_NETIF_CLIENT_DATA_INDEX_IGMP",))
    station, host, hints, result, sem = (SSL + offset for offset in (0x8000, 0x9000, 0xa000, 0xb000, 0xc000))
    sizes, freed = [], []
    sends = []
    pending = {}
    refuse = False
    return_pc = None

    def run(name, *args):
        return OutputProbe.run(cpu, name, *args)

    def word(address):
        return int.from_bytes(cpu.uc.mem_read(address, 4), "little")

    def allocate(*_):
        size = cpu.uc.reg_read(UC_RISCV_REG_A0)
        sizes.append(size)
        if refuse:
            cpu.finish(0)
        else:
            ptr = SSL + 0x10000 + 512 * len(sizes)
            assert 0 < size <= 512
            pending[ptr] = size
            cpu.finish(ptr)

    def release(*_):
        ptr = cpu.uc.reg_read(UC_RISCV_REG_A0)
        freed.append(pending.pop(ptr))
        cpu.finish(0)

    # Execute the real resolver callback synchronously in place of the TCP/IP thread.
    def dispatch(*_):
        nonlocal return_pc
        callback = cpu.uc.reg_read(UC_RISCV_REG_A0)
        assert callback == cpu.symbols["lwip_netconn_do_gethostbyname"] and return_pc is None
        return_pc = cpu.uc.reg_read(UC_RISCV_REG_RA)
        cpu.uc.reg_write(UC_RISCV_REG_A0, cpu.uc.reg_read(UC_RISCV_REG_A1))
        cpu.uc.reg_write(UC_RISCV_REG_RA, RECV + 8)
        cpu.uc.reg_write(UC_RISCV_REG_PC, callback)

    def wake(*_):
        nonlocal return_pc
        assert return_pc is not None
        cpu.uc.reg_write(UC_RISCV_REG_RA, return_pc)
        return_pc = None
        cpu.finish(0)

    for name in ("c3bport_enter", "c3bport_leave", "sys_arch_protect", "sys_arch_unprotect", "sys_sem_signal"):
        cpu.hook(cpu.symbols[name], lambda *_: cpu.finish(0))
    cpu.hook(cpu.symbols["tcpip_send_msg_wait_sem"], dispatch)
    cpu.hook(cpu.symbols["sys_thread_sem_get"], lambda *_: cpu.finish(sem))
    cpu.hook(RECV + 8, wake)
    def strlen(*_):
        ptr = cpu.uc.reg_read(UC_RISCV_REG_A0)
        cpu.finish(bytes(cpu.uc.mem_read(ptr, 256)).index(0))

    cpu.hook(cpu.symbols["strlen"], strlen)
    for sender in ("dns_send", "igmp_send"):
        pc = cpu.symbols[sender]
        cpu.uc.hook_add(UC_HOOK_CODE, lambda _uc, addr, *_: sends.append(addr), begin=pc, end=pc)
    cpu.hook(cpu.symbols["mem_malloc"], allocate)
    cpu.hook(cpu.symbols["mem_free"], release)
    cpu.uc.mem_write(sem, struct.pack("<I", 1))

    run("igmp_init")
    assert run("igmp_start", station) == 0 and len(sizes) == 1
    group_bytes = sizes[0]
    group_ptr = station + layout["netif"]["client_data"][0] + 4 * layout["LWIP_NETIF_CLIENT_DATA_INDEX_IGMP"]
    group = word(group_ptr)
    assert bytes(cpu.uc.mem_read(group + layout["igmp_group"]["group_address"][0], 4)) == bytes([224, 0, 0, 1])

    # Match the SDK's zeroed hints, SOCK_STREAM and null service. The group stays live across both lookups.
    cpu.uc.mem_write(host, b"192.0.2.1\0")
    socktype, width = layout["addrinfo"]["ai_socktype"]
    cpu.uc.mem_write(hints + socktype, (1).to_bytes(width, "little"))
    for call in (1, 2):
        assert run("lwip_getaddrinfo", host, 0, hints, result) == 0
        record = word(result)
        sockaddr = word(record + layout["addrinfo"]["ai_addr"][0])
        assert bytes(cpu.uc.mem_read(sockaddr + layout["sockaddr_in"]["sin_addr"][0], 4)) == bytes([192, 0, 2, 1])
        assert len(sizes) == 1 + call and len(pending) == 2 and return_pc is None
        address_bytes = sizes[1]
        assert sizes[-1] == address_bytes and word(group_ptr) == group
        run("lwip_freeaddrinfo", record)
        assert freed == [address_bytes] * call and len(pending) == 1
    assert run("igmp_stop", station) == 0 and not word(group_ptr)
    assert freed == [address_bytes, address_bytes, group_bytes] and not pending and not sends

    # Positive controls ask each native sender for a packet. Refuse it at the
    # heap boundary so this replay requires neither a peer nor a Wi-Fi driver.
    refuse = True
    name = b"broker.example"
    cpu.uc.mem_write(cpu.symbols["dns_table"] + layout["dns_table_entry"]["name"][0], name + b"\0")
    cpu.uc.mem_write(host, bytes([192, 0, 2, 53]))
    run("dns_setserver", 0, host)
    assert (run("dns_send", 0) & 255) == 255 and len(sizes) == 4
    run("igmp_send", station, host, 0x16)
    assert len(sizes) == 5 and not pending
    assert sends == [cpu.symbols["dns_send"], cpu.symbols["igmp_send"]]
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                numeric_address_heap_bytes=address_bytes, group_heap_bytes=group_bytes,
                dns_packet_heap_request=sizes[3], igmp_packet_heap_request=sizes[4])


def verify_endpoint(elf, gdb):
    """Run the SDK socket setup, with only socket creation and ROM routines supplied."""
    cpu = Probe(elf)
    layout = target_layout(elf, gdb, ("addrinfo", "sockaddr_in"),
                           ("ESP_TLS_AF_UNSPEC", "ESP_TLS_AF_INET"))
    host, target, fd, hints, out = (SSL + n for n in (0x8000, 0x9000, 0xa000, 0xb000, 0xc000))
    sockets = []
    descriptor = 7

    def run(name, *args):
        return OutputProbe.run(cpu, name, *args)

    def reject(_uc, pc, *_):
        raise AssertionError(f"Broker endpoint reached general lookup or heap at {pc:#x}")

    def socket(*_):
        args = tuple(cpu.uc.reg_read(r) for r in (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2))
        assert args == (2, 1, 0)
        sockets.append(descriptor)
        cpu.finish(descriptor)

    for name in ("malloc", "calloc", "free", "mem_malloc", "mem_free", "lwip_getaddrinfo", "dns_send"):
        if name in cpu.symbols:
            cpu.hook(cpu.symbols[name], reject)
    cpu.hook(cpu.symbols["lwip_socket"], socket)

    def connect(literal, family, port):
        cpu.uc.mem_write(host, literal + b"\0")
        cpu.uc.mem_write(target, bytes(32))
        cpu.uc.reg_write(UC_RISCV_REG_A4, target)
        cpu.uc.reg_write(UC_RISCV_REG_A5, fd)
        return run("esp_tls_hostname_to_fd", host, len(literal), port, layout[family])

    def check_peer(ip, port):
        pos = layout["sockaddr_in"]
        assert bytes(cpu.uc.mem_read(target + pos["sin_addr"][0], 4)) == ip
        assert bytes(cpu.uc.mem_read(target + pos["sin_port"][0], 2)) == struct.pack("!H", port)
        assert cpu.uc.mem_read(target + pos["sin_family"][0], 1) == b"\x02"
        assert struct.unpack("<i", cpu.uc.mem_read(fd, 4))[0] == descriptor

    assert connect(b"192.0.2.1", "ESP_TLS_AF_UNSPEC", 8883) == 0
    check_peer(bytes([192, 0, 2, 1]), 8883)
    assert connect(b"broker.example", "ESP_TLS_AF_UNSPEC", 8883) != 0
    assert sockets == [7]
    descriptor = -1
    assert connect(b"192.0.2.1", "ESP_TLS_AF_INET", 8883) != 0
    descriptor = 8
    assert connect(b"203.0.113.4", "ESP_TLS_AF_INET", 65535) == 0
    check_peer(bytes([203, 0, 113, 4]), 65535)

    # Hold a live result while the real SDK attempts another connection.
    socktype, width = layout["addrinfo"]["ai_socktype"]
    cpu.uc.mem_write(hints + socktype, (1).to_bytes(width, "little"))
    assert run("c3mqtt_resolve", host, 0, hints, out) == 0
    held = int.from_bytes(cpu.uc.mem_read(out, 4), "little")
    ai_addr = int.from_bytes(cpu.uc.mem_read(held + layout["addrinfo"]["ai_addr"][0], 4), "little")
    ip = ai_addr + layout["sockaddr_in"]["sin_addr"][0]
    run("c3mqtt_release", out)
    run("c3mqtt_release", 0)
    assert connect(b"192.0.2.5", "ESP_TLS_AF_INET", 8883) != 0
    assert bytes(cpu.uc.mem_read(ip, 4)) == bytes([203, 0, 113, 4])
    run("c3mqtt_release", held)
    assert connect(b"192.0.2.5", "ESP_TLS_AF_INET", 8883) == 0
    check_peer(bytes([192, 0, 2, 5]), 8883)
    assert sockets == [7, -1, 8, 8]
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                socket_attempts=len(sockets), general_lookup_calls=0, endpoint_heap_calls=0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--gdb", default="riscv32-esp-elf-gdb")
    parser.add_argument("--fixed-endpoint", action="store_true", help="exercise the SDK socket path with the fixed numeric endpoint")
    args = parser.parse_args()
    check = verify_endpoint if args.fixed_endpoint else verify
    print(json.dumps(check(args.elf, args.gdb), indent=2))
