"""Check fixed hostname lifetime using the compiled SDK setter and cleanup.

No handshake, authentication, radio or timing is tested. Heap, ROM string/byte
operations and PSA hash setup/abort are doubled, as in the record-buffer probes.
"""
import argparse
import hashlib
import json
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_CODE
from unicorn.riscv_const import UC_RISCV_REG_A0
from .tls_fragment_probe import CONFIG, SSL
from .tls_output_probe import OutputProbe, record_storage
from .tls_state_probe import assert_storage, reserve_slots, state_storage


def hostname_storage(path):
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        for unit in elf.get_dwarf_info().iter_CUs():
            source = unit.get_top_DIE().attributes['DW_AT_name'].value.decode()
            if Path(source.replace('\\', '/')).name != 'c3tout.c':
                continue
            for die in unit.iter_DIEs():
                name = die.attributes.get('DW_AT_name')
                if die.tag != 'DW_TAG_variable' or not name or name.value != b'g_hostname':
                    continue
                location = die.attributes['DW_AT_location'].value
                assert elf.elfclass == 32 and elf.little_endian
                assert len(location) == 5 and location[0] == 3  # DW_OP_addr
                address = int.from_bytes(location[1:], 'little')
                array = die.get_DIE_from_attribute('DW_AT_type')
                bounds = [c for c in array.iter_children() if c.tag == 'DW_TAG_subrange_type']
                assert len(bounds) == 1
                capacity = bounds[0].attributes['DW_AT_upper_bound'].value + 1
                assert capacity == 254
                symbols = elf.get_section_by_name('.symtab').get_symbol_by_name('g_hostname')
                assert any(s['st_value'] == address and s['st_size'] == capacity for s in symbols)
                return dict(address=address, capacity=capacity)
    raise ValueError('ELF must include the fixed c3tout hostname array')


class NameProbe(OutputProbe):
    def __init__(self, path):
        super().__init__(path)
        self.name_heap_requests = 0
        self.reference_heap_requests = 0
        self.hook(self.symbols['strlen'], self.strlen)
        name = 'mbedtls_ssl_set_hostname'
        self.calls[name] = 0
        self.uc.hook_add(UC_HOOK_CODE, lambda *_: self.count(name),
                         begin=self.symbols[name], end=self.symbols[name])

    def strlen(self, *args):
        pointer = self.uc.reg_read(UC_RISCV_REG_A0)
        for size in range(512):
            if self.uc.mem_read(pointer + size, 1) == b'\0':
                self.finish(size)
                return
        raise AssertionError('Unexpected unterminated probe string')

    def set_name(self, context, value, native=False):
        source = SSL + 0x1800
        if value is not None:
            self.uc.mem_write(source, value + b'\0')
        before = len(self.allocations)
        result = self.run('mbedtls_ssl_set_hostname' if native else 'c3tout_set_name',
                          context, 0 if value is None else source)
        added = len(self.allocations) - before
        if native:
            self.reference_heap_requests += added
        else:
            self.name_heap_requests += added
            assert added == 0, 'Wrapped hostname request reached the heap'
        # The caller's temporary source expires once the setter returns.
        self.uc.mem_write(source, b'?' * 256)
        return result


def verify(path):
    storage = hostname_storage(path)
    states = state_storage(path)
    probe = NameProbe(path)
    address, capacity = storage['address'], storage['capacity']
    for slot in record_storage(path).values():
        assert (address + capacity <= slot['address']
                or slot['address'] + slot['linked_bytes'] <= address)
    owner, reference, other = SSL, SSL + 0x400, SSL + 0x800
    for context in (owner, reference, other):
        probe.run('mbedtls_ssl_init', context)
    assert probe.run('mbedtls_ssl_config_defaults', CONFIG, 0, 0, 0) == 0

    # The unchanged setter is an allocation-positive reference in this same ELF.
    for value in (b'a' * 253, b'broker.local', b'', b'x', None):
        assert probe.set_name(owner, value) == probe.set_name(reference, value, native=True) == 0
        assert probe.field(owner, 'flags') == probe.field(reference, 'flags')
        if value is None:
            assert probe.field(owner, 'hostname') == probe.field(reference, 'hostname') == 0
            assert bytes(probe.uc.mem_read(address, capacity)) == bytes(capacity)
        else:
            assert probe.field(owner, 'hostname') == address and address not in probe.live
            expected = value + b'\0'
            assert bytes(probe.uc.mem_read(address, capacity)) == value + bytes(capacity - len(value))
            assert bytes(probe.uc.mem_read(probe.field(reference, 'hostname'), len(expected))) == expected
    assert probe.reference_heap_requests == 4 and not probe.live

    assert probe.set_name(owner, b'retained') == 0
    original = bytes(probe.uc.mem_read(address, capacity))
    flags, calls = probe.field(owner, 'flags'), probe.calls['mbedtls_ssl_set_hostname']
    for length in (254, 255, 256):
        assert probe.set_name(owner, b'z' * length) == -135  # PSA_ERROR_INVALID_ARGUMENT
        assert probe.field(owner, 'hostname') == address and probe.field(owner, 'flags') == flags
        assert bytes(probe.uc.mem_read(address, capacity)) == original
    assert probe.calls['mbedtls_ssl_set_hostname'] == calls
    assert probe.set_name(other, b'second') == -141  # PSA_ERROR_INSUFFICIENT_MEMORY
    assert probe.field(other, 'hostname') == probe.field(other, 'flags') == 0
    assert bytes(probe.uc.mem_read(address, capacity)) == original
    probe.run('mbedtls_ssl_free', owner)
    assert bytes(probe.uc.mem_read(address, capacity)) == bytes(capacity)
    assert probe.set_name(other, b'second') == 0
    assert probe.field(other, 'hostname') == address
    probe.run('mbedtls_ssl_free', other)

    # Failed TLS setup must retain the name until full SDK cleanup releases it.
    for kind in states:
        probe.run('mbedtls_ssl_init', owner)
        assert probe.set_name(owner, b'broker.local') == 0
        reserved = reserve_slots(probe, states, kind)
        before = len(probe.allocations)
        assert probe.run('c3tout_setup', owner, CONFIG) != 0
        assert len(probe.allocations) == before
        assert probe.field(owner, 'hostname') == address
        assert bytes(probe.uc.mem_read(address, 13)) == b'broker.local\0'
        probe.run('mbedtls_ssl_free', owner)
        assert_storage(probe, states, reserved)
        for pointer, content in reserved.items():
            assert bytes(probe.uc.mem_read(pointer, len(content))) == content
            probe.run('c3tout_free', pointer)
        assert_storage(probe, states, [])
        assert bytes(probe.uc.mem_read(address, capacity)) == bytes(capacity)

    for _ in range(100):
        probe.run('mbedtls_ssl_init', owner)
        assert probe.set_name(owner, b'broker.local') == 0
        assert probe.run('c3tout_setup', owner, CONFIG) == 0
        assert probe.field(owner, 'hostname') == address
        assert bytes(probe.uc.mem_read(address, 13)) == b'broker.local\0'
        probe.run('mbedtls_ssl_free', owner)
        assert_storage(probe, states, [])
        assert bytes(probe.uc.mem_read(address, capacity)) == bytes(capacity)

    before = len(probe.allocations)
    unrelated = probe.run('c3tout_alloc', 1, capacity)
    assert unrelated in probe.live and unrelated != address
    assert len(probe.allocations) == before + 1
    probe.run('c3tout_free', unrelated)
    probe.run('mbedtls_ssl_free', reference)
    assert not probe.live
    return dict(passed=True, elf_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                scope='RV32 SDK hostname setter/setup/free; no handshake, authentication or hardware',
                doubles=['SDK heap', 'ROM strlen/byte operations', 'PSA hash setup/abort'],
                hostname_storage=storage, hostname_heap_requests=probe.name_heap_requests,
                unwrapped_reference_heap_requests=probe.reference_heap_requests,
                boundary_replacement_clear_cases=5, oversize_rejections=3,
                exhaustion_reuse_cases=1, setup_failure_cases=3, reconnect_cycles=100,
                unrelated_allocation_cases=1, sdk_calls=probe.calls)


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
