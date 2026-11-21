"""Check MQTT connect retries in a compiled ESP32-C3 image.

Runs the owned adapter and pinned ESP-TLS instructions. Socket readiness,
socket-error reads, TLS context creation and the handshake are doubled.
Uses the existing optional Unicorn dependency documented in the target guide.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile
from unicorn.riscv_const import (
    UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2,
    UC_RISCV_REG_A3, UC_RISCV_REG_A4, UC_RISCV_REG_PC,
    UC_RISCV_REG_RA, UC_RISCV_REG_SP,
)

from .tls_fragment_probe import SSL, CONFIG, STACK, STACK_SIZE, STOP
from .tls_output_probe import OutputProbe


def type_layout(die):
    while die.tag in ('DW_TAG_typedef', 'DW_TAG_const_type'):
        die = die.get_DIE_from_attribute('DW_AT_type')
    size = die.attributes['DW_AT_byte_size'].value
    fields = {child.attributes['DW_AT_name'].value.decode():
              child.attributes['DW_AT_data_member_location'].value
              for child in die.iter_children()
              if child.tag == 'DW_TAG_member' and 'DW_AT_name' in child.attributes}
    return size, fields


def mqtt_layout(path):
    """Resolve private types and same-named globals in the MQTT compilation unit."""
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        result = {}
        for unit in elf.get_dwarf_info().iter_CUs():
            name = unit.get_top_DIE().attributes['DW_AT_name'].value
            mqtt_unit = name.replace(b'\\', b'/').endswith(b'/c3mqtt.c')
            for die in unit.iter_DIEs():
                name = die.attributes.get('DW_AT_name')
                if name is None:
                    continue
                name = name.value.decode()
                if (die.tag == 'DW_TAG_structure_type' and name == 'esp_tls'
                        and 'DW_AT_byte_size' in die.attributes):
                    result['tls'] = type_layout(die)
                if die.tag == 'DW_TAG_enumeration_type' and name == 'esp_tls_conn_state':
                    result['connecting'] = next(
                        child.attributes['DW_AT_const_value'].value
                        for child in die.iter_children()
                        if child.attributes['DW_AT_name'].value == b'ESP_TLS_CONNECTING')
                if (mqtt_unit and die.tag == 'DW_TAG_variable'
                        and name in ('g_config', 'g_tls_config', 'gp_tls')):
                    location = die.attributes['DW_AT_location'].value
                    assert len(location) == 5 and location[0] == 3, 'expected DW_OP_addr'
                    address = int.from_bytes(bytes(location[1:]), 'little')
                    result[name] = address
                    if name != 'gp_tls':
                        result[name + '_layout'] = type_layout(die.get_DIE_from_attribute('DW_AT_type'))
            if len(result) == 7:
                return result
    raise ValueError('ELF must include c3mqtt.c and private ESP-TLS DWARF types')


class ConnectProbe(OutputProbe):
    def __init__(self, path, layout, timeouts, ready_set):
        super().__init__(path)
        self.remaining = timeouts
        self.ready_set = ready_set
        self.polls = []
        self.handshakes = 0
        self.creations = 0
        tls_size, self.tls = layout['tls']
        config_size, config_fields = layout['g_config_layout']
        tls_config_size, tls_config_fields = layout['g_tls_config_layout']
        # The selected VFS uses 64-bit fd_set, shared with ESP-TLS. Reject other
        # layouts instead of assuming a socket bit that the image cannot watch.
        assert self.tls['wset'] - self.tls['rset'] == 8
        assert self.tls['is_tls'] - self.tls['wset'] == 8
        self.socket_bit = 1 << 54
        self.readiness = {self.symbols[name]: name for name in ('select', 'lwip_select')
                          if name in self.symbols}
        for address in self.readiness:
            self.hook(address, self.select)
        self.hook(self.symbols['lwip_getsockopt'], self.socket_error)
        self.hook(self.symbols['esp_create_mbedtls_handle'], self.create)
        self.hook(self.symbols['esp_mbedtls_handshake'], self.handshake)
        self.hook(self.symbols['strlen'], self.name_length)
        self.uc.mem_write(SSL, bytes(tls_size))
        self.put(SSL + self.tls['sockfd'], 54)
        self.put(SSL + self.tls['conn_state'], layout['connecting'])
        self.uc.mem_write(SSL + self.tls['is_tls'], b'\x01')
        for name in ('rset', 'wset'):
            self.uc.mem_write(SSL + self.tls[name], self.socket_bit.to_bytes(8, 'little'))
        self.uc.mem_write(CONFIG, bytes(tls_config_size))
        self.uc.mem_write(CONFIG + tls_config_fields['non_block'], b'\x01')
        self.put(CONFIG + tls_config_fields['timeout_ms'], 1)
        self.uc.mem_write(SSL + 0x3000, b'127.0.0.1\0')
        self.uc.mem_write(layout['g_config'], bytes(config_size))
        self.put(layout['g_config'] + config_fields['p_address'], SSL + 0x3000)
        self.uc.mem_write(layout['g_config'] + config_fields['port'], struct.pack('<H', 8883))
        self.uc.mem_write(layout['g_tls_config'], bytes(self.uc.mem_read(CONFIG, tls_config_size)))
        self.put(layout['gp_tls'], SSL)

    def put(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value))

    def select(self, *args):
        assert self.uc.reg_read(UC_RISCV_REG_A0) == 55
        pointers = [self.uc.reg_read(reg) for reg in (UC_RISCV_REG_A1, UC_RISCV_REG_A2)]
        inputs = [int.from_bytes(self.uc.mem_read(pointer, 8), 'little') for pointer in pointers]
        ready = self.remaining == 0
        self.polls.append({'target': self.readiness[self.uc.reg_read(UC_RISCV_REG_PC)],
                           'ready': ready, 'read_set': hex(inputs[0]), 'write_set': hex(inputs[1])})
        if self.remaining:
            self.remaining -= 1
        result = ready and bool(inputs[self.ready_set] & self.socket_bit)
        for pointer in pointers:
            self.uc.mem_write(pointer, bytes(8))
        if result:
            self.uc.mem_write(pointers[self.ready_set], self.socket_bit.to_bytes(8, 'little'))
        self.finish(int(result))

    def socket_error(self, *args):
        self.put(self.uc.reg_read(UC_RISCV_REG_A3), 0)
        self.finish(0)

    def name_length(self, *args):
        address = self.uc.reg_read(UC_RISCV_REG_A0)
        assert bytes(self.uc.mem_read(address, 10)) == b'127.0.0.1\0'
        self.finish(9)

    def create(self, *args):
        self.creations += 1
        self.finish(0)

    def handshake(self, *args):
        self.handshakes += 1
        self.finish(1)

    def connect(self, adapter):
        name = 'connect_step' if adapter else 'esp_tls_conn_new_async'
        arguments = (0,) if adapter else (SSL + 0x3000, 9, 8883, CONFIG, SSL)
        for register, value in zip((UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2,
                                    UC_RISCV_REG_A3, UC_RISCV_REG_A4), arguments):
            self.uc.reg_write(register, value)
        self.uc.reg_write(UC_RISCV_REG_RA, STOP)
        self.uc.reg_write(UC_RISCV_REG_SP, STACK + STACK_SIZE - 16)
        self.uc.emu_start(self.symbols[name], STOP, count=500000)
        assert self.uc.reg_read(UC_RISCV_REG_PC) == STOP, 'instruction bound exceeded'
        return struct.unpack('<i', struct.pack('<I', self.uc.reg_read(UC_RISCV_REG_A0)))[0]


def verify(path, expect_recovery):
    layout = mqtt_layout(path)
    cases = []
    for adapter in (False, True):
        for timeouts in (0, 1, 3):
            for ready_set in (0, 1):
                probe = ConnectProbe(path, layout, timeouts, ready_set)
                outcomes = [probe.connect(adapter) for _ in range(timeouts)]
                assert outcomes == [0] * timeouts
                assert probe.creations == probe.handshakes == 0
                recovered = timeouts == 0 or (adapter and expect_recovery)
                outcomes.append(probe.connect(adapter))
                assert outcomes[-1] == int(recovered), (adapter, timeouts, outcomes)
                if not recovered:
                    outcomes.extend(probe.connect(adapter) for _ in range(2))
                    assert outcomes[-3:] == [0, 0, 0]
                assert probe.creations == probe.handshakes == int(recovered)
                cases.append({'adapter': adapter, 'initial_timeouts': timeouts,
                              'ready_set': ('read', 'write')[ready_set], 'results': outcomes,
                              'handshake_calls': probe.handshakes, 'select_calls': probe.polls})
    return {'passed': True, 'elf_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'adapter_recovers': expect_recovery, 'cases': cases,
            'scope': 'RV32 MQTT adapter and ESP-TLS connect instruction emulation',
            'doubles': ['socket readiness', 'socket error', 'TLS context creation', 'handshake',
                        'ROM string length', 'inherited heap, ROM copy and PSA hash boundaries'],
            'hardware_tested': False, 'encrypted_handshake_tested': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--expect', choices=('stalled', 'recovered'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        parser.error('assertions must remain enabled')
    if args.output.exists():
        parser.error('use a new report path')
    result = verify(args.elf, args.expect == 'recovered')
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(f'12 compiled connect cases passed, adapter expected {args.expect}')


if __name__ == '__main__':
    main()
