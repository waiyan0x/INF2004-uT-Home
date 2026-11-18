"""Exercise compiled PSA derivation input ownership, not cryptographic output.

Native input validation, copying, error abort and cleanup execute in RV32.
Heap, ROM byte operations and hash support setup/abort are doubled.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile
from .tls_fragment_probe import SSL
from .tls_output_probe import OutputProbe

SEED, SECRET, LABEL = 0x0204, 0x0101, 0x0201
ALGORITHMS = (0x08000209, 0x0800020A)
DATA = SSL + 0x1000


def layouts(path, extra=()):
    wanted = {'psa_key_derivation_s', 'psa_tls12_prf_key_derivation_t',
              'psa_driver_key_derivation_context_t', *extra}
    result = {}
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf.little_endian
        for unit in elf.get_dwarf_info().iter_CUs():
            for die in unit.iter_DIEs():
                name = die.attributes.get('DW_AT_name')
                if not name or name.value.decode() not in wanted:
                    continue
                name = name.value.decode()
                while die.tag == 'DW_TAG_typedef':
                    die = die.get_DIE_from_attribute('DW_AT_type')
                if 'DW_AT_byte_size' not in die.attributes:
                    continue
                result[name] = dict(size=die.attributes['DW_AT_byte_size'].value,
                                   fields={c.attributes['DW_AT_name'].value.decode():
                                           c.attributes['DW_AT_data_member_location'].value
                                           if 'DW_AT_data_member_location' in c.attributes else 0
                                           for c in die.iter_children() if c.tag == 'DW_TAG_member'})
            if set(result) == wanted:
                break
    assert set(result) == wanted
    assert result['psa_driver_key_derivation_context_t']['fields']['tls12_prf'] == 0
    return result


def fixed_storage(path):
    sizes = {'g_prf_seed': 64, 'g_prf_label': 22,
             'gb_seed_used': 1, 'gb_label_used': 1,
             'g_prf_step': 2, 'g_prf_size': 4, 'gh_prf_owner': 4}
    result = {}
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        for unit in elf.get_dwarf_info().iter_CUs():
            source = unit.get_top_DIE().attributes['DW_AT_name'].value.decode()
            if Path(source.replace('\\', '/')).name != 'c3tout.c':
                continue
            for die in unit.iter_DIEs():
                name = die.attributes.get('DW_AT_name')
                if die.tag != 'DW_TAG_variable' or not name or name.value.decode() not in sizes:
                    continue
                name = name.value.decode()
                location = die.attributes['DW_AT_location'].value
                assert len(location) == 5 and location[0] == 3
                address = int.from_bytes(location[1:], 'little')
                symbols = elf.get_section_by_name('.symtab').get_symbol_by_name(name)
                assert any(s['st_value'] == address and s['st_size'] == sizes[name] for s in symbols)
                result[name] = dict(address=address, size=sizes[name])
    assert set(result) == set(sizes), 'ELF lacks fixed PRF input stores'
    ranges = sorted((v['address'], v['size']) for v in result.values())
    assert all(a + n <= b for (a, n), (b, _) in zip(ranges, ranges[1:]))
    return result


class PrfProbe(OutputProbe):
    def __init__(self, path, fixed):
        super().__init__(path)
        self.types = layouts(path)
        self.storage = fixed_storage(path) if fixed else {}
        self.input_name = 'c3tout_prf_input' if fixed else 'psa_key_derivation_input_bytes'
        self.task = 1
        if 'c3tout_prf_alloc' in self.symbols:
            self.hook(self.symbols['c3bport_task'], lambda *args: self.finish(self.task))
            self.hook(self.symbols['c3bport_enter'], lambda *args: self.finish(0))
            self.hook(self.symbols['c3bport_leave'], lambda *args: self.finish(0))

    def start(self, alg=ALGORITHMS[0]):
        size = self.types['psa_key_derivation_s']['size']
        self.uc.mem_write(SSL, bytes(size))
        assert self.run('psa_key_derivation_setup', SSL, alg) == 0

    def input(self, step, data):
        self.uc.mem_write(DATA, data or b'\0')
        return self.run(self.input_name, SSL, step, DATA, len(data))

    def field(self, name):
        offset = self.types['psa_key_derivation_s']['fields']['ctx']
        offset += self.types['psa_tls12_prf_key_derivation_t']['fields'][name]
        return struct.unpack('<I', self.uc.mem_read(SSL + offset, 4))[0]

    def clean(self):
        assert self.run('psa_key_derivation_abort', SSL) == 0
        assert not self.live, self.live
        size = self.types['psa_key_derivation_s']['size']
        assert bytes(self.uc.mem_read(SSL, size)) == bytes(size)
        for slot in self.storage.values():
            assert bytes(self.uc.mem_read(slot['address'], slot['size'])) == bytes(slot['size'])


def verify(path, fixed):
    probe = PrfProbe(path, fixed)
    for alg in ALGORITHMS:
        for _ in range(100):
            probe.start(alg)
            for step, name, data in ((SEED, 'seed', bytes(range(64))),
                                     (SECRET, 'secret', b'k' * 48),
                                     (LABEL, 'label', b'extended master secret')):
                assert probe.input(step, data) == 0
                pointer = probe.field(name)
                assert probe.field(name + '_length') == len(data)
                probe.uc.mem_write(DATA, b'!' * len(data))
                assert bytes(probe.uc.mem_read(pointer, len(data))) == data
                if fixed and step != SECRET:
                    assert pointer == probe.storage['g_prf_' + name]['address']
                else:
                    assert pointer in probe.live
            probe.clean()
    assert len(probe.allocations) == (200 if fixed else 600)
    allocation_count = len(probe.allocations)

    boundaries = []
    for size in (0, 1, 32, 48, 64):
        for label in (b'', b'x', b'master secret', b'client finished', b'extended master secret'):
            probe.start()
            assert probe.input(SEED, b's' * size) == 0
            assert probe.input(SECRET, b'') == 0
            assert probe.input(LABEL, label) == 0
            assert probe.field('seed_length') == size
            assert probe.field('label_length') == len(label)
            probe.clean()
            boundaries.append([size, len(label)])

    errors = []
    for case in ('label_first', 'seed_twice', 'label_twice', 'invalid_step', 'secret_allocation'):
        probe.start()
        if case == 'label_first':
            result = probe.input(LABEL, b'label')
        else:
            assert probe.input(SEED, b's' * 64) == 0
            if case == 'seed_twice':
                result = probe.input(SEED, b's')
            elif case == 'secret_allocation':
                probe.fail_call = len(probe.allocations) + 1
                result = probe.input(SECRET, b'k' * 48)
                probe.fail_call = None
            else:
                assert probe.input(SECRET, b'k' * 48) == 0
                assert probe.input(LABEL, b'extended master secret') == 0
                result = probe.input(LABEL if case == 'label_twice' else 0xFFFF, b'x')
        expected = -141 if case == 'secret_allocation' else -135 if case == 'invalid_step' else -137
        assert result == expected, (case, result)
        probe.clean()
        errors.append(dict(case=case, result=result))

    exhaustion = []
    for kind, step in (('seed', SEED), ('label', LABEL)):
        probe.start()
        if step == LABEL:
            assert probe.input(SEED, b's' * 64) == 0
            assert probe.input(SECRET, b'k' * 48) == 0
        if fixed:
            slot = probe.storage['g_prf_' + kind]
            probe.uc.mem_write(slot['address'], b'X' * slot['size'])
            probe.uc.mem_write(probe.storage['gb_' + kind + '_used']['address'], b'\1')
        else:
            probe.fail_call = len(probe.allocations) + 1
        before = len(probe.allocations)
        assert probe.input(step, b'x') == -141
        if fixed:
            assert len(probe.allocations) == before
            assert bytes(probe.uc.mem_read(slot['address'], slot['size'])) == b'X' * slot['size']
            probe.run('c3tout_free', slot['address'])
        probe.fail_call = None
        probe.clean()
        probe.start()
        assert probe.input(SEED, b's') == 0
        assert probe.input(SECRET, b'k') == 0
        assert probe.input(LABEL, b'l') == 0
        probe.clean()
        exhaustion.append(kind)

    isolation = []
    if fixed:
        for kind, step, size in (('seed', SEED, 64), ('label', LABEL, 22)):
            # Model the owner paused after publishing its protected scope.
            for name, value in (('g_prf_step', step), ('g_prf_size', size), ('gh_prf_owner', 1)):
                slot = probe.storage[name]
                probe.uc.mem_write(slot['address'], value.to_bytes(slot['size'], 'little'))
            probe.task = 2
            before = len(probe.allocations)
            pointer = probe.run('c3tout_prf_alloc', 1, size)
            assert pointer in probe.live and len(probe.allocations) == before + 1
            probe.run('c3tout_free', pointer)
            probe.task = 1
            pointer = probe.run('c3tout_prf_alloc', 1, size)
            assert pointer == probe.storage['g_prf_' + kind]['address']
            assert len(probe.allocations) == before + 1
            probe.run('c3tout_free', pointer)
            # A real wrapper return clears all scope metadata, even on error.
            assert probe.input(SEED, b'x') == -137
            probe.clean()
            isolation.append(kind)

    return dict(passed=True, fixed_storage=fixed, store_layout=probe.storage,
                elf_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                cycles=200, lifecycle_heap_requests=allocation_count,
                seed_label_heap_requests=0 if fixed else 400,
                secret_heap_requests=200, boundaries=boundaries,
                errors=errors, exhaustion=exhaustion, foreign_task_cases=isolation,
                doubles=['SDK heap', 'ROM byte operations', 'PSA hash setup/abort',
                         'native task identity and interrupt exclusion when fixed hooks are linked'],
                scope='Native PSA input validation/copying/abort; no PRF output or handshake',
                authenticated_tls_tested=False, hardware_tested=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--expect', choices=('heap', 'fixed'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        parser.error('Assertions must remain enabled')
    if args.output.exists():
        parser.error('Report already exists; choose a new path')
    result = verify(args.elf, args.expect == 'fixed')
    with args.output.open('x', encoding='utf-8') as report:
        report.write(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
