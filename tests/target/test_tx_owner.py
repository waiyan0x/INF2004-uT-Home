"""Actual application transaction owner and real HALs with explicit bus doubles."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from tools.target.transaction import request, result

ROOT=Path(__file__).resolve().parents[2]

def profile(protocol='i2c', **values):
    return dict(version=1,device='sensor',revision=1,protocol=protocol,
                address=119 if protocol=='i2c' else 0, mode=0, dc_pin=255,
                cs_pin=4 if protocol=='spi' else 255, dc_level=0,
                rate=256000 if protocol=='uart' else 100000, timeout_ms=500,
                write_hex='00' if protocol!='uart' else '',
                read_length=0 if protocol=='spi' else 1, **values)

class OwnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='uthome-owner-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.folder=Path(cls.temp.name)
        (cls.folder/'netsetup.h').write_text('#define UTHOME_SETUP_MQTT 1\n')
        sources=['firmware/app/c3tx.c','firmware/src/services/transaction/txjob.c',
                 'firmware/src/services/transaction/txport.c','firmware/hal/i2cbus.c',
                 'firmware/hal/spibus.c','firmware/hal/uartio.c',
                 'firmware/hal/ports/host_sim/i2cfake.c',
                 'firmware/hal/ports/host_sim/spifake.c','firmware/hal/ports/host_sim/uartfake.c',
                 'tests/target/txptest.c']
        includes={ROOT/'firmware/include', ROOT/'firmware/third_party/bmp3', cls.folder, ROOT/'tests/target'}
        includes.update(path.parent for path in (ROOT/'firmware').rglob('*.h') if 'third_party' not in str(path))
        aliases={'c3bind_operations':'txptest_bind','cfgbind_discovery_ready':'txptest_idle',
                 'cfgmux_busy':'txptest_mux','c3uart_tx_empty':'txptest_empty',
                 'c3mqtt_request':'txptest_request','c3mqtt_result':'txptest_result'}
        cls.binary=cls.folder/'owner.so'
        subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra','-Werror','-pedantic',
                        '-shared',*(['-fPIC'] if sys.platform!='win32' else []),
                        '-DUTHOME_NETWORK_WIFI=1',*['-D'+a+'='+b for a,b in aliases.items()],
                        *['-I'+str(p) for p in sorted(includes)],
                        *[str(ROOT/p) for p in sources],'-o',str(cls.binary)],check=True)
        cls.serial=0

    def setUp(self):
        type(self).serial+=1
        path=self.folder/f'owner-{self.serial}.so';shutil.copy2(self.binary,path)
        self.lib=c.CDLL(str(path))
        self.lib.txptest_send.argtypes=[c.c_char_p,c.c_size_t]
        self.lib.txptest_feed.argtypes=[c.c_char_p,c.c_size_t]
        self.lib.txptest_reply.argtypes=[c.c_void_p]
        self.lib.txptest_pump.argtypes=[c.c_uint64,c.c_bool,c.c_bool]
        self.lib.txptest_drain.argtypes=[c.c_bool]
        self.assertEqual(self.lib.txptest_start(),0)
        self.now=0;self.rid=0

    def pump(self, idle=True, online=True):
        self.now+=1;self.lib.txptest_pump(self.now,online,idle)

    def exchange(self, spec, execute=False, idle=True, feed=None):
        self.rid+=1
        self.assertEqual(self.lib.txptest_send(request(spec,self.rid,4,execute=execute),96),0)
        self.pump(idle=idle)
        if feed is not None: self.assertEqual(self.lib.txptest_feed(feed,len(feed)),0)
        output=c.create_string_buffer(96)
        for _ in range(600):
            self.pump(idle=idle)
            if self.lib.txptest_reply(output)==0:
                return result(output.raw,request_id=self.rid,generation=4)
        self.fail('no reply before transaction deadline')

    def test_i2c_rules_select_actual_registers_and_release_claims(self):
        a=profile(); self.assertEqual(self.exchange(a)['status'],'ok')
        self.assertEqual(self.exchange(a,True)['raw_hex'],'50')
        b={**a,'revision':2,'write_hex':'03'}
        self.assertEqual(self.exchange(b)['status'],'ok')
        self.assertEqual(self.exchange(b,True)['raw_hex'],'70')
        self.assertEqual(self.lib.txptest_observed(0),2)
        self.assertFalse(self.lib.c3tx_busy())

    def test_spi_executes_configured_bytes_and_dc_level(self):
        a={**profile('spi'),'write_hex':'AE','dc_pin':3,'dc_level':0}
        self.exchange(a)
        reply=self.exchange(a,True)
        self.assertEqual((reply['status'],reply['tx_accepted'],reply['raw_hex']),('ok',1,''))
        self.assertEqual(self.lib.txptest_observed(3),0xae)
        self.assertEqual(self.lib.txptest_observed(4),0)
        b={**a,'revision':2,'write_hex':'AF'}
        self.exchange(b);self.exchange(b,True)
        self.assertEqual(self.lib.txptest_observed(1),2)

    def test_uart_assembles_partial_bytes_without_waiting_or_interpreting_frames(self):
        spec={**profile('uart'),'read_length':4}
        self.exchange(spec)
        reply=self.exchange(spec,True,feed=b'\xaa\xff\x03\x00')
        self.assertEqual(reply['raw_hex'],'AA FF 03 00')
        self.assertEqual(self.lib.txptest_observed(2),4)
        self.assertFalse(self.lib.c3tx_busy())

    def test_full_raw_result_text_fits_native_frame_and_expires(self):
        spec={**profile('uart'),'read_length':32}
        self.exchange(spec)
        reply=self.exchange(spec,True,feed=bytes(range(32)))
        self.assertEqual(len(reply['raw_hex'].split()),32)
        self.lib.c3tx_view.restype=c.c_void_p
        self.lib.txjob_text.argtypes=[c.c_void_p,c.c_uint64,c.c_void_p,c.c_size_t]
        output=c.create_string_buffer(120)
        view=self.lib.c3tx_view()
        self.assertEqual(self.lib.txjob_text(view,self.now,output,120),0)
        self.assertLessEqual(len(output.value),116)
        self.assertIn(b'UART OK',output.value)
        self.assertEqual(self.lib.txjob_text(view,self.now+30001,output,120),1)
        self.assertEqual(output.value,b'')

    def test_nonidle_plan_never_touches_hardware_and_uart_timeout_cleans_up(self):
        self.assertEqual(self.exchange(profile(),idle=False)['status'],'busy')
        self.assertEqual(self.lib.txptest_observed(0),0)
        spec=profile('uart');self.exchange(spec)
        self.assertEqual(self.exchange(spec,True)['status'],'timeout')
        self.assertFalse(self.lib.c3tx_busy())

    def test_uart_write_waits_for_ring_and_fifo_budget_before_close(self):
        spec={**profile('uart'),'write_hex':'01 02','read_length':0}
        self.exchange(spec)
        self.lib.txptest_drain(False)
        self.rid+=1;self.lib.txptest_send(request(spec,self.rid,4,execute=True),96)
        for _ in range(5):self.pump()
        self.assertTrue(self.lib.c3tx_busy())
        self.assertEqual(self.lib.txptest_observed(5),2)
        self.lib.txptest_drain(True)
        for _ in range(15):self.pump()
        output=c.create_string_buffer(96)
        self.assertEqual(self.lib.txptest_reply(output),0)
        self.assertEqual(result(output.raw)['tx_accepted'],2)
        self.assertFalse(self.lib.c3tx_busy())

    def test_disconnect_cancels_uart_without_restarting_it_on_reconnect(self):
        spec=profile('uart');self.exchange(spec)
        self.rid+=1;self.lib.txptest_send(request(spec,self.rid,4,execute=True),96)
        self.pump();self.pump(online=False)
        self.assertFalse(self.lib.c3tx_busy())
        for _ in range(4):self.pump()
        output=c.create_string_buffer(96)
        self.assertEqual(self.lib.txptest_reply(output),0)
        self.assertEqual(result(output.raw)['status'],'cancelled')

if __name__=='__main__':unittest.main()
