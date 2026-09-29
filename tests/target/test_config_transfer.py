"""Portable framing, durability failure injection and C/Python wire parity."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from tools.config_transfer.protocol import Command, decode_reply, encode_request

ROOT = Path(__file__).resolve().parents[2]


def setUpModule():
    subprocess.run([sys.executable, str(ROOT / "tools/c_standard/check.py"),
                    "--root", str(ROOT)], cwd=ROOT, check=True)


class ConfigTransferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("gcc")
        if compiler is None:
            raise AssertionError("GCC is required")
        cls.folder = tempfile.TemporaryDirectory(prefix="uthome-config-tests-")
        cls.addClassCleanup(cls.folder.cleanup)
        cls.binary = Path(cls.folder.name) / "config.exe"
        source = ROOT / "firmware/src/config"
        command = [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-I", str(source), str(ROOT / "tests/target/cfg_main.c")]
        command.extend(str(source / f"{name}.c") for name in
                       ("cfgbytes", "cfgwire", "cfgstore", "cfgctrl", "cfgpump"))
        result = subprocess.run(command + ["-o", str(cls.binary)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_interrupted_save_and_request_lifecycle(self):
        result = subprocess.run([str(self.binary)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("torn-save recovery", result.stdout)

    def exchange(self, frame):
        path = Path(self.folder.name) / "request.bin"
        path.write_bytes(frame)
        return subprocess.run([str(self.binary), str(path)], capture_output=True,
                              text=True, timeout=5)

    def test_python_requests_and_c_replies_use_the_same_bytes(self):
        for command in (Command.APPLY, Command.QUERY, Command.SAVE, Command.RECOVER, Command.PROBE):
            profile = (ROOT / "configs/nodes/distance-only.json").read_bytes() if command == Command.APPLY else b""
            revision = 19 if command in (Command.APPLY, Command.SAVE) else 0
            frame = encode_request(command, 0xFFFFFFFF, revision, profile)
            with self.subTest(command=command):
                result = self.exchange(frame)
                self.assertEqual(result.returncode, 0, result.stderr)
                reply = decode_reply(bytes.fromhex(result.stdout), command=command,
                                     request_id=0xFFFFFFFF, revision=revision)
                first = reply.last_request_id if command == Command.PROBE else reply.status
                self.assertEqual((first, reply.active_revision, reply.saved_revision), (0, 17, 16))

    def test_corruption_truncation_and_trailing_bytes_are_rejected(self):
        frame = encode_request(Command.APPLY, 1, 1, b'{"node":"test"}')
        for changed in (frame[:-1], frame + b"x", frame[:20] + bytes([frame[20] ^ 1]) + frame[21:]):
            with self.subTest(frame=changed):
                self.assertNotEqual(self.exchange(changed).returncode, 0)
        response = bytes.fromhex(self.exchange(frame).stdout)
        for changed in (response[:-1], response + b"x", response[:-1] + bytes([response[-1] ^ 1])):
            with self.assertRaises(ValueError):
                decode_reply(changed, command=Command.APPLY, request_id=1, revision=1)
        with self.assertRaises(ValueError):
            decode_reply(response, command=Command.APPLY, request_id=2, revision=1)

    def test_encoder_rejects_ambiguous_or_unbounded_requests(self):
        bad = [(Command.APPLY, 0, 1, b"{}"), (Command.APPLY, 1, 0, b"{}"),
               (Command.APPLY, 1, 1, b"x" * 8193), (Command.APPLY, 1, 1, b""),
               (Command.QUERY, 1, 0, b"{}"), (Command.QUERY, 1, 2, b""),
               (Command.SAVE, 1, 0, b""), (Command.RECOVER, 1, 1, b""),
               (Command.APPLY, True, 1, b"{}")]
        for arguments in bad:
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                encode_request(*arguments)
