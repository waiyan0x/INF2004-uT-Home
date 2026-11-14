"""Reproduce the pinned SDK's partial TLS header loss in an isolated RV32 ELF.

This is instruction emulation, not a board, TLS handshake or timing test.
Only transport, allocator and ROM byte-copy routines are simulated. Both SDK
receive functions execute their compiled instructions. No ELF file is modified.
Requires optional unicorn==2.1.4 and pyelftools; see the target guide.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_CODE
from unicorn.riscv_const import (
    UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2,
    UC_RISCV_REG_PC, UC_RISCV_REG_RA, UC_RISCV_REG_SP, UC_RISCV_REG_GP,
)

HEADER = bytes.fromhex('1603030006')
WANT_READ = -0x6900
STOP = 0x62000000
RECV = STOP + 0x100
SSL = 0x60000000
CONFIG = SSL + 0x1000
STACK = 0x61000000
STACK_SIZE = 0x10000


def ssl_layout(elf):
    """Read target offsets from DWARF rather than assuming host struct layout."""
    assert elf.elfclass == 32 and elf.little_endian
    assert elf['e_machine'] == 'EM_RISCV'
    for unit in elf.get_dwarf_info().iter_CUs():
        for die in unit.iter_DIEs():
            name = die.attributes.get('DW_AT_name')
            if (die.tag == 'DW_TAG_structure_type' and name
                    and name.value == b'mbedtls_ssl_context'
                    and 'DW_AT_byte_size' in die.attributes):
                return {child.attributes['DW_AT_name'].value.decode():
                        child.attributes['DW_AT_data_member_location'].value
                        for child in die.iter_children() if child.tag == 'DW_TAG_member'}
    raise ValueError('ELF must include mbedtls_ssl_context DWARF type information')


class Probe:
    def __init__(self, path):
        self.uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        self.allocations = []
        self.heap_next = SSL + 0x2000
        self.chunks = []
        self.symbols = {}
        self.fetch_calls = 0
        with path.open('rb') as stream:
            elf = ELFFile(stream)
            self.layout = ssl_layout(elf)
            segments = [segment for segment in elf.iter_segments()
                        if segment['p_type'] == 'PT_LOAD']
            pages = set()
            for segment in segments:
                start = segment['p_vaddr'] & ~4095
                end = (segment['p_vaddr'] + segment['p_memsz'] + 4095) & ~4095
                pages.update(range(start, end, 4096))
            # ROM libc entry points have symbols but no loadable image bytes.
            pages.update(range(0x40000000, 0x40010000, 4096))
            for page in sorted(pages):
                self.uc.mem_map(page, 4096)
            for segment in segments:
                self.uc.mem_write(segment['p_vaddr'], segment.data())
            for symbol in elf.get_section_by_name('.symtab').iter_symbols():
                if symbol['st_value']:
                    self.symbols[symbol.name] = symbol['st_value']
        self.uc.mem_map(SSL, 0x40000)
        self.uc.mem_map(STACK, STACK_SIZE)
        self.uc.mem_map(STOP, 4096)
        self.write_field('conf', CONFIG)
        self.write_field('f_recv', RECV)
        self.uc.reg_write(UC_RISCV_REG_GP, self.symbols.get('__global_pointer$', 0))
        fetch = self.symbols['mbedtls_ssl_fetch_input']
        self.uc.hook_add(UC_HOOK_CODE, self.count_fetch, begin=fetch, end=fetch)
        self.hook(RECV, self.recv)
        self.hook(self.symbols['esp_mbedtls_mem_calloc'], self.calloc)
        self.hook(self.symbols['esp_mbedtls_mem_free'], self.free)
        for name in ('memcpy', 'memmove'):
            self.hook(self.symbols[name], self.copy)
        self.hook(self.symbols['memset'], self.fill)

    def hook(self, address, callback):
        self.uc.mem_write(address, bytes.fromhex('67800000'))
        self.uc.hook_add(UC_HOOK_CODE, callback, begin=address, end=address)

    def read_field(self, name):
        return struct.unpack('<I', self.uc.mem_read(SSL + self.layout[name], 4))[0]

    def write_field(self, name, value):
        self.uc.mem_write(SSL + self.layout[name], struct.pack('<I', value))

    def count_fetch(self, _uc, _address, _size, _data):
        self.fetch_calls += 1

    def finish(self, value):
        self.uc.reg_write(UC_RISCV_REG_A0, value & 0xffffffff)

    def recv(self, _uc, _address, _size, _data):
        target = self.uc.reg_read(UC_RISCV_REG_A1)
        maximum = self.uc.reg_read(UC_RISCV_REG_A2)
        if not self.chunks:
            self.finish(WANT_READ)
            return
        chunk = self.chunks.pop(0)
        if isinstance(chunk, int):
            self.finish(chunk)
            return
        assert len(chunk) <= maximum, (len(chunk), maximum)
        self.uc.mem_write(target, chunk)
        self.finish(len(chunk))

    def calloc(self, _uc, _address, _size, _data):
        count = self.uc.reg_read(UC_RISCV_REG_A0)
        size = self.uc.reg_read(UC_RISCV_REG_A1)
        requested = count * size
        self.allocations.append(requested)
        assert requested > 0 and self.heap_next + requested < SSL + 0x40000
        result = self.heap_next
        self.heap_next += (requested + 15) & ~15
        self.uc.mem_write(result, bytes(requested))
        self.finish(result)

    def free(self, _uc, _address, _size, _data):
        self.finish(0)

    def copy(self, _uc, _address, _size, _data):
        target = self.uc.reg_read(UC_RISCV_REG_A0)
        source = self.uc.reg_read(UC_RISCV_REG_A1)
        size = self.uc.reg_read(UC_RISCV_REG_A2)
        self.uc.mem_write(target, bytes(self.uc.mem_read(source, size)))
        self.finish(target)

    def fill(self, _uc, _address, _size, _data):
        target = self.uc.reg_read(UC_RISCV_REG_A0)
        value = self.uc.reg_read(UC_RISCV_REG_A1) & 255
        size = self.uc.reg_read(UC_RISCV_REG_A2)
        self.uc.mem_write(target, bytes([value]) * size)
        self.finish(target)

    def call(self, chunks):
        self.chunks = chunks.copy()
        self.uc.reg_write(UC_RISCV_REG_A0, SSL)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        self.uc.reg_write(UC_RISCV_REG_SP, STACK + STACK_SIZE - 16)
        try:
            self.uc.emu_start(self.symbols['esp_mbedtls_add_rx_buffer'], STOP,
                              count=500000)
        except Exception:
            print('Emulation stopped at', hex(self.uc.reg_read(UC_RISCV_REG_PC)))
            raise
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP, 'Instruction bound exceeded'
        return struct.unpack('<i', struct.pack('<I', self.uc.reg_read(UC_RISCV_REG_A0)))[0]


def reproduce(path):
    cases = []
    for split in range(5):
        probe = Probe(path)
        case = {'split_after': split or 'none'}
        if split:
            case['first_result'] = probe.call([HEADER[:split], WANT_READ])
            assert case['first_result'] == WANT_READ
            case['retained_byte_count'] = probe.read_field('in_left')
            assert case['retained_byte_count'] == split
            case['header_points_into_returned_stack'] = (
                STACK <= probe.read_field('in_hdr') < STACK + STACK_SIZE)
            assert case['header_points_into_returned_stack']
            # Legitimate intervening owner work can reuse this returned stack.
            probe.uc.mem_write(STACK, bytes([0xa5]) * STACK_SIZE)
        case['final_result'] = probe.call([HEADER[split:]])
        assert case['final_result'] == 0
        case['header'] = bytes(probe.uc.mem_read(probe.read_field('in_hdr'), 5)).hex()
        case['allocation_bytes'] = probe.allocations
        case['sdk_fetch_calls'] = probe.fetch_calls
        assert probe.fetch_calls == (2 if split else 1)
        case['header_matches'] = case['header'] == HEADER.hex()
        assert case['header_matches'] == (split == 0)
        cases.append(case)
    result = {'scope': 'RV32 instruction emulation; no board or TLS handshake',
              'elf_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
              'expected_header': HEADER.hex(), 'cases': cases,
              'partial_header_loss_reproduced': True}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    if not __debug__:
        parser.error('Assertions must be enabled; run without -O')
    parser.add_argument('elf', type=Path, help='Isolated dynamic-buffer RV32 ELF with DWARF')
    parser.add_argument('--output', type=Path, required=True, help='New local JSON report')
    args = parser.parse_args()
    result = reproduce(args.elf)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print('Partial header loss reproduced at all four split points; report:', args.output)
