"""Real controller/pump/store/JSON parser with explicit host I/O callbacks."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
READ=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_uint,c.c_size_t,c.c_void_p,c.c_size_t)
WRITE=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_uint,c.c_size_t,c.c_void_p,c.c_size_t)
ERASE=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_uint)
SYNC=c.CFUNCTYPE(c.c_int,c.c_void_p)
VALIDATE=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_void_p,c.c_size_t)
ACTION=c.CFUNCTYPE(c.c_int,c.c_void_p)
IOREAD=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t))
IOWRITE=IOREAD
class StoreOps(c.Structure):
    _fields_=[('read',READ),('write',WRITE),('erase',ERASE),('sync',SYNC)]
class Store(c.Structure):
    _fields_=[('ops',c.c_void_p),('context',c.c_void_p),('slot_bytes',c.c_size_t),
              ('workspace',c.c_void_p),('workspace_size',c.c_size_t),('busy',c.c_bool)]
class ControlOps(c.Structure):
    _fields_=[('validate',VALIDATE),('activate',ACTION),('recover',ACTION)]
class Control(c.Structure):
    _fields_=[('ops',c.c_void_p),('context',c.c_void_p),('store',c.c_void_p),
              ('active',c.c_uint8*8192),('cached',c.c_uint8*8192),
              ('active_size',c.c_size_t),('cached_size',c.c_size_t),
              ('active_revision',c.c_uint32),('active_crc',c.c_uint32),
              ('saved_revision',c.c_uint32),('saved_crc',c.c_uint32),
              ('last_id',c.c_uint32),('last_revision',c.c_uint32),('last_command',c.c_int),
              ('reply',c.c_uint32*6),('restore_attempted',c.c_bool),
              ('initialized',c.c_bool),('busy',c.c_bool),('recovery',c.c_bool),('saved_known',c.c_bool)]
class Receiver(c.Structure):
    _fields_=[('header',c.c_uint8*24),('data',c.c_uint8*8192),
              ('header_used',c.c_size_t),('used',c.c_size_t),('expected',c.c_size_t),
              ('start',c.c_uint64),('last',c.c_uint64),('ready',c.c_bool)]
class PumpOps(c.Structure):
    _fields_=[('read',IOREAD),('write',IOWRITE)]
class Pump(c.Structure):
    _fields_=[('receiver',Receiver),('control',c.c_void_p),('ops',c.c_void_p),
              ('context',c.c_void_p),('received',c.c_uint8*64),('reply',c.c_uint8*2072),
              ('reply_size',c.c_size_t),('observe',c.c_void_p),('observer',c.c_void_p),
              ('capabilities',c.c_void_p),('cap_context',c.c_void_p),('discover',c.c_void_p),
              ('disc_context',c.c_void_p),('workcost',c.c_void_p),('work_context',c.c_void_p),
              ('received_size',c.c_size_t),('received_used',c.c_size_t),('sent',c.c_size_t),
              ('reply_start',c.c_uint64),('pending',c.c_bool),('busy',c.c_bool)]
class Mux(c.Structure):
    _fields_=[('control',c.c_void_p),('pump',c.c_void_p),('upload_token',c.c_void_p),
              ('boot',c.c_uint64),('reply',c.c_uint32*6),
              *[(name,c.c_uint32) for name in ('request_id','revision','expected','size',
                 'upload_id','upload_revision','upload_expected','upload_size','used')],
              ('token',c.c_char*65),('initialized',c.c_bool),('boot_set',c.c_bool),
              ('lease',c.c_bool),('retry',c.c_bool),('result',c.c_bool)]

class ConfigLibrary:
    def __init__(self):
        self.folder=tempfile.TemporaryDirectory(prefix='uthome-config-mux-')
        binary=Path(self.folder.name)/'config.dll'
        config=ROOT/'firmware/src/config'
        sources=[config/(name+'.c') for name in ['cfgmux','cfgctrl','cfgpump','cfgstore',
                                                'cfgwire','cfgbytes','cfgjson','jsonscan']]
        sources.append(ROOT/'firmware/third_party/cjson/cJSON.c')
        result=subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra','-Werror',
             '-pedantic','-shared',*(['-fPIC'] if sys.platform!='win32' else []),*(['-Wl,--export-all-symbols'] if sys.platform=='win32' else []),'-O2','-I'+str(config),
             '-I'+str(ROOT/'firmware/third_party/cjson'),*map(str,sources),'-o',str(binary)],
             capture_output=True,text=True)
        if result.returncode:
            raise AssertionError(result.stdout+result.stderr)
        self.lib=c.CDLL(str(binary))
        specs={'cfgctrl_init':[c.c_void_p,c.c_void_p,c.c_void_p,c.c_void_p],
               'cfgpump_init':[c.c_void_p,c.c_void_p,c.c_void_p,c.c_void_p],
               'cfgpump_step':[c.c_void_p,c.c_uint64],
               'cfgmux_init':[c.c_void_p,c.c_void_p,c.c_void_p],
               'cfgmux_boot':[c.c_void_p,c.c_uint64],
               'cfgmux_begin':[c.c_void_p,c.c_uint64,c.c_uint32,c.c_uint32,c.c_uint32,c.c_char_p,c.c_uint32],
               'cfgmux_write':[c.c_void_p,c.c_char_p,c.c_size_t],
               'cfgmux_finish':[c.c_void_p,c.c_void_p],
               'cfgmux_abort':[c.c_void_p],
               'cfgmux_result':[c.c_void_p,c.c_uint32,c.c_char_p,c.c_void_p],
               'cfgjson_open':[c.c_void_p,c.c_size_t],
               'cfgjson_object':[c.c_void_p,c.POINTER(c.c_char_p),c.c_size_t,c.c_uint32],
               'cfgjson_get':[c.c_void_p,c.c_char_p],
               'cfgjson_uint':[c.c_void_p,c.c_uint32,c.c_uint32,c.c_void_p]}
        for name,args in specs.items():getattr(self.lib,name).argtypes=args
        self.lib.cfgjson_open.restype=c.c_void_p
        self.lib.cfgjson_get.restype=c.c_void_p
        self.lib.cfgjson_object.restype=c.c_bool
        self.lib.cfgjson_uint.restype=c.c_bool
        assert self.lib.cfgjson_init()==0
    def close(self):
        import _ctypes
        if sys.platform=='win32':
            _ctypes.FreeLibrary(self.lib._handle)
        self.folder.cleanup()

class Fixture:
    def __init__(self,library):
        self.lib=library.lib
        self.slots=[(c.c_uint8*8224)(*([255]*8224)) for _ in range(2)]
        self.validations=self.activations=self.writes=self.syncs=self.read_calls=0
        self.fail_sync=0;self.read_error=False;self.activation_error=0
        self.rx=bytearray();self.tx=bytearray();self.tokens=[];self.now=0
        self.during_validate=None
        def read(context,slot,offset,data,size):
            if self.read_error or slot>=2 or offset+size>8224:return -1
            c.memmove(data,c.addressof(self.slots[slot])+offset,size);return 0
        def write(context,slot,offset,data,size):
            if slot>=2 or offset+size>8224:return -1
            self.writes+=1;c.memmove(c.addressof(self.slots[slot])+offset,data,size);return 0
        def erase(context,slot):
            if slot>=2:return -1
            c.memset(self.slots[slot],255,8224);return 0
        def sync(context):
            self.syncs+=1;return -1 if self.syncs==self.fail_sync else 0
        def validate(context,data,size):
            self.validations+=1
            if self.during_validate:self.during_validate()
            tree=self.lib.cfgjson_open(data,size)
            if not tree:return -1
            try:
                keys=(c.c_char_p*1)(b'value');value=c.c_uint32()
                valid=self.lib.cfgjson_object(tree,keys,1,1) and self.lib.cfgjson_uint(
                    self.lib.cfgjson_get(tree,b'value'),0,100,c.byref(value))
                return 0 if valid else -1
            finally:self.lib.cfgjson_close()
        def activate(context):
            self.activations+=1;return self.activation_error
        def recover(context):return 0
        def usb_read(context,data,size,count):
            self.read_calls+=1;count[0]=min(size,len(self.rx))
            c.memmove(data,bytes(self.rx[:count[0]]),count[0]);del self.rx[:count[0]];return 0
        def usb_write(context,data,size,count):
            self.tx.extend(c.string_at(data,size));count[0]=size;return 0
        self.store_ops=StoreOps(READ(read),WRITE(write),ERASE(erase),SYNC(sync))
        self.ops=ControlOps(VALIDATE(validate),ACTION(activate),ACTION(recover))
        self.pump_ops=PumpOps(IOREAD(usb_read),IOWRITE(usb_write))
        self.control=Control();self.pump=Pump();self.mux=Mux()
        self.store=Store(c.addressof(self.store_ops),None,8224,
                         c.addressof(self.control.cached),8192)
        assert self.lib.cfgctrl_init(c.byref(self.control),c.byref(self.store),c.byref(self.ops),None)==0
        assert self.lib.cfgpump_init(c.byref(self.pump),c.byref(self.control),c.byref(self.pump_ops),None)==0
        assert self.lib.cfgmux_init(c.byref(self.mux),c.byref(self.control),c.byref(self.pump))==0
        assert self.lib.cfgmux_boot(c.byref(self.mux),1)==0
        self.last_fields=None
    def begin(self,id=1,revision=1,expected=0,token=b'op',size=11,boot=1):
        self.tokens.append(token)
        return self.lib.cfgmux_begin(c.byref(self.mux),boot,id,revision,expected,token,size)
    def write(self,data):return self.lib.cfgmux_write(c.byref(self.mux),data,len(data))
    def finish(self):
        fields=(c.c_uint32*6)(*([0xffffffff]*6))
        status=self.lib.cfgmux_finish(c.byref(self.mux),fields)
        self.last_fields=list(fields);return status
    def apply(self,data=b'{"value":1}',**kwargs):
        status=self.begin(size=len(data),**kwargs)
        if status!=200:return status
        assert self.write(data)==200
        return self.finish()
    def result(self,id=1,token=b'op'):
        fields=(c.c_uint32*6)()
        status=self.lib.cfgmux_result(c.byref(self.mux),id,token,fields)
        return status,list(fields)
    def abort(self):self.lib.cfgmux_abort(c.byref(self.mux))
    def usb_step(self):
        self.now+=1
        # Exact application-level gate; USB pump implementation is unchanged.
        if self.mux.lease:return 0
        return self.lib.cfgpump_step(c.byref(self.pump),self.now)
    def usb(self,frame):
        self.rx.extend(frame);self.tx.clear()
        for _ in range(300):
            self.usb_step()
            if len(self.tx)>=48:return bytes(self.tx)
        raise AssertionError('USB reply not produced')
