"""Execute the real UART adapter against an owned, fault-injectable boundary."""
import ctypes as ct
import _ctypes
from contextlib import contextmanager
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / 'firmware/hal/ports/esp32c3'
INIT, LOCK, UNLOCK, START, STOP, ATTR, FAULT, RX, TX, DROP, READ, WRITE = range(1, 13)
IO, STATE, AGAIN, INVALID = -3, -2, 1, -1


class Controls(ct.Structure):
    _fields_ = [(name, ct.c_bool) for name in
                ('owner', 'cleanup', 'valid', 'running', 'held', 'refill', 'stuck', 'invalid')]
    _fields_ += [(name, ct.c_uint32) for name in ('calls', 'fail_call')]
    _fields_ += [('error', ct.c_int32)]
    _fields_ += [(name, ct.c_uint32) for name in
                 ('rx_pending', 'tx_pending', 'tx_free', 'read_size', 'write_size', 'fault_calls')]
    _fields_ += [('fault_before', ct.c_int32), ('fault_after', ct.c_int32),
                ('events', ct.c_uint32 * 2048)]


class Config(ct.Structure):
    _fields_ = [('baud', ct.c_uint32), ('rx_buffer_bytes', ct.c_uint16)]
    _fields_ += [(name, ct.c_uint8) for name in
                ('port', 'tx_gpio', 'rx_gpio', 'data_bits', 'stop_bits', 'parity')]


Open = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.POINTER(Config))
Close = ct.CFUNCTYPE(ct.c_int, ct.c_void_p)
Bytes = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.POINTER(ct.c_uint8),
                    ct.c_size_t, ct.POINTER(ct.c_size_t))
Now = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.POINTER(ct.c_uint64))


class Operations(ct.Structure):
    _fields_ = [('open', Open), ('close', Close), ('read', Bytes),
                ('write', Bytes), ('flush', Close), ('now', Now)]


class Port(ct.Structure):
    _fields_ = [('operations', ct.POINTER(Operations)), ('context', ct.c_void_p),
                ('owner', ct.c_void_p), ('ready', ct.c_bool)]


class Adapter:
    def __init__(self, lib):
        self.lib = lib
        lib.ufake_state.restype = ct.POINTER(Controls)
        lib.c3uart_port.restype = ct.POINTER(Port)
        lib.c3uart_init.argtypes = []
        self.control = lib.ufake_state().contents
        self.port = lib.c3uart_port()
        self.operations = self.port.contents.operations.contents
        self.context = self.port.contents.context
        self.config = Config(115200, 512, 1, 21, 20, 8, 1, 0)
        self.data = (ct.c_uint8 * 64)()
        self.count = ct.c_size_t()
        self.identity = ct.c_uint8(1)
        self.owner = ct.cast(ct.pointer(self.identity), ct.c_void_p)
        lib.uartio_open.argtypes = [ct.POINTER(Port), ct.POINTER(Config), ct.c_void_p]
        lib.uartio_close.argtypes = [ct.POINTER(Port), ct.c_void_p]
        for name in ('read', 'write'):
            getattr(lib, 'uartio_' + name).argtypes = [ct.POINTER(Port), ct.c_void_p,
                ct.POINTER(ct.c_uint8), ct.c_size_t, ct.POINTER(ct.c_size_t)]

    @property
    def events(self):
        return list(self.control.events[:self.control.calls])

    def clear(self):
        self.control.calls = self.control.fail_call = self.control.fault_calls = 0

    def start(self):
        result = self.lib.c3uart_init()
        if result:
            raise AssertionError(('startup', result, self.events))
        self.clear()

    def open(self):
        return self.operations.open(self.context, ct.byref(self.config))

    def close(self):
        return self.operations.close(self.context)

    def read(self):
        self.count.value = 0
        return self.operations.read(self.context, self.data, 64, ct.byref(self.count))


class UartStartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler, objcopy = shutil.which('gcc'), shutil.which('objcopy')
        if not compiler or not objcopy:
            raise AssertionError('GCC and objcopy are required')
        subprocess.run([sys.executable, str(ROOT/'tools/c_standard/check.py')], check=True)
        cls.folder = tempfile.TemporaryDirectory(prefix='uthome-uart-adapter-')
        cls.addClassCleanup(cls.folder.cleanup)
        cls.directory = Path(cls.folder.name)
        includes = [PORT, ROOT/'firmware/hal/include', ROOT/'firmware/src/runtime',
                    ROOT/'firmware/include', ROOT/'tests/target']
        flags = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2']
        if sys.platform != 'win32':
            flags.append('-fPIC')
        for path in includes:
            flags.extend(['-I', str(path)])
        obj = cls.directory/'c3uart.o'
        subprocess.run([compiler, *flags, '-c', str(PORT/'c3uart.c'), '-o', str(obj)], check=True)
        native = ('init', 'valid', 'capacity', 'lock', 'unlock', 'start', 'stop',
                  'attributes', 'fault', 'available', 'discard_tx', 'read', 'write')
        aliases = {f'c3ukern_{name}': f'ufake_{name}' for name in native}
        aliases.update(rt_port_is_owner='ufake_owner', rt_port_is_cleanup_owner='ufake_cleanup',
                       rt_port_now_ms='ufake_now', c3cost_begin='ufake_begin',
                       c3cost_acquired='ufake_acquired', c3cost_end='ufake_end')
        command = [objcopy]
        for source, target in aliases.items():
            command.extend(['--redefine-sym', f'{source}={target}'])
        subprocess.run([*command, str(obj)], check=True)
        cls.binary = cls.directory/'adapter.dll'
        subprocess.run([compiler, *flags, '-shared', str(obj),
                        str(ROOT/'firmware/hal/uartio.c'), str(ROOT/'tests/target/ufake.c'),
                        '-o', str(cls.binary)], check=True)
        cls.serial = 0

    @contextmanager
    def adapter(self):
        type(self).serial += 1
        path = self.directory / f'boot-{self.serial}.dll'
        shutil.copyfile(self.binary, path)
        lib = ct.CDLL(str(path))
        adapter = Adapter(lib)
        try:
            yield adapter
            self.assertFalse(adapter.control.invalid, 'fixture bounds/call capacity exceeded')
        finally:
            if sys.platform == 'win32':
                _ctypes.FreeLibrary(lib._handle)
            else:
                _ctypes.dlclose(lib._handle)
            path.unlink()

    def test_factory_has_no_effect_and_startup_is_once(self):
        with self.adapter() as a:
            self.assertEqual([], a.events)
            a.control.owner = False
            self.assertEqual(STATE, a.lib.c3uart_init())
            self.assertEqual([], a.events)
            a.control.owner = True
            self.assertEqual(0, a.lib.c3uart_init())
            self.assertEqual([INIT, STOP, START, STOP, DROP, RX, FAULT], a.events)
            self.assertFalse(a.control.running)
            before = a.events
            self.assertEqual(0, a.lib.c3uart_init())
            self.assertEqual(before, a.events)

    def test_failed_startup_never_retries_any_resource_or_opens(self):
        for call in (1, 2, 3, 4, 6, 7):
            with self.subTest(call=call), self.adapter() as a:
                a.control.fail_call = call
                self.assertEqual(IO, a.lib.c3uart_init())
                before = a.events
                a.control.fail_call = 0
                self.assertEqual(STATE, a.lib.c3uart_init())
                self.assertEqual(STATE, a.open())
                self.assertEqual(before, a.events)
                if call == 3:
                    self.assertEqual(STOP, a.events[-1], 'failed START must still be followed by STOP')

    def test_open_preconditions_do_not_touch_native_state(self):
        with self.adapter() as a:
            self.assertEqual(STATE, a.open())
            self.assertEqual([], a.events)
            a.start()
            a.control.owner = False
            self.assertEqual(STATE, a.open())
            a.control.owner = True
            a.control.valid = False
            self.assertEqual(INVALID, a.open())
            self.assertEqual([], a.events)

    def test_open_close_order_and_many_cycles_keep_boot_resources(self):
        with self.adapter() as a:
            a.start()
            for _ in range(50):
                a.clear()
                self.assertEqual(0, a.open())
                self.assertEqual([LOCK, STOP, START, STOP, DROP, RX, FAULT, ATTR, START, UNLOCK], a.events)
                self.assertTrue(a.control.running)
                self.assertFalse(a.control.held)
                self.assertEqual(STATE, a.open())
                a.clear()
                self.assertEqual(0, a.close())
                self.assertEqual([LOCK, STOP, START, STOP, DROP, RX, FAULT, UNLOCK], a.events)
                self.assertFalse(a.control.running)

    def test_open_failures_retain_claim_until_successful_cleanup(self):
        for call in (2, 3, 4, 6, 7, 8, 9):
            with self.subTest(call=call), self.adapter() as a:
                a.start()
                a.control.fail_call = call
                self.assertEqual(IO, a.open())
                self.assertEqual([STOP, UNLOCK], a.events[-2:])
                self.assertFalse(a.control.running)
                self.assertEqual(STATE, a.open())
                self.assertEqual(STATE, a.read())
                a.clear()
                self.assertEqual(0, a.close())
                self.assertEqual(0, a.open())

    def test_failed_locks_return_immediately_without_claiming(self):
        for error in (AGAIN, IO):
            with self.subTest(error=error), self.adapter() as a:
                a.start()
                a.control.error, a.control.fail_call = error, 1
                self.assertEqual(error, a.open())
                self.assertEqual([LOCK], a.events)
                a.clear()
                self.assertEqual(0, a.open())

    def test_close_failure_retains_ownership_but_blocks_transfers(self):
        for call in (2, 3, 4, 6, 7):
            with self.subTest(call=call), self.adapter() as a:
                a.start()
                self.assertEqual(0, a.open())
                a.clear()
                a.control.fail_call = call
                self.assertEqual(IO, a.close())
                self.assertEqual(UNLOCK, a.events[-1])
                self.assertEqual(STATE, a.open())
                self.assertEqual(STATE, a.read())
                a.clear()
                self.assertEqual(0, a.close())
                self.assertEqual(0, a.open())

    def test_unlock_failure_stops_and_permanently_poisons(self):
        for operation, call in (('open', 10), ('close', 8), ('read', 5)):
            with self.subTest(operation=operation), self.adapter() as a:
                a.start()
                if operation != 'open':
                    self.assertEqual(0, a.open())
                a.clear()
                a.control.fail_call = call
                self.assertEqual(IO, getattr(a, operation)())
                self.assertEqual([UNLOCK, STOP], a.events[-2:])
                self.assertFalse(a.control.running)
                before = a.events
                self.assertEqual(STATE, a.open())
                self.assertEqual(STATE, a.close())
                self.assertEqual(STATE, a.read())
                self.assertEqual(before, a.events)

    def test_cleanup_owner_may_close_but_cannot_open(self):
        with self.adapter() as a:
            a.start()
            self.assertEqual(0, a.open())
            a.clear()
            a.control.owner = False
            self.assertEqual(STATE, a.close())
            self.assertEqual([], a.events)
            a.control.cleanup = True
            self.assertEqual(0, a.close())
            self.assertEqual(STATE, a.open())

    def test_reset_masks_interrupts_before_bounded_ring_discard(self):
        with self.adapter() as a:
            a.control.tx_pending = 512
            a.control.rx_pending = 130
            self.assertEqual(0, a.lib.c3uart_init())
            self.assertEqual([INIT, STOP, START, STOP], a.events[:4])
            self.assertEqual(513, a.events.count(DROP))
            self.assertEqual(3, a.events.count(READ))
            self.assertEqual(130, a.control.read_size)
            self.assertEqual(0, a.control.rx_pending)
        with self.adapter() as a:
            a.control.stuck = True
            self.assertEqual(IO, a.lib.c3uart_init())
            self.assertEqual(513, a.events.count(DROP))
            self.assertNotIn(RX, a.events)

    def test_flush_discards_entry_snapshot_even_when_rx_refills(self):
        with self.adapter() as a:
            a.start()
            self.assertEqual(0, a.open())
            a.clear()
            a.control.rx_pending, a.control.refill = 130, True
            self.assertEqual(0, a.operations.flush(a.context))
            self.assertEqual(3, a.events.count(READ))
            self.assertEqual(130, a.control.read_size)
            self.assertEqual(130, a.control.rx_pending)

    def test_read_faults_before_or_after_rx_discard_entire_batch(self):
        for field in ('fault_before', 'fault_after'):
            for fault in (-3, -4, -5):
                with self.subTest(field=field, fault=fault), self.adapter() as a:
                    a.start()
                    self.assertEqual(0, a.lib.uartio_open(a.port, ct.byref(a.config), a.owner))
                    a.clear()
                    a.control.rx_pending = 10
                    setattr(a.control, field, fault)
                    self.assertEqual(fault, a.lib.uartio_read(a.port, a.owner, a.data, 64, ct.byref(a.count)))
                    self.assertEqual(0, a.count.value)
                    self.assertEqual(field == 'fault_after', READ in a.events)
                    self.assertEqual(UNLOCK, a.events[-1])

    def test_partial_write_failure_preserves_accepted_count(self):
        with self.adapter() as a:
            a.start()
            self.assertEqual(0, a.lib.uartio_open(a.port, ct.byref(a.config), a.owner))
            a.clear()
            a.control.tx_free, a.control.fail_call = 10, 3
            self.assertEqual(IO, a.lib.uartio_write(a.port, a.owner, a.data, 64, ct.byref(a.count)))
            self.assertEqual([LOCK, TX, WRITE, UNLOCK], a.events)
            self.assertEqual(5, a.count.value)

    def test_public_transport_retains_owner_on_failed_close(self):
        with self.adapter() as a:
            a.start()
            self.assertEqual(0, a.lib.uartio_open(a.port, ct.byref(a.config), a.owner))
            a.clear()
            a.control.fail_call = 2
            self.assertEqual(IO, a.lib.uartio_close(a.port, a.owner))
            self.assertEqual(a.owner.value, a.port.contents.owner)
            self.assertFalse(a.port.contents.ready)
            a.clear()
            self.assertEqual(0, a.lib.uartio_close(a.port, a.owner))
            self.assertIsNone(a.port.contents.owner)


class UartStructureTests(unittest.TestCase):
    def test_only_native_boundary_may_create_and_neither_layer_deletes(self):
        adapter = (PORT/'c3uart.c').read_text()
        native = (PORT/'c3ukern.c').read_text()
        for operation in ('tk_cre_mtx(', 'tk_opn_dev('):
            self.assertNotIn(operation, adapter)
            self.assertEqual(1, native.count(operation))
        for operation in ('tk_cls_dev(', 'tk_del_mtx(', 'malloc('):
            self.assertNotIn(operation, adapter + native)

    def test_startup_precedes_activation_and_is_not_lazy(self):
        application = (ROOT/'firmware/app/c3app.c').read_text()
        self.assertLess(application.index('activate_init()'), application.index('c3uart_init()'))
        self.assertLess(application.index('c3uart_init()'), application.index('cfgctrl_restore('))
        for path in ('firmware/src/config/cfgbind.c', 'firmware/src/config/cfgctrl.c',
                     'firmware/hal/ports/esp32c3/c3bind.c'):
            self.assertNotIn('c3uart_init(', (ROOT/path).read_text())


if __name__ == '__main__':
    unittest.main()
