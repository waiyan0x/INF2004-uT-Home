"""Actual C MQTT session with fragmented broker replies and bounded TLS doubles."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
CONNECT=c.CFUNCTYPE(c.c_int,c.c_void_p)
READ=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_void_p,c.c_size_t)
WRITE=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_void_p,c.c_size_t)
CLOSE=c.CFUNCTYPE(None,c.c_void_p)
class View(c.Structure):
    _fields_=[(name,c.c_uint32) for name in ('state','failures','replaced','discarded')]
class Config(c.Structure):
    _fields_=[('connect',CONNECT),('read',READ),('write',WRITE),('close',CLOSE),
              ('context',c.c_void_p),('client',c.c_char_p),('user',c.c_char_p),
              ('password',c.c_char_p),('availability',c.c_char_p)]

class BrokerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(prefix='uthome-broker-')
        cls.addClassCleanup(cls.folder.cleanup)
        binary=Path(cls.folder.name)/'broker.dll'
        sources=[ROOT/'firmware/src/network'/name for name in
                 ['netbroker.c','netmqtt.c','netretry.c']]
        subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra','-Werror',
                        '-pedantic','-O2','-shared',*(['-fPIC'] if sys.platform!='win32' else []),*map(str,sources),'-o',str(binary)],
                       check=True,capture_output=True)
        cls.lib=c.CDLL(str(binary))
        cls.lib.netbroker_init.argtypes=[c.c_void_p,c.POINTER(Config)]
        cls.lib.netbroker_step.argtypes=[c.c_void_p,c.c_uint64,c.c_bool,c.c_uint32,c.c_uint32]
        cls.lib.netbroker_snapshot.argtypes=[c.c_void_p,c.POINTER(View)]
        cls.lib.netbroker_offer.argtypes=[c.c_void_p,c.c_char_p,c.c_char_p,c.c_size_t,c.c_bool]
        cls.lib.netbroker_idle.argtypes=[c.c_void_p]
        cls.lib.netbroker_idle.restype=c.c_bool
        cls.addClassCleanup(cls.unload)

    @classmethod
    def unload(cls):
        import _ctypes
        if sys.platform=='win32':
            _ctypes.FreeLibrary(cls.lib._handle)

    def setUp(self):
        self.state=(c.c_uint64*1024)()
        self.input=bytearray()
        self.output=bytearray()
        self.limit=4096
        self.read_limit=4
        self.connect_result=1
        self.closes=0
        self.connects=0
        self.operations=0
        self.now=0
        def connect(context):
            self.operations+=1
            self.connects+=1
            return self.connect_result
        def read(context,data,size):
            self.operations+=1
            if self.input==b'EOF':
                return -1
            count=min(size,len(self.input),self.read_limit)
            if count:
                c.memmove(data,bytes(self.input[:count]),count)
                del self.input[:count]
            return count
        def write(context,data,size):
            self.operations+=1
            count=min(size,self.limit)
            self.output.extend(c.string_at(data,count))
            return count
        def close(context):
            self.closes+=1
        self.config=Config(CONNECT(connect),READ(read),WRITE(write),CLOSE(close),
                           None,b'node',b'user',b'secret',b'uthome/v1/node/availability')
        self.assertEqual(self.lib.netbroker_init(self.state,c.byref(self.config)),0)

    def step(self,now=None,ip=True,generation=1):
        self.now=self.now+1 if now is None else now
        before=self.operations
        result=self.lib.netbroker_step(self.state,self.now,ip,generation,0)
        self.assertLessEqual(self.operations-before,1)
        return result

    def offer(self,data=b'data',retain=False):
        return self.lib.netbroker_offer(self.state,b'uthome/v1/node/telemetry/light',data,len(data),retain)

    def online(self):
        self.step()
        self.step()
        self.input.extend(b'\x20\x02\x00\x00')
        for _ in range(8):
            self.step()
        self.assertEqual(self.offer(),0)

    def view(self):
        before=bytes(self.state)
        value=View()
        self.assertEqual(self.lib.netbroker_snapshot(self.state,c.byref(value)),0)
        self.assertEqual(bytes(self.state),before)
        return dict((name,getattr(value,name)) for name,_ in View._fields_)

    def test_diagnostics_preserve_counts_across_plan_change_and_failure(self):
        self.assertEqual(self.view(),dict(state=0,failures=0,replaced=0,discarded=0))
        self.assertEqual(self.offer(),1)
        self.online()
        self.offer(b'two');self.offer(b'three')
        self.assertEqual(self.view()['replaced'],2)
        self.step()  # Active packet is now immutable.
        self.offer(b'four')
        self.limit=1
        self.step()
        self.step(generation=2)
        self.assertEqual(self.view(),dict(state=0,failures=0,replaced=2,discarded=3))
        self.offer(b'offline')
        self.assertEqual(self.view()['discarded'],4)
        self.setUp();self.online()
        self.input.extend(b'EOF')
        self.step()
        self.assertEqual(self.view()['failures'],1)
        self.assertEqual(self.view()['discarded'],1)
        self.assertEqual(self.view()['state'],0)

    def test_uninitialized_diagnostics_are_unavailable_and_nonmutating(self):
        state=(c.c_uint64*1024)()
        view=View()
        self.assertEqual(self.lib.netbroker_snapshot(state,c.byref(view)),0)
        self.assertEqual(view.state,6)
        self.assertEqual(bytes(state),bytes(c.sizeof(state)))
        self.assertEqual(self.lib.netbroker_snapshot(None,c.byref(view)),-1)
        self.assertEqual(self.lib.netbroker_snapshot(state,None),-1)

    def test_connect_clean_session_credentials_retained_will_and_availability(self):
        self.online()
        self.assertIn(b'\x00\x04MQTT\x04\xe6\x00\x1e',self.output)
        self.assertIn(b'\x00\x07offline\x00\x04user\x00\x06secret',self.output)
        self.assertIn(b'\x31\x23\x00\x1buthome/v1/node/availabilityonline',self.output)

    def test_retain_argument_sets_only_the_publish_header_bit(self):
        self.online()
        for _ in range(4):
            self.step()
        self.output.clear()
        self.assertEqual(self.offer(b'kept',retain=True),0)
        self.step();self.step()
        self.assertEqual(bytes(self.output),b'\x31\x24\x00\x1euthome/v1/node/telemetry/lightkept')
        self.output.clear()
        self.assertEqual(self.offer(b'kept'),0)
        self.step();self.step()
        self.assertEqual(bytes(self.output),b'\x30\x24\x00\x1euthome/v1/node/telemetry/lightkept')

    def test_idle_only_when_ready_with_nothing_active_pending_or_pinging(self):
        self.assertFalse(self.lib.netbroker_idle(None))
        self.assertFalse(self.lib.netbroker_idle((c.c_uint64*1024)()))
        self.assertFalse(self.lib.netbroker_idle(self.state))
        self.online()                      # Leaves one telemetry offer pending.
        self.assertFalse(self.lib.netbroker_idle(self.state))
        self.limit=1
        self.step()                        # Taken: now active, partially written.
        self.assertFalse(self.lib.netbroker_idle(self.state))
        self.limit=4096
        self.step()                        # Active packet completes.
        self.assertTrue(self.lib.netbroker_idle(self.state))
        self.step(self.now+15000);self.step()   # PINGREQ queued then written.
        self.assertTrue(self.output.endswith(b'\xc0\x00'))
        self.assertFalse(self.lib.netbroker_idle(self.state))
        self.input.extend(b'\xd0\x00')
        self.step()
        self.assertTrue(self.lib.netbroker_idle(self.state))
        self.step(generation=2)
        self.assertFalse(self.lib.netbroker_idle(self.state))

    def test_fragmented_connack_and_partial_writes(self):
        self.limit=1
        self.read_limit=1
        self.step()
        for _ in range(150):
            self.step()
        self.input.extend(b'\x20\x02\x00\x00')
        for _ in range(150):
            self.step()
        self.assertEqual(self.offer(b'\x00\xff'),0)
        for _ in range(150):
            self.step()
        self.assertTrue(self.output.endswith(b'\x00\xff'))

    def test_connack_rejection_malformed_and_unsolicited_packets(self):
        for packet in [b'\x20\x02\x00\x05',b'\x20\x02\x01\x00',
                       b'\x20\x80\x00\x00',b'\x30\x02\x00\x00']:
            self.setUp()
            self.step();self.step()
            self.input.extend(packet)
            self.assertEqual(self.step(),-1)
            self.assertEqual(self.closes,1)
            self.assertEqual(self.offer(),1)
        self.setUp();self.online()
        self.input.extend(b'\xd0')
        self.assertEqual(self.step(),-1)

    def test_keepalive_success_and_missing_response_timeout(self):
        self.online()
        self.step(16000);self.step()
        self.assertTrue(self.output.endswith(b'\xc0\x00'))
        self.input.extend(b'\xd0\x00')
        self.assertEqual(self.step(),0)
        self.step()
        self.step(32000);self.step()
        self.assertEqual(self.step(42001),-1)
        self.assertEqual(self.closes,1)

    def test_outage_eof_backoff_and_no_offline_backlog(self):
        self.online()
        self.input.extend(b'EOF')
        self.assertEqual(self.step(),-1)
        connects=self.connects
        self.assertEqual(self.offer(),1)
        self.step(self.now+999)
        self.assertEqual(self.connects,connects)
        self.step(self.now+1)
        self.assertEqual(self.connects,connects+1)
        self.step(ip=False)
        self.assertEqual(self.offer(),1)

    def test_handshake_write_and_connack_absolute_deadlines(self):
        self.connect_result=0
        self.step(1)
        self.assertEqual(self.step(30001),-1)
        self.setUp();self.limit=0
        self.step(1);self.step(29999)
        self.assertEqual(self.step(30001),-1)
        self.setUp();self.step(1);self.step(2)
        self.assertEqual(self.step(30001),-1)

    def test_generation_change_cancels_partial_packet_and_pending(self):
        self.online()
        self.offer(b'old')
        self.step()
        self.limit=1
        self.step()
        self.assertEqual(self.step(generation=2),0)
        self.assertEqual(self.closes,1)
        self.assertEqual(self.offer(b'new'),1)

    def test_maximum_connect_fields_use_canonical_two_byte_length(self):
        self.config.client=b'n'*32
        self.config.user=b'u'*64
        self.config.password=b'p'*64
        self.config.availability=b'a'*128
        self.state=(c.c_uint64*1024)()
        self.assertEqual(self.lib.netbroker_init(self.state,c.byref(self.config)),0)
        self.step();self.step()
        packet=bytes(self.output)
        self.assertEqual(packet[0],0x10)
        self.assertTrue(packet[1]&0x80)
        self.assertFalse(packet[2]&0x80)
        self.assertEqual((packet[1]&127)+128*packet[2],len(packet)-3)
        offset=3
        def field():
            nonlocal offset
            size=int.from_bytes(packet[offset:offset+2],'big')
            offset+=2
            value=packet[offset:offset+size]
            offset+=size
            return value
        self.assertEqual(field(),b'MQTT')
        self.assertEqual(packet[offset:offset+4],b'\x04\xe6\x00\x1e')
        offset+=4
        self.assertEqual([field() for _ in range(5)],
                         [b'n'*32,b'a'*128,b'offline',b'u'*64,b'p'*64])
        self.assertEqual(offset,len(packet))

    def test_invalid_connect_identity_rejected_before_io(self):
        for name,value in [('client',b'n'*33),('user',b''),('password',b'p'*65),
                           ('availability',b'bad/#'),('user',b'bad\nname')]:
            self.setUp()
            setattr(self.config,name,value)
            self.state=(c.c_uint64*1024)()
            self.assertEqual(self.lib.netbroker_init(self.state,c.byref(self.config)),-1)
            self.assertEqual(self.operations,0)

    def test_clock_and_capacity_fail_without_transport_work(self):
        self.step(100)
        operations=self.operations
        self.assertEqual(self.step(99),-1)
        self.assertEqual(self.operations,operations)
        self.assertEqual(self.step(2**64-1),-1)
        self.assertEqual(self.offer(b'x'*1025),-1)

if __name__=='__main__':
    unittest.main()
