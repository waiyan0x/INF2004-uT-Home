"""Runtime rules: actual fixed C decision engine, injected owner operations."""
import ctypes as c
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

BOOT = 0x1122334455667788
ROOT = Path(__file__).resolve().parents[2]
BEGIN = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_void_p, c.c_uint64)
POLL = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint64, c.c_void_p, c.c_void_p, c.c_void_p)
CLOSE = c.CFUNCTYPE(c.c_int, c.c_void_p)
class Ops(c.Structure):
    _fields_ = [('begin', BEGIN), ('poll', POLL), ('close', CLOSE), ('context', c.c_void_p)]

def request(kind=1, rid=1, revision=1, protocol=1, generation=4, boot=BOOT, tx=b'\x00', rx=1, **changes):
    result = bytearray(96)
    result[:4] = b'TJ\x02' + bytes([kind])
    struct.pack_into('<IQII', result, 4, rid, boot, generation, revision)
    result[24:30] = b'sensor'
    if kind == 1:
        result[40:44] = bytes([protocol, 119 if protocol == 1 else 0, 0, 255])
        struct.pack_into('<IH', result, 44, 256000 if protocol == 3 else 100000, 100)
        result[50:54] = bytes([len(tx), rx, 4 if protocol == 2 else 255, 0])
        result[56:56+len(tx)] = tx
    for offset, value in changes.items():
        result[int(offset)] = value
    return bytes(result)

class JobTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='uthome-txjob-')
        cls.addClassCleanup(cls.temp.cleanup)
        target = Path(cls.temp.name)/'job.so'
        subprocess.run([shutil.which('gcc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-I'+str(ROOT/'firmware/include'), '-shared', *(['-fPIC'] if sys.platform != 'win32' else []),
                        str(ROOT/'firmware/src/services/transaction/txjob.c'), '-o', str(target)], check=True)
        cls.lib = c.CDLL(str(target))
        if sys.platform == "win32":
            import _ctypes
            cls.addClassCleanup(lambda: _ctypes.FreeLibrary(cls.lib._handle))
        cls.lib.txjob_init.argtypes = [c.c_void_p, c.POINTER(Ops), c.c_uint64]
        cls.lib.txjob_submit.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t, c.c_uint32, c.c_bool, c.c_uint64]
        cls.lib.txjob_query.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t, c.c_uint32, c.c_bool, c.c_uint64, c.c_void_p]
        cls.lib.txjob_step.argtypes = [c.c_void_p, c.c_uint64]
        cls.lib.txjob_cancel.argtypes = [c.c_void_p]
        cls.lib.txjob_take.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t]

    def setUp(self):
        self.state = (c.c_uint64*512)()
        self.begins = 0; self.polls = 0; self.closes = 0
        self.begin_status = 0; self.poll_status = 0; self.close_status = 0
        self.written = 1; self.data = b'\x50'
        def begin(ctx, rule, now):
            self.begins += 1
            return self.begin_status
        def poll(ctx, now, written, data, size):
            self.polls += 1
            c.cast(written, c.POINTER(c.c_uint8))[0] = self.written
            c.cast(size, c.POINTER(c.c_uint8))[0] = len(self.data)
            c.memmove(data, self.data, len(self.data))
            return self.poll_status
        def close(ctx):
            self.closes += 1
            return self.close_status
        self.ops = Ops(BEGIN(begin), POLL(poll), CLOSE(close), None)
        self.assertEqual(self.lib.txjob_init(self.state, c.byref(self.ops), BOOT), 0)

    def submit(self, data=None, idle=True, generation=4, now=10):
        data = request() if data is None else data
        return self.lib.txjob_submit(self.state, data, len(data), generation, idle, now)

    def take(self):
        data = c.create_string_buffer(96)
        self.assertEqual(self.lib.txjob_take(self.state, data, 96), 0)
        return data.raw

    def load(self, **values):
        self.assertEqual(self.submit(request(**values)), 0)
        reply = self.take()
        self.assertEqual(reply[41], 0)

    def test_three_protocols_select_different_runtime_operations_without_rebuild(self):
        for index, proto in enumerate((1, 2, 3)):
            self.load(rid=1+index*2, revision=proto, protocol=proto, rx=0 if proto==2 else 1)
            self.data = b'' if proto==2 else b'\x50'
            self.submit(request(kind=2, rid=2+index*2, revision=proto))
            self.lib.txjob_step(self.state, 11)
            result = self.take()
            self.assertEqual((result[40], result[41], result[42], result[43]),
                             (proto, 0, 1, len(self.data)))
        self.assertEqual((self.begins, self.polls, self.closes), (3, 3, 3))

    def test_rejected_update_preserves_rule_and_wrong_identity_has_no_io(self):
        self.load()
        bad = request(rid=2, revision=2, **{'50':31})
        self.submit(bad); result=self.take()
        self.assertEqual(result[41],1)
        self.assertEqual(struct.unpack_from('<I',result,20)[0],1)
        self.submit(request(kind=2,rid=3,revision=2)); self.assertEqual(self.take()[41],3)
        self.assertEqual(self.begins,0)

    def test_exact_retry_does_not_repeat_write_and_changed_or_older_ids_reject(self):
        self.load()
        execute=request(kind=2,rid=2)
        self.submit(execute); self.lib.txjob_step(self.state,11); original=self.take()
        self.submit(execute,now=20); self.assertEqual(self.take(),original)
        self.assertEqual(self.begins,1)
        self.submit(request(kind=2,rid=2,revision=9)); self.assertEqual(self.take()[41],8)
        self.submit(execute); self.assertEqual(self.take(),original)
        self.submit(request(rid=1)); self.assertEqual(self.take()[41],8)

    def test_generation_nonidle_and_malformed_input_never_call_driver(self):
        for data in [b'',request()[:-1],request()+b'x',b'X'+request()[1:]]:
            self.submit(data); self.assertEqual(self.take()[41],1)
        self.submit(idle=False); self.assertEqual(self.take()[41],6)
        self.submit(request(rid=2,generation=3)); self.assertEqual(self.take()[41],3)
        self.assertEqual(self.begins,0)
        self.load(rid=3)
        self.submit(request(kind=2,rid=4,generation=5),generation=5)
        self.assertEqual(self.take()[41],2)

    def test_fault_timeout_and_cancel_zero_rx_and_partial_write_is_explicit(self):
        self.load()
        for rid, status in [(2,5),(3,4),(4,9)]:
            self.poll_status=10 if status!=5 else 5
            self.submit(request(kind=2,rid=rid))
            if status==9: self.lib.txjob_cancel(self.state)
            self.lib.txjob_step(self.state,110 if status==4 else 11)
            result=self.take()
            self.assertEqual(result[41],status)
            self.assertEqual(result[43:76],bytes(33))

    def test_failed_cleanup_retains_busy_and_never_reexecutes(self):
        self.load(); self.close_status=-1
        self.submit(request(kind=2,rid=2)); self.lib.txjob_step(self.state,11)
        self.assertEqual(self.submit(request(rid=3)),-1)
        self.close_status=0; self.lib.txjob_step(self.state,12)
        self.assertEqual(self.take()[41],5)
        self.assertEqual(self.polls,1)

    def test_spi_read_is_explicitly_unsupported_and_pins_are_checked(self):
        self.submit(request(protocol=2,rx=1)); self.assertEqual(self.take()[41],7)
        for rid,pin in enumerate((2,6,7,8,9,18,19,20,21,255),2):
            self.submit(request(protocol=2,rx=0,rid=rid,**{'52':pin}))
            self.assertEqual(self.take()[41],1)
        self.assertEqual(self.begins,0)

    def query(self, rid=99, boot=0, generation=4, idle=True, now=20):
        from tools.transaction.client import query, status_result
        packet = query(rid, boot)
        output = c.create_string_buffer(96)
        self.assertEqual(self.lib.txjob_query(self.state, packet, len(packet),
                                             generation, idle, now, output), 0)
        return status_result(output.raw, request_id=rid)

    def test_query_discovers_boot_without_consuming_ids_or_peripheral_io(self):
        state = self.query(rid=0xffffffff, idle=False)
        self.assertEqual((state['boot_id'], state['generation'], state['last_request_id']),
                         (BOOT, 4, 0))
        self.assertFalse(state['ready'] or state['loaded'] or state['busy'])
        self.assertEqual((self.begins, self.polls, self.closes), (0, 0, 0))
        self.load(rid=1)
        self.assertEqual(self.query(rid=1)['last_completed_id'], 1)

    def test_busy_query_preserves_pending_operation_and_reports_cleanup(self):
        self.load()
        self.poll_status = 10
        self.submit(request(kind=2, rid=2))
        state = self.query()
        self.assertTrue(state['busy'] and state['loaded'])
        self.assertFalse(state['ready'] or state['closing'])
        self.assertEqual((state['last_request_id'], state['last_completed_id'], state['pending_id']), (2, 1, 2))
        self.assertEqual((self.begins, self.polls, self.closes), (1, 0, 0))
        self.close_status = -1
        self.lib.txjob_cancel(self.state)
        self.lib.txjob_step(self.state, 21)
        state = self.query(now=22)
        self.assertTrue(state['closing'] and state['busy'])
        self.close_status = 0
        self.lib.txjob_step(self.state, 23)
        self.assertEqual(self.take()[41], 5)
        state = self.query(now=24)
        self.assertEqual((state['last_completed_id'], state['last_completed_ms'], state['last_status']), (2, 23, 'io'))
        self.assertTrue(state['ready'])

    def test_query_and_retry_preserve_original_completion_time(self):
        self.load()
        packet = request(kind=2, rid=2)
        self.submit(packet)
        self.lib.txjob_step(self.state, 11)
        first = self.take()
        self.assertEqual(struct.unpack_from('<Q', first, 76)[0], 11)
        for now in (100, 200):
            state = self.query(rid=2, now=now)
            self.assertEqual((state['last_completed_ms'], state['now_ms']), (11, now))
            self.submit(packet, now=now)
            self.assertEqual(self.take(), first)
        self.assertEqual(self.begins, 1)

    def test_old_boot_commands_cannot_execute_after_restart_with_same_generation(self):
        self.load()
        old = request(kind=2, rid=2)
        new_boot = BOOT + 1
        self.assertEqual(self.lib.txjob_init(self.state, c.byref(self.ops), new_boot), 0)
        self.submit(old)
        self.assertEqual(self.take()[41], 3)
        self.submit(request(rid=1))
        self.assertEqual(self.take()[41], 3)
        self.assertEqual(self.query()['last_request_id'], 0)
        self.assertEqual(self.query(boot=BOOT)['status'], 'mismatch')
        self.submit(request(boot=new_boot))
        self.assertEqual(self.take()[41], 0)
        self.assertEqual(self.begins, 0)

    def test_generation_change_hides_old_rule_but_preserves_boot_id_history(self):
        self.load(rid=10)
        state = self.query(generation=5)
        self.assertFalse(state['loaded'])
        self.assertEqual((state['device'], state['generation'], state['last_generation']), ('', 5, 4))
        self.submit(request(kind=2, rid=11), generation=5)
        self.assertEqual(self.take()[41], 3)
        self.submit(request(rid=1, generation=5), generation=5)
        self.assertEqual(self.take()[41], 8)
        self.submit(request(rid=11, generation=5), generation=5)
        self.assertEqual(self.take()[41], 0)
        self.assertEqual(self.begins, 0)

    def test_id_exhaustion_never_wraps_and_queries_remain_available(self):
        self.load(rid=0xffffffff)
        self.submit(request(kind=2, rid=1))
        self.assertEqual(self.take()[41], 8)
        self.assertEqual(self.query()['last_request_id'], 0xffffffff)
        self.submit(request(rid=0xffffffff))
        self.assertEqual(self.take()[41], 0)
        self.assertEqual(self.begins, 0)

    def test_old_version_and_malformed_queries_do_not_change_session(self):
        from tools.transaction.client import query
        self.load()
        packet = bytearray(request(rid=2)); packet[2] = 1
        self.submit(bytes(packet)); self.assertEqual(self.take()[41], 1)
        output = c.create_string_buffer(96)
        good = query(9)
        for packet in (good[:-1], good + b'x', good[:2] + b'\x01' + good[3:],
                       good[:16] + b'\x01' + good[17:]):
            with self.subTest(packet=packet.hex()):
                self.assertEqual(self.lib.txjob_query(self.state, packet, len(packet),
                                                     4, True, 10, output), -1)
        self.assertEqual(self.query()['last_request_id'], 1)
        self.assertEqual(self.lib.txjob_init(None, c.byref(self.ops), BOOT), -1)
        self.assertEqual(self.lib.txjob_init(self.state, c.byref(self.ops), 0), -1)
        self.assertEqual(self.query()['last_request_id'], 1)

if __name__=='__main__': unittest.main()
