"""Check fixed TLS state storage with compiled SDK lifecycle instructions.

Empty crypto state advances to native wrapup synthetically; handshake progress
returns WANT_READ through a double. No authentication, crypto peak or hardware
acceptance follows. The ELF's actual typed arrays and leases remain in use.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

from elftools.elf.elffile import ELFFile
from .tls_fragment_probe import CONFIG, SSL, WANT_READ
from .tls_output_probe import OutputProbe, record_storage

FIELDS = ('transform', 'session', 'transform_negotiate', 'session_negotiate', 'handshake')
ENTRIES = ('c3tout_start', 'c3tout_renegotiate')
TYPES = {'transform': ('mbedtls_ssl_transform', 84, 2),
         'session': ('mbedtls_ssl_session', 128, 2),
         'handshake': ('mbedtls_ssl_handshake_params', 2448, 1)}


def state_storage(path):
    """Bind typed record arrays and one-byte leases to this exact linked ELF."""
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
                name = name.value.decode()
                matches = [(kind, lease) for kind in TYPES for lease in (False, True)
                           if name == (f'gb_{kind}_used' if lease else f'g_{kind}s')]
                if not matches:
                    continue
                kind, lease = matches[0]
                location = die.attributes['DW_AT_location'].value
                assert len(location) == 5 and location[0] == 3  # DW_OP_addr, RV32
                address = int.from_bytes(location[1:], 'little')
                array = die.get_DIE_from_attribute('DW_AT_type')
                assert array.tag == 'DW_TAG_array_type'
                bounds = [c for c in array.iter_children() if c.tag == 'DW_TAG_subrange_type']
                assert len(bounds) == 1
                count = bounds[0].attributes['DW_AT_upper_bound'].value + 1
                item = array.get_DIE_from_attribute('DW_AT_type')
                while item.tag == 'DW_TAG_typedef':
                    item = item.get_DIE_from_attribute('DW_AT_type')
                size = item.attributes['DW_AT_byte_size'].value
                expected_type, expected_size, expected_count = TYPES[kind]
                assert count == expected_count
                if lease:
                    assert item.tag == 'DW_TAG_base_type' and size == 1
                else:
                    assert item.tag == 'DW_TAG_structure_type'
                    assert item.attributes['DW_AT_name'].value.decode() == expected_type
                    assert size == expected_size and address % 4 == 0
                symbols = elf.get_section_by_name('.symtab').get_symbol_by_name(name)
                assert any(s['st_value'] == address and s['st_size'] == size * count for s in symbols)
                slot = result.setdefault(kind, {})
                key = 'leases' if lease else 'address'
                assert key not in slot
                slot[key] = address
                if not lease:
                    slot.update(size=size, count=count, linked_bytes=size * count)
    assert set(result) == set(TYPES), 'ELF must contain all fixed state arrays'
    ranges = []
    for slot in result.values():
        assert set(slot) == {'address', 'leases', 'size', 'count', 'linked_bytes'}
        ranges += [(slot['address'], slot['linked_bytes']), (slot['leases'], slot['count'])]
    ranges += [(slot['address'], slot['linked_bytes']) for slot in record_storage(path).values()]
    ranges.sort()
    assert all(start + size <= next_start for (start, size), (next_start, _) in zip(ranges, ranges[1:]))
    return result


def slots(storage):
    for kind, store in storage.items():
        for index in range(store['count']):
            yield kind, store['address'] + index * store['size'], store['size'], store['leases'] + index


def assert_storage(probe, storage, expected):
    """Free outer records must be zero, including the SDK session verify field."""
    actual = set()
    for _, address, size, lease in slots(storage):
        value = bytes(probe.uc.mem_read(lease, 1))
        assert value in (b'\0', b'\1')
        if value == b'\1':
            actual.add(address)
        else:
            assert bytes(probe.uc.mem_read(address, size)) == bytes(size)
    assert actual == set(expected), (actual, expected)
    assert not probe.live, probe.live


def reserve_slots(probe, storage, kind):
    """Occupy currently free leases to inject exhaustion without changing code."""
    reserved = {}
    for found, address, size, lease in slots(storage):
        if found == kind and bytes(probe.uc.mem_read(lease, 1)) == b'\0':
            assert bytes(probe.uc.mem_read(address, size)) == bytes(size)
            content = b'\xa5' * size
            probe.uc.mem_write(address, content)
            probe.uc.mem_write(lease, b'\1')
            reserved[address] = content
    assert reserved
    return reserved


class StateProbe(OutputProbe):
    def __init__(self, path, storage):
        self.storage = storage
        self.handshake_calls = 0
        self.phase_checks = 0
        super().__init__(path)
        assert not any(name in self.symbols for name in
                       ('mbedtls_ssl_session_reset', 'mbedtls_ssl_session_reset_int')), (
            'Session reset requires a separately reviewed allocation scope')
        self.hook(self.symbols['mbedtls_ssl_handshake'], self.handshake)
        self.hook(self.symbols['psa_hash_setup'], self.hash_setup)

    def phase_closed(self):
        pending = int.from_bytes(self.uc.mem_read(self.symbols['g_state_pending'], 4), 'little')
        assert pending == 0, pending
        self.phase_checks += 1

    def hash_setup(self, *args):
        self.phase_closed()
        self.finish(0)

    def handshake(self, *args):
        self.phase_closed()
        self.handshake_calls += 1
        self.finish(WANT_READ)

    def pointers(self):
        return {name: self.field(SSL, name) for name in FIELDS}

    def active(self):
        return {self.field(SSL, name): bytes(self.uc.mem_read(self.field(SSL, name), TYPES[name][1]))
                for name in ('transform', 'session')}

    def unchanged(self, active):
        for pointer, content in active.items():
            assert bytes(self.uc.mem_read(pointer, len(content))) == content

    def check_pointers(self, pointers):
        for name, pointer in pointers.items():
            kind = name.removesuffix('_negotiate')
            assert pointer in [a for k, a, _, _ in slots(self.storage) if k == kind]
        assert len(set(pointers.values())) == len(pointers)


def initialize(path, storage):
    probe = StateProbe(path, storage)
    probe.run('mbedtls_ssl_init', SSL)
    assert probe.run('mbedtls_ssl_config_defaults', CONFIG, 0, 0, 0) == 0
    probe.run('mbedtls_ssl_conf_renegotiation', CONFIG, 1)  # SDK ENABLED value
    return probe


def prepare(path, storage):
    probe = initialize(path, storage)
    assert probe.run('c3tout_setup', SSL, CONFIG) == 0
    initial = {name: pointer for name, pointer in probe.pointers().items() if pointer}
    assert set(initial) == {'transform_negotiate', 'session_negotiate', 'handshake'}
    probe.check_pointers(initial)
    assert_storage(probe, storage, initial.values())
    probe.run('mbedtls_ssl_handshake_wrapup', SSL)
    assert probe.field(SSL, 'transform') == initial['transform_negotiate']
    assert probe.field(SSL, 'session') == initial['session_negotiate']
    assert probe.field(SSL, 'handshake') == 0
    assert_storage(probe, storage, probe.active())
    assert not probe.allocations
    return probe


def verify(path, memory):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    assert memory['passed']
    assert memory['artifacts']['build/uthome_runtime_port.elf']['sha256'] == digest
    storage = state_storage(path)
    results = []
    for entry in ENTRIES:
        probe = prepare(path, storage)
        for _ in range(100):
            old = probe.pointers()
            active = probe.active()
            assert probe.run(entry, SSL) == WANT_READ
            negotiating = probe.pointers()
            probe.check_pointers(negotiating)
            assert negotiating['transform'] == old['transform']
            assert negotiating['session'] == old['session']
            probe.unchanged(active)
            assert_storage(probe, storage, negotiating.values())
            assert probe.run('c3tout_renegotiate', SSL) == WANT_READ
            assert probe.pointers() == negotiating
            probe.unchanged(active)
            probe.run('mbedtls_ssl_handshake_wrapup', SSL)
            assert probe.field(SSL, 'transform') == negotiating['transform_negotiate']
            assert probe.field(SSL, 'session') == negotiating['session_negotiate']
            for name in ('transform_negotiate', 'session_negotiate', 'handshake'):
                assert probe.field(SSL, name) == 0
            assert_storage(probe, storage, probe.active())
            assert not probe.allocations
        probe.run('mbedtls_ssl_free', SSL)
        assert_storage(probe, storage, [])
        assert not probe.allocations
        results.append(dict(entry=entry, cycles=100, allocation_free_retries=100,
                            peak_live_slots={k: v['count'] for k, v in storage.items()},
                            handshake_double_calls=probe.handshake_calls,
                            initialization_phase_checks=probe.phase_checks))

    # Reinitialize existing records and every missing-record combination. Only
    # empty crypto state is removed: hash allocation/progress remain doubled.
    reinitializations = []
    names = ('transform_negotiate', 'session_negotiate', 'handshake')
    for missing in range(8):
        probe = initialize(path, storage)
        assert probe.run('c3tout_setup', SSL, CONFIG) == 0
        before = probe.pointers()
        for bit, name in enumerate(names):
            if missing & (1 << bit):
                probe.run('c3tout_free', before[name])
                probe.write_field(name, 0)
        assert probe.run('c3tout_start', SSL) == WANT_READ
        after = {name: probe.field(SSL, name) for name in names}
        probe.check_pointers(after)
        for bit, name in enumerate(names):
            if not missing & (1 << bit):
                assert after[name] == before[name]
        assert probe.field(SSL, 'transform') == probe.field(SSL, 'session') == 0
        assert_storage(probe, storage, after.values())
        probe.run('mbedtls_ssl_free', SSL)
        assert_storage(probe, storage, [])
        assert not probe.allocations
        reinitializations.append(dict(missing_mask=missing, existing_pointers_preserved=True,
                                      heap_requests=0))

    failures = []
    for entry in ('c3tout_setup', *ENTRIES):
        for kind in TYPES:
            probe = initialize(path, storage) if entry == 'c3tout_setup' else prepare(path, storage)
            old = probe.pointers()
            active = {} if entry == 'c3tout_setup' else probe.active()
            reserved = reserve_slots(probe, storage, kind)
            arguments = (SSL, CONFIG) if entry == 'c3tout_setup' else (SSL,)
            assert probe.run(entry, *arguments) == -141  # PSA_ERROR_INSUFFICIENT_MEMORY
            probe.unchanged(active)
            probe.unchanged(reserved)
            assert all(probe.field(SSL, name) == old[name] for name in ('transform', 'session'))
            assert probe.handshake_calls == 0
            for name in ('transform_negotiate', 'session_negotiate', 'handshake'):
                assert probe.field(SSL, name) == 0
            assert_storage(probe, storage, set(active) | set(reserved))
            for pointer in reserved:
                probe.run('c3tout_free', pointer)
            probe.run('mbedtls_ssl_free', SSL)
            assert_storage(probe, storage, [])
            assert not probe.allocations
            failures.append(dict(entry=entry, exhausted_kind=kind,
                                 active_and_reserved_state_preserved=True, heap_fallback=False))

    # Outside the synchronous initialization scope equal-size requests stay heap-owned.
    probe = prepare(path, storage)
    active = probe.active()
    for kind, (_, size, _) in TYPES.items():
        pointer = probe.run('c3tout_alloc', 1, size)
        assert probe.live == {pointer: size}
        assert pointer not in [address for _, address, _, _ in slots(storage)]
        probe.run('c3tout_free', pointer)
        probe.unchanged(active)
        assert_storage(probe, storage, active)
    probe.run('mbedtls_ssl_free', SSL)
    assert_storage(probe, storage, [])
    assert Counter(probe.allocations) == Counter({84: 1, 128: 1, 2448: 1})

    headroom = memory['budgets']['application_fixed_limit'] - memory['groups']['application_fixed']
    return dict(passed=True, elf_sha256=digest, state_storage=storage,
                scope='Compiled fixed-state lifecycle with synthetic wrapup; no handshake, crypto peak or hardware proof',
                doubles=['SDK heap', 'ROM byte operations', 'PSA hash setup/abort',
                         'mbedtls_ssl_handshake returning WANT_READ'],
                injected_state='Occupied free leases; removed empty records for reinit; zero crypto state for wrapup',
                lifecycle_cases=results, reinitialization_cases=reinitializations,
                allocation_failures=failures,
                unrelated_equal_size_heap_cases=3, lifecycle_state_heap_requests=0,
                fixed_state_storage_implemented=True,
                session_reset_linked='mbedtls_ssl_session_reset' in probe.symbols,
                capacity=dict(projection_only=False,
                              typed_payload_bytes=sum(v['linked_bytes'] for v in storage.values()),
                              application_headroom=headroom,
                              excludes='Runtime peaks and remaining certificate/key/ticket/TCPIP allocations'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--memory-report', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        parser.error('Assertions must remain enabled; do not use python -O')
    if args.output.exists():
        parser.error('Report already exists; choose a new output path')
    result = verify(args.elf, json.loads(args.memory_report.read_text(encoding='utf-8')))
    result['memory_report_sha256'] = hashlib.sha256(args.memory_report.read_bytes()).hexdigest()
    with args.output.open('x', encoding='utf-8') as report:
        report.write(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
