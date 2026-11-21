"""Measure the pinned ESP-NETIF station constructor at its general heap boundary."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1

from .tcp_packet_probe import target_layout
from .tls_fragment_probe import Probe, SSL
from .tls_output_probe import OutputProbe


def verify(elf, gdb):
    cpu = Probe(elf)
    output_offset = "(int)&((esp_netif_api_msg_t *)0)->p_esp_netif"
    fixed = "c3iface_alloc" in cpu.symbols
    names = {"esp_netif": "g_station", "lwip_netif": "g_interface", "ip_info": "g_addresses[0]",
             "ip_info_old": "g_addresses[1]", "if_key": "g_key", "if_desc": "g_desc"}
    addresses = {name: "(unsigned)&'c3iface.c'::" + symbol for name, symbol in names.items()} if fixed else {}
    layout = target_layout(elf, gdb, ("esp_netif_obj", "esp_netif_config", "esp_netif_api_msg_s",
                                     "slist_netifs_s", "netif"),
                           (output_offset, *addresses.values()))
    message, config, output = (SSL + offset for offset in (0x8000, 0x9000, 0xa000))
    allocations = {}
    requests = 0
    fail_at = None

    def word(address):
        return int.from_bytes(cpu.uc.mem_read(address, 4), "little")

    def store(address, value):
        cpu.uc.mem_write(address, struct.pack("<I", value))

    def field(kind, base, name):
        return base + layout[kind][name][0]

    def allocate(count, size):
        nonlocal requests
        requests += 1
        if requests == fail_at:
            cpu.finish(0)
            return
        assert 0 < count * size <= 256
        address = SSL + 0x10000 + 256 * len(allocations)
        allocations[address] = count * size
        cpu.uc.mem_write(address, bytes(count * size))
        cpu.finish(address)

    def calloc(*_):
        allocate(cpu.uc.reg_read(UC_RISCV_REG_A0), cpu.uc.reg_read(UC_RISCV_REG_A1))

    def malloc(*_):
        allocate(1, cpu.uc.reg_read(UC_RISCV_REG_A0))

    def release(*_):
        address = cpu.uc.reg_read(UC_RISCV_REG_A0)
        if address:
            del allocations[address]
        cpu.finish(0)

    def strlen(*_):
        cpu.finish(bytes(cpu.uc.mem_read(cpu.uc.reg_read(UC_RISCV_REG_A0), 64)).index(0))

    cpu.hook(cpu.symbols["malloc"], malloc)
    cpu.hook(cpu.symbols["calloc"], calloc)
    cpu.hook(cpu.symbols["free"], release)
    cpu.hook(cpu.symbols["strlen"], strlen)
    store(field("esp_netif_config", config, "base"), cpu.symbols["_g_esp_netif_inherent_sta_config"])
    store(field("esp_netif_config", config, "stack"), word(cpu.symbols["_g_esp_netif_netstack_default_wifi_sta"]))
    store(field("esp_netif_api_msg_s", message, "data"), config)
    store(message + layout[output_offset], output)
    def run(name, *args):
        return OutputProbe.run(cpu, name, *args)

    if fixed:
        for symbol in ("c3bport_enter", "c3bport_leave"):
            cpu.hook(cpu.symbols[symbol], lambda *_: cpu.finish(0))
        # Hold the exact record required at each of the four failure sites.
        for sizes in ((104,), (12, 12), (12,), (84,)):
            held = [run("c3iface_alloc", 1, size) for size in sizes]
            assert all(held) and len(set(held)) == len(held)
            assert run("esp_netif_new_api", message) != 0
            assert not allocations and not requests and not word(output) and not word(cpu.symbols["s_head"])
            for pointer in held:
                run("c3iface_free", pointer)
    else:
        for fail_at in range(1, 5):
            requests = 0
            assert run("esp_netif_new_api", message) != 0
            assert requests == fail_at and not allocations and not word(output)
            assert word(cpu.symbols["s_head"]) == 0
    requests = 0
    fail_at = None
    assert run("esp_netif_new_api", message) == 0
    obj = word(output)
    nodes = {name: word(field("esp_netif_obj", obj, name))
             for name in ("ip_info", "ip_info_old", "lwip_netif", "if_key", "if_desc")}
    nodes.update(esp_netif=obj, list_entry=word(cpu.symbols["s_head"]))
    assert word(field("slist_netifs_s", nodes["list_entry"], "netif")) == obj
    assert word(field("netif", nodes["lwip_netif"], "state")) == obj
    assert len(set(nodes.values())) == len(nodes)
    for name, value in (("if_key", b"WIFI_STA_DEF"), ("if_desc", b"sta")):
        assert bytes(cpu.uc.mem_read(nodes[name], len(value) + 1)) == value + bytes(1)
    if fixed:
        assert all(nodes[name] == layout[address] for name, address in addresses.items())
        assert requests == 1 and allocations == {nodes["list_entry"]: 8}
        assert not run("c3iface_alloc", 1, 12) and not run("c3iface_alloc", 0, 104)
        # Both identifiers remain live while a full-length hostname is replaced.
        scratch = SSL + 0xb000
        cpu.uc.mem_write(scratch, b"x" * 33 + bytes(1))
        assert not run("c3iface_dup", scratch)
        cpu.uc.mem_write(scratch, b"x" * 32 + bytes(1))
        hostname = run("c3iface_dup", scratch)
        assert hostname and hostname not in nodes.values()
        assert bytes(cpu.uc.mem_read(hostname, 33)) == b"x" * 32 + bytes(1)
        assert not run("c3iface_dup", scratch)
        store(field("esp_netif_obj", obj, "hostname"), hostname)
        # Exercise the native destructor on this never-started interface.
        store(message + layout[output_offset], obj)
        assert run("esp_netif_destroy_api", message) == 0
        assert not allocations and not word(cpu.symbols["s_head"])
        store(message + layout[output_offset], output)
        store(output, 0)
        assert run("esp_netif_new_api", message) == 0 and word(output) == obj and requests == 2
        assert run("c3iface_dup", scratch) == hostname
    else:
        assert set(nodes.values()) == allocations.keys()
    heap_records = {name: allocations[ptr] for name, ptr in nodes.items() if ptr in allocations}
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                requested_bytes=heap_records, retained_payload_bytes=sum(allocations.values()),
                retained_heap_payload_bytes=sum(allocations.values()), early_allocation_failures_checked=4,
                fixed_records_verified=sorted(addresses), destruction_and_reuse_checked=fixed)



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--gdb", default="riscv32-esp-elf-gdb")
    args = parser.parse_args()
    print(json.dumps(verify(args.elf, args.gdb), indent=2))
