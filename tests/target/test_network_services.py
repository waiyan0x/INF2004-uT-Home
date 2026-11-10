"""Real C HTTP admission/read dispatch and sensor BLE fragmentation tests."""
import ctypes as c
import json
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SECRET = b'0123456789abcdef0123456789abcdef'

class Http(c.Structure):
    _fields_ = [('headers',c.c_char*1025),('secret',c.c_uint8*64),
                ('used',c.c_size_t),('secret_size',c.c_size_t),
                ('started',c.c_uint64),('last',c.c_uint64),
                ('status',c.c_int),('route',c.c_int),('source',c.c_uint16),('content_size',c.c_uint32)]
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
class BrokerView(c.Structure):
    _fields_ = [(name,c.c_uint32) for name in ('state','failures','replaced','discarded')]
class Info(c.Structure):
    _fields_ = [('meta',Meta),('saved',c.c_uint32),('state',c.c_uint32),
                ('durable',c.c_bool),('wifi',c.c_bool),('node',c.c_char*32),
                ('mqtt',BrokerView),('mqtt_enabled',c.c_bool),('configuration',c.c_bool),('transaction',c.c_void_p)]
CAPTURE = c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_uint16,c.POINTER(Entry),
                     c.POINTER(c.c_uint64))

def request(path=b'/v1/status', method=b'GET', headers=b'', auth=SECRET):
    return method+b' '+path+b' HTTP/1.1\r\nHost: esp.local\r\n'+(
        b'Authorization: Bearer '+auth+b'\r\n' if auth is not None else b'')+headers+b'\r\n'

class NetworkServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('gcc')
        if compiler is None:
            raise AssertionError('GCC required')
        cls.folder=tempfile.TemporaryDirectory(prefix='uthome-services-')
        cls.addClassCleanup(cls.folder.cleanup)
        dll=Path(cls.folder.name)/'services.dll'
        paths=['firmware/src/network/'+name+'.c' for name in ['nethttp','netble','netread']]
        paths+=['firmware/src/runtime/sampcache.c']
        subprocess.run([compiler,'-std=c99','-Wall','-Wextra','-Werror','-pedantic',
                        '-O2','-shared',*(['-fPIC'] if sys.platform!='win32' else []),'-I'+str(ROOT/'firmware/include'),
                        '-I'+str(ROOT/'firmware/src/services'),
                        '-I'+str(ROOT/'firmware/src/runtime'),
                        *[str(ROOT/p) for p in paths],'-o',str(dll)],
                       check=True,capture_output=True)
        cls.lib=c.CDLL(str(dll))
        cls.init=cls.lib.nethttp_init
        cls.init.argtypes=[c.POINTER(Http),c.c_char_p,c.c_size_t,c.c_uint64]
        cls.feed=cls.lib.nethttp_feed
        cls.feed.argtypes=[c.POINTER(Http),c.c_char_p,c.c_size_t,c.c_uint64]
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
        cls.reply=cls.lib.netread_reply
        cls.reply.argtypes=[c.POINTER(Http),c.POINTER(Info),CAPTURE,c.c_void_p,
                            c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
        cls.http_reply=cls.lib.nethttp_reply
        cls.http_reply.argtypes=[c.c_int,c.c_bool,c.c_char_p,c.c_size_t,
                                 c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
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

    def http(self, data, chunks=None):
        state=Http()
        self.assertEqual(self.init(c.byref(state),SECRET,len(SECRET),0),0)
        if chunks is None:
            chunks=[data]
        result=0
        for piece in chunks:
            self.assertEqual(result,0)
            result=self.feed(c.byref(state),piece,len(piece),1)
        return result,state

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

    def test_http_every_fragment_boundary_and_case_insensitive_fields(self):
        data=request().replace(b'Host:',b'hOsT:').replace(b'Authorization:',b'aUtHoRiZaTiOn:')
        for split in range(1,len(data)):
            result,state=self.http(data,chunks=[data[:split],data[split:]])
            self.assertEqual((result,state.route),(200,2))
        result,state=self.http(data,chunks=[bytes([x]) for x in data])
        self.assertEqual(result,200)
        self.assertEqual(self.feed(c.byref(state),b'',0,2),-1)

    def test_http_auth_missing_wrong_prefix_suffix_and_duplicate(self):
        for auth in [None,b'',SECRET[:-1],SECRET+b'x',b'x'+SECRET[1:],b'bearer-secret']:
            self.assertEqual(self.http(request(auth=auth))[0],401)
        self.assertEqual(self.http(request(headers=b'Authorization: Bearer '+SECRET+b'\r\n'))[0],400)

    def test_http_framing_controls_pipeline_and_mutations_fail_closed(self):
        for header in [b'Content-Length: 1\r\n',b'Content-Length: 00\r\n',
                       b'Content-Length: 0\r\nContent-Length: 0\r\n',
                       b'Transfer-Encoding: chunked\r\n',b'Host: second\r\n',
                       b'Bad Header: x\r\n',b' folded: value\r\n',
                       b'X: value\nInjected: yes\r\n',b'X: value\rInjected: yes\r\n',
                       b'X: a\x00b\r\n']:
            self.assertEqual(self.http(request(headers=header))[0],400,header)
        self.assertEqual(self.http(request()+b'GET / HTTP/1.1\r\n')[0],400)
        self.assertEqual(self.http(request(method=b'DELETE'))[0],405)
        self.assertEqual(self.http(request(path=b'/v1/configuration',method=b'POST'))[0],415)
        self.assertEqual(self.http(request(path=b'/v1/configuration',method=b'POST',auth=None))[0],401)

    def test_configuration_metadata_is_canonical_bounded_and_authenticated(self):
        fields=(b'Content-Type: application/json\r\nContent-Length: 11\r\n'
                b'X-Request-ID: 1\r\nX-Revision: 1\r\nX-Expected-Revision: 0\r\n'
                b'X-Expected-Boot-ID: 0000000000000001\r\nX-Request-Token: op\r\n')
        data=request(b'/v1/configuration',b'POST',fields)
        status,state=self.http(data)
        self.assertEqual((status,state.route,state.content_size),(200,5,11))
        for split in range(1,len(data)):
            self.assertEqual(self.http(data,chunks=[data[:split],data[split:]])[0],200)
        invalid=[(b'Content-Length: 11',b'Content-Length: 8193',413),
                 (b'Content-Length: 11',b'Content-Length: 011',400),
                 (b'Content-Length: 11',b'Content-Length: 4294967296',400),
                 (b'X-Request-ID: 1',b'X-Request-ID: 0',400),
                 (b'X-Request-ID: 1',b'X-Request-ID: +1',400),
                 (b'X-Revision: 1',b'X-Revision: 01',400),
                 (b'X-Expected-Boot-ID: 0000000000000001',b'X-Expected-Boot-ID: 1',400),
                 (b'X-Request-Token: op',b'X-Request-Token: '+b'x'*65,400),
                 (b'X-Request-Token: op',b'X-Request-Token: bad/token',400),
                 (b'application/json',b'text/plain',415)]
        for old,new,expected in invalid:
            self.assertEqual(self.http(data.replace(old,new))[0],expected,(old,new))
        for header in [b'X-Request-ID: 1',b'X-Revision: 1',b'X-Expected-Revision: 0',
                       b'X-Expected-Boot-ID: 0000000000000001',b'X-Request-Token: op',
                       b'Content-Type: application/json']:
            self.assertEqual(self.http(data.replace(b'\r\n\r\n',b'\r\n'+header+b'\r\n\r\n'))[0],400)
        self.assertEqual(self.http(request(b'/v1/configuration',b'POST',fields,auth=None))[0],401)
        self.assertEqual(self.http(data.replace(b'\r\n\r\n',b'\r\nExpect: 100-continue\r\n\r\n'))[0],417)
        self.assertEqual(self.http(data.replace(b'\r\n\r\n',b'\r\nContent-Encoding: gzip\r\n\r\n'))[0],415)

    def test_http_limits_deadline_no_slowloris_extension(self):
        self.assertEqual(self.http(b'G'*1025)[0],431)
        self.assertEqual(self.http(request(headers=b'X: '+b'x'*257+b'\r\n'))[0],400)
        headers=b''.join(b'X%d: x\r\n'%x for x in range(17))
        self.assertEqual(self.http(request(headers=headers))[0],400)
        state=Http()
        self.init(c.byref(state),SECRET,len(SECRET),0)
        self.assertEqual(self.feed(c.byref(state),b'G',1,9999),0)
        self.assertEqual(self.feed(c.byref(state),None,0,10000),408)
        self.init(c.byref(state),SECRET,len(SECRET),50)
        self.assertEqual(self.feed(c.byref(state),None,0,49),408)

    def test_http_routes_and_invalid_sources(self):
        self.assertEqual(self.http(request(b'/v1/configuration/result'))[0],400)
        self.assertEqual(self.http(request(b'/v1/configuration'))[0],200)
        for source in range(8):
            result,state=self.http(request(b'/v1/telemetry?source='+str(source).encode()))
            self.assertEqual((result,state.route,state.source),(200,3,source))
        for path in [b'/v1/telemetry',b'/v1/telemetry?source=8',
                     b'/v1/telemetry?source=00',b'/v1/telemetry?source=-1',
                     b'/v1/telemetry?source=2&source=3']:
            self.assertEqual(self.http(request(path))[0],404)

    def test_http_exact_header_ceiling_and_fuzz_have_bounded_storage(self):
        base=request(headers=b'X: \r\n')
        # Multiple short fields keep each line within the separate line bound.
        data=request(headers=b''.join(b'X: '+b'x'*200+b'\r\n' for _ in range(4)))
        self.assertEqual(self.http(data)[0],200)
        rng=random.Random(8321)
        for _ in range(500):
            data=bytes(rng.randrange(256) for _ in range(rng.randrange(1100)))
            result,state=self.http(data)
            self.assertIn(result,[0,400,408,431])
            self.assertLessEqual(state.used,1024)

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

    def test_http_independent_contexts_and_exact_capacity(self):
        first=Http()
        second=Http()
        self.init(c.byref(first),SECRET,len(SECRET),0)
        self.init(c.byref(second),b'x'*32,32,0)
        data=request()
        split=len(data)//2
        self.assertEqual(self.feed(c.byref(first),data[:split],split,1),0)
        self.assertEqual(self.feed(c.byref(second),data,len(data),1),401)
        self.assertEqual(self.feed(c.byref(first),data[split:],len(data)-split,2),200)
        # Exactly 1024 bytes with all lines <=256 and no more than 16 fields.
        remaining=1024-len(request())
        fields=[]
        while remaining:
            field_size=min(remaining,240)
            if remaining-field_size in range(1,5):
                field_size-=5
            self.assertGreaterEqual(field_size,5)
            fields.append(b'X: '+b'x'*(field_size-5)+b'\r\n')
            remaining-=field_size
        exact=request(headers=b''.join(fields))
        self.assertEqual(len(exact),1024)
        self.assertEqual(self.http(exact)[0],200)
        self.assertEqual(self.http(exact[:-2]+b'X\r\n')[0],431)

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

    def dispatch(self, path, capture, result=0, auth=SECRET):
        _,http=self.http(request(path,auth=auth))
        info=Info(Meta(0x1234,12,4),11,1,True,True,b'node-01')
        output=(c.c_uint8*512)()
        size=c.c_size_t()
        callback=CAPTURE(capture)
        self.assertEqual(self.reply(c.byref(http),c.byref(info),callback,None,
                                    output,512,c.byref(size)),0)
        headers,body=bytes(output[:size.value]).split(b'\r\n\r\n',1)
        self.assertIn(b'Content-Length: '+str(len(body)).encode(),headers)
        self.assertIn(b'Connection: close',headers)
        return headers,body

    def test_largest_read_metadata_fits_fixed_512_byte_response(self):
        info=Info(Meta(2**64-1,2**32-1,2**32-1),2**32-1,2**32-1,False,False,b'n'*31,
                  BrokerView(6,2**32-1,2**32-1,2**32-1),False)
        callback=CAPTURE(lambda *args: -1)
        for path in [b'/v1/status',b'/v1/identity']:
            _,http=self.http(request(path))
            output=(c.c_uint8*512)()
            size=c.c_size_t()
            self.assertEqual(self.reply(c.byref(http),c.byref(info),callback,None,
                                        output,512,c.byref(size)),0)
            self.assertLessEqual(size.value,512)
            headers,body=bytes(output[:size.value]).split(b'\r\n\r\n',1)
            self.assertIn(b'200',headers)
            parsed=json.loads(body)
            if path==b'/v1/status':
                self.assertEqual(parsed['mqtt'],dict(enabled=False,state=6,failures=2**32-1,replaced=2**32-1,discarded=2**32-1))

    def test_http_dispatch_identity_status_auth_and_busy(self):
        def forbidden(*args):
            self.fail('capture should not run')
            return -1
        headers,body=self.dispatch(b'/v1/status',forbidden)
        self.assertIn(b'200',headers)
        self.assertEqual(json.loads(body)['saved_revision'],11)
        _,body=self.dispatch(b'/v1/identity',forbidden)
        self.assertFalse(json.loads(body)['configuration_supported'])
        headers,_=self.dispatch(b'/v1/status',forbidden,auth=None)
        self.assertIn(b'401',headers)
        self.assertIn(b'WWW-Authenticate: Bearer',headers)
        for result,status in [(1,404),(-1,503)]:
            headers,_=self.dispatch(b'/v1/telemetry?source=2',lambda *args:result)
            self.assertIn(str(status).encode(),headers)

    def test_actual_cache_sample_reaches_http_and_ble_without_dequeue(self):
        self.lib.sampcache_reset()
        source=Source(b'pressure',0,1,2,1000)
        self.assertEqual(self.lib.sampcache_expect(c.byref(source)),0)
        sample=Environment(Header(100,7,2,1),100820.5,26.25)
        self.lib.sampcache_record(0,1,c.byref(sample),c.sizeof(sample),150)
        def capture(context, source_index, entry, now):
            now[0]=150
            return self.lib.sampcache_source(source_index,entry)
        headers,body=self.dispatch(b'/v1/telemetry?source=2',capture)
        self.assertIn(b'application/octet-stream',headers)
        self.assertEqual(len(body),112)
        self.assertEqual(struct.unpack_from('<ff',body,76),(100820.5,26.25))
        entry=Entry()
        self.lib.sampcache_source(2,c.byref(entry))
        expected=(c.c_uint8*112)()
        meta=Meta(0x1234,12,4)
        self.assertEqual(self.encode(c.byref(entry),c.byref(meta),150,expected,112),0)
        self.assertEqual(body,bytes(expected))
        again=Entry()
        self.lib.sampcache_source(2,c.byref(again))
        self.assertEqual(bytes(entry),bytes(again))

    def test_actual_light_cache_reaches_authenticated_http_without_consumption(self):
        self.lib.sampcache_reset()
        source = Source(b'room-light', 0, 5, 2, 1000)
        self.assertEqual(self.lib.sampcache_expect(c.byref(source)), 0)
        sample = Light(Header(100, 7, 2, 1), 123456)
        self.lib.sampcache_record(0, 5, c.byref(sample), c.sizeof(sample), 150)
        def capture(context, source_index, entry, now):
            now[0] = 150
            return self.lib.sampcache_source(source_index, entry)
        headers, body = self.dispatch(b'/v1/telemetry?source=2', capture)
        self.assertIn(b'application/octet-stream', headers)
        self.assertEqual(body[1:4], bytes([5, 1, 1]))
        self.assertEqual(struct.unpack_from('<I', body, 76)[0], 123456)
        self.assertEqual(body[80:], bytes(32))
        entry, again = Entry(), Entry()
        self.lib.sampcache_source(2, c.byref(entry))
        headers, repeated = self.dispatch(b'/v1/telemetry?source=2', capture)
        self.lib.sampcache_source(2, c.byref(again))
        self.assertEqual(repeated, body)
        self.assertEqual(bytes(entry), bytes(again))

    def test_reply_short_buffer_no_partial_write_and_binary_nul(self):
        output=(c.c_uint8*256)(*([0xDA]*256))
        size=c.c_size_t(99)
        self.assertEqual(self.http_reply(200,True,b'a\x00b',3,output,10,c.byref(size)),-1)
        self.assertEqual(bytes(output),b'\xda'*256)
        self.assertEqual(size.value,99)
        self.assertEqual(self.http_reply(200,True,b'a\x00b',3,output,256,c.byref(size)),0)
        self.assertTrue(bytes(output[:size.value]).endswith(b'a\x00b'))

if __name__=='__main__':
    unittest.main()
