"""Runtime rules: actual fixed C decision engine, injected owner operations."""
import ctypes as c
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BEGIN = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_void_p, c.c_uint64)
POLL = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint64, c.c_void_p, c.c_void_p, c.c_void_p)
CLOSE = c.CFUNCTYPE(c.c_int, c.c_void_p)
class Ops(c.Structure):
    _fields_ = [('begin', BEGIN), ('poll', POLL), ('close', CLOSE), ('context', c.c_void_p)]

def request(kind=1, rid=1, revision=1, protocol=1, generation=4, tx=b'\x00', rx=1, **changes):
    result = bytearray(96)
    result[:4] = b'TJ\x01' + bytes([kind])
    struct.pack_into('<II', result, 4, rid, revision)
    result[12:18] = b'sensor'
    struct.pack_into('<I', result, 90, generation)
    if kind == 1:
        result[44:48] = bytes([protocol, 119 if protocol == 1 else 0, 0, 255])
        struct.pack_into('<IH', result, 48, 256000 if protocol == 3 else 100000, 100)
        result[54:58] = bytes([len(tx), rx, 4 if protocol == 2 else 255, 0])
        result[60:60+len(tx)] = tx
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
        cls.lib.txjob_init.argtypes = [c.c_void_p, c.POINTER(Ops)]
        cls.lib.txjob_submit.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t, c.c_uint32, c.c_bool, c.c_uint64]
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
        self.assertEqual(self.lib.txjob_init(self.state, c.byref(self.ops)), 0)

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
        self.assertEqual(reply[45], 0)

    def test_three_protocols_select_different_runtime_operations_without_rebuild(self):
        for index, proto in enumerate((1, 2, 3)):
            self.load(rid=1+index*2, revision=proto, protocol=proto, rx=0 if proto==2 else 1)
            self.data = b'' if proto==2 else b'\x50'
            self.submit(request(kind=2, rid=2+index*2, revision=proto))
            self.lib.txjob_step(self.state, 11)
            result = self.take()
            self.assertEqual((result[44], result[45], result[46], result[47]),
                             (proto, 0, 1, len(self.data)))
        self.assertEqual((self.begins, self.polls, self.closes), (3, 3, 3))

    def test_rejected_update_preserves_rule_and_wrong_identity_has_no_io(self):
        self.load()
        bad = request(rid=2, revision=2, **{'54':31})
        self.submit(bad); result=self.take()
        self.assertEqual(result[45],1)
        self.assertEqual(struct.unpack_from('<I',result,8)[0],1)
        self.submit(request(kind=2,rid=3,revision=2)); self.assertEqual(self.take()[45],3)
        self.assertEqual(self.begins,0)

    def test_exact_retry_does_not_repeat_write_and_changed_or_older_ids_reject(self):
        self.load()
        execute=request(kind=2,rid=2)
        self.submit(execute); self.lib.txjob_step(self.state,11); original=self.take()
        self.submit(execute,now=20); self.assertEqual(self.take(),original)
        self.assertEqual(self.begins,1)
        self.submit(request(kind=2,rid=2,revision=9)); self.assertEqual(self.take()[45],8)
        self.submit(execute); self.assertEqual(self.take(),original)
        self.submit(request(rid=1)); self.assertEqual(self.take()[45],8)

    def test_generation_nonidle_and_malformed_input_never_call_driver(self):
        for data in [b'',request()[:-1],request()+b'x',b'X'+request()[1:]]:
            self.submit(data); self.assertEqual(self.take()[45],1)
        self.submit(idle=False); self.assertEqual(self.take()[45],6)
        self.submit(request(rid=2,generation=3)); self.assertEqual(self.take()[45],3)
        self.assertEqual(self.begins,0)
        self.load(rid=3)
        self.submit(request(kind=2,rid=4,generation=5),generation=5)
        self.assertEqual(self.take()[45],2)

    def test_fault_timeout_and_cancel_zero_rx_and_partial_write_is_explicit(self):
        self.load()
        for rid, status in [(2,5),(3,4),(4,9)]:
            self.poll_status=10 if status!=5 else 5
            self.submit(request(kind=2,rid=rid))
            if status==9: self.lib.txjob_cancel(self.state)
            self.lib.txjob_step(self.state,110 if status==4 else 11)
            result=self.take()
            self.assertEqual(result[45],status)
            self.assertEqual(result[47:80],bytes(33))

    def test_failed_cleanup_retains_busy_and_never_reexecutes(self):
        self.load(); self.close_status=-1
        self.submit(request(kind=2,rid=2)); self.lib.txjob_step(self.state,11)
        self.assertEqual(self.submit(request(rid=3)),-1)
        self.close_status=0; self.lib.txjob_step(self.state,12)
        self.assertEqual(self.take()[45],5)
        self.assertEqual(self.polls,1)

    def test_spi_read_is_explicitly_unsupported_and_pins_are_checked(self):
        self.submit(request(protocol=2,rx=1)); self.assertEqual(self.take()[45],7)
        for rid,pin in enumerate((2,6,7,8,9,18,19,20,21,255),2):
            self.submit(request(protocol=2,rx=0,rid=rid,**{'56':pin}))
            self.assertEqual(self.take()[45],1)
        self.assertEqual(self.begins,0)

if __name__=='__main__': unittest.main()
