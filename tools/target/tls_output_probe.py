"""Run pinned RV32 TLS setup/cleanup instructions; no handshake or hardware.

The SDK allocation order, output-pointer setup and both cleanup paths execute
unchanged. Heap, ROM byte-copy and PSA hash setup/abort calls are doubled.
Requires unicorn==2.1.4 and pyelftools, as for the fragment reproducer.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from .tls_fragment_probe import Probe, SSL, CONFIG, STACK, STACK_SIZE, STOP
from unicorn import UC_HOOK_CODE
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3, UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC


class OutputProbe(Probe):
    def __init__(self, path):
        self.live = {}
        self.reusable = []
        self.fail_call = None
        self.calls = {'mbedtls_ssl_setup': 0, 'mbedtls_ssl_free': 0}
        super().__init__(path)
        for name in ('psa_hash_setup', 'psa_hash_abort'):
            self.hook(self.symbols[name], lambda *args: self.finish(0))
        for name in self.calls:
            self.uc.hook_add(UC_HOOK_CODE, lambda _u, _a, _s, _d, n=name: self.count(n),
                             begin=self.symbols[name], end=self.symbols[name])

    def count(self, name):
        self.calls[name] += 1

    def calloc(self, *args):
        size = self.uc.reg_read(UC_RISCV_REG_A0) * self.uc.reg_read(UC_RISCV_REG_A1)
        self.allocations.append(size)
        if len(self.allocations) == self.fail_call:
            self.finish(0)
            return
        assert 0 < size < 0x10000, size
        match = next((i for i, (_, capacity) in enumerate(self.reusable) if capacity == size), None)
        if match is None:
            address = self.heap_next
            self.heap_next += (size + 15) & ~15
            assert self.heap_next < SSL + 0x40000
        else:
            address, _ = self.reusable.pop(match)
        self.live[address] = size
        self.uc.mem_write(address, bytes(size))
        self.finish(address)

    def free(self, *args):
        address = self.uc.reg_read(UC_RISCV_REG_A0)
        if address:
            assert address in self.live, ('SDK heap was asked to free foreign storage', hex(address))
            self.reusable.append((address, self.live.pop(address)))
        self.finish(0)

    def run(self, name, *args):
        for reg, value in zip((UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3), args):
            self.uc.reg_write(reg, value)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        self.uc.reg_write(UC_RISCV_REG_SP, STACK + STACK_SIZE - 16)
        try:
            self.uc.emu_start(self.symbols[name], STOP, count=2000000)
        except Exception:
            pc = self.uc.reg_read(UC_RISCV_REG_PC)
            near = sorted((address, symbol) for symbol, address in self.symbols.items() if address <= pc)[-4:]
            print(name, hex(pc), near, [hex(self.uc.reg_read(r)) for r in (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2)])
            raise
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP, (name, hex(self.uc.reg_read(UC_RISCV_REG_PC)))
        return struct.unpack('<i', struct.pack('<I', self.uc.reg_read(UC_RISCV_REG_A0)))[0]

    def field(self, context, name):
        return struct.unpack('<I', self.uc.mem_read(context + self.layout[name], 4))[0]


def verify(path):
    probe = OutputProbe(path)
    assert probe.run('mbedtls_ssl_config_defaults', CONFIG, 0, 0, 0) == 0
    contexts = [SSL + i * 0x400 for i in range(2)]
    outputs = []
    for i, context in enumerate(contexts[:1]):
        probe.run('mbedtls_ssl_init', context)
        assert probe.run('c3tout_setup', context, CONFIG) == 0
        pointer = probe.field(context, 'out_buf')
        assert pointer not in probe.live and pointer % 8 == 0
        assert bytes(probe.uc.mem_read(pointer, 4429)) == bytes(4429)
        assert probe.field(context, 'out_hdr') == pointer + 8
        outputs.append(pointer)
        probe.uc.mem_write(pointer, bytes([0x31 + i]) * 4429)
    assert len(set(outputs)) == 1
    assert 4429 not in probe.allocations, probe.allocations
    probe.run('mbedtls_ssl_init', contexts[1])
    before = dict(probe.live)
    assert probe.run('c3tout_setup', contexts[1], CONFIG) != 0
    assert probe.live == before
    for i, pointer in enumerate(outputs):
        assert bytes(probe.uc.mem_read(pointer, 4429)) == bytes([0x31 + i]) * 4429
    probe.run('mbedtls_ssl_free', contexts[0])
    assert bytes(probe.uc.mem_read(outputs[0], 4429)) == bytes(4429)
    assert probe.run('c3tout_setup', contexts[1], CONFIG) == 0
    assert probe.field(contexts[1], 'out_buf') == outputs[0]
    probe.run('mbedtls_ssl_free', contexts[1])
    assert not probe.live, probe.live
    for failure in (1, 2, 3, 4):
        probe.run('mbedtls_ssl_init', contexts[0])
        probe.fail_call = len(probe.allocations) + failure
        assert probe.run('c3tout_setup', contexts[0], CONFIG) != 0
        probe.run('mbedtls_ssl_free', contexts[0])
        assert not probe.live
        probe.fail_call = None
    for context in contexts[:1]:
        probe.run('mbedtls_ssl_init', context)
        assert probe.run('c3tout_setup', context, CONFIG) == 0
    for context in contexts[:1]:
        probe.run('mbedtls_ssl_free', context)
    assert not probe.live
    return {'passed': True,
            'scope': 'RV32 SDK setup/free instructions; no TLS handshake or board execution',
            'doubles': ['SDK heap', 'ROM byte-copy routines', 'PSA hash setup/abort'],
            'elf_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'sdk_calls': probe.calls, 'output_buffers': [hex(p) for p in outputs],
            'output_heap_requests': probe.allocations.count(4429),
            'other_allocation_sizes': sorted(set(probe.allocations))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        parser.error('Assertions must remain enabled; do not use python -O')
    if args.output.exists():
        parser.error('Report already exists; choose a new output path')
    result = verify(args.elf)
    with args.output.open('x', encoding='utf-8') as report:
        report.write(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
