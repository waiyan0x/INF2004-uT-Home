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
    layout = target_layout(elf, gdb, ("esp_netif_obj", "esp_netif_config", "esp_netif_api_msg_s",
                                     "slist_netifs_s", "netif"),
                           (output_offset,))
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
        cpu.finish(bytes(cpu.uc.mem_read(cpu.uc.reg_read(UC_RISCV_REG_A0), 32)).index(0))

    cpu.hook(cpu.symbols["malloc"], malloc)
    cpu.hook(cpu.symbols["calloc"], calloc)
    cpu.hook(cpu.symbols["free"], release)
    cpu.hook(cpu.symbols["strlen"], strlen)
    store(field("esp_netif_config", config, "base"), cpu.symbols["_g_esp_netif_inherent_sta_config"])
    store(field("esp_netif_config", config, "stack"), word(cpu.symbols["_g_esp_netif_netstack_default_wifi_sta"]))
    store(field("esp_netif_api_msg_s", message, "data"), config)
    store(message + layout[output_offset], output)
    # The four object allocations precede list insertion and configuration.
    for fail_at in range(1, 5):
        requests = 0
        assert OutputProbe.run(cpu, "esp_netif_new_api", message) != 0
        assert requests == fail_at and not allocations and not word(output)
        assert word(cpu.symbols["s_head"]) == 0
    requests = 0
    fail_at = None
    assert OutputProbe.run(cpu, "esp_netif_new_api", message) == 0
    obj = word(output)
    nodes = {name: word(field("esp_netif_obj", obj, name))
             for name in ("ip_info", "ip_info_old", "lwip_netif", "if_key", "if_desc")}
    nodes.update(esp_netif=obj, list_entry=word(cpu.symbols["s_head"]))
    assert word(field("slist_netifs_s", nodes["list_entry"], "netif")) == obj
    assert word(field("netif", nodes["lwip_netif"], "state")) == obj
    assert len(set(nodes.values())) == len(nodes) and set(nodes.values()) == allocations.keys()
    for name, value in (("if_key", b"WIFI_STA_DEF"), ("if_desc", b"sta")):
        assert bytes(cpu.uc.mem_read(nodes[name], allocations[nodes[name]])) == value + bytes(1)
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                requested_bytes={name: allocations[ptr] for name, ptr in nodes.items()},
                retained_payload_bytes=sum(allocations.values()), early_allocation_failures_checked=4)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--gdb", default="riscv32-esp-elf-gdb")
    args = parser.parse_args()
    print(json.dumps(verify(args.elf, args.gdb), indent=2))
