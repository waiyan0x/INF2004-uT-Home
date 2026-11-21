"""Check the Wi-Fi image's compiled SHA, HMAC and two-slot AES implementation."""
import argparse
import hashlib
import hmac
import json
from pathlib import Path
import struct

from unicorn.riscv_const import (
    UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_PC, UC_RISCV_REG_RA,
    UC_RISCV_REG_SP,
)

from .tls_fragment_probe import Probe, SSL, STACK, STACK_SIZE, STOP


class CryptoProbe(Probe):
    def __init__(self, elf):
        super().__init__(elf)
        for name in ('malloc', 'calloc', 'realloc', 'heap_caps_malloc', 'heap_caps_calloc',
                     'heap_caps_realloc', 'heap_caps_aligned_alloc', 'mbedtls_calloc'):
            if name in self.symbols:
                self.hook(self.symbols[name], self.unexpected_allocation)
        for name in ('c3bport_enter', 'c3bport_leave'):
            self.hook(self.symbols[name], lambda *_: self.finish(0))
        self.hook(self.symbols['__bswapsi2'], self.swap32)
        self.hook(self.symbols['__bswapdi2'], self.swap64)

    def unexpected_allocation(self, _cpu, address, _size, _data):
        raise AssertionError(f'Compiled crypto reached allocator at {address:#x}')

    def swap32(self, *_):
        value = self.uc.reg_read(UC_RISCV_REG_A0)
        self.finish(int.from_bytes(value.to_bytes(4, 'little'), 'big'))

    def swap64(self, *_):
        value = self.uc.reg_read(UC_RISCV_REG_A0) | (self.uc.reg_read(UC_RISCV_REG_A1) << 32)
        swapped = int.from_bytes(value.to_bytes(8, 'little'), 'big')
        self.uc.reg_write(UC_RISCV_REG_A1, swapped >> 32)
        self.finish(swapped)

    def call(self, name, *args):
        stack = STACK + STACK_SIZE - 128
        for index, value in enumerate(args[:8]):
            self.uc.reg_write(UC_RISCV_REG_A0 + index, value)
        for index, value in enumerate(args[8:]):
            self.uc.mem_write(stack + 4 * index, struct.pack('<I', value))
        self.uc.reg_write(UC_RISCV_REG_SP, stack)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        self.uc.emu_start(self.symbols[name], STOP, count=1_000_000)
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP, name
        return self.uc.reg_read(UC_RISCV_REG_A0)


def verify(elf):
    probe = CryptoProbe(elf)
    read = probe.uc.mem_read
    write = probe.uc.mem_write
    call = probe.call
    operation = SSL + 0x12000
    message = operation + 0x2000
    key = message + 0x200
    attributes = key + 64
    nonce = attributes + 64
    output = message + 0x500
    size = output + 128

    write(message, b'abc')
    assert call('psa_hash_setup', operation, 0x02000009) == 0
    assert call('psa_hash_update', operation, message, 3) == 0
    assert call('psa_hash_finish', operation, output, 32, size) == 0
    assert read(output, 32) == hashlib.sha256(b'abc').digest()
    assert int.from_bytes(read(size, 4), 'little') == 32

    write(operation, bytes(512))
    write(key, b'Jefe')
    write(attributes, struct.pack('<HH', 0x1100, 32) + bytes(20))
    assert call('mbedtls_psa_mac_sign_setup', operation, attributes, key, 4, 0x03800009) == 0
    assert call('mbedtls_psa_mac_update', operation, message, 3) == 0
    assert call('mbedtls_psa_mac_sign_finish', operation, output, 32, size) == 0
    assert read(output, 32) == hmac.digest(b'Jefe', b'abc', 'sha256')

    write(key, bytes(16))
    write(attributes, struct.pack('<HH', 0x2400, 128) + bytes(20))
    write(nonce, bytes(12))
    write(message, bytes(16))
    common = (attributes, key, 16, 0x05500200, nonce, 12, 0, 0, message)
    encrypt = (*common, 16, output, 32, size)
    decrypt = (*common, 32, output, 16, size)
    vector = bytes.fromhex('0388dace60b6a392f328c2b971b2fe78ab6e47d42cec13bdf53a67b21257bddf')
    assert call('mbedtls_psa_aead_encrypt', *encrypt) == 0
    assert read(output, 32) == vector and int.from_bytes(read(size, 4), 'little') == 32
    write(message, vector)
    assert call('mbedtls_psa_aead_decrypt', *decrypt) == 0
    assert read(output, 16) == bytes(16) and int.from_bytes(read(size, 4), 'little') == 16
    write(output, b'X' * 16)
    write(message, vector[:-1] + bytes([vector[-1] ^ 1]))
    assert call('mbedtls_psa_aead_decrypt', *decrypt) == (-149 & 0xffffffff)
    assert read(output, 16) == bytes(16)

    left, right = call('aes_ctx_alloc'), call('aes_ctx_alloc')
    assert left and right and abs(left - right) == 280
    write(left, b'L' * 280)
    write(right, b'R' * 280)
    assert call('aes_ctx_alloc') == 0
    assert call('mbedtls_psa_aead_encrypt', *encrypt) == (-141 & 0xffffffff)
    assert read(left, 280) == b'L' * 280 and read(right, 280) == b'R' * 280
    call('aes_ctx_free', left)
    assert read(left, 280) == bytes(280)
    write(message, bytes(16))
    assert call('mbedtls_psa_aead_encrypt', *encrypt) == 0
    assert read(output, 32) == vector and read(left, 280) == bytes(280)
    assert read(right, 280) == b'R' * 280
    call('aes_ctx_free', right)
    assert read(right, 280) == bytes(280)
    return dict(elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                passed=True, context_bytes=280, slots=2, observed_allocations=0,
                simulated=['ROM byte operations', 'interrupt save and restore'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__ or args.output.exists():
        parser.error('enable assertions and use a new output path')
    result = verify(args.elf)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print('Compiled crypto vectors, exhaustion, wipe and reuse passed.')
