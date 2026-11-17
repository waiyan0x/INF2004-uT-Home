"""Run pinned RV32 TLS setup/cleanup instructions; no handshake or hardware.

The SDK allocation order, record-pointer setup and both cleanup paths execute
unchanged. Heap, ROM byte-copy and PSA hash setup/abort calls are doubled.
Requires unicorn==2.1.4 and pyelftools, as for the fragment reproducer.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile
from .tls_fragment_probe import Probe, SSL, CONFIG, STACK, STACK_SIZE, STOP
from unicorn import UC_HOOK_CODE
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A3, UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_PC


def record_storage(path):
    """Bind checks to this adapter's linked arrays and full SDK-sized members."""
    result = {}
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf.little_endian
        for unit in elf.get_dwarf_info().iter_CUs():
            source = unit.get_top_DIE().attributes['DW_AT_name'].value.decode()
            if Path(source.replace('\\', '/')).name != 'c3tout.c':
                continue
            for die in unit.iter_DIEs():
                name = die.attributes.get('DW_AT_name')
                if die.tag != 'DW_TAG_variable' or not name:
                    continue
                kind = {b'g_inputs': 'input', b'g_buffers': 'output'}.get(name.value)
                if kind is None:
                    continue
                assert kind not in result
                location = die.attributes['DW_AT_location'].value
                assert len(location) == 5 and location[0] == 3  # DW_OP_addr, RV32
                address = int.from_bytes(location[1:], 'little')
                array = die.get_DIE_from_attribute('DW_AT_type')
                bounds = [c for c in array.iter_children() if c.tag == 'DW_TAG_subrange_type']
                assert len(bounds) == 1 and bounds[0].attributes['DW_AT_upper_bound'].value == 0
                slot = array.get_DIE_from_attribute('DW_AT_type')
                while slot.tag == 'DW_TAG_typedef':
                    slot = slot.get_DIE_from_attribute('DW_AT_type')
                assert slot.tag == 'DW_TAG_union_type'
                member = next(c for c in slot.iter_children()
                              if c.attributes.get('DW_AT_name')
                              and c.attributes['DW_AT_name'].value == b'bytes')
                byte_array = member.get_DIE_from_attribute('DW_AT_type')
                bound = next(c for c in byte_array.iter_children() if c.tag == 'DW_TAG_subrange_type')
                capacity = bound.attributes['DW_AT_upper_bound'].value + 1
                stride = slot.attributes['DW_AT_byte_size'].value
                assert stride == (capacity + 7) & ~7 and address % 8 == 0
                symbols = elf.get_section_by_name('.symtab').get_symbol_by_name(name.value.decode())
                assert any(s['st_value'] == address and s['st_size'] == stride for s in symbols)
                result[kind] = dict(address=address, capacity=capacity, linked_bytes=stride)
    assert set(result) == {'input', 'output'}, result
    assert result['input']['capacity'] == 16717 and result['output']['capacity'] == 4429
    assert (result['input']['address'] + result['input']['linked_bytes'] <= result['output']['address']
            or result['output']['address'] + result['output']['linked_bytes'] <= result['input']['address'])
    return result


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
    storage = record_storage(path)
    assert probe.run('mbedtls_ssl_config_defaults', CONFIG, 0, 0, 0) == 0
    contexts = [SSL + i * 0x400 for i in range(2)]
    probe.run('mbedtls_ssl_init', contexts[0])
    assert probe.run('c3tout_setup', contexts[0], CONFIG) == 0
    setup_allocations = len(probe.allocations)
    for kind, field in (('input', 'in_buf'), ('output', 'out_buf')):
        pointer = probe.field(contexts[0], field)
        size = storage[kind]['capacity']
        assert pointer == storage[kind]['address'] and pointer not in probe.live
        assert bytes(probe.uc.mem_read(pointer, size)) == bytes(size)
        probe.uc.mem_write(pointer, b'1' * size)
    assert probe.field(contexts[0], 'out_hdr') == storage['output']['address'] + 8
    probe.run('mbedtls_ssl_init', contexts[1])
    before = dict(probe.live)
    assert probe.run('c3tout_setup', contexts[1], CONFIG) != 0
    assert probe.live == before and len(probe.allocations) == setup_allocations
    assert probe.field(contexts[1], 'in_buf') == probe.field(contexts[1], 'out_buf') == 0
    for slot in storage.values():
        assert bytes(probe.uc.mem_read(slot['address'], slot['capacity'])) == b'1' * slot['capacity']
    probe.run('mbedtls_ssl_free', contexts[0])
    for slot in storage.values():
        assert bytes(probe.uc.mem_read(slot['address'], slot['capacity'])) == bytes(slot['capacity'])
    assert probe.run('c3tout_setup', contexts[1], CONFIG) == 0
    assert probe.field(contexts[1], 'in_buf') == storage['input']['address']
    assert probe.field(contexts[1], 'out_buf') == storage['output']['address']
    probe.run('mbedtls_ssl_free', contexts[1])
    assert not probe.live, probe.live
    for failure in range(1, setup_allocations + 1):
        probe.run('mbedtls_ssl_init', contexts[0])
        probe.fail_call = len(probe.allocations) + failure
        assert probe.run('c3tout_setup', contexts[0], CONFIG) != 0
        assert probe.field(contexts[0], 'in_buf') == probe.field(contexts[0], 'out_buf') == 0
        for slot in storage.values():
            assert bytes(probe.uc.mem_read(slot['address'], slot['capacity'])) == bytes(slot['capacity'])
        probe.run('mbedtls_ssl_free', contexts[0])
        assert not probe.live
        probe.fail_call = None
    for context in contexts[:1]:
        probe.run('mbedtls_ssl_init', context)
        assert probe.run('c3tout_setup', context, CONFIG) == 0
    for context in contexts[:1]:
        probe.run('mbedtls_ssl_free', context)
    assert not probe.live
    assert not any(slot['capacity'] in probe.allocations for slot in storage.values()), probe.allocations
    return {'passed': True,
            'scope': 'RV32 SDK setup/free instructions; no TLS handshake or board execution',
            'doubles': ['SDK heap', 'ROM byte-copy routines', 'PSA hash setup/abort'],
            'elf_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'sdk_calls': probe.calls, 'record_storage': storage,
            'setup_heap_failure_cases': setup_allocations,
            'input_heap_requests': probe.allocations.count(16717),
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
