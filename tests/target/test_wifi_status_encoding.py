"""Execute the actual owner serializer across the USB Wi-Fi wire boundary."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

from tools.config_transfer.network import decode_network
from tools.config_transfer.protocol import HEADER

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''/**
 * @file statuscheck.c
 * @brief Check copied station status without an SDK or radio operation.
 */
#include "statuscheck.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int          g_status_result = 0;
static unsigned int g_calls         = 0U;

/** @brief Double the reviewed status API with asymmetric byte-order values. */
int c3wsta_status (c3wsta_status_t * p_status)
{
    ++g_calls;
    *p_status                   = (c3wsta_status_t){0};
    p_status->stage             = 14U;
    p_status->retry_state       = 2U;
    p_status->flags             = 63U;
    p_status->sdk_error         = -19088743;
    p_status->disconnect_reason = UINT16_C(0x1234);
    p_status->address[0]        = 192U;
    p_status->address[1]        = 0U;
    p_status->address[2]        = 2U;
    p_status->address[3]        = 19U;
    p_status->deadline_ms       = UINT64_C(0x12345678ABCDEF01);
    return g_status_result;
}

/** @brief Protect caller buffers before exercising the real serializer. */
int main (void)
{
    uint8_t storage[26] = {0};
    size_t  size        = 77U;
    memset(storage, 0xA5, sizeof(storage));
    assert(-1 == c3net_observe(NULL, 24U, &size));
    assert(-1 == c3net_observe(&storage[1], 24U, NULL));
    assert(-1 == c3net_observe(&storage[1], 23U, &size));
    assert((0U == g_calls) && (77U == size));
    g_status_result = -1;
    assert(-1 == c3net_observe(&storage[1], 24U, &size));
    for (size_t index = 0U; index < sizeof(storage); ++index)
    {
        assert(0xA5U == storage[index]);
    }
    assert((1U == g_calls) && (77U == size));
    g_status_result = 0;
    assert(0 == c3net_observe(&storage[1], 24U, &size));
    assert((24U == size) && (2U == g_calls));
    assert((0xA5U == storage[0]) && (0xA5U == storage[25]));
    for (size_t index = 1U; index < 25U; ++index)
    {
        printf("%02x", (unsigned int)storage[index]);
    }
    return 0;
}

/* End of file. */

'''


def write_header(folder: Path, name: str, body: str) -> None:
    """Keep temporary C boundary declarations in paired, guarded headers."""
    guard = 'UTHOME_STATUSCHECK_' + name.upper().replace('.', '_')
    (folder / name).write_text(
        f'/**\n * @file {name}\n'
        ' * @brief Isolated status-encoding test boundary declarations.\n */\n'
        f'#ifndef {guard}\n#define {guard}\n\n'
        + body + '\n#endif\n\n/* End of file. */\n\n')


def build_status_check(folder: Path, source: Path) -> Path:
    """Link only the actual observer; absent SDK operations stay unresolved.

    The harness implements the exact reviewed c3wsta_status API as its sole
    behavior double. It supplies copied fields; it never emulates the radio.
    """
    write_header(folder, 'esp_random.h',
                 '#include <stddef.h>\n'
                 'void esp_fill_random (void * p_data, size_t size);\n')
    write_header(folder, 'esp_mac.h',
                 '#include <stdint.h>\n#define ESP_OK           0\n'
                 '#define ESP_MAC_WIFI_STA 0\n'
                 'int esp_read_mac (uint8_t * p_address, int type);\n')
    write_header(folder, 'netsetup.h',
                 '#include <stdint.h>\n'
                 '#define UTHOME_SETUP_MQTT     0\n'
                 '#define UTHOME_SETUP_PORT     8443U\n'
                 '#define UTHOME_SETUP_TX_POWER 0U\n'
                 'static const char    g_setup_ssid[]        = "ap";\n'
                 'static const char    g_setup_password[]    = "password";\n'
                 'static const uint8_t g_setup_certificate[] = "certificate";\n'
                 'static const uint8_t g_setup_key[]         = "key";\n'
                 'static const uint8_t g_setup_token[]       = "token";\n')
    write_header(folder, 'statuscheck.h',
                 '#include "c3net.h"\n#include "c3wsta.h"\n')
    harness = folder / 'statuscheck.c'
    harness.write_text(HARNESS)
    includes = [folder, ROOT / 'firmware/app', ROOT / 'firmware/include',
                ROOT / 'firmware/src/config', ROOT / 'firmware/src/network',
                ROOT / 'firmware/src/runtime', ROOT / 'firmware/src/services',
                ROOT / 'firmware/hal/ports/esp32c3']
    binary = folder / 'statuscheck.exe'
    linker = ['-Wl,-dead_strip'] if sys.platform == 'darwin' else ['-Wl,--gc-sections']
    compiler = shutil.which('gcc')
    if compiler is None:
        raise AssertionError('GCC is required')
    command = [compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
               # MinGW needs LTO to discard unused SDK entry points before linking.
               *(['-O2', '-flto'] if sys.platform == 'win32' else []),
               '-ffunction-sections', '-fdata-sections', '-DUTHOME_NETWORK_WIFI=1',
               '-DUTHOME_NETWORK_BLE=0', *['-I'+str(path) for path in includes],
               str(source), str(ROOT / 'firmware/src/config/cfgbytes.c'),
               str(harness), *linker, '-o', str(binary)]
    built = subprocess.run(command, capture_output=True, text=True)
    if built.returncode:
        raise AssertionError(built.stdout + built.stderr)
    return binary


class WifiStatusEncodingTests(unittest.TestCase):
    def test_actual_c_owner_encoding_bounds_and_host_decode(self):
        with tempfile.TemporaryDirectory(prefix='uthome-status-encoding-') as name:
            binary = build_status_check(Path(name), ROOT / 'firmware/app/c3net.c')
            run = subprocess.run([str(binary)], capture_output=True, text=True,
                                 check=True, timeout=5)
        data = bytes.fromhex(run.stdout)
        expected = struct.pack('<4BiHH4sQ', 1, 63, 14, 2, -19088743,
                               0x1234, 0, bytes([192, 0, 2, 19]), 0x12345678abcdef01)
        self.assertEqual(data, expected)
        prefix = HEADER.pack(b'UTH1', 1, 0x8a, 0, 91, 0, len(data), 0)[:20]
        frame = prefix + struct.pack('<I', zlib.crc32(data, zlib.crc32(prefix))) + data
        status = decode_network(frame, request_id=91)
        self.assertEqual((status.stage, status.retry_state, status.ipv4),
                         ('disconnect', 'online', '192.0.2.19'))
        self.assertEqual((status.sdk_error, status.disconnect_reason, status.deadline_ms),
                         (-19088743, 0x1234, 0x12345678abcdef01))


if __name__ == '__main__':
    unittest.main()
