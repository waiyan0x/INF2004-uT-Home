"""Real C sensor encoding and BLE fragmentation tests."""
import ctypes as c
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class Header(c.Structure):
    _fields_ = [('time',c.c_uint64),('sequence',c.c_uint32),
                ('source',c.c_uint16),('status',c.c_uint16)]
class Environment(c.Structure):
    _fields_ = [('header',Header),('pressure',c.c_float),('temperature',c.c_float)]
class Distance(c.Structure):
    _fields_ = [('header',Header),('distance',c.c_uint32),('echo',c.c_uint32)]
class Target(c.Structure):
    _fields_ = [('x',c.c_int16),('y',c.c_int16),('speed',c.c_int16),
                ('resolution',c.c_uint16),('present',c.c_bool)]
class Targets(c.Structure):
    _fields_ = [('header',Header),('targets',Target*3)]
class Health(c.Structure):
    _fields_ = [('header',Header),('count',c.c_uint32),('event',c.c_uint16)]
class Light(c.Structure):
    _fields_ = [('header',Header),('millilux',c.c_uint32)]
class Value(c.Union):
    _fields_ = [('environment',Environment),('distance',Distance),
                ('targets',Targets),('health',Health),('light',Light)]
class Source(c.Structure):
    _fields_ = [('name',c.c_char*32),('queue',c.c_int32),('type',c.c_uint16),
                ('source',c.c_uint16),('stale',c.c_uint32)]
class Entry(c.Structure):
    _fields_ = [('source',Source),('header',Header),('value',Value),
                ('expected',c.c_bool),('seen',c.c_bool)]
class Meta(c.Structure):
    _fields_ = [('boot',c.c_uint64),('revision',c.c_uint32),('generation',c.c_uint32)]
class Ble(c.Structure):
    _fields_ = [('active',c.c_uint8*112),('pending',c.c_uint8*112),
                ('active_generation',c.c_uint32),('pending_generation',c.c_uint32),
                ('replaced',c.c_uint32),('sequence',c.c_uint16),
                ('payload',c.c_uint16),('index',c.c_uint8),
                ('active_flag',c.c_bool),('pending_flag',c.c_bool),
                ('fragment_flag',c.c_bool)]


class NetworkServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('gcc')
        if compiler is None:
            raise AssertionError('GCC required')
        cls.folder=tempfile.TemporaryDirectory(prefix='uthome-services-')
        cls.addClassCleanup(cls.folder.cleanup)
        dll=Path(cls.folder.name)/'services.dll'
        paths=['firmware/src/network/'+name+'.c' for name in ['netble']]
        paths+=['firmware/src/runtime/sampcache.c']
        subprocess.run([compiler,'-std=c99','-Wall','-Wextra','-Werror','-pedantic',
                        '-O2','-shared',*(['-fPIC'] if sys.platform!='win32' else []),'-I'+str(ROOT/'firmware/include'),
                        '-I'+str(ROOT/'firmware/src/services'),
                        '-I'+str(ROOT/'firmware/src/runtime'),
                        *[str(ROOT/p) for p in paths],'-o',str(dll)],
                       check=True,capture_output=True)
        cls.lib=c.CDLL(str(dll))
        cls.encode=cls.lib.netble_encode
        cls.encode.argtypes=[c.POINTER(Entry),c.POINTER(Meta),c.c_uint64,c.c_void_p,c.c_size_t]
        cls.offer=cls.lib.netble_offer
        cls.offer.argtypes=[c.POINTER(Ble),c.POINTER(Entry),c.POINTER(Meta),c.c_uint64]
        cls.fragment=cls.lib.netble_fragment
        cls.fragment.argtypes=[c.POINTER(Ble),c.c_uint32,c.c_uint16,c.c_void_p,
                               c.c_size_t,c.POINTER(c.c_size_t)]
        cls.sent=cls.lib.netble_sent
        cls.sent.argtypes=[c.POINTER(Ble)]
        cls.reset=cls.lib.netble_reset
        cls.reset.argtypes=[c.POINTER(Ble)]
        cls.lib.sampcache_expect.argtypes=[c.POINTER(Source)]
        cls.lib.sampcache_record.argtypes=[c.c_int32,c.c_uint16,c.c_void_p,
                                           c.c_size_t,c.c_uint64]
        cls.lib.sampcache_source.argtypes=[c.c_uint16,c.POINTER(Entry)]
        cls.addClassCleanup(cls.unload)

    @classmethod
    def unload(cls):
        import _ctypes
        if sys.platform=='win32':
            _ctypes.FreeLibrary(cls.lib._handle)


    def entry(self, kind=1):
        entry=Entry()
        entry.source=Source(b'environment',0,kind,2,1000)
        entry.header=Header(100,7,2,1)
        entry.expected=True
        entry.seen=True
        entry.value.environment.pressure=100820.5
        entry.value.environment.temperature=26.25
        return entry

    def frame(self, entry, now=150):
        output=(c.c_uint8*128)(*([0xDA]*128))
        meta=Meta(0x1122334455667788,12,4)
        self.assertEqual(self.encode(c.byref(entry),c.byref(meta),now,output,128),0)
        self.assertEqual(bytes(output[112:]),b'\xda'*16)
        return bytes(output[:112])


    def test_ble_environment_metadata_missing_stale_and_fault(self):
        entry=self.entry()
        frame=self.frame(entry)
        self.assertEqual(frame[:8],b'\x01\x01\x01\x01\x02\x00\x00\x00')
        self.assertEqual(struct.unpack_from('<IIIQQII',frame,8),
                         (7,12,4,0x1122334455667788,100,50,1000))
        self.assertEqual(frame[44:55],b'environment')
        self.assertEqual(struct.unpack_from('<ff',frame,76),(100820.5,26.25))
        self.assertEqual(self.frame(entry,1100)[2],2)
        entry.seen=False
        frame=self.frame(entry)
        self.assertEqual(frame[2:4],b'\x00\x00')
        self.assertEqual(frame[76:],bytes(36))
        self.assertEqual(struct.unpack_from('<I',frame,36)[0],0xffffffff)
        entry.seen=True
        entry.header.status=2
        self.assertEqual(self.frame(entry)[2:4],b'\x03\x02')
        self.assertEqual(self.frame(entry)[76:],bytes(36))
        entry.header.status=1
        self.assertEqual(self.frame(entry,99)[2],3)

    def test_ble_distance_targets_health_and_invalid_float(self):
        entry=self.entry(3)
        entry.value.distance.distance=1234
        entry.value.distance.echo=7200
        self.assertEqual(struct.unpack_from('<II',self.frame(entry),76),(1234,7200))
        entry=self.entry(2)
        entry.value=Value()
        entry.value.targets.targets[0]=Target(-320,160,-25,8,True)
        entry.value.targets.targets[1]=Target(99,99,99,99,False)
        frame=self.frame(entry)
        self.assertEqual(struct.unpack_from('<IhhH',frame,76),(1+(8<<16),-320,160,65511))
        self.assertEqual(frame[88:],bytes(24))
        entry=self.entry(4)
        entry.value.health.count=42
        entry.value.health.event=6
        self.assertEqual(struct.unpack_from('<II',self.frame(entry),76),(6,42))
        entry=self.entry()
        entry.value.environment.temperature=float('nan')
        frame=self.frame(entry)
        self.assertEqual(frame[2:4],b'\x03\x04')
        self.assertEqual(frame[76:],bytes(36))


    def test_ble_invalid_health_and_missing_status_are_normalized(self):
        entry=self.entry(4)
        entry.value.health.event=99
        frame=self.frame(entry)
        self.assertEqual(frame[2:4],b'\x03\x04')
        self.assertEqual(frame[76:],bytes(36))
        entry.seen=False
        entry.header.status=65535
        self.assertEqual(self.frame(entry)[2:4],b'\x00\x00')

    def test_ble_minimum_mtu_reassembly_busy_and_latest_pending(self):
        state=Ble()
        entry=self.entry()
        meta=Meta(1,12,4)
        self.assertEqual(self.offer(c.byref(state),c.byref(entry),c.byref(meta),150),0)
        active=[]
        while True:
            output=(c.c_uint8*20)()
            size=c.c_size_t()
            result=self.fragment(c.byref(state),4,23,output,20,c.byref(size))
            if result==1:
                break
            self.assertEqual(result,0)
            fragment=bytes(output[:size.value])
            self.assertEqual(fragment[:2],b'\x01\x01')
            self.assertEqual(fragment[4:8],bytes([len(active),10,112,0]))
            # Busy SDK send: repeat same fragment; no advance until acknowledged.
            self.assertEqual(self.fragment(c.byref(state),4,23,output,20,c.byref(size)),0)
            self.assertEqual(bytes(output[:size.value]),fragment)
            active.append(fragment[8:])
            self.assertEqual(self.sent(c.byref(state)),0)
        expected=(c.c_uint8*112)()
        self.encode(c.byref(entry),c.byref(meta),150,expected,112)
        self.assertEqual(b''.join(active),bytes(expected))
        self.assertEqual(self.sent(c.byref(state)),-1)

    def test_ble_pending_replacement_keeps_active_and_generation_reset(self):
        state=Ble()
        entry=self.entry()
        meta=Meta(1,12,4)
        self.offer(c.byref(state),c.byref(entry),c.byref(meta),150)
        output=(c.c_uint8*128)()
        size=c.c_size_t()
        self.assertEqual(self.fragment(c.byref(state),4,23,output,128,c.byref(size)),0)
        old=bytes(state.active)
        for index in range(1000):
            entry.header.sequence=index
            self.assertEqual(self.offer(c.byref(state),c.byref(entry),c.byref(meta),150),0)
        self.assertEqual(state.replaced,999)
        self.assertEqual(bytes(state.active),old)
        before=bytes(state)
        self.assertEqual(self.fragment(c.byref(state),4,24,output,128,c.byref(size)),-1)
        self.assertEqual(bytes(state),before)
        self.assertEqual(self.fragment(c.byref(state),5,23,output,128,c.byref(size)),1)
        self.assertFalse(state.active_flag)
        self.assertFalse(state.pending_flag)
        self.reset(c.byref(state))
        self.assertEqual(bytes(state),bytes(c.sizeof(Ble)))

    def test_ble_invalid_offer_and_small_fragment_do_not_consume(self):
        state=Ble()
        entry=self.entry()
        meta=Meta(1,12,4)
        self.offer(c.byref(state),c.byref(entry),c.byref(meta),150)
        before=bytes(state)
        output=(c.c_uint8*20)(*([0xDA]*20))
        size=c.c_size_t()
        self.assertEqual(self.fragment(c.byref(state),4,23,output,19,c.byref(size)),-1)
        self.assertEqual(bytes(state),before)
        self.assertEqual(bytes(output),b'\xda'*20)
        entry.expected=False
        self.assertEqual(self.offer(c.byref(state),c.byref(entry),c.byref(meta),150),-1)
        self.assertEqual(bytes(state),before)


    def test_actual_cache_sample_reaches_encoder_without_dequeue(self):
        self.lib.sampcache_reset()
        source=Source(b'pressure',0,1,2,1000)
        self.assertEqual(self.lib.sampcache_expect(c.byref(source)),0)
        sample=Environment(Header(100,7,2,1),100820.5,26.25)
        self.lib.sampcache_record(0,1,c.byref(sample),c.sizeof(sample),150)
        entry=Entry()
        self.assertEqual(self.lib.sampcache_source(2,c.byref(entry)),0)
        body=self.frame(entry)
        self.assertEqual(len(body),112)
        self.assertEqual(struct.unpack_from('<ff',body,76),(100820.5,26.25))
        again=Entry()
        self.lib.sampcache_source(2,c.byref(again))
        self.assertEqual(bytes(entry),bytes(again))

    def test_actual_light_cache_reaches_encoder_without_consumption(self):
        self.lib.sampcache_reset()
        source = Source(b'room-light', 0, 5, 2, 1000)
        self.assertEqual(self.lib.sampcache_expect(c.byref(source)), 0)
        sample = Light(Header(100, 7, 2, 1), 123456)
        self.lib.sampcache_record(0, 5, c.byref(sample), c.sizeof(sample), 150)
        entry, again = Entry(), Entry()
        self.assertEqual(self.lib.sampcache_source(2, c.byref(entry)), 0)
        body = self.frame(entry)
        self.assertEqual(body[1:4], bytes([5, 1, 1]))
        self.assertEqual(struct.unpack_from('<I', body, 76)[0], 123456)
        self.assertEqual(body[80:], bytes(32))
        self.assertEqual(self.lib.sampcache_source(2, c.byref(again)), 0)
        self.assertEqual(self.frame(again), body)
        self.assertEqual(bytes(entry), bytes(again))

if __name__=='__main__':
    unittest.main()
