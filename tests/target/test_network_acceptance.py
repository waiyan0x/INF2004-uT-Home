"""Offline client-tool tests. All sockets and BLE clients are explicit doubles."""
import asyncio
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tools.target import network_acceptance as client
from tools.target import network_size


def sensor():
    data=bytearray(112)
    data[:4]=bytes([1,5,1,1])
    struct.pack_into('<IIIQQII',data,8,1,2,3,1,1000,10,1000)
    data[44:49]=b'light'
    struct.pack_into('<I',data,76,1234)
    return bytes(data)


def publish(node='node'):
    topic=f'uthome/v1/{node}/telemetry/light'.encode()
    return client._wire(0x30,struct.pack('>H',len(topic))+topic+sensor())


class Stream:
    def __init__(self,data):
        self.data=bytearray(data);self.sent=[];self.closed=False;self.timeouts=[]
    def __enter__(self):
        return self
    def __exit__(self,*args):
        self.closed=True
    def settimeout(self,value):
        assert 0<value<=120
        self.timeouts.append(value)
    def recv(self,size):
        size=min(size,3)
        value=bytes(self.data[:size]);del self.data[:size]
        return value
    def sendall(self,data):
        self.sent.append(data)


class AcceptanceTests(unittest.TestCase):
    def capture(self,wire,count=1):
        stream=Stream(wire)
        context=SimpleNamespace(wrap_socket=lambda raw,server_hostname: stream)
        with patch.object(client.socket,'create_connection',return_value=stream) as connect, \
             patch.object(client.ssl,'create_default_context',return_value=context) as tls:
            result=client.mqtt_capture('127.0.0.1','broker.test',8883,'ca.pem',
                                       'observer','secret','node',count,5)
            connect.assert_called_once_with(('127.0.0.1',8883),timeout=5)
            tls.assert_called_once_with(cafile='ca.pem')
        return result,stream

    def test_tls_subscriber_reads_fragmented_packets_and_never_publishes(self):
        wire=b'\x20\x02\x00\x00\x90\x04\x00\x01\x00\x00'+publish()
        result,stream=self.capture(wire)
        self.assertEqual(result['messages'][0]['observation']['values']['millilux'],1234)
        self.assertEqual([item[0] for item in stream.sent],[0x10,0x82,0xe0])
        self.assertNotIn('secret',json.dumps(result))
        self.assertTrue(stream.closed)

    def test_retained_publish_before_suback_is_accepted_but_ack_required(self):
        topic=b'uthome/v1/node/availability'
        retained=client._wire(0x31,len(topic).to_bytes(2,'big')+topic+b'online')
        result,_=self.capture(b'\x20\x02\x00\x00'+retained+b'\x90\x04\x00\x01\x00\x00')
        self.assertTrue(result['messages'][0]['retained'])
        with self.assertRaises(ConnectionError):
            self.capture(b'\x20\x02\x00\x00'+retained)

    def test_broker_rejection_foreign_node_and_qos_mismatch_fail(self):
        for data in [b'\x20\x02\x00\x05',
                     b'\x20\x02\x00\x00\x90\x04\x00\x01\x80\x00',
                     b'\x20\x02\x00\x00\x90\x04\x00\x01\x00\x00'+publish('other'),
                     b'\x20\x02\x00\x00\x90\x04\x00\x01\x00\x00'+bytes([0x32])+publish()[1:]]:
            with self.subTest(data=data[:8]),self.assertRaises(ValueError):
                self.capture(data)

    def test_packet_bounds_noncanonical_lengths_eof_and_absolute_deadline(self):
        for data in [b'\x30\xff\x7f',b'\x30\x80\x00',b'\x30\x01']:
            with self.assertRaises((ValueError,ConnectionError)):
                client._packet(Stream(data),time.monotonic()+2)
        with self.assertRaises(TimeoutError):
            client._packet(Stream(b'x'),0)
        with self.assertRaises(ValueError):
            client._wire(0x30,b'x'*1155)
        with patch.object(client.socket,'create_connection') as connection:
            with self.assertRaises(ValueError):
                client.mqtt_capture('broker.test','broker.test',8883,'ca.pem','u','p','node')
            connection.assert_not_called()

    def test_diagnostic_validation_distinguishes_disabled_unavailable_ready(self):
        value=dict(enabled=False,state=0,failures=0,replaced=1,discarded=2)
        self.assertEqual(client.diagnostics(value)['state_name'],'disabled')
        self.assertEqual(client.diagnostics(dict(value,enabled=True,state=6))['state_name'],'unavailable')
        self.assertEqual(client.diagnostics(dict(value,enabled=True,state=5))['state_name'],'ready')
        for bad in [dict(value,state=7),dict(value,failures=-1),dict(value,replaced=True),dict(value,enabled=1)]:
            with self.assertRaises(ValueError):
                client.diagnostics(bad)

    def test_https_capture_rejects_boot_or_generation_changes(self):
        identity=dict(boot_id='one')
        status=dict(boot_id='one',runtime_generation=3,active_revision=2,
                    mqtt=dict(enabled=True,state=5,failures=0,replaced=0,discarded=0))
        sample=dict(boot_id='one',runtime_generation=3,active_revision=2)
        with patch.object(client,'fetch',side_effect=[identity,status,sample]):
            self.assertEqual(client.https_capture('origin','ca','token',0)['mqtt']['state_name'],'ready')
        for changed in [dict(sample,boot_id='two'),dict(sample,runtime_generation=4)]:
            with patch.object(client,'fetch',side_effect=[identity,status,changed]),self.assertRaises(ValueError):
                client.https_capture('origin','ca','token',0)

    def test_cli_failure_report_is_sanitized_and_never_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/'capture.json'
            with patch.object(client,'https_capture',side_effect=ValueError('secret')),contextlib.redirect_stdout(io.StringIO()):
                code=client.main(['--report',str(target),'https','https://node','--ca','ca.pem'])
            self.assertEqual(code,1)
            self.assertNotIn('secret',target.read_text())
            original=target.read_bytes()
            with patch.object(client,'https_capture') as capture,contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                client.main(['--report',str(target),'https','https://node','--ca','ca.pem'])
            capture.assert_not_called()
            self.assertEqual(target.read_bytes(),original)

    def test_invalid_report_directory_and_ble_mtu_fail_before_contact(self):
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/'missing'/'capture.json'
            with patch.object(client,'https_capture') as capture,contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                client.main(['--report',str(target),'https','https://node','--ca','ca.pem'])
            capture.assert_not_called()
        with patch.dict(sys.modules,{'bleak':SimpleNamespace(BleakClient=None)}):
            with self.assertRaises(ValueError):
                asyncio.run(client.ble_capture('explicit-device',1,2,22))

    def test_ble_explicit_client_reads_identity_and_stops_subscription(self):
        instances=[]
        class FakeBle:
            mtu_size=23
            def __init__(self,address,timeout):
                self.address=address;self.stopped=False;self.closed=False;instances.append(self)
            async def __aenter__(self):
                return self
            async def __aexit__(self,*args):
                self.closed=True
            async def read_gatt_char(self,uuid):
                if uuid==client.IDENTITY:
                    data=bytearray(44);data[0]=1;struct.pack_into('<Q',data,4,1);data[12:16]=b'node'
                else:
                    data=bytearray(20);data[0]=1;struct.pack_into('<Q',data,12,1)
                return data
            async def start_notify(self,uuid,callback):
                data=sensor()
                for index in range(10):
                    callback(uuid,struct.pack('<BBHBBH',1,1,1,index,10,112)+data[index*12:(index+1)*12])
            async def stop_notify(self,uuid):
                self.stopped=True
        with patch.dict(sys.modules,{'bleak':SimpleNamespace(BleakClient=FakeBle)}):
            result=asyncio.run(client.ble_capture('explicit-device',1,2))
        self.assertEqual(result['samples'][0]['values']['millilux'],1234)
        self.assertTrue(instances[0].closed and instances[0].stopped)
        self.assertFalse(result['paired_security_verified'])

    def test_size_ranking_excludes_static_bss_and_reads_factory_size(self):
        files={'lib.a:a.o':dict(memory_types={'DRAM':dict(size=99999),'Flash Code':dict(size=10)}),
               'lib.a:b.o':dict(memory_types={'Flash Data':dict(size=5)}),
               'other.a:c.o':dict(memory_types={'Flash Code':dict(size=20)})}
        self.assertEqual(network_size.rank_archives(files),[
            dict(archive='other.a',flash_mapped_bytes=20),dict(archive='lib.a',flash_mapped_bytes=15)])
        table=struct.pack('<2sBBII16sI',b'\xaa\x50',0,0,0x10000,0x100000,b'factory',0)
        self.assertEqual(network_size.factory_partition(table)['size'],1048576)
        with self.assertRaises(ValueError):
            network_size.factory_partition(bytes(32))

if __name__=='__main__':
    unittest.main()
