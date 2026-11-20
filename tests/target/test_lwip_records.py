"""Exercise typed record leases; fake SDK types do not simulate TCP/IP."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADERS = {
    "memp.h": r"""/**
 * @file memp.h
 * @brief Minimal foreign pool identifiers; no SDK allocator implementation.
 */
#ifndef UTHOME_TEST_MEMP_H
#define UTHOME_TEST_MEMP_H
#define MEMP_NUM_NETCONN 6
#define MEMP_NUM_UDP_PCB 2
#define ARP_QUEUE_LEN 3
#define MEMP_NUM_NETBUF  2
#define MEMP_NUM_TCP_PCB 3
#define MEMP_NUM_TCP_PCB_LISTEN 1
#define MEMP_NUM_TCP_SEG 16
#define MEMP_NUM_TCPIP_MSG_INPKT 8
#define MEMP_NUM_TCPIP_MSG_API 8
typedef enum
{
    MEMP_NETCONN = 0,
    MEMP_NETBUF,
    MEMP_OTHER,
    MEMP_TCP_PCB,
    MEMP_TCP_PCB_LISTEN,
    MEMP_TCP_SEG,
    MEMP_TCPIP_MSG_INPKT,
    MEMP_TCPIP_MSG_API,
    MEMP_UDP_PCB,
    MEMP_ARP_QUEUE,
    MEMP_MAX
} memp_t;
#endif

/* End of file. */

""",
    "udp.h": r"""/**
 * @file udp.h
 * @brief Typed host record for the udp adapter test.
 */
#ifndef UTHOME_TEST_UDP_H
#define UTHOME_TEST_UDP_H
#include <stdint.h>
struct udp_pcb
{
    uint64_t words[5];
};
#endif

/* End of file. */

""",
    "etharp.h": r"""/**
 * @file etharp.h
 * @brief Typed host record for the etharp adapter test.
 */
#ifndef UTHOME_TEST_ETHARP_H
#define UTHOME_TEST_ETHARP_H
#include <stdint.h>
struct etharp_q_entry
{
    uint64_t words[1];
};
#endif

/* End of file. */

""",
    "priv/tcpip_priv.h": r"""/**
 * @file tcpip_priv.h
 * @brief Foreign type double for native input and callback messages.
 */
#ifndef UTHOME_TEST_TCPIP_PRIV_H
#define UTHOME_TEST_TCPIP_PRIV_H
#include <stdint.h>
struct tcpip_msg
{
    uint64_t words[2];
};
#endif

/* End of file. */

""",
    "priv/tcp_priv.h": r"""/**
 * @file tcp_priv.h
 * @brief Host doubles for naturally aligned native TCP records.
 */
#ifndef UTHOME_TEST_TCP_PRIV_H
#define UTHOME_TEST_TCP_PRIV_H
#include <stdint.h>
struct tcp_pcb
{
    uint64_t words[21];
};
struct tcp_pcb_listen
{
    uint64_t words[5];
};
struct tcp_seg
{
    uint64_t words[2];
};
#endif

/* End of file. */

""",
    "stats.h": r"""/**
 * @file stats.h
 * @brief Ordinary firmware omits native pool statistics.
 */
#ifndef UTHOME_TEST_STATS_H
#define UTHOME_TEST_STATS_H
#define MEMP_STATS 0
#endif

/* End of file. */

""",
    "api.h": r"""/**
 * @file api.h
 * @brief Typed host connection storage; the target uses actual SDK fields.
 */
#ifndef UTHOME_TEST_API_H
#define UTHOME_TEST_API_H
#include <stdint.h>
struct netconn
{
    uint64_t words[16];
};
#endif

/* End of file. */

""",
    "netbuf.h": r"""/**
 * @file netbuf.h
 * @brief Typed host datagram storage; the target uses actual SDK fields.
 */
#ifndef UTHOME_TEST_NETBUF_H
#define UTHOME_TEST_NETBUF_H
#include <stdint.h>
struct netbuf
{
    uint64_t words[4];
};
#endif

/* End of file. */

""",
}


class LwipRecordTests(unittest.TestCase):
    pools = ((0, 6, 128), (1, 2, 32), (3, 3, 168), (4, 1, 40), (5, 16, 16), (6, 8, 16), (7, 8, 16),
             (8, 2, 40), (9, 7, 8))

    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("gcc")
        objcopy = shutil.which("objcopy") or shutil.which("llvm-objcopy")
        if not compiler or not objcopy:
            raise AssertionError("GCC and objcopy are required")
        cls.temp = tempfile.TemporaryDirectory(prefix="uthome-lwip-records-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        (cls.directory / "lwip/priv").mkdir(parents=True)
        for name, source in HEADERS.items():
            (cls.directory / "lwip" / name).write_text(source)
        port = ROOT / "firmware/hal/ports/esp32c3/network"
        flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        if sys.platform != "win32":
            flags += ["-fPIC"]
        obj = cls.directory / "c3lrec.o"
        subprocess.run([compiler, *flags, "-DUTHOME_NETWORK_WIFI=1",
                        "-DUTHOME_STATIC_NET_QUEUES=1", "-I" + str(cls.directory),
                        "-I" + str(port), "-I" + str(ROOT / "firmware/src/runtime"),
                        "-c", str(port / "c3lrec.c"), "-o", str(obj)],
                       check=True, capture_output=True)
        prefix = "_" if sys.platform == "darwin" else ""
        replacements = {"malloc": "heaptrap_allocate", "calloc": "heaptrap_zero",
                        "realloc": "heaptrap_resize", "free": "heaptrap_release",
                        "c3bport_enter": "lrectest_enter", "c3bport_leave": "lrectest_leave"}
        aliases = []
        for original, replacement in replacements.items():
            aliases += ["--redefine-sym", f"{prefix}{original}={prefix}{replacement}"]
        subprocess.run([objcopy, *aliases, str(obj)], check=True, capture_output=True)
        cls.binary = cls.directory / "records.dll"
        subprocess.run([compiler, *flags, "-shared", str(obj),
                        str(ROOT / "tests/target/lrectest.c"),
                        str(ROOT / "tests/target/heaptrap.c"), "-o", str(cls.binary)],
                       check=True, capture_output=True)

    def setUp(self):
        path = self.directory / (self._testMethodName + ".dll")
        shutil.copyfile(self.binary, path)
        self.lib = c.CDLL(str(path))
        if sys.platform == "win32":
            import _ctypes
            self.addCleanup(_ctypes.FreeLibrary, self.lib._handle)
        signatures = {
            "c3lrec_alloc": ([c.c_int], c.c_void_p),
            "c3lrec_free": ([c.c_int, c.c_void_p], None),
            "lrectest_enter": ([], c.c_uint32),
            "lrectest_leave": ([c.c_uint32], None),
            "lrectest_mask": ([], c.c_uint32),
            "lrectest_calls": ([], c.c_size_t),
            "heaptrap_count": ([], c.c_size_t),
        }
        for name, (args, result) in signatures.items():
            function = getattr(self.lib, name)
            function.argtypes, function.restype = args, result

    def tearDown(self):
        self.assertEqual(self.lib.heaptrap_count(), 0)
        self.assertEqual(self.lib.lrectest_mask(), 0)

    def test_independent_capacity_alignment_zeroing_and_reuse(self):
        addresses = []
        for kind, count, size in self.pools:
            records = [self.lib.c3lrec_alloc(kind) for _ in range(count)]
            addresses += records
            for record in records:
                self.assertTrue(record)
                self.assertEqual(record % c.alignment(c.c_uint64), 0)
                self.assertEqual(c.string_at(record, size), bytes(size))
                c.memset(record, 0xA5, size)
            self.assertFalse(self.lib.c3lrec_alloc(kind))
            self.lib.c3lrec_free(kind, records[-1])
            self.assertEqual(self.lib.c3lrec_alloc(kind), records[-1])
            self.assertEqual(c.string_at(records[-1], size), bytes(size))
            for other in records[:-1]:
                self.assertEqual(c.string_at(other, size), bytes([0xA5]) * size)
            self.assertFalse(self.lib.c3lrec_alloc(kind))
        self.assertEqual(len(set(addresses)), sum(count for _, count, _ in self.pools))

    def test_invalid_kinds_and_releases_preserve_every_lease(self):
        records = [self.lib.c3lrec_alloc(0) for _ in range(6)]
        buffers = [self.lib.c3lrec_alloc(1) for _ in range(2)]
        foreign = c.create_string_buffer(128)
        for kind in (-1, 2, 255):
            self.assertFalse(self.lib.c3lrec_alloc(kind))
            self.lib.c3lrec_free(kind, records[0])
        for kind, address in ((0, None), (1, None), (0, c.addressof(foreign)),
                              (0, records[0] + 1), (1, buffers[0] + 8),
                              (0, buffers[0]), (1, records[0])):
            self.lib.c3lrec_free(kind, address)
        self.assertFalse(self.lib.c3lrec_alloc(0))
        self.assertFalse(self.lib.c3lrec_alloc(1))
        self.lib.c3lrec_free(0, records[0])
        self.lib.c3lrec_free(0, records[0])
        self.assertEqual(self.lib.c3lrec_alloc(0), records[0])
        self.assertFalse(self.lib.c3lrec_alloc(0))

    def test_every_operation_restores_preexisting_interrupt_mask(self):
        for nested in (False, True):
            saved = self.lib.lrectest_enter() if nested else 0
            before = self.lib.lrectest_calls()
            for kind in range(10):
                record = self.lib.c3lrec_alloc(kind)
                self.assertEqual(self.lib.lrectest_mask(), int(nested))
                self.lib.c3lrec_free(kind, record)
                self.assertEqual(self.lib.lrectest_mask(), int(nested))
            self.assertEqual(self.lib.lrectest_calls() - before, 20)
            if nested:
                self.lib.lrectest_leave(saved)

    def test_repeated_exhaustion_and_failure_cleanup_leak_no_slots(self):
        expected = None
        for _ in range(1000):
            leased = [(kind, self.lib.c3lrec_alloc(kind))
                      for kind, count, _ in self.pools for _ in range(count)]
            self.assertTrue(all(pointer for _, pointer in leased))
            if expected is None:
                expected = leased
            self.assertEqual(leased, expected)
            self.assertFalse(self.lib.c3lrec_alloc(0))
            self.assertFalse(self.lib.c3lrec_alloc(1))
            for kind in (3, 4, 5, 6, 7, 8, 9):
                self.assertFalse(self.lib.c3lrec_alloc(kind))
            # The SDK returns a record after cleanup even if setup failed.
            for kind, pointer in reversed(leased):
                self.lib.c3lrec_free(kind, pointer)


if __name__ == "__main__":
    unittest.main()
