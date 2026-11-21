"""Actual fixed-record adapter with SDK call/allocator doubles, not TLS crypto."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
OUTPUT_BYTES = 4429
INPUT_BYTES = 16717
NAME_BYTES = 254
CERTIFICATE_BYTES = 952
DIGEST_BYTES = 32
STATE_SIZES = {'transform_new': 84, 'session_new': 128, 'handshake': 2448,
               'transform': 84, 'session': 128}


class Context(c.Structure):
    _fields_ = [('input', c.c_void_p), ('output', c.c_void_p),
                ('hostname', c.c_void_p), ('flags', c.c_uint32),
                ('config', c.c_void_p), ('transform_new', c.c_void_p),
                ('session_new', c.c_void_p), ('handshake', c.c_void_p),
                ('transform', c.c_void_p), ('session', c.c_void_p),
                ('state', c.c_int), ('renegotiation', c.c_int)]


class Config(c.Structure):
    _fields_ = [('failure', c.c_uint32), ('certificate_case', c.c_uint32)]


class Derivation(c.Structure):
    _fields_ = [('alg', c.c_uint32), ('failure', c.c_uint32), ('input', c.c_void_p)]


class ImportAttributes(c.Structure):
    _fields_ = [('key_type', c.c_uint32), ('algorithm', c.c_uint32), ('usage', c.c_uint32),
                ('lifetime', c.c_uint32), ('copy', c.POINTER(Derivation))]


class Session(c.Structure):
    _fields_ = [('digest', c.c_void_p), ('bytes', c.c_uint8 * (128 - c.sizeof(c.c_void_p)))]


class TlsOutputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('gcc')
        objcopy = shutil.which('objcopy') or shutil.which('llvm-objcopy')
        if not compiler or not objcopy:
            raise AssertionError('GCC and objcopy are required')
        cls.temp = tempfile.TemporaryDirectory(prefix='uthome-tls-output-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        (cls.directory / 'mbedtls').mkdir()
        (cls.directory / 'psa').mkdir()
        (cls.directory / 'psa/crypto.h').write_text(
            '#include "touttest.h"\n'
            '#define psa_status_t int32_t\n'
            '#define psa_key_attributes_t touttest_attrs_t\n'
            '#define psa_get_key_type(attrs) ((attrs)->key_type)\n'
            '#define psa_get_key_algorithm(attrs) ((attrs)->algorithm)\n'
            '#define psa_get_key_usage_flags(attrs) ((attrs)->usage)\n'
            '#define psa_get_key_lifetime(attrs) ((attrs)->lifetime)\n'
            '#define PSA_KEY_TYPE_DERIVE 2U\n'
            '#define PSA_KEY_USAGE_DERIVE 8U\n'
            '#define PSA_KEY_LIFETIME_VOLATILE 0U\n'
            '#define psa_import_key touttest_import\n'
            '#define psa_key_derivation_operation_t touttest_derivation_t\n'
            '#define psa_key_derivation_step_t uint16_t\n'
            '#define mbedtls_svc_key_id_t uint32_t\n'
            '#define psa_key_derivation_input_key touttest_key\n'
            '#define PSA_ALG_IS_TLS12_PRF(alg) ((alg) == 1U)\n'
            '#define PSA_KEY_DERIVATION_INPUT_SECRET 4U\n'
            '#define PSA_KEY_DERIVATION_INPUT_SEED 2U\n'
            '#define PSA_KEY_DERIVATION_INPUT_LABEL 3U\n')
        # Foreign header spellings map to maintained, conventionally named test
        # types. The target build uses the full, unmodified SDK declarations.
        (cls.directory / 'mbedtls/ssl.h').write_text(
            '#include "touttest.h"\n#define MBEDTLS_PRIVATE(field) field\n'
            '#define MBEDTLS_ERR_SSL_BAD_INPUT_DATA (-135)\n'
            '#define MBEDTLS_SSL_PEER_CERT_DIGEST_DFL_LEN 32\n'
            '#define MBEDTLS_PREMASTER_SIZE 66U\n'
            '#define MBEDTLS_SSL_HANDSHAKE_OVER 16\n'
            '#define MBEDTLS_SSL_RENEGOTIATION_IN_PROGRESS 1\n')
        for header in ('mbedtls/platform.h', 'mbedtls/platform_util.h', 'mbedtls/x509_crt.h'):
            (cls.directory / header).write_text('#include "touttest.h"\n')
        (cls.directory / 'ssl_misc.h').write_text(
            '#define MBEDTLS_SSL_IN_BUFFER_LEN 16717\n'
            '#define MBEDTLS_SSL_OUT_BUFFER_LEN 4429\n'
            '#define MBEDTLS_SSL_PROTO_TLS1_2\n'
            '#define MBEDTLS_SSL_CLI_C\n'
            '#define MBEDTLS_SSL_RENEGOTIATION\n')
        port = ROOT / 'firmware/hal/ports/esp32c3/network'
        flags = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                 '-DUTHOME_NETWORK_WIFI=1', '-DUTHOME_STATIC_NET_QUEUES=1',
                 '-DMBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS',
                 '-Dpsa_key_derivation_input_bytes=touttest_input',
                 '-Dc3bport_task=touttest_task', '-Dc3bport_enter=touttest_enter',
                 '-Dc3bport_leave=touttest_leave',
                 '-I' + str(ROOT / 'firmware/src/runtime'),
                 '-Dmbedtls_ssl_context=touttest_context_t',
                 '-Dmbedtls_ssl_config=touttest_config_t',
                 '-Dmbedtls_ssl_transform=touttest_transform_t',
                 '-Dmbedtls_ssl_session=touttest_session_t',
                 '-Dmbedtls_ssl_handshake_params=touttest_handshake_t',
                 '-Dmbedtls_x509_crt=touttest_certificate_t',
                 '-Dmbedtls_asn1_sequence=touttest_sequence_t',
                 '-Dpeer_cert_digest=p_digest',
                 '-Din_buf=p_in_buf', '-Dout_buf=p_out_buf', '-Dhostname=p_hostname',
                 '-Dconf=p_config', '-Dtransform_negotiate=p_transform_new',
                 '-Dsession_negotiate=p_session_new', '-Dhandshake=p_handshake',
                 '-Drenego_status=renegotiation',
                 '-Dc3tout_native_setup=touttest_setup',
                 '-Dc3tout_native_name=touttest_name',
                 '-Dc3tout_native_start=touttest_start',
                 '-Dc3tout_native_renegotiate=touttest_renegotiate',
                 '-Dc3tout_native_certificate=touttest_certificate',
                 '-Dmbedtls_calloc=touttest_alloc',
                 '-Dmbedtls_free=touttest_free',
                 '-Dmbedtls_platform_zeroize=touttest_zeroize',
                 '-Dmbedtls_zeroize_and_free=touttest_wipe',
                 '-I' + str(cls.directory), '-I' + str(ROOT / 'tests/target'), '-I' + str(port)]
        if sys.platform != 'win32':
            flags += ['-fPIC']
        cls.command = [compiler, *flags, '-c', str(port / 'c3tout.c')]
        obj = cls.directory / 'output.o'
        subprocess.run([*cls.command, '-o', str(obj)], check=True, capture_output=True)
        prefix = '_' if sys.platform == 'darwin' else ''
        aliases = []
        for original, replacement in (('malloc', 'allocate'), ('calloc', 'zero'),
                                      ('realloc', 'resize'), ('free', 'release')):
            aliases += ['--redefine-sym', f'{prefix}{original}={prefix}heaptrap_{replacement}']
        subprocess.run([objcopy, *aliases, str(obj)], check=True, capture_output=True)
        cls.binary = cls.directory / 'output.dll'
        subprocess.run([compiler, *flags, '-shared', str(obj),
                        str(ROOT / 'tests/target/touttest.c'),
                        str(ROOT / 'tests/target/heaptrap.c'), '-o', str(cls.binary)],
                       check=True, capture_output=True)

    def setUp(self):
        path = self.directory / (self._testMethodName + '.dll')
        shutil.copyfile(self.binary, path)
        self.lib = c.CDLL(str(path))
        if sys.platform == 'win32':
            import _ctypes
            self.addCleanup(_ctypes.FreeLibrary, self.lib._handle)
        for name, args, result in (
                ('c3tout_setup', [c.POINTER(Context), c.POINTER(Config)], c.c_int),
                ('c3tout_set_name', [c.POINTER(Context), c.c_char_p], c.c_int),
                ('c3tout_start', [c.POINTER(Context)], c.c_int),
                ('c3tout_renegotiate', [c.POINTER(Context)], c.c_int),
                ('c3tout_certificate', [c.POINTER(Context)], c.c_int),
                ('touttest_head', [], c.c_void_p),
                ('touttest_promote', [c.POINTER(Context)], None),
                ('c3tout_alloc', [c.c_size_t, c.c_size_t], c.c_void_p),
                ('c3tout_prf_alloc', [c.c_size_t, c.c_size_t], c.c_void_p),
                ('c3tout_prf_import', [c.POINTER(ImportAttributes), c.c_char_p, c.c_size_t,
                                       c.POINTER(c.c_uint32)], c.c_int32),
                ('c3tout_prf_key', [c.POINTER(Derivation), c.c_uint16, c.c_uint32], c.c_int32),
                ('c3tout_prf_input', [c.POINTER(Derivation), c.c_uint16,
                                      c.c_char_p, c.c_size_t], c.c_int32),
                ('c3tout_free', [c.c_void_p], None),
                ('c3tout_wipe', [c.c_void_p, c.c_size_t], None),
                ('touttest_count', [c.c_uint32], c.c_uint32),
                ('heaptrap_count', [], c.c_size_t)):
            function = getattr(self.lib, name)
            function.argtypes, function.restype = args, result
        self.contexts = []
        self.configs = []
        self.reserved = []
        self.certificate_contexts = []

    def tearDown(self):
        for context in self.certificate_contexts:
            self.close_certificate(context)
        for context in self.contexts:
            self.close(context)
        for pointer in self.reserved:
            self.lib.c3tout_free(pointer)
        self.assertEqual(self.lib.heaptrap_count(), 0)

    def open(self, failure=0):
        context = Context()
        self.contexts.append(context)
        config = Config(failure)
        self.configs.append(config)
        result = self.lib.c3tout_setup(c.byref(context), c.byref(config))
        return result, context

    def close(self, context):
        for name, size in (('input', INPUT_BYTES), ('output', OUTPUT_BYTES),
                           ('hostname', NAME_BYTES), *STATE_SIZES.items()):
            pointer = getattr(context, name)
            if pointer:
                self.lib.c3tout_wipe(pointer, size)
                setattr(context, name, None)

    def release_reservations(self):
        for pointer in self.reserved:
            self.lib.c3tout_free(pointer)
        self.reserved.clear()

    def certificate(self, context, scenario=0):
        if context not in self.certificate_contexts:
            self.certificate_contexts.append(context)
        Config.from_address(context.config).certificate_case = scenario
        return self.lib.c3tout_certificate(c.byref(context))

    def close_certificate(self, context):
        for field in ('session', 'session_new'):
            address = getattr(context, field)
            if address:
                session = Session.from_address(address)
                if session.digest:
                    self.lib.c3tout_free(session.digest)
                    session.digest = None
        self.close(context)

    def test_hostname_copy_boundaries_replacement_and_clear(self):
        context = Context()
        self.contexts.append(context)
        pointer = None
        for name in (b'a' * 253, b'broker.local', b'', b'x'):
            source = c.create_string_buffer(name)
            self.assertEqual(self.lib.c3tout_set_name(c.byref(context), source), 0)
            pointer = pointer or context.hostname
            self.assertEqual(context.hostname, pointer)
            self.assertEqual(c.string_at(pointer, NAME_BYTES), name + bytes(NAME_BYTES - len(name)))
            c.memset(source, ord('z'), len(name))
            self.assertEqual(c.string_at(pointer), name)
            self.assertEqual(context.flags, 1)
        before = self.lib.touttest_count(4)
        for name in (b'y' * 254, b'y' * 255, b'y' * 4096):
            self.assertEqual(self.lib.c3tout_set_name(c.byref(context), name), -135)
            self.assertEqual(context.hostname, pointer)
            self.assertEqual(c.string_at(pointer), b'x')
        self.assertEqual(self.lib.touttest_count(4), before)
        self.assertEqual(self.lib.c3tout_set_name(c.byref(context), None), 0)
        self.assertFalse(context.hostname)
        self.assertEqual(context.flags, 1)
        self.assertEqual(c.string_at(pointer, NAME_BYTES), bytes(NAME_BYTES))
        self.assertEqual(self.lib.touttest_count(0), 0)

    def test_hostname_exhaustion_preserves_owner_and_clears_scope(self):
        owner, rejected = Context(), Context()
        self.contexts.extend((owner, rejected))
        self.assertEqual(self.lib.c3tout_set_name(c.byref(owner), b'first'), 0)
        self.assertEqual(self.lib.c3tout_set_name(c.byref(rejected), b'second'), -141)
        self.assertFalse(rejected.hostname)
        self.assertEqual(rejected.flags, 0)
        self.assertEqual(c.string_at(owner.hostname), b'first')
        self.assertEqual(self.lib.touttest_count(0), 0)
        pointer = self.lib.c3tout_alloc(1, 7)  # same size outside the setter
        self.assertTrue(pointer)
        self.assertNotEqual(pointer, owner.hostname)
        self.assertEqual(self.lib.touttest_count(0), 1)
        self.lib.c3tout_free(pointer)
        pointer = owner.hostname
        self.close(owner)
        self.assertEqual(self.lib.c3tout_set_name(c.byref(rejected), b'second'), 0)
        self.assertEqual(rejected.hostname, pointer)

    def test_hostname_both_cleanup_paths_erase_all_storage(self):
        for wipe in (False, True):
            context = Context()
            self.contexts.append(context)
            self.assertEqual(self.lib.c3tout_set_name(c.byref(context), b'broker'), 0)
            pointer = context.hostname
            c.memset(pointer, 0xA5, NAME_BYTES)
            before = self.lib.touttest_count(1)
            if wipe:
                self.lib.c3tout_wipe(pointer, 1)
            else:
                self.lib.c3tout_free(pointer)
            context.hostname = None
            self.assertEqual(self.lib.touttest_count(1), before)
            self.assertEqual(c.string_at(pointer, NAME_BYTES), bytes(NAME_BYTES))

    def test_hostname_and_records_coexist_through_reconnects_and_failure(self):
        pointer = None
        for index in range(1000):
            context = Context()
            self.contexts.append(context)
            self.assertEqual(self.lib.c3tout_set_name(c.byref(context), b'broker.local'), 0)
            pointer = pointer or context.hostname
            self.assertEqual(context.hostname, pointer)
            fail = index % 2 == 0
            result = self.lib.c3tout_setup(c.byref(context), c.byref(Config(2 if fail else 0)))
            self.assertEqual(result != 0, fail)
            self.assertEqual(c.string_at(context.hostname), b'broker.local')
            if not fail:
                self.assertEqual(len({context.hostname, context.input, context.output}), 3)
            self.close(context)
            self.assertEqual(c.string_at(pointer, NAME_BYTES), bytes(NAME_BYTES))
        self.assertEqual(self.lib.touttest_count(0), 0)

    def test_one_session_exhaust_without_record_heap_fallback(self):
        result, context = self.open()
        self.assertEqual(result, 0)
        pointers = (context.input, context.output)
        self.assertTrue(all(pointers))
        self.assertEqual(len(set(pointers)), 2)
        self.assertEqual(self.lib.touttest_count(0), 0)
        for pointer, size in zip(pointers, (INPUT_BYTES, OUTPUT_BYTES)):
            self.assertEqual(pointer % c.alignment(c.c_uint64), 0)
            self.assertEqual(c.string_at(pointer, size), bytes(size))
            c.memset(pointer, 0xA5, size)
        result, rejected = self.open()
        self.assertNotEqual(result, 0)
        self.assertFalse(rejected.input or rejected.output)
        self.assertEqual(self.lib.touttest_count(0), 0)
        for pointer, size in zip(pointers, (INPUT_BYTES, OUTPUT_BYTES)):
            self.assertEqual(c.string_at(pointer, size), b'\xa5' * size)
        self.close(context)
        for pointer, size in zip(pointers, (INPUT_BYTES, OUTPUT_BYTES)):
            self.assertEqual(c.string_at(pointer, size), bytes(size))
        result, replacement = self.open()
        self.assertEqual(result, 0)
        self.assertEqual((replacement.input, replacement.output), pointers)

    def test_setup_failures_release_slots_and_clear_selection_scope(self):
        for failure in (1, 2, 4):
            for _ in range(20):
                result, context = self.open(failure)
                self.assertNotEqual(result, 0)
                self.assertFalse(context.input or context.output)
        before = self.lib.touttest_count(0)
        for size in (INPUT_BYTES, OUTPUT_BYTES):
            pointer = self.lib.c3tout_alloc(1, size)
            self.assertTrue(pointer)
            self.lib.c3tout_free(pointer)
        self.assertEqual(self.lib.touttest_count(0), before + 2)
        self.assertEqual(self.open()[0], 0)

    def test_same_size_later_in_setup_uses_sdk_not_record_slots(self):
        result, context = self.open(3)
        self.assertEqual(result, 0)
        self.assertTrue(context.input and context.output)
        self.assertEqual(self.lib.touttest_count(0), 2)  # later objects only
        self.assertEqual(self.lib.touttest_count(3), 2)
        self.assertNotEqual(self.open()[0], 0)

    def test_both_cleanup_paths_erase_entire_buffer_without_sdk_free(self):
        for wipe in (False, True):
            result, context = self.open()
            self.assertEqual(result, 0)
            for name, size in (('input', INPUT_BYTES), ('output', OUTPUT_BYTES)):
                pointer = getattr(context, name)
                c.memset(pointer, 0xA5, size)
                before = self.lib.touttest_count(1)
                if wipe:
                    self.lib.c3tout_wipe(pointer, 1)  # owned capacity governs erasure
                else:
                    self.lib.c3tout_free(pointer)
                setattr(context, name, None)
                self.assertEqual(self.lib.touttest_count(1), before)
                self.assertEqual(c.string_at(pointer, size), bytes(size))
            self.close(context)

    def test_output_exhaustion_returns_new_input_lease(self):
        result, owner = self.open()
        self.assertEqual(result, 0)
        pointer = owner.input
        self.lib.c3tout_free(pointer)
        owner.input = None
        c.memset(owner.output, 0x5A, OUTPUT_BYTES)
        result, rejected = self.open()
        self.assertNotEqual(result, 0)
        self.assertFalse(rejected.input or rejected.output)
        self.assertEqual(self.lib.touttest_count(0), 0)
        self.assertEqual(c.string_at(owner.output, OUTPUT_BYTES), b'\x5a' * OUTPUT_BYTES)
        self.close(owner)
        result, replacement = self.open()
        self.assertEqual(result, 0)
        self.assertEqual(replacement.input, pointer)

    def test_unrelated_allocation_shapes_keep_sdk_path(self):
        result, context = self.open(5)
        self.assertEqual(result, 0)
        self.assertTrue(context.input and context.output)
        self.assertEqual(self.lib.touttest_count(0), 3)
        for count, size in ((2, INPUT_BYTES), (2, OUTPUT_BYTES), (1, 17)):
            pointer = self.lib.c3tout_alloc(count, size)
            self.assertTrue(pointer)
            self.lib.c3tout_wipe(pointer, count * size)
        self.assertEqual(self.lib.touttest_count(0), 6)
        self.assertEqual(self.lib.touttest_count(3), 3)

    def test_reconnect_cycles_reuse_all_storage(self):
        expected = None
        for _ in range(1000):
            result, context = self.open()
            self.assertEqual(result, 0)
            pointers = (context.input, context.output, context.transform_new,
                        context.session_new, context.handshake)
            self.assertTrue(all(pointers))
            if expected is None:
                expected = pointers
            self.assertEqual(pointers, expected)
            self.close(context)
        self.assertEqual(self.lib.touttest_count(0), 0)

    def test_states_keep_active_and_negotiating_records_distinct(self):
        for entry in ('c3tout_start', 'c3tout_renegotiate'):
            result, context = self.open()
            self.assertEqual(result, 0)
            self.lib.touttest_promote(c.byref(context))
            for _ in range(50):
                active = (context.transform, context.session)
                for pointer, size in zip(active, (84, 128)):
                    c.memset(pointer, 0x5A, size)
                before = self.lib.touttest_count(0)
                self.assertEqual(getattr(self.lib, entry)(c.byref(context)), -0x6900)
                pending = (context.transform_new, context.session_new, context.handshake)
                self.assertEqual(len(set((*active, *pending))), 5)
                self.assertTrue(all(pending))
                self.assertEqual(self.lib.touttest_count(0), before)
                for pointer, size in zip(active, (84, 128)):
                    self.assertEqual(c.string_at(pointer, size), b'\x5a' * size)
                self.assertEqual(self.lib.c3tout_renegotiate(c.byref(context)), -0x6900)
                self.assertEqual((context.transform_new, context.session_new,
                                  context.handshake), pending)
                self.assertEqual(self.lib.touttest_count(0), before + 3)
                self.lib.touttest_promote(c.byref(context))
                self.assertEqual((context.transform, context.session), pending[:2])
                for pointer, size in zip((*active, pending[2]), (84, 128, 2448)):
                    self.assertEqual(c.string_at(pointer, size), bytes(size))
            self.close(context)

    def test_each_state_store_exhausts_without_heap_and_reuses(self):
        for field, capacity in (('transform_new', 2), ('session_new', 2), ('handshake', 1)):
            before = self.lib.touttest_count(0)
            for _ in range(capacity):
                result, holder = self.open()
                self.assertEqual(result, 0)
                pointer = getattr(holder, field)
                self.reserved.append(pointer)
                setattr(holder, field, None)
                self.close(holder)
                c.memset(pointer, 0xA5, STATE_SIZES[field])
            result, rejected = self.open()
            self.assertEqual(result, -141)
            self.assertFalse(rejected.input or rejected.output or rejected.transform_new
                             or rejected.session_new or rejected.handshake)
            self.assertEqual(self.lib.touttest_count(0), before)
            old = tuple(self.reserved)
            for pointer in old:
                self.assertEqual(c.string_at(pointer, STATE_SIZES[field]),
                                 b'\xa5' * STATE_SIZES[field])
            self.release_reservations()
            for pointer in old:
                self.assertEqual(c.string_at(pointer, STATE_SIZES[field]),
                                 bytes(STATE_SIZES[field]))
            result, replacement = self.open()
            self.assertEqual(result, 0)
            self.assertIn(getattr(replacement, field), old)
            self.close(replacement)

    def test_each_renegotiation_failure_preserves_active_state(self):
        for entry in ('c3tout_start', 'c3tout_renegotiate'):
            for field in ('transform_new', 'session_new', 'handshake'):
                result, owner = self.open()
                self.assertEqual(result, 0)
                self.lib.touttest_promote(c.byref(owner))
                # Borrow existing buffers only to reserve the spare state slot.
                holder = Context(input=owner.input, output=owner.output, config=owner.config)
                self.assertEqual(self.lib.c3tout_start(c.byref(holder)), -0x6900)
                pointer = getattr(holder, field)
                self.reserved.append(pointer)
                setattr(holder, field, None)
                holder.input = holder.output = None
                self.close(holder)
                active = (owner.transform, owner.session)
                for current, size in zip(active, (84, 128)):
                    c.memset(current, 0x5A, size)
                before = self.lib.touttest_count(0)
                self.assertEqual(getattr(self.lib, entry)(c.byref(owner)), -141)
                self.assertEqual((owner.transform, owner.session), active)
                self.assertFalse(owner.transform_new or owner.session_new or owner.handshake)
                self.assertEqual(self.lib.touttest_count(0), before)
                for current, size in zip(active, (84, 128)):
                    self.assertEqual(c.string_at(current, size), b'\x5a' * size)
                self.release_reservations()
                self.assertEqual(getattr(self.lib, entry)(c.byref(owner)), -0x6900)
                self.close(owner)

    def test_partial_state_reinitialization_keeps_existing_record_identities(self):
        fields = ('transform_new', 'session_new', 'handshake')
        for mask in range(8):
            result, context = self.open()
            self.assertEqual(result, 0)
            old = tuple(getattr(context, field) for field in fields)
            for index, field in enumerate(fields):
                if mask & (1 << index):
                    self.lib.c3tout_free(getattr(context, field))
                    setattr(context, field, None)
            before = self.lib.touttest_count(0)
            self.assertEqual(self.lib.c3tout_start(c.byref(context)), -0x6900)
            self.assertEqual(tuple(getattr(context, field) for field in fields), old)
            self.assertEqual(self.lib.touttest_count(0), before)
            self.close(context)

    def test_state_cleanup_wipes_full_typed_records_on_both_paths(self):
        for wipe in (False, True):
            result, context = self.open()
            self.assertEqual(result, 0)
            for field in ('transform_new', 'session_new', 'handshake'):
                pointer, size = getattr(context, field), STATE_SIZES[field]
                self.assertEqual(pointer % c.alignment(c.c_uint32), 0)
                self.assertEqual(c.string_at(pointer, size), bytes(size))
                c.memset(pointer, 0xA5, size)
                before = self.lib.touttest_count(1)
                if wipe:
                    self.lib.c3tout_wipe(pointer, 1)
                else:
                    self.lib.c3tout_free(pointer)
                setattr(context, field, None)
                self.assertEqual(self.lib.touttest_count(1), before)
                self.assertEqual(c.string_at(pointer, size), bytes(size))
            self.close(context)

    def test_same_sized_state_requests_outside_init_phase_keep_heap_path(self):
        result, context = self.open(6)
        self.assertEqual(result, 0)
        self.assertTrue(context.transform and context.session)
        self.assertFalse(context.transform_new or context.session_new or context.handshake)
        self.assertEqual(self.lib.touttest_count(0), 16)
        for size in (84, 128, 2448):
            pointer = self.lib.c3tout_alloc(1, size)
            self.assertTrue(pointer)
            self.lib.c3tout_free(pointer)
        self.assertEqual(self.lib.touttest_count(0), 19)

    def test_invalid_renegotiation_and_retry_do_not_arm_state_selection(self):
        result, context = self.open()
        self.assertEqual(result, 0)
        before = self.lib.touttest_count(0)
        self.assertEqual(self.lib.c3tout_renegotiate(None), -135)
        saved = context.config
        context.config = None
        self.assertEqual(self.lib.c3tout_renegotiate(c.byref(context)), -135)
        context.config = saved
        self.assertEqual(self.lib.c3tout_renegotiate(c.byref(context)), -135)
        self.lib.touttest_promote(c.byref(context))
        context.renegotiation = 1  # missing negotiating fields during a retry
        self.assertEqual(self.lib.c3tout_renegotiate(c.byref(context)), -0x6900)
        self.assertEqual(self.lib.touttest_count(0), before + 12)

    def test_certificate_head_and_digest_reuse_through_renegotiation(self):
        result, context = self.open()
        self.assertEqual(result, 0)
        self.assertEqual(self.certificate(context), 0)
        head = self.lib.touttest_head()
        digest = Session.from_address(context.session_new).digest
        self.assertTrue(head and digest)
        self.assertNotEqual(head, digest)
        for _ in range(100):
            self.lib.touttest_promote(c.byref(context))
            previous = context.session
            self.assertEqual(self.lib.c3tout_start(c.byref(context)), -0x6900)
            self.assertEqual(self.certificate(context), 0)
            self.assertFalse(Session.from_address(previous).digest)
            self.assertEqual(Session.from_address(context.session_new).digest, digest)
            self.assertEqual(self.lib.touttest_head(), head)
            self.assertEqual(c.string_at(digest, DIGEST_BYTES), b'\xd3' * DIGEST_BYTES)
            self.assertEqual(c.string_at(head, CERTIFICATE_BYTES), bytes(CERTIFICATE_BYTES))
        self.assertEqual(self.lib.touttest_count(0), 0)
        self.close_certificate(context)
        self.assertEqual(c.string_at(digest, DIGEST_BYTES), bytes(DIGEST_BYTES))

    def test_certificate_failures_preserve_native_digest_ownership_and_clear_scope(self):
        for scenario, expected in ((1, -0x6900), (2, -0x7A00), (3, -0x2700),
                                   (4, -0x7100), (5, -0x7180)):
            with self.subTest(scenario=scenario):
                result, context = self.open()
                self.assertEqual(result, 0)
                self.assertEqual(self.certificate(context), 0)
                digest = Session.from_address(context.session_new).digest
                self.lib.touttest_promote(c.byref(context))
                self.assertEqual(self.lib.c3tout_start(c.byref(context)), -0x6900)
                before = self.lib.touttest_count(0)
                self.assertEqual(self.certificate(context, scenario), expected)
                old = Session.from_address(context.session).digest
                new = Session.from_address(context.session_new).digest
                self.assertEqual(old, digest if scenario in (1, 2) else None)
                self.assertEqual(new, digest if scenario == 5 else None)
                self.assertEqual(self.lib.touttest_count(0), before)
                self.assertEqual(c.string_at(self.lib.touttest_head(), CERTIFICATE_BYTES),
                                 bytes(CERTIFICATE_BYTES))
                if old or new:
                    self.assertEqual(c.string_at(digest, DIGEST_BYTES), b'\xd3' * DIGEST_BYTES)
                else:
                    self.assertEqual(c.string_at(digest, DIGEST_BYTES), bytes(DIGEST_BYTES))
                for size in (CERTIFICATE_BYTES, DIGEST_BYTES):
                    pointer = self.lib.c3tout_alloc(1, size)
                    self.assertTrue(pointer)
                    self.lib.c3tout_free(pointer)
                self.assertEqual(self.lib.touttest_count(0), before + 2)
                self.close_certificate(context)

    def test_certificate_head_exhaustion_preserves_lease_and_both_releases_erase(self):
        for wipe in (False, True):
            result, context = self.open()
            self.assertEqual(result, 0)
            # The double intentionally retains one acquired head for occupancy injection.
            self.assertEqual(self.certificate(context, 6), -200)
            head = self.lib.touttest_head()
            self.reserved.append(head)
            before = self.lib.touttest_count(0)
            self.assertEqual(self.certificate(context), -141)
            self.assertFalse(Session.from_address(context.session_new).digest)
            self.assertEqual(self.lib.touttest_count(0), before)
            self.assertEqual(c.string_at(head, CERTIFICATE_BYTES), b'\xa5' * CERTIFICATE_BYTES)
            if wipe:
                self.lib.c3tout_wipe(head, 1)
            else:
                self.lib.c3tout_free(head)
            self.reserved.remove(head)
            self.assertEqual(c.string_at(head, CERTIFICATE_BYTES), bytes(CERTIFICATE_BYTES))
            self.assertEqual(self.certificate(context), 0)
            self.assertEqual(self.lib.touttest_head(), head)
            self.close_certificate(context)

    def test_certificate_digest_exhaustion_has_no_fallback_and_both_releases_erase(self):
        for wipe in (False, True):
            result, context = self.open()
            self.assertEqual(result, 0)
            self.assertEqual(self.certificate(context), 0)
            session = Session.from_address(context.session_new)
            digest = session.digest
            self.reserved.append(digest)
            session.digest = None  # retain the lease outside the parser as an occupancy control
            before = self.lib.touttest_count(0)
            self.assertEqual(self.certificate(context), -141)
            self.assertFalse(session.digest)
            self.assertEqual(self.lib.touttest_count(0), before)
            self.assertEqual(c.string_at(digest, DIGEST_BYTES), b'\xd3' * DIGEST_BYTES)
            if wipe:
                self.lib.c3tout_wipe(digest, 1)
            else:
                self.lib.c3tout_free(digest)
            self.reserved.remove(digest)
            self.assertEqual(c.string_at(digest, DIGEST_BYTES), bytes(DIGEST_BYTES))
            self.assertEqual(self.certificate(context), 0)
            self.assertEqual(session.digest, digest)
            self.close_certificate(context)

    def test_unrelated_certificate_shapes_and_phase_requests_keep_sdk_path(self):
        result, context = self.open()
        self.assertEqual(result, 0)
        self.assertEqual(self.certificate(context, 7), 0)
        self.assertEqual(self.lib.touttest_count(0), 9)
        for size in (CERTIFICATE_BYTES, DIGEST_BYTES):
            pointer = self.lib.c3tout_alloc(1, size)
            self.assertTrue(pointer)
            self.lib.c3tout_free(pointer)
        self.assertEqual(self.lib.touttest_count(0), 11)
        self.close_certificate(context)

    def test_variable_buffers_are_rejected_before_build(self):
        result = subprocess.run([*self.command, '-DMBEDTLS_SSL_VARIABLE_BUFFER_LENGTH',
                                 '-o', str(self.directory / 'rejected.o')], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('without buffer resizing', result.stderr)

    def test_prf_copies_coexist_erase_and_reuse(self):
        for _ in range(100):
            seed, label = Derivation(1), Derivation(1)
            for operation, step, size in ((seed, 2, 64), (label, 3, 22)):
                self.assertEqual(self.lib.c3tout_prf_input(c.byref(operation), step, b'x' * size, size), 0)
                self.assertEqual(c.string_at(operation.input, size), b'x' * size)
            self.assertNotEqual(seed.input, label.input)
            self.lib.c3tout_wipe(seed.input, 1)
            self.lib.c3tout_free(label.input)
            self.assertEqual(c.string_at(seed.input, 64), bytes(64))
            self.assertEqual(c.string_at(label.input, 22), bytes(22))
        self.assertEqual(self.lib.touttest_count(0), 0)

    def test_prf_exhaustion_boundaries_and_failure_scope(self):
        for step, capacity in ((2, 64), (3, 22)):
            first, second = Derivation(1), Derivation(1)
            self.assertEqual(self.lib.c3tout_prf_input(c.byref(first), step, b'a' * capacity, capacity), 0)
            self.assertEqual(self.lib.c3tout_prf_input(c.byref(second), step, b'b', 1), -141)
            self.assertFalse(second.input)
            self.assertEqual(c.string_at(first.input, capacity), b'a' * capacity)
            self.lib.c3tout_free(first.input)
            self.assertEqual(self.lib.c3tout_prf_input(c.byref(second), step, b'b' * (capacity + 1), capacity + 1), -141)
            for failure in (1, 3):
                operation = Derivation(1, failure)
                self.assertEqual(self.lib.c3tout_prf_input(c.byref(operation), step, b'z', 1), -137)
                self.assertFalse(operation.input)
            self.assertEqual(self.lib.c3tout_prf_input(c.byref(second), step, b'b', 1), 0)
            self.assertEqual(c.string_at(second.input, capacity), b'b' + bytes(capacity - 1))
            self.lib.c3tout_free(second.input)
        self.assertEqual(self.lib.touttest_count(0), 0)

    def test_prf_unselected_requests_and_empty_inputs(self):
        operation = Derivation(1)
        self.assertEqual(self.lib.c3tout_prf_input(c.byref(operation), 2, None, 0), 0)
        self.assertFalse(operation.input)
        for alg, step, failure, forwarded in ((0, 2, 0, 1), (1, 4, 0, 1),
                                              (1, 2, 2, 1), (1, 2, 4, 1), (1, 3, 4, 1)):
            operation = Derivation(alg, failure)
            before = self.lib.touttest_count(0)
            self.assertEqual(self.lib.c3tout_prf_input(c.byref(operation), step, b'z', 1), 0)
            self.assertEqual(self.lib.touttest_count(0) - before, forwarded)
            self.lib.c3tout_free(operation.input)
        for size in (22, 64):
            before = self.lib.touttest_count(0)
            pointer = self.lib.c3tout_prf_alloc(1, size)
            self.assertTrue(pointer)
            self.assertEqual(self.lib.touttest_count(0), before + 1)
            self.lib.c3tout_wipe(pointer, size)

    def test_key_id_secret_storage_and_native_errors(self):
        first, second = Derivation(1), Derivation(1)
        key_input = self.lib.c3tout_prf_key
        for size in (32, 48, 66):
            self.assertEqual(key_input(c.byref(first), 4, size), 0)
            self.assertEqual(c.string_at(first.input, size), b'K' + bytes(size - 1))
            self.assertEqual(key_input(c.byref(second), 4, 1), -141)
            c.memset(first.input, 0xA5, 66)
            self.lib.c3tout_wipe(first.input, size)
            self.assertEqual(c.string_at(first.input, 66), bytes(66))
        self.assertEqual(key_input(c.byref(second), 4, 67), -141)
        self.assertEqual(key_input(c.byref(second), 4, 0), -136)
        self.assertEqual(self.lib.touttest_count(0), 0)
        for alg, step, failure in ((0, 4, 0), (1, 3, 0), (1, 4, 4)):
            operation = Derivation(alg, failure)
            before = self.lib.touttest_count(0)
            self.assertEqual(key_input(c.byref(operation), step, 48), 0)
            self.assertEqual(self.lib.touttest_count(0), before + 1)
            self.lib.c3tout_free(operation.input)
        for failure in (1, 3):
            operation = Derivation(1, failure)
            self.assertEqual(key_input(c.byref(operation), 4, 48), -137)
            self.assertFalse(operation.input)
        self.assertEqual(key_input(c.byref(second), 4, 1), 0)
        self.lib.c3tout_free(second.input)

    def test_prf_import_owns_distinct_storage_until_destruction(self):
        copy = Derivation(1)
        attrs = ImportAttributes(2, 1, 8, 0, c.pointer(copy))
        key = c.c_uint32(17)
        def load(data):
            return self.lib.c3tout_prf_import(c.byref(attrs), data, len(data), c.byref(key))

        for length in (1, 32, 48, 66):
            value = bytes(range(1, length + 1))
            self.assertEqual(load(value), 0)
            self.assertEqual(key.value, 1)
            first = copy.input
            secret = Derivation(1)
            self.assertEqual(self.lib.c3tout_prf_key(c.byref(secret), 4, length), 0)
            self.assertNotEqual(first, secret.input)
            self.assertEqual(load(b'x'), -141)
            self.assertEqual(key.value, 0)
            self.assertEqual(c.string_at(first, length), value)
            self.lib.c3tout_wipe(secret.input, length)
            c.memset(first, 0xA5, 66)
            self.lib.c3tout_wipe(first, length)
            self.assertEqual(c.string_at(first, 66), bytes(66))
        self.assertEqual(load(b'x' * 67), -141)
        self.assertEqual(load(b''), -135)
        for failure in (1, 3):
            copy.failure = failure
            self.assertEqual(load(b'x'), -137)
            self.assertEqual(key.value, 0)
        self.assertEqual(self.lib.touttest_count(0), 0)
        copy.failure = 4  # Simulate a different task allocating the same size.
        self.assertEqual(load(b'x'), 0)
        self.assertEqual(self.lib.touttest_count(0), 1)
        self.lib.c3tout_free(copy.input)

    def test_other_key_import_policies_keep_the_native_allocator(self):
        for policy in ((3, 1, 8, 0), (2, 2, 8, 0), (2, 1, 9, 0), (2, 1, 8, 1)):
            copy, key = Derivation(), c.c_uint32()
            attrs = ImportAttributes(*policy, c.pointer(copy))
            before = self.lib.touttest_count(0)
            self.assertEqual(self.lib.c3tout_prf_import(c.byref(attrs), b'k', 1, c.byref(key)), 0)
            self.assertEqual(key.value, 1)
            self.assertEqual(self.lib.touttest_count(0), before + 1)
            self.lib.c3tout_wipe(copy.input, 1)

    def test_global_static_psa_slots_are_outside_the_selected_build(self):
        command = [*self.command, '-DMBEDTLS_PSA_STATIC_KEY_SLOTS', '-o', str(self.directory / 'rejected.o')]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('PSA key-buffer path', result.stderr)

    def test_psa_copy_mode_is_rejected_before_build(self):
        command = [arg for arg in self.command if arg != '-DMBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS']
        result = subprocess.run([*command, '-o', str(self.directory / 'rejected.o')], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('PSA direct-input path', result.stderr)

    def test_unsupported_tls_state_profiles_are_rejected_before_build(self):
        for profile in ('MBEDTLS_SSL_PROTO_TLS1_3', 'MBEDTLS_SSL_SRV_C',
                        'MBEDTLS_SSL_SESSION_TICKETS',
                        'MBEDTLS_SSL_PROTO_DTLS', 'MBEDTLS_SSL_KEEP_PEER_CERTIFICATE',
                        'MBEDTLS_ECP_RESTARTABLE', 'MBEDTLS_SSL_ECP_RESTARTABLE_ENABLED'):
            with self.subTest(profile=profile):
                result = subprocess.run([*self.command, '-D' + profile,
                                         '-o', str(self.directory / 'rejected.o')],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('TLS', result.stderr)


if __name__ == '__main__':
    unittest.main()
