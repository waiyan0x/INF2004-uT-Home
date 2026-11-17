"""Measure compiled TLS state lifetimes before designing fixed typed storage.

SDK allocation, renegotiation entry, wrapup and cleanup instructions run. Empty
crypto state is advanced to wrapup synthetically; the handshake entry returns
WANT_READ through a double. This is not handshake/authentication or hardware proof.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1
from .tls_fragment_probe import CONFIG, SSL, WANT_READ
from .tls_output_probe import OutputProbe

FIELDS = ('transform', 'session', 'transform_negotiate', 'session_negotiate', 'handshake')
ENTRIES = ('mbedtls_ssl_start_renegotiation', 'mbedtls_ssl_renegotiate')


def state_sizes(path):
    names = {'mbedtls_ssl_transform': 'transform', 'mbedtls_ssl_session': 'session',
             'mbedtls_ssl_handshake_params': 'handshake'}
    result = {}
    with path.open('rb') as stream:
        for unit in ELFFile(stream).get_dwarf_info().iter_CUs():
            for die in unit.iter_DIEs():
                name = die.attributes.get('DW_AT_name')
                if die.tag != 'DW_TAG_structure_type' or not name or 'DW_AT_byte_size' not in die.attributes:
                    continue
                kind = names.get(name.value.decode())
                if kind:
                    size = die.attributes['DW_AT_byte_size'].value
                    assert kind not in result or result[kind] == size
                    result[kind] = size
    assert result == dict(transform=84, session=144, handshake=2448), result
    return result


class StateProbe(OutputProbe):
    def __init__(self, path, limits):
        self.limits = limits
        self.peak = Counter()
        self.denials = 0
        self.handshake_calls = 0
        super().__init__(path)
        self.hook(self.symbols['mbedtls_ssl_handshake'], self.handshake)

    def handshake(self, *args):
        self.handshake_calls += 1
        self.finish(WANT_READ)

    def calloc(self, *args):
        size = self.uc.reg_read(UC_RISCV_REG_A0) * self.uc.reg_read(UC_RISCV_REG_A1)
        assert size in self.limits, ('Unexpected allocation outside scoped records', size)
        counts = Counter(self.live.values())
        if counts[size] >= self.limits[size]:
            self.allocations.append(size)
            self.denials += 1
            self.finish(0)
            return
        super().calloc(*args)
        self.peak |= Counter(self.live.values())

    def pointers(self):
        return {name: self.field(SSL, name) for name in FIELDS}

    def active(self):
        return {self.field(SSL, name): bytes(self.uc.mem_read(self.field(SSL, name), size))
                for name, size in (('transform', 84), ('session', 144))}

    def unchanged(self, active):
        for pointer, content in active.items():
            assert pointer in self.live
            assert bytes(self.uc.mem_read(pointer, len(content))) == content


def prepare(path, limits):
    probe = StateProbe(path, limits)
    probe.run('mbedtls_ssl_init', SSL)
    assert probe.run('mbedtls_ssl_config_defaults', CONFIG, 0, 0, 0) == 0
    probe.run('mbedtls_ssl_conf_renegotiation', CONFIG, 1)  # SDK ENABLED value
    assert probe.run('c3tout_setup', SSL, CONFIG) == 0
    assert Counter(probe.live.values()) == Counter({84: 1, 144: 1, 2448: 1})
    initial = probe.pointers()
    probe.run('mbedtls_ssl_handshake_wrapup', SSL)
    assert probe.field(SSL, 'transform') == initial['transform_negotiate']
    assert probe.field(SSL, 'session') == initial['session_negotiate']
    assert probe.field(SSL, 'handshake') == 0
    assert Counter(probe.live.values()) == Counter({84: 1, 144: 1})
    return probe


def verify(path, memory):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    assert memory['passed']
    assert memory['artifacts']['build/uthome_runtime_port.elf']['sha256'] == digest
    sizes = state_sizes(path)
    limits = {84: 2, 144: 2, 2448: 1}
    results = []
    for entry in ENTRIES:
        probe = prepare(path, limits)
        for _ in range(100):
            old = probe.pointers()
            active = probe.active()
            before = len(probe.allocations)
            assert probe.run(entry, SSL) == WANT_READ
            assert probe.allocations[before:] == [84, 144, 2448]
            negotiating = probe.pointers()
            assert len(set(negotiating.values())) == 5 and all(negotiating.values())
            assert negotiating['transform'] == old['transform']
            assert negotiating['session'] == old['session']
            probe.unchanged(active)
            assert Counter(probe.live.values()) == Counter(limits)
            # An in-progress retry must use existing negotiating records.
            before = len(probe.allocations)
            assert probe.run('mbedtls_ssl_renegotiate', SSL) == WANT_READ
            assert probe.pointers() == negotiating and len(probe.allocations) == before
            probe.unchanged(active)
            probe.run('mbedtls_ssl_handshake_wrapup', SSL)
            assert probe.field(SSL, 'transform') == negotiating['transform_negotiate']
            assert probe.field(SSL, 'session') == negotiating['session_negotiate']
            for name in ('transform_negotiate', 'session_negotiate', 'handshake'):
                assert probe.field(SSL, name) == 0
            for pointer in (*active, negotiating['handshake']):
                assert pointer not in probe.live
            assert Counter(probe.live.values()) == Counter({84: 1, 144: 1})
        probe.run('mbedtls_ssl_free', SSL)
        assert not probe.live and not probe.denials
        assert probe.peak == Counter(limits)
        results.append(dict(entry=entry, cycles=100, allocation_free_retries=100,
                            peak_live_slots=dict(probe.peak), handshake_double_calls=probe.handshake_calls))

    failures = []
    for entry in ENTRIES:
        for failure in (1, 2, 3):
            probe = prepare(path, limits)
            old = probe.pointers()
            active = probe.active()
            probe.fail_call = len(probe.allocations) + failure
            assert probe.run(entry, SSL) == -141  # PSA_ERROR_INSUFFICIENT_MEMORY
            probe.unchanged(active)
            assert all(probe.field(SSL, name) == old[name] for name in ('transform', 'session'))
            assert set(probe.live) == set(active) and probe.handshake_calls == 0
            for name in ('transform_negotiate', 'session_negotiate', 'handshake'):
                assert probe.field(SSL, name) == 0
            probe.run('mbedtls_ssl_free', SSL)
            assert not probe.live
            failures.append(dict(entry=entry, failed_allocation=failure, active_state_preserved=True))

    # Negative controls isolate each insufficient proposed record count.
    controls = []
    for size in (84, 144):
        reduced = dict(limits)
        reduced[size] = 1
        probe = prepare(path, reduced)
        old = probe.pointers()
        active = probe.active()
        assert probe.run(ENTRIES[0], SSL) == -141 and probe.denials == 1
        probe.unchanged(active)
        assert all(probe.field(SSL, name) == old[name] for name in ('transform', 'session'))
        assert set(probe.live) == set(active)
        probe.run('mbedtls_ssl_free', SSL)
        assert not probe.live
        controls.append(dict(record_bytes=size, proposed_slots=1, rejected=True))

    payload = sum(size * count for size, count in limits.items())
    headroom = memory['budgets']['application_fixed_limit'] - memory['groups']['application_fixed']
    return dict(passed=True, elf_sha256=digest, state_sizes=sizes,
                scope='Compiled state lifecycle with synthetic wrapup; no handshake, crypto peak or hardware proof',
                doubles=['SDK heap with candidate count limits', 'ROM byte operations',
                         'PSA hash setup/abort', 'mbedtls_ssl_handshake returning WANT_READ'],
                lifecycle_cases=results, allocation_failures=failures, negative_controls=controls,
                fixed_state_storage_implemented=False,
                session_reset_linked='mbedtls_ssl_session_reset' in probe.symbols,
                capacity=dict(projection_only=True, typed_payload_bytes=payload,
                              application_headroom=headroom, remaining_before_metadata=headroom-payload,
                              excludes='Lease/scope metadata, certificates, keys, tickets and TCP/IP storage'))


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
