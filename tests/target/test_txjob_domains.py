"""Exhaustive finite TJ v2 admission domains through the real C decision engine.

Each candidate has a fresh command/revision. Accepted loads perform no bus I/O;
rejections must preserve the complete previous identity and bus settings.
These are field-domain sweeps, not all combinations of all 96 packet bytes.
"""
import struct
import unittest

import test_txjob as engine


class AdmissionDomainTests(unittest.TestCase):
    setUpClass = classmethod(engine.JobTests.setUpClass.__func__)
    submit = engine.JobTests.submit
    take = engine.JobTests.take

    def setUp(self):
        engine.JobTests.setUp(self)
        self.request_id = 0
        self.identity = bytes(20)
        self.settings = bytes(12)
        self.admit(engine.request(), 0, 'initial rule')

    def admit(self, original, expected, label):
        self.request_id += 1
        packet = bytearray(original)
        struct.pack_into('<I', packet, 4, self.request_id)
        struct.pack_into('<I', packet, 20, self.request_id)
        self.assertEqual(self.submit(bytes(packet)), 0, label)
        reply = self.take()
        self.assertEqual(reply[41], expected, label)
        if expected == 0:
            self.identity = bytes(packet[20:40])
            self.settings = bytes(packet[40:41] + packet[41:43] + packet[52:53]
                                  + packet[43:44] + packet[44:50] + packet[53:54])
        self.assertEqual(reply[20:40], self.identity, label)
        self.assertEqual(reply[40:41] + reply[84:95], self.settings, label)
        self.assertEqual(reply[42:76], bytes(34), label)
        self.assertEqual((self.begins, self.polls, self.closes), (0, 0, 0), label)

    def test_every_protocol_and_address_byte(self):
        for protocol in range(256):
            self.admit(engine.request(protocol=protocol, rx=0),
                       0 if protocol in (1, 2, 3) else 7, f'protocol={protocol}')
        for protocol in (1, 2, 3):
            for address in range(256):
                valid = 8 <= address <= 119 if protocol == 1 else address == 0
                self.admit(engine.request(protocol=protocol, rx=0, **{'41': address}),
                           0 if valid else 1, f'protocol={protocol}, address={address}')

    def test_every_tx_rx_length_pair_for_each_protocol(self):
        for protocol in (1, 2, 3):
            for tx_size in range(256):
                for rx_size in range(256):
                    # Zero-filled payload also exercises valid zero-valued writes.
                    packet = engine.request(protocol=protocol, tx=b'', rx=rx_size,
                                            **{'50': tx_size})
                    valid_size = tx_size <= 30 and rx_size <= 32 and tx_size + rx_size > 0
                    expected = (7 if protocol == 2 and rx_size else 0) if valid_size else 1
                    self.admit(packet, expected,
                               f'protocol={protocol}, tx={tx_size}, rx={rx_size}')

    def test_every_spi_pin_pair_mode_and_control_level(self):
        allowed = {3, 4, 5, 10}
        for cs in range(256):
            for dc in range(256):
                valid = cs in allowed and dc in allowed | {255} and dc != cs
                self.admit(engine.request(protocol=2, rx=0, **{'52': cs, '43': dc}),
                           0 if valid else 1, f'CS={cs}, DC={dc}')
        for mode in range(256):
            self.admit(engine.request(protocol=2, rx=0, **{'42': mode}),
                       0 if mode <= 3 else 1, f'SPI mode={mode}')
        for dc in (5, 255):
            for level in range(256):
                valid = level in ((0, 1) if dc == 5 else (0,))
                self.admit(engine.request(protocol=2, rx=0, **{'43': dc, '53': level}),
                           0 if valid else 1, f'DC={dc}, level={level}')

    def test_every_16_bit_timeout_value(self):
        for timeout in range(65536):
            packet = bytearray(engine.request())
            struct.pack_into('<H', packet, 48, timeout)
            self.admit(packet, 0 if 10 <= timeout <= 1000 else 1,
                       f'timeout_ms={timeout}')

    def test_every_nonzero_value_in_each_unused_update_byte(self):
        # One TX byte is used; bytes 57..85 are unused payload, 86..95 reserved.
        for offset in (54, 55, *range(57, 96)):
            for value in range(1, 256):
                self.admit(engine.request(**{str(offset): value}), 1,
                           f'offset={offset}, value={value}')

    def test_every_single_character_identifier_byte(self):
        allowed = b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-'
        for value in range(256):
            packet = bytearray(engine.request())
            packet[24:40] = bytes([value]) + bytes(15)
            self.admit(packet, 0 if value in allowed else 1, f'identifier byte={value}')


if __name__ == '__main__':
    unittest.main()
