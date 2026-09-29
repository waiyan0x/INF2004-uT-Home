"""Compile and run Member 1's host modules and integrated lifecycle regressions."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def setUpModule():
    """Reject nonconforming C before the suite can compile project sources."""
    subprocess.run(
        [sys.executable, str(ROOT / "tools/c_standard/check.py"), "--root", str(ROOT)],
        cwd=ROOT, check=True,
    )
MODULES = [
    "firmware/src/runtime/runtime.c", "firmware/src/runtime/hostport.c",
    "firmware/src/runtime/component.c", "firmware/src/config/drvreg.c",
    "firmware/src/messaging/msgbus.c", "firmware/src/health/health.c",
    "firmware/src/health/hlthbus.c", "firmware/src/health/watchdog.c",
]


class RuntimeIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("gcc")
        if not compiler:
            raise RuntimeError("GCC is required for runtime integration tests")
        cls.temporary = tempfile.TemporaryDirectory(prefix="uthome-runtime-tests-")
        cls.flags = [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                     "-I", str(ROOT / "firmware/include"), "-I", str(ROOT / "firmware/src/runtime")]

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def build_and_run(self, source, modules=MODULES):
        binary = Path(self.temporary.name) / (Path(source).stem + ".exe")
        result = subprocess.run(self.flags + [str(ROOT / source)] + [str(ROOT / name) for name in modules]
                                + ["-o", str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


def add_test(source):
    def run(self):
        modules = MODULES
        if Path(source).stem == "guard_main":
            modules = ["firmware/src/runtime/runtime.c", "firmware/src/runtime/tkport.c"]
        self.build_and_run(source, modules)
    setattr(RuntimeIntegrationTests, "test_" + Path(source).stem, run)


for folder in ("tests/unit", "tests/integration", "tests/fault_injection"):
    for source in sorted((ROOT / folder).glob("*.c")):
        add_test(str(source.relative_to(ROOT)))


if __name__ == "__main__":
    unittest.main()
