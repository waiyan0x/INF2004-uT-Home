"""Check pinned RV32 TLS input lifetime and fixed-buffer capacity.

Runs actual SDK setup, fetch, plaintext-consumption and cleanup instructions.
Transport, heap, ROM byte copies and PSA hash setup/abort are doubled. The
plaintext case injects post-decryption state; no handshake or crypto is tested.
Requires the optional environment documented for the other TLS probes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile
from .tls_fragment_probe import CONFIG, RECV, SSL, STACK, STACK_SIZE, WANT_READ
from .tls_output_probe import OutputProbe, record_storage


def handshake_over(path):
    with path.open('rb') as stream:
        for unit in ELFFile(stream).get_dwarf_info().iter_CUs():
            for die in unit.iter_DIEs():
                name = die.attributes.get('DW_AT_name')
                if (die.tag == 'DW_TAG_enumerator' and name
                        and name.value == b'MBEDTLS_SSL_HANDSHAKE_OVER'):
                    return die.attributes['DW_AT_const_value'].value
    raise ValueError('ELF must retain the SDK handshake-state enumeration')


class InputProbe(OutputProbe):
    def set_field(self, context, name, value):
        self.uc.mem_write(context + self.layout[name], struct.pack('<I', value))

    def fetch(self, context, wanted, chunks):
        self.chunks = chunks.copy()
        result = self.run('mbedtls_ssl_fetch_input', context, wanted)
        # Legitimate work between owner calls can overwrite the returned stack.
        self.uc.mem_write(STACK, bytes([0xA5]) * STACK_SIZE)
        return result

    def pending(self, context):
        return bytes(self.uc.mem_read(self.field(context, 'in_hdr'),
                                      self.field(context, 'in_left')))


def capacity(memory, elf_hash, storage):
    assert memory['passed'], 'Use a passing fixed-memory report'
    assert memory['artifacts']['build/uthome_runtime_port.elf']['sha256'] == elf_hash, (
        'Memory report must describe this exact ELF')
    groups, budgets = memory['groups'], memory['budgets']
    headroom = budgets['application_fixed_limit'] - groups['application_fixed']
    return {'projection_only': False, 'input_storage': storage,
            'application_headroom': headroom,
            'total_dram': groups['total_dram'],
            'total_dram_headroom': budgets['total_dram_limit'] - groups['total_dram'],
            'limits': 'Linker capacity only; remaining crypto/TCP/IP storage and runtime peaks are unqualified'}


def verify(path, memory):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    # Reject a mismatched report before running any emulated SDK instructions.
    assert memory['passed']
    assert memory['artifacts']['build/uthome_runtime_port.elf']['sha256'] == digest
    probe = InputProbe(path)
    storage = record_storage(path)['input']
    assert 'esp_mbedtls_add_rx_buffer' not in probe.symbols, 'Dynamic-buffer image is unsupported'
    state = handshake_over(path)
    contexts = [SSL]
    assert probe.run('mbedtls_ssl_config_defaults', CONFIG, 0, 0, 0) == 0
    sizes, inputs = [], []
    for index, context in enumerate(contexts):
        probe.run('mbedtls_ssl_init', context)
        assert probe.run('c3tout_setup', context, CONFIG) == 0
        probe.set_field(context, 'f_recv', RECV)
        pointer = probe.field(context, 'in_buf')
        assert pointer == storage['address'] and pointer not in probe.live
        assert bytes(probe.uc.mem_read(pointer, storage['capacity'])) == bytes(storage['capacity'])
        inputs.append(pointer)
        sizes.append(storage['capacity'])
    assert len(set(inputs)) == 1
    memory_result = capacity(memory, digest, storage)

    # The single MQTT connection must retain each partial header across calls.
    headers = [bytes.fromhex('1703030011')]
    for split in range(1, 5):
        for index, context in enumerate(contexts):
            probe.set_field(context, 'in_left', 0)
            assert probe.fetch(context, 5, [headers[index][:split], WANT_READ]) == WANT_READ
        for index, context in reversed(list(enumerate(contexts))):
            assert probe.pending(context) == headers[index][:split]
            assert probe.fetch(context, 5, [headers[index][split:]]) == 0
            assert probe.pending(context) == headers[index]

    # Full-size content is fetched, not decrypted or semantically validated.
    records = [bytes.fromhex('1703034000') + b'1' * 16384]
    for split in (1, 128, 16383):
        for index, context in enumerate(contexts):
            probe.set_field(context, 'in_left', 0)
            assert probe.fetch(context, 5, [records[index][:5]]) == 0
            assert probe.fetch(context, len(records[index]),
                               [records[index][5:5 + split], WANT_READ]) == WANT_READ
        for index, context in reversed(list(enumerate(contexts))):
            assert probe.pending(context) == records[index][:5 + split]
            assert probe.fetch(context, len(records[index]), [records[index][5 + split:]]) == 0
            assert probe.pending(context) == records[index]

    # Bounds fail before transport; EOF retains the partial count for cleanup.
    for index, context in enumerate(contexts):
        limit = sizes[index] - (probe.field(context, 'in_hdr') - inputs[index])
        before = probe.pending(context)
        assert probe.fetch(context, limit + 1, [b'untouched']) < 0
        assert probe.chunks == [b'untouched'] and probe.pending(context) == before
        probe.set_field(context, 'in_left', 0)
        assert probe.fetch(context, 5, [headers[index][:2], 0]) == -0x7280
        assert probe.pending(context) == headers[index][:2]

    # Inject already-decrypted application data and run the real read path.
    payloads = [bytes(i % 256 for i in range(257))]
    consumed = [0]
    scratch = SSL + 0x1800
    for index, context in enumerate(contexts):
        pointer = probe.field(context, 'in_msg')
        probe.uc.mem_write(pointer, payloads[index])
        probe.set_field(context, 'state', state)
        probe.set_field(context, 'in_offt', pointer)
        probe.set_field(context, 'in_msglen', len(payloads[index]))
    for request in (1, 128, 256):
        for index, context in reversed(list(enumerate(contexts))):
            expected = payloads[index][consumed[index]:consumed[index] + request]
            assert probe.run('mbedtls_ssl_read', context, scratch, request) == len(expected)
            assert bytes(probe.uc.mem_read(scratch, len(expected))) == expected
            consumed[index] += len(expected)
            assert bytes(probe.uc.mem_read(probe.field(context, 'in_msg'), consumed[index])) == bytes(consumed[index])
            remaining = payloads[index][consumed[index]:]
            assert probe.field(context, 'in_msglen') == len(remaining)
            pointer = probe.field(context, 'in_offt')
            assert (bytes(probe.uc.mem_read(pointer, len(remaining))) == remaining
                    if remaining else pointer == 0)
            probe.uc.mem_write(STACK, bytes([0x5A]) * STACK_SIZE)
    assert consumed == [257]
    for context in contexts:
        probe.run('mbedtls_ssl_free', context)
    assert not probe.live, probe.live
    assert sizes[0] not in probe.allocations, probe.allocations
    assert bytes(probe.uc.mem_read(inputs[0], sizes[0])) == bytes(sizes[0])
    return {'passed': True, 'scope': 'RV32 input lifetime/capacity probe; no handshake, decryption or hardware',
            'elf_sha256': digest, 'fixed_input_storage_implemented': True,
            'input_heap_requests': probe.allocations.count(sizes[0]),
            'doubles': ['SDK heap', 'transport', 'ROM byte copies', 'PSA hash setup/abort'],
            'injected_state': 'Post-decryption plaintext for the application-read cases',
            'sessions': 1, 'header_split_cases': 4, 'body_split_cases': 3,
            'oversize_cases': 1, 'partial_eof_cases': 1, 'plaintext_read_cases': 3,
            'sdk_fetch_calls': probe.fetch_calls, 'sdk_setup_cleanup_calls': probe.calls,
            'capacity': memory_result}


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
    memory = json.loads(args.memory_report.read_text(encoding='utf-8'))
    result = verify(args.elf, memory)
    result['memory_report_sha256'] = hashlib.sha256(args.memory_report.read_bytes()).hexdigest()
    with args.output.open('x', encoding='utf-8') as report:
        report.write(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
