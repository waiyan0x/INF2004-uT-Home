"""Compile and exercise the actual portable MQTT encoder and bounded mailbox."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class Mailbox(c.Structure):
    _fields_ = [('pending', c.c_uint8 * 1280), ('size', c.c_size_t),
                ('generation', c.c_uint32), ('replaced', c.c_uint32),
                ('pending_flag', c.c_bool)]

class MqttServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('gcc')
        if compiler is None:
            raise AssertionError('GCC required')
        cls.folder = tempfile.TemporaryDirectory(prefix='uthome-mqtt-')
        cls.addClassCleanup(cls.folder.cleanup)
        binary = Path(cls.folder.name) / 'mqtt.dll'
        subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-pedantic', '-O2', '-shared',*(['-fPIC'] if sys.platform!='win32' else []),
                        str(ROOT/'firmware/src/network/netmqtt.c'),
                        '-o', str(binary)], check=True, capture_output=True)
        cls.lib = c.CDLL(str(binary))
        cls.offer = cls.lib.netmqtt_offer
        cls.offer.argtypes = [c.POINTER(Mailbox), c.c_char_p, c.c_size_t,
                             c.c_char_p, c.c_size_t, c.c_uint32]
        cls.take = cls.lib.netmqtt_take
        cls.take.argtypes = [c.POINTER(Mailbox), c.c_uint32, c.c_void_p,
                            c.c_size_t, c.POINTER(c.c_size_t)]
        cls.addClassCleanup(cls.unload)

    @classmethod
    def unload(cls):
        import _ctypes
        if sys.platform=='win32':
            _ctypes.FreeLibrary(cls.lib._handle)

    def submit(self, box, topic=b'uthome/v1/node/telemetry/sensor', value=b'{}', generation=1):
        return self.offer(c.byref(box), topic, len(topic), value, len(value), generation)

    def read(self, box, generation=1, capacity=1280):
        output = (c.c_uint8 * 1280)(*([0xDA] * 1280))
        size = c.c_size_t(99)
        result = self.take(c.byref(box), generation, output, capacity, c.byref(size))
        return result, bytes(output[:size.value]), bytes(output)

    def test_wire_packet_matches_mqtt_publish(self):
        box = Mailbox()
        self.assertEqual(self.submit(box, b'a/b', b'\x00\xff'), 0)
        result, packet, _ = self.read(box)
        self.assertEqual(result, 0)
        self.assertEqual(packet, b'\x30\x07\x00\x03a/b\x00\xff')
        self.assertEqual(self.read(box)[0], 1)

    def test_maximum_lengths_and_remaining_length_encoding(self):
        box = Mailbox()
        self.assertEqual(self.submit(box, b'a'*128, b'z'*1024), 0)
        _, packet, output = self.read(box)
        self.assertEqual(len(packet), 1157)
        self.assertEqual(packet[:5], b'\x30\x82\x09\x00\x80')
        self.assertEqual(packet[5:133], b'a'*128)
        self.assertEqual(packet[133:], b'z'*1024)
        self.assertEqual(output[1157:], b'\xda'*(1280-1157))

    def test_invalid_offers_preserve_previous_packet(self):
        box = Mailbox()
        self.assertEqual(self.submit(box, b'valid', b'old'), 0)
        before = bytes(box)
        for topic, value in [(b'', b'x'), (b'a'*129,b'x'), (b'a',b'x'*1025),
                             (b'a/+',b'x'), (b'#',b'x'), (b'a\x00b',b'x'),
                             (b'\xff',b'x'), (b'a b',b'x')]:
            self.assertEqual(self.submit(box, topic, value), -1)
            self.assertEqual(bytes(box), before)

    def test_latest_only_and_active_copy_is_immutable(self):
        box = Mailbox()
        for index in range(10000):
            self.assertEqual(self.submit(box, b'a', str(index).encode()), 0)
        self.assertEqual(box.replaced, 9999)
        _, active, _ = self.read(box)
        self.assertTrue(active.endswith(b'9999'))
        self.assertEqual(self.submit(box, b'a', b'new'), 0)
        self.assertTrue(active.endswith(b'9999'))
        self.assertTrue(self.read(box)[1].endswith(b'new'))

    def test_short_buffer_preserves_pending_and_generation_discards(self):
        box = Mailbox()
        self.submit(box)
        self.assertEqual(self.read(box, capacity=1)[0], -1)
        self.assertTrue(box.pending_flag)
        result, packet, output = self.read(box, generation=2)
        self.assertEqual((result, packet), (1, b''))
        self.assertEqual(output, b'\xda'*1280)
        self.assertFalse(box.pending_flag)

    def test_empty_payload_null_arguments_and_counter_saturation(self):
        box = Mailbox()
        self.assertEqual(self.offer(None,b'a',1,b'',0,1), -1)
        self.assertEqual(self.offer(c.byref(box),b'a',1,None,1,1), -1)
        self.assertEqual(self.offer(c.byref(box),b'a',1,None,0,1), 0)
        box.replaced = 0xffffffff
        self.submit(box, b'a', b'')
        self.assertEqual(box.replaced, 0xffffffff)
        self.assertEqual(self.read(box)[1], b'\x30\x03\x00\x01a')

if __name__ == '__main__':
    unittest.main()
