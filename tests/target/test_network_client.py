"""Independent client decoding of real C encoder output and malformed frames."""
import ctypes as c
import importlib.util
from pathlib import Path
import struct
import unittest

import test_network_services as services
from test_network_services import Meta, Ble, Target, Value

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('network_client', ROOT/'tools/target/network_client.py')
client = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(client)


class ClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Reuse the real compiled C fixture without inheriting/rerunning its tests.
        cls.fixture = services.NetworkServiceTests()
        services.NetworkServiceTests.setUpClass()

    @classmethod
    def tearDownClass(cls):
        services.NetworkServiceTests.doClassCleanups()

    def test_decode_real_c_environment_and_signed_radar(self):
        entry=self.fixture.entry()
        decoded=client.decode_frame(self.fixture.frame(entry))
        self.assertEqual(decoded['values']['temperature_c'],26.25)
        self.assertEqual(decoded['boot_id'],'1122334455667788')
        entry=self.fixture.entry(2)
        entry.value=Value()
        entry.value.targets.targets[0]=Target(-320,160,-25,8,True)
        decoded=client.decode_frame(self.fixture.frame(entry))
        self.assertEqual(decoded['values']['targets'][0]['speed_cm_s'],-25)

    def test_real_c_light_boundaries_quality_and_fragmentation(self):
        for value in (0, 1, 200_000_000):
            entry = self.fixture.entry(5)
            entry.value = Value()
            entry.value.light.millilux = value
            decoded = client.decode_frame(self.fixture.frame(entry))
            self.assertEqual(decoded['values'], {'millilux': value})
            self.assertEqual(client.decode_frame(self.fixture.frame(entry, 1100))['quality'], 'stale')
            state, meta = Ble(), Meta(1, 12, 4)
            self.assertEqual(self.fixture.offer(c.byref(state), c.byref(entry), c.byref(meta), 150), 0)
            receiver = client.BleReassembler(23)
            decoded = None
            for index in range(10):
                output, size = (c.c_uint8 * 514)(), c.c_size_t()
                self.assertEqual(self.fixture.fragment(c.byref(state), 4, 23, output, 514, c.byref(size)), 0)
                decoded = receiver.feed(bytes(output[:size.value]), index / 100)
                self.fixture.sent(c.byref(state))
            self.assertEqual(decoded['values']['millilux'], value)
        entry.value.light.millilux = 200_000_001
        decoded = client.decode_frame(self.fixture.frame(entry))
        self.assertEqual(decoded['quality'], 'fault')
        self.assertIsNone(decoded['values'])
        entry.value.light.millilux = 42
        entry.header.status = 2
        self.assertIsNone(client.decode_frame(self.fixture.frame(entry))['values'])
        entry.seen = False
        self.assertEqual(client.decode_frame(self.fixture.frame(entry))['quality'], 'missing')

    def test_light_client_rejects_out_of_range_and_unused_bytes(self):
        entry = self.fixture.entry(5)
        entry.value = Value()
        frame = bytearray(self.fixture.frame(entry))
        struct.pack_into('<I', frame, 76, 200_000_001)
        with self.assertRaises(ValueError):
            client.decode_frame(bytes(frame))
        struct.pack_into('<I', frame, 76, 0)
        frame[80] = 1
        with self.assertRaises(ValueError):
            client.decode_frame(bytes(frame))

    def test_mqtt_topic_identity_and_binary_sensor_payload(self):
        entry=self.fixture.entry(5)
        entry.value=Value()
        entry.value.light.millilux=12345
        decoded=client.decode_mqtt('uthome/v1/node/telemetry/environment',self.fixture.frame(entry))
        self.assertEqual(decoded['values']['millilux'],12345)
        self.assertEqual(decoded['node_id'],'node')
        self.assertEqual(client.decode_mqtt('uthome/v1/node/availability',b'offline'),
                         dict(node_id='node',online=False))
        for topic,payload in [('uthome/v1/node/telemetry/wrong',self.fixture.frame(entry)),
                              ('uthome/v1/node/availability',b'unknown'),
                              ('uthome/v1/+/availability',b'online'),
                              ('uthome/v1/node/commands',b'apply'),
                              ('uthome/v1/node/telemetry/environment',b'x'*113)]:
            with self.assertRaises(ValueError):
                client.decode_mqtt(topic,payload)

    def test_decode_rejects_bad_lengths_reserved_fields_and_quality(self):
        frame=self.fixture.frame(self.fixture.entry())
        for invalid in [frame[:-1],frame+b'x']:
            with self.assertRaises(ValueError):
                client.decode_frame(invalid)
        for index,value in [(0,2),(1,9),(2,9),(3,9),(6,1),(111,1),(75,1)]:
            bad=bytearray(frame)
            bad[index]=value
            with self.assertRaises(ValueError):
                client.decode_frame(bytes(bad))

    def test_missing_fault_and_stale_preserve_semantics(self):
        entry=self.fixture.entry()
        self.assertEqual(client.decode_frame(self.fixture.frame(entry,1100))['quality'],'stale')
        entry.seen=False
        decoded=client.decode_frame(self.fixture.frame(entry))
        self.assertIsNone(decoded['values'])
        self.assertIsNone(decoded['age_ms'])
        entry.seen=True
        entry.header.status=2
        self.assertEqual(client.decode_frame(self.fixture.frame(entry))['quality'],'fault')

    def fragments(self, mtu=23):
        state=Ble()
        meta=Meta(1,12,4)
        entry=self.fixture.entry()
        self.fixture.offer(c.byref(state),c.byref(entry),c.byref(meta),150)
        fragments=[]
        while True:
            output=(c.c_uint8*514)()
            size=c.c_size_t()
            result=self.fixture.fragment(c.byref(state),4,mtu,output,514,c.byref(size))
            if result==1:
                return fragments
            self.assertEqual(result,0)
            fragments.append(bytes(output[:size.value]))
            self.fixture.sent(c.byref(state))

    def test_reassemble_actual_c_minimum_and_largest_mtu(self):
        for mtu in [23,24,64,247,517]:
            receiver=client.BleReassembler(mtu)
            decoded=None
            for index,fragment in enumerate(self.fragments(mtu)):
                decoded=receiver.feed(fragment,index/100)
            self.assertEqual(decoded['values']['pressure_pa'],100820.5)
            self.assertEqual(len(receiver.buffer),0)

    def test_reassembly_gaps_timeout_bad_count_and_recovery(self):
        fragments=self.fragments()
        receiver=client.BleReassembler()
        self.assertIsNone(receiver.feed(fragments[0],0))
        with self.assertRaises(ValueError):
            receiver.feed(fragments[2],0.1)
        receiver.feed(fragments[0],1)
        with self.assertRaises(ValueError):
            receiver.feed(fragments[1],3)
        bad=bytearray(fragments[0])
        bad[5]=255
        with self.assertRaises(ValueError):
            receiver.feed(bytes(bad),4)
        decoded=None
        for index,fragment in enumerate(fragments):
            decoded=receiver.feed(fragment,5+index/100)
        self.assertEqual(decoded['quality'],'fresh')

    def test_client_rejects_insecure_urls_and_invalid_tokens_before_network(self):
        for origin in ['http://esp.local','https://user:pass@esp.local',
                       'https://esp.local/path','https://esp.local/?x=1']:
            with self.assertRaises(ValueError):
                client.fetch(origin,'status','a'*32)
        with self.assertRaises(ValueError):
            client.fetch('https://esp.local','status','bad\r\ntoken')


if __name__=='__main__':
    unittest.main()
