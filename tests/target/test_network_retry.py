"""Actual C retry deadlines, upstream loss, overflow and stable reset."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
class Retry(c.Structure):
    _fields_ = [('last',c.c_uint64),('deadline',c.c_uint64),('online',c.c_uint64),
                ('delay',c.c_uint32),('state',c.c_int)]
class RetryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(prefix='uthome-retry-')
        cls.addClassCleanup(cls.folder.cleanup)
        binary=Path(cls.folder.name)/'retry.dll'
        subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra',
                        '-Werror','-pedantic','-shared',*(['-fPIC'] if sys.platform!='win32' else []),
                        str(ROOT/'firmware/src/network/netretry.c'),'-o',str(binary)],
                       check=True,capture_output=True)
        cls.lib=c.CDLL(str(binary))
        cls.init=cls.lib.netretry_init
        cls.init.argtypes=[c.POINTER(Retry),c.c_uint64]
        cls.poll=cls.lib.netretry_poll
        cls.poll.argtypes=[c.POINTER(Retry),c.c_uint64,c.c_bool,c.c_uint32]
        cls.result=cls.lib.netretry_result
        cls.result.argtypes=cls.poll.argtypes
        cls.addClassCleanup(cls.unload)
    @classmethod
    def unload(cls):
        import _ctypes
        if sys.platform=='win32':
            _ctypes.FreeLibrary(cls.lib._handle)
    def test_attempt_deadline_and_capped_backoff(self):
        state=Retry()
        self.assertEqual(self.init(c.byref(state),0),0)
        now=0
        for expected in [1000,2000,4000,8000,16000,30000,30000]:
            self.assertEqual(self.poll(c.byref(state),now,True,0),1)
            self.assertEqual(self.poll(c.byref(state),now,True,0),0)
            now+=30000
            self.assertEqual(self.poll(c.byref(state),now,True,0),0)
            self.assertEqual(state.deadline,now+expected)
            self.assertEqual(self.poll(c.byref(state),state.deadline-1,True,0),0)
            now=state.deadline
    def test_upstream_loss_and_stable_reset(self):
        state=Retry()
        self.init(c.byref(state),0)
        self.assertEqual(self.poll(c.byref(state),0,False,0),0)
        self.assertEqual(self.poll(c.byref(state),0,True,0),1)
        self.assertEqual(self.result(c.byref(state),1,False,0),0)
        self.assertEqual(self.poll(c.byref(state),1001,True,0),1)
        self.assertEqual(self.result(c.byref(state),1002,True,0),0)
        self.assertEqual(self.poll(c.byref(state),61002,False,250),0)
        self.assertEqual(state.deadline,62252)
        self.assertEqual(state.delay,2000)
        self.assertEqual(self.result(c.byref(state),61003,True,0),-1)
    def test_invalid_clock_jitter_overflow_and_late_success(self):
        state=Retry()
        self.init(c.byref(state),10)
        before=bytes(state)
        self.assertEqual(self.poll(c.byref(state),9,True,0),-1)
        self.assertEqual(self.poll(c.byref(state),10,True,251),-1)
        self.assertEqual(bytes(state),before)
        self.poll(c.byref(state),10,True,0)
        before=bytes(state)
        self.assertEqual(self.result(c.byref(state),30010,True,0),-1)
        self.assertEqual(bytes(state),before)
        self.init(c.byref(state),2**64-1)
        before=bytes(state)
        self.assertEqual(self.poll(c.byref(state),2**64-1,True,0),-1)
        self.assertEqual(bytes(state),before)
if __name__=='__main__':
    unittest.main()
