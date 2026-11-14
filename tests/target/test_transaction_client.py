"""Three-bus host contract and C admission parity, with no broker needed."""
from copy import deepcopy
import ctypes as c
import json
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout
import io
from tools.transaction import client
import test_txjob as engine

BOOT=engine.BOOT
ROOT=Path(__file__).resolve().parents[2]
EXAMPLES=ROOT/'configs/transactions'

def settings(data):
    data[84:88]=bytes([119,0,255,255])
    struct.pack_into('<IH',data,88,100000,100)


class ClientTests(unittest.TestCase):
    def setUp(self):
        self.profile=json.loads((EXAMPLES/'bmp-id.json').read_text())

    def test_every_reviewed_template_compiles_to_fixed_canonical_envelope(self):
        for path in EXAMPLES.glob('*.json'):
            profile=json.loads(path.read_text())
            wire=client.request(profile,1,4,BOOT)
            self.assertEqual(len(wire),96)
            self.assertEqual(wire[:4],b'TJ\x02\x01')
            self.assertEqual(wire[54:56]+wire[86:96],bytes(12))
            self.assertEqual(client.request(profile,2,4,BOOT,execute=True)[40:96],bytes(56))

    def test_bad_types_reserved_pins_lengths_and_float_values_reject(self):
        for key,value in [('version',True),('revision',0),('protocol',[]),
                          ('address',120),('rate',9999),('timeout_ms',0),
                          ('write_hex','00; ff'),('write_hex','00 '*32),
                          ('read_length',33),('read_length',1.0),('cs_pin',4)]:
            with self.subTest(key=key,value=value):
                with self.assertRaises(ValueError):client.request({**self.profile,key:value},1,4,BOOT)
        with self.assertRaises(ValueError):client.request({**self.profile,'extra':True},1,4,BOOT)
        with self.assertRaises(ValueError):json.loads('{"revision":1,"revision":2}',object_pairs_hook=client.unique)
        for rid in [0,-1,True,2**32]:
            with self.assertRaises(ValueError):client.request(self.profile,rid,4,BOOT)

    def test_result_rejects_wrong_identity_context_padding_and_old_bytes_on_fault(self):
        reply=bytearray(96);reply[:4]=b'TJ\x02\x02'
        struct.pack_into('<IQII',reply,4,2,BOOT,4,1);reply[24:30]=b'bmp388'
        reply[40:45]=bytes([1,0,1,1,80]);settings(reply)
        self.assertEqual(client.result(bytes(reply),request_id=2,generation=4)['raw_hex'],'50')
        with self.assertRaises(ValueError):client.result(bytes(reply),device='other')
        with self.assertRaises(ValueError):client.result(bytes(reply),revision=2)
        self.assertEqual(client.result(bytes(reply),device='bmp388',revision=1)['status'],'ok')
        for offset,value in [(41,10),(43,33),(75,1),(95,1),(94,1),(41,5),(86,4)]:
            bad=bytearray(reply);bad[offset]=value
            with self.assertRaises(ValueError):client.result(bytes(bad))
        with self.assertRaises(ValueError):client.result(bytes(reply),request_id=3)
        with self.assertRaises(ValueError):client.result(bytes(reply),generation=5)

    def test_rejected_update_reports_active_identity_and_generation(self):
        reply=bytearray(96);reply[:4]=b'TJ\x02\x01'
        struct.pack_into('<IQII',reply,4,2,BOOT,4,1);reply[24:30]=b'bmp388'
        reply[40:42]=bytes([1,1]);settings(reply)
        decoded=client.result(bytes(reply),request_id=2,generation=3,
                              device='candidate',revision=2)
        self.assertEqual((decoded['status'],decoded['device'],decoded['revision'],
                          decoded['generation'],decoded['raw_hex']),
                         ('invalid','bmp388',1,4,''))
        with self.assertRaises(ValueError):
            client.result(bytes(reply),request_id=3)

    def test_cli_failure_status_does_not_report_success_or_retry(self):
        def reply(rid,kind,status):
            data=bytearray(96);data[:4]=b'TJ\x02'+bytes([kind])
            struct.pack_into('<IQII',data,4,rid,BOOT,4,1);data[24:30]=b'bmp388'
            data[40:42]=bytes([1,status]);settings(data)
            return bytes(data)
        args=[str(EXAMPLES/'bmp-id.json'),'--generation','4','--request-id','1',
              '--boot-id',hex(BOOT),'--execute','--broker','example.test','--ca','fixture.pem',
              '--node','uthome-0123456789ab']
        for messages in ([reply(1,1,1)], [reply(1,1,0),reply(2,2,5)]):
            with self.subTest(messages=len(messages)), patch.object(client,'Mqtt') as mqtt:
                mqtt.return_value.exchange.side_effect=messages
                with redirect_stdout(io.StringIO()):
                    self.assertEqual(client.main(args),1)
                self.assertEqual(mqtt.return_value.exchange.call_count,len(messages))
                mqtt.return_value.close.assert_called_once()

class ParityTests(unittest.TestCase):
    setUpClass=classmethod(engine.JobTests.setUpClass.__func__)
    setUp=engine.JobTests.setUp
    submit=engine.JobTests.submit
    take=engine.JobTests.take
    query=engine.JobTests.query
    def test_host_compiled_templates_are_independently_admitted_by_c(self):
        for rid,path in enumerate(sorted(EXAMPLES.glob('*.json')),1):
            profile=json.loads(path.read_text())
            self.submit(client.request(profile,rid,4,BOOT))
            decoded=client.result(self.take(),request_id=rid,generation=4)
            self.assertEqual(decoded['status'],'ok')
            self.assertEqual(decoded['device'],profile['device'])
            self.assertEqual(decoded['protocol'],client.PROTOCOLS[profile['protocol']])

    def test_update_kind_cannot_be_mistaken_for_execution(self):
        self.submit(engine.request())
        ack = self.take()
        decoded = client.result(ack, request_id=1, boot_id=BOOT, kind=1)
        self.assertEqual((decoded['kind'], decoded['tx_accepted'], decoded['rx_received']), ('update', 0, 0))
        self.assertEqual((decoded['address'], decoded['rate'], decoded['timeout_ms'], decoded['dc_pin']),
                         (119, 100000, 100, 255))
        with self.assertRaisesRegex(ValueError, 'kind'):
            client.result(ack, request_id=1, boot_id=BOOT, kind=2)
        with self.assertRaisesRegex(ValueError, 'boot'):
            client.result(ack, request_id=1, boot_id=BOOT+1, kind=1)

    def test_transport_filters_delayed_boot_kind_and_request_replies(self):
        from types import SimpleNamespace
        self.submit(engine.request()); ack = self.take()
        old_boot = bytearray(ack); struct.pack_into('<Q', old_boot, 8, BOOT+1)
        wrong_kind = bytearray(ack); wrong_kind[3] = 2
        wrong_id = bytearray(ack); struct.pack_into('<I', wrong_id, 4, 99)
        link = client.Mqtt.__new__(client.Mqtt)
        link.responses = []
        link.request_topic = 'uthome/v1/fixture/transaction/request'
        sent = []
        def publish(topic, payload, **kwargs):
            sent.append(payload)
            return SimpleNamespace(rc=0)
        link.client = SimpleNamespace(publish=publish)
        def wait(condition):
            for packet in (bytes(old_boot), bytes(wrong_kind), bytes(wrong_id), ack):
                link.responses.append(packet)
                matched = condition()
                if packet != ack:
                    self.assertFalse(matched)
                    self.assertEqual(link.responses, [])
                else:
                    self.assertTrue(matched)
        link.wait = wait
        self.assertEqual(link.exchange(engine.request()), ack)
        self.assertEqual(len(sent), 1)

    def test_status_decoder_rejects_inconsistent_counts_flags_and_padding(self):
        self.submit(engine.request()); self.take()
        output = c.create_string_buffer(96)
        packet = client.query(99)
        self.assertEqual(self.lib.txjob_query(self.state, packet, 96, 4, True, 20, output), 0)
        decoded = client.status_result(output.raw, request_id=99, boot_id=BOOT)
        self.assertEqual((decoded['kind'], decoded['last_kind'], decoded['write_length'], decoded['read_length']),
                         ('status', 'update', 1, 1))
        args = ['--status', '--request-id', '99', '--broker', 'example.test',
                '--ca', 'fixture.pem', '--node', 'uthome-0123456789ab']
        for extra in ([], ['--boot-id', '0'], ['--boot-id', hex(BOOT)]):
            with patch.object(client, 'Mqtt') as mqtt:
                mqtt.return_value.exchange.return_value = output.raw
                with redirect_stdout(io.StringIO()):
                    self.assertEqual(client.main(args+extra), 0)
                mqtt.return_value.exchange.assert_called_once_with(client.query(99, BOOT if extra and extra[-1] == hex(BOOT) else 0))
                mqtt.return_value.close.assert_called_once()
        for offset, value in ((42, 1), (60, 15), (61, 3), (63, 31), (64, 33), (65, 1), (80, 1), (95, 1)):
            bad = bytearray(output.raw); bad[offset] = value
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                client.status_result(bytes(bad))
        with self.assertRaises(ValueError):
            client.result(output.raw)

    def test_identifier_capacity_rejects_instead_of_truncating(self):
        profile = json.loads((EXAMPLES/'bmp-id.json').read_text())
        accepted = {**profile, 'device': 'x'*15}
        self.submit(client.request(accepted, 1, 4, BOOT))
        self.assertEqual(client.result(self.take())['device'], 'x'*15)
        for value in ('x'*16, 'x'*31, 'sensor\0suffix'):
            with self.assertRaises(ValueError):
                client.request({**profile, 'device': value}, 2, 4, BOOT)
        invalid = bytearray(engine.request(rid=2)); invalid[24:40] = b'x'*16
        self.submit(bytes(invalid))
        self.assertEqual(client.result(self.take())['status'], 'invalid')

if __name__=='__main__':unittest.main()
