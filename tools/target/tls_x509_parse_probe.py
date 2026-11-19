"""Measure live heap requests from the compiled ESP broker certificate parser."""
import argparse
import hashlib
import json
from pathlib import Path
import ssl
import struct

from unicorn.riscv_const import (
    UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_RA,
)

from .tls_fragment_probe import SSL
from .tls_output_probe import OutputProbe


class ParserProbe(OutputProbe):
    def __init__(self, elf):
        super().__init__(elf)
        self.peak = 0
        self.sites = []
        self.multiplications = 0
        self.hook(self.symbols['memcmp'], self.compare)
        self.hook(self.symbols['__clzsi2'], self.leading_zeros)
        self.hook(self.symbols['__bswapsi2'], self.byte_swap)
        self.product = None
        self.hook(self.symbols['esp_mpi_mul_mpi_hw_op'], self.multiply)
        self.hook(self.symbols['mpi_hal_read_result_hw_op'], self.read_product)
        for name in ('mbedtls_mutex_lock', 'mbedtls_mutex_unlock',
                     'esp_mpi_enable_hardware_hw_op', 'esp_mpi_disable_hardware_hw_op'):
            self.hook(self.symbols[name], lambda *_: self.finish(0))

    def multiply(self, *_):
        """Model the C3 peripheral product; SDK limb growth and curve checks run."""
        def magnitude(register):
            mpi = self.uc.reg_read(register)
            # ESP RV32 mbedtls_mpi stores the limb pointer, sign and limb count.
            limbs, _, count = struct.unpack('<IhH', self.uc.mem_read(mpi, 8))
            return int.from_bytes(self.uc.mem_read(limbs, count * 4), 'little')

        assert self.product is None
        self.multiplications += 1
        self.product = magnitude(UC_RISCV_REG_A0) * magnitude(UC_RISCV_REG_A1)
        self.finish(0)

    def read_product(self, *_):
        pointer, capacity, words = (self.uc.reg_read(r) for r in
                                    (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2))
        assert words <= capacity and self.product.bit_length() <= 32 * words
        self.uc.mem_write(pointer, self.product.to_bytes(4 * capacity, 'little'))
        self.product = None
        self.finish(0)

    def leading_zeros(self, *_):
        self.finish(32 - self.uc.reg_read(UC_RISCV_REG_A0).bit_length())

    def byte_swap(self, *_):
        word = self.uc.reg_read(UC_RISCV_REG_A0)
        self.finish(int.from_bytes(word.to_bytes(4, 'little'), 'big'))

    def compare(self, *_):
        left, right, size = (self.uc.reg_read(r) for r in
                             (UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2))
        a = bytes(self.uc.mem_read(left, size))
        b = bytes(self.uc.mem_read(right, size))
        self.finish((a > b) - (a < b))

    def calloc(self, *args):
        self.sites.append(self.uc.reg_read(UC_RISCV_REG_RA))
        super().calloc(*args)
        self.peak = max(self.peak, sum(self.live.values()))


def measure(elf, pem):
    end_marker = '-----END CERTIFICATE-----'
    *blocks, trailing = pem.read_text(encoding='ascii').strip().split(end_marker)
    if not blocks or trailing.strip():
        raise ValueError('Expected a PEM certificate chain')
    certs = [ssl.PEM_cert_to_DER_cert(block.strip() + '\n' + end_marker) for block in blocks]
    probe = ParserProbe(elf)
    assert probe.run('psa_initialize_key_slots') == 0
    assert not probe.live and not probe.allocations, 'Expected the fixed-slot target'
    chain = probe.symbols['g_certificate']
    address = SSL + 0x40000
    total_bytes = sum(map(len, certs))
    probe.uc.mem_map(address, (total_bytes + 4095) & ~4095)
    inputs = []
    for der in certs:
        probe.uc.mem_write(address, der)
        inputs.append((address, der))
        address += len(der)

    def parse(fail_at=None):
        probe.run('mbedtls_x509_crt_init', chain)
        probe.fail_call = fail_at
        probe.allocations.clear()
        probe.sites.clear()
        probe.multiplications = 0
        records = []
        for source, der in inputs:
            size = len(der)
            probe.peak = sum(probe.live.values())
            status = probe.run('mbedtls_x509_crt_parse_der_nocopy', chain, source, size)
            if status != 0:
                break
            records.append(dict(der_bytes=size, der_sha256=hashlib.sha256(der).hexdigest(),
                                peak_requested_bytes=probe.peak, retained_bytes=sum(probe.live.values())))
        result = dict(certificates=records,
                      requests=list(probe.allocations), caller_return_pcs=list(probe.sites),
                      retained_blocks=sorted(probe.live.values()), multiplications=probe.multiplications)
        probe.run('mbedtls_x509_crt_free', chain)
        assert not probe.live and probe.product is None
        return status, result

    status, baseline = parse()
    assert status == 0
    for index in range(1, len(baseline['requests']) + 1):
        status, failed = parse(fail_at=index)
        assert status < 0 and len(failed['requests']) >= index, (index, status)
        assert parse() == (0, baseline), 'Successful retry must match the original trace'
    peak = max(record['peak_requested_bytes'] for record in baseline['certificates'])
    return dict(**baseline, peak_requested_bytes=peak, after_free_bytes=0,
                allocation_failures_retried=len(baseline['requests']))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('pem', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        parser.error('Assertions must be enabled')
    result = measure(args.elf, args.pem)
    result['elf_sha256'] = hashlib.sha256(args.elf.read_bytes()).hexdigest()
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(result['peak_requested_bytes'], 'peak requested bytes')
