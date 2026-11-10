"""Three-bus host contract and C admission parity, with no broker needed."""
from copy import deepcopy
import ctypes as c
import json
from pathlib import Path
import struct
import unittest
from tools.target import transaction as client
import test_txjob as engine

ROOT=Path(__file__).resolve().parents[2]
EXAMPLES=ROOT/'experiments/runtime_rules/examples/transactions'

class ClientTests(unittest.TestCase):
    def setUp(self):
        self.profile=json.loads((EXAMPLES/'bmp-id.json').read_text())

    def test_every_reviewed_template_compiles_to_fixed_canonical_envelope(self):
        for path in EXAMPLES.glob('*.json'):
            profile=json.loads(path.read_text())
            wire=client.request(profile,1,4)
            self.assertEqual(len(wire),96)
            self.assertEqual(wire[:4],b'TJ\x01\x01')
            self.assertEqual(wire[58:60]+wire[94:96],bytes(4))
            self.assertEqual(client.request(profile,2,4,execute=True)[44:90],bytes(46))

    def test_bad_types_reserved_pins_lengths_and_float_values_reject(self):
        for key,value in [('version',True),('revision',0),('protocol',[]),
                          ('address',120),('rate',9999),('timeout_ms',0),
                          ('write_hex','00; ff'),('write_hex','00 '*32),
                          ('read_length',33),('read_length',1.0),('cs_pin',4)]:
            with self.subTest(key=key,value=value):
                with self.assertRaises(ValueError):client.request({**self.profile,key:value},1,4)
        with self.assertRaises(ValueError):client.request({**self.profile,'extra':True},1,4)
        with self.assertRaises(ValueError):json.loads('{"revision":1,"revision":2}',object_pairs_hook=client.unique)
        for rid in [0,-1,True,2**32]:
            with self.assertRaises(ValueError):client.request(self.profile,rid,4)

    def test_result_rejects_wrong_identity_context_padding_and_old_bytes_on_fault(self):
        reply=bytearray(96);reply[:4]=b'TJ\x01\x02'
        struct.pack_into('<II',reply,4,2,1);reply[12:18]=b'bmp388'
        reply[44:49]=bytes([1,0,1,1,80]);struct.pack_into('<I',reply,90,4)
        self.assertEqual(client.result(bytes(reply),request_id=2,generation=4)['raw_hex'],'50')
        with self.assertRaises(ValueError):client.result(bytes(reply),device='other')
        with self.assertRaises(ValueError):client.result(bytes(reply),revision=2)
        self.assertEqual(client.result(bytes(reply),device='bmp388',revision=1)['status'],'ok')
        for offset,value in [(45,10),(47,33),(79,1),(83,1),(94,1),(45,5)]:
            bad=bytearray(reply);bad[offset]=value
            with self.assertRaises(ValueError):client.result(bytes(bad))
        with self.assertRaises(ValueError):client.result(bytes(reply),request_id=3)
        with self.assertRaises(ValueError):client.result(bytes(reply),generation=5)

class ParityTests(unittest.TestCase):
    setUpClass=classmethod(engine.JobTests.setUpClass.__func__)
    setUp=engine.JobTests.setUp
    submit=engine.JobTests.submit
    take=engine.JobTests.take
    def test_host_compiled_templates_are_independently_admitted_by_c(self):
        for rid,path in enumerate(sorted(EXAMPLES.glob('*.json')),1):
            profile=json.loads(path.read_text())
            self.submit(client.request(profile,rid,4))
            decoded=client.result(self.take(),request_id=rid,generation=4)
            self.assertEqual(decoded['status'],'ok')
            self.assertEqual(decoded['device'],profile['device'])
            self.assertEqual(decoded['protocol'],client.PROTOCOLS[profile['protocol']])

if __name__=='__main__':unittest.main()
