"""Exercise the single-broker TCP send window, split and packet recovery in RV32."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from .tls_fragment_probe import Probe, SSL
from .tls_output_probe import OutputProbe


def target_layout(elf, gdb):
    """Read the selected lwIP types and enum values from this ELF's DWARF."""
    query = """
import json
names = ("tcp_pcb", "tcp_seg", "pbuf")
layout = {name: {field.name: [field.bitpos // 8, field.type.sizeof]
                 for field in gdb.lookup_type("struct " + name).fields() if field.name}
          for name in names}
for name in ("ESTABLISHED", "MEMP_TCP_PCB", "PBUF_IP", "PBUF_TRANSPORT", "PBUF_RAM", "ERR_MEM"):
    layout[name] = int(gdb.parse_and_eval(name))
print(json.dumps(layout))
"""
    with tempfile.TemporaryDirectory() as temp:
        commands = Path(temp) / "tcp-layout.gdb"
        commands.write_text("python\n" + query + "\nend\n", encoding="utf-8")
        output = subprocess.check_output([gdb, "-nx", "-batch", str(elf), "-x", str(commands)], text=True)
    return json.loads(output.splitlines()[-1])


def verify(elf, gdb):
    layout = target_layout(elf, gdb)
    cpu = Probe(elf)

    def run(name, *args):
        return OutputProbe.run(cpu, name, *args)

    def reject_heap(*_):
        raise AssertionError("TCP send or cleanup reached the general allocator")

    for name in ("malloc", "free", "mem_malloc", "mem_free"):
        cpu.hook(cpu.symbols[name], reject_heap)
    for name in ("sys_arch_protect", "sys_arch_unprotect", "c3bport_enter", "c3bport_leave"):
        cpu.hook(cpu.symbols[name], lambda *_: cpu.finish(0))

    def field(kind, pointer, name, value=None):
        offset, width = layout[kind][name]
        if value is not None:
            cpu.uc.mem_write(pointer + offset, value.to_bytes(width, "little"))
        return int.from_bytes(cpu.uc.mem_read(pointer + offset, width), "little")

    def allocate(layer, length):
        return run("c3pkt_alloc", layout[layer], length, layout["PBUF_RAM"])

    data = bytes(i % 251 for i in range(2440))
    address = SSL + 0x38000
    cpu.uc.mem_write(address, data)
    pcb = run("c3lrec_alloc", layout["MEMP_TCP_PCB"])
    assert pcb
    for name, value in (("state", layout["ESTABLISHED"]), ("mss", 610), ("snd_wnd", 5760),
                        ("snd_wnd_max", 5760), ("snd_buf", len(data))):
        field("tcp_pcb", pcb, name, value)

    def queue(expected_lengths):
        segment = field("tcp_pcb", pcb, "unsent")
        contents = bytearray()
        for expected in expected_lengths:
            assert segment and field("tcp_seg", segment, "len") == expected
            packet = field("tcp_seg", segment, "p")
            payload = field("pbuf", packet, "payload")
            header = field("pbuf", packet, "len") - expected
            assert header >= 20
            contents.extend(cpu.uc.mem_read(payload + header, expected))
            segment = field("tcp_seg", segment, "next")
        assert not segment and contents == data
        assert field("tcp_pcb", pcb, "snd_queuelen") == len(expected_lengths)

    held = allocate("PBUF_TRANSPORT", 610)
    assert held and allocate("PBUF_TRANSPORT", 611) == 0
    assert (run("tcp_write", pcb, address, len(data), 1) & 255) == (layout["ERR_MEM"] & 255)
    assert field("tcp_pcb", pcb, "unsent") == field("tcp_pcb", pcb, "snd_queuelen") == 0
    assert field("tcp_pcb", pcb, "snd_buf") == len(data)
    run("pbuf_ref", held)
    assert run("pbuf_free", held) == 0
    assert (run("tcp_write", pcb, address, len(data), 1) & 255) == (layout["ERR_MEM"] & 255)
    assert run("pbuf_free", held) == 1
    assert run("tcp_write", pcb, address, len(data), 1) == 0
    queue([610] * 4)
    assert allocate("PBUF_TRANSPORT", 610) == 0
    assert run("c3pkt_split", pcb, 1) == 0
    queue([1, 609, 610, 610, 610])
    assert field("tcp_pcb", pcb, "snd_buf") == 0

    control = [allocate("PBUF_IP", 61) for _ in range(2)]
    assert all(control) and control[0] != control[1]
    assert allocate("PBUF_IP", 61) == allocate("PBUF_IP", 62) == 0
    for packet in control:
        assert run("pbuf_free", packet) == 1
    packet = allocate("PBUF_IP", 61)
    assert packet in control and run("pbuf_free", packet) == 1
    run("tcp_segs_free", field("tcp_pcb", pcb, "unsent"))
    run("c3lrec_free", layout["MEMP_TCP_PCB"], pcb)
    # All four ordinary slots must also be reusable after split cleanup.
    recovered = [allocate("PBUF_TRANSPORT", 610) for _ in range(4)]
    assert all(recovered) and len(set(recovered)) == 4
    assert allocate("PBUF_TRANSPORT", 610) == 0
    for packet in recovered:
        assert run("pbuf_free", packet) == 1
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                send_bytes=len(data), lengths_after_split=[1, 609, 610, 610, 610],
                control_slots=len(control), recovered_data_slots=len(recovered),
                general_heap_calls=0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--gdb", default="riscv32-esp-elf-gdb")
    args = parser.parse_args()
    print(json.dumps(verify(args.elf, args.gdb), indent=2))
