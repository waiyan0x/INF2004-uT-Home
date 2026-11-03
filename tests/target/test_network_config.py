"""USB/HTTP arbitration with real cfgctrl, cfgstore, cfgpump and strict JSON."""
import ctypes as c
import unittest
from _network_config_fixture import ConfigLibrary,Fixture
from tools.config_transfer.protocol import Command,encode_request,decode_reply

class ConfigMuxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.library=ConfigLibrary();cls.addClassCleanup(cls.library.close)
    def setUp(self):self.fixture=Fixture(self.library)
    def test_saved_apply_exact_retry_and_token_content_conflict(self):
        f=self.fixture
        self.assertEqual(f.apply(),200)
        self.assertEqual(f.control.last_id,1)
        self.assertEqual(f.last_fields[:3],[1,1,1])
        self.assertEqual((f.activations,f.validations),(1,1))
        writes=f.writes
        self.assertEqual(f.apply(),200)
        self.assertEqual(f.writes,writes)
        self.assertEqual(f.activations,1)
        self.assertEqual(f.apply(b'{"value":2}'),409)
        self.assertEqual(f.writes,writes)
        self.assertEqual(f.begin(id=2,revision=2,expected=1,token=b'op'),409)
    def test_usb_global_ids_are_not_renumbered_and_evicted_retry_is_rejected(self):
        f=self.fixture
        reply=decode_reply(f.usb(encode_request(Command.APPLY,1,1,b'{"value":1}')),
                           command=Command.APPLY,request_id=1,revision=1)
        self.assertEqual(reply.status,1)
        self.assertEqual(f.begin(id=1,revision=2,expected=1),409)
        self.assertEqual(f.apply(b'{"value":2}',id=2,revision=2,expected=1),200)
        original=f.result(id=2)
        f.usb(encode_request(Command.QUERY,3,0))
        self.assertEqual(f.control.last_id,3)
        self.assertEqual(f.result(id=2),original)
        self.assertEqual(f.begin(id=2,revision=2,expected=1),409)
        self.assertEqual(f.activations,2)
    def test_upload_leases_idle_usb_buffer_and_gates_receive_until_abort(self):
        f=self.fixture
        self.assertEqual(f.begin(),200)
        self.assertEqual(f.begin(token=b'other'),503)
        frame=encode_request(Command.QUERY,1,0)
        f.rx.extend(frame)
        f.usb_step();self.assertEqual(f.read_calls,0)
        f.abort();self.assertFalse(f.mux.lease)
        self.assertEqual(bytes(f.pump.receiver.data),bytes(8192))
        for _ in range(4):f.usb_step()
        self.assertEqual(f.control.last_id,1)
    def test_partial_usb_request_and_pending_reply_prevent_http_admission(self):
        f=self.fixture
        frame=encode_request(Command.QUERY,1,0)
        f.rx.extend(frame[:10]);f.usb_step()
        self.assertEqual(f.begin(),503)
        f.rx.extend(frame[10:]);f.usb_step()
        self.assertTrue(f.pump.pending)
        self.assertEqual(f.begin(id=2),503)
        f.usb_step()
        self.assertEqual(f.begin(id=2),200)
    def test_malformed_duplicate_and_nonfinite_json_use_existing_validator(self):
        for data in [b'{',b'{"value":1,"value":2}',b'{"value":NaN}',b'{"value":101}',b'{"unknown":1}']:
            self.setUp();f=self.fixture
            self.assertEqual(f.apply(data),422,data)
            self.assertEqual((f.activations,f.writes),(0,0))
            self.assertEqual(f.result()[1][0],3)
    def test_uncertain_save_retry_does_not_activate_or_write_again(self):
        f=self.fixture;f.fail_sync=5
        self.assertEqual(f.apply(),503)
        self.assertEqual(f.last_fields[0],2)
        self.assertEqual(f.last_fields[1:3],[1,0])
        self.assertFalse(f.last_fields[5]&16)
        writes=f.writes;syncs=f.syncs
        self.assertEqual(f.apply(),503)
        self.assertEqual((f.writes,f.syncs,f.activations),(writes,syncs,1))
        f.usb(encode_request(Command.QUERY,2,0))
        self.assertEqual(f.control.saved_revision,1)
        self.assertEqual(f.result()[1][2],0)  # Historical result is not rewritten.
    def test_recovery_and_storage_errors_preserve_controller_outcomes(self):
        f=self.fixture;f.activation_error=2
        self.assertEqual(f.apply(),503)
        self.assertEqual(f.last_fields[0],4)
        self.assertTrue(f.control.recovery)
        self.assertEqual(f.writes,0)
        self.setUp();f=self.fixture;f.read_error=True
        self.assertEqual(f.apply(),503)
        self.assertEqual(f.last_fields[0],7)
        self.assertEqual((f.validations,f.activations,f.writes),(0,0,0))
    def test_full_8192_body_reuses_rx_and_no_other_request_reenters(self):
        f=self.fixture;attempts=[]
        f.during_validate=lambda: attempts.append(f.begin(id=2,revision=2,expected=0,token=b'other'))
        data=b'{"value":1}'+b' '*(8192-11)
        self.assertEqual(f.begin(size=8192),200)
        for offset in range(0,len(data),127):self.assertEqual(f.write(data[offset:offset+127]),200)
        self.assertEqual(f.finish(),200)
        self.assertEqual(attempts,[503])
        self.assertEqual(f.control.active_size,8192)
        self.assertEqual(bytes(f.control.active),data)
    def test_invalid_preconditions_lengths_tokens_and_exhaustion_never_apply(self):
        f=self.fixture
        for kwargs,expected in [({'boot':2},409),({'expected':1},409),({'id':0},400),
                                ({'revision':0},400),({'size':8193},400),({'token':b'x'*65},400),
                                ({'token':b'bad/token'},400)]:
            self.assertEqual(f.begin(**kwargs),expected)
            self.assertFalse(f.mux.lease)
        self.assertEqual(f.begin(size=1),200)
        self.assertEqual(f.write(b'xx'),400)
        f.abort();self.assertEqual(f.control.last_id,0)
        f.control.last_id=0xffffffff
        self.assertEqual(f.begin(id=0xffffffff),409)
        self.assertEqual(f.activations,0)
    def test_result_observation_does_not_evict_or_mutate_and_unknown_is_404(self):
        f=self.fixture;self.assertEqual(f.result()[0],404)
        f.apply();before=(bytes(f.control),bytes(f.mux),f.writes,f.syncs)
        self.assertEqual(f.result()[0],200)
        self.assertEqual(f.result(token=b'wrong')[0],404)
        self.assertEqual((bytes(f.control),bytes(f.mux),f.writes,f.syncs),before)

if __name__=='__main__':unittest.main()
