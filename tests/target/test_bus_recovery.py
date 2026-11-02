"""Exercise real C3 transfer loops with register, clock and lock faults."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HAL = ROOT / 'firmware/hal'
PORT = HAL / 'ports/esp32c3'


class BusRecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler, objcopy = shutil.which('gcc'), shutil.which('objcopy')
        if not compiler or not objcopy:
            raise AssertionError('GCC and objcopy are required')
        subprocess.run([sys.executable, str(ROOT / 'tools/c_standard/check.py')], check=True)
        folder = tempfile.TemporaryDirectory(prefix='uthome-bus-recovery-')
        cls.addClassCleanup(folder.cleanup)
        directory = Path(folder.name)
        flags = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2']
        for path in (PORT, HAL / 'include', ROOT / 'firmware/src/runtime',
                     ROOT / 'firmware/include', ROOT / 'tests/target'):
            flags.extend(['-I', str(path)])
        aliases = dict(rt_port_now_ms='busfake_now', rt_port_is_owner='busfake_owner',
                       c3pin_valid='busfake_pin_valid', c3pin_reserve='busfake_pin_reserve',
                       c3pin_release='busfake_pin_release')
        for prefix, replacement in (('c3bwait', 'busfake_wait'), ('c3ihw', 'busfake_i'),
                                    ('c3shw', 'busfake_s'), ('c3cost', 'busfake_cost')):
            header = (PORT / f'{prefix}.h').read_text(encoding='utf-8')
            for name in re.findall(r'\b(' + prefix + r'_\w+)\s*\(', header):
                aliases[name] = name.replace(prefix, replacement, 1)
        objects = []
        for module in ('c3i2c', 'c3spi'):
            obj = directory / f'{module}.o'
            subprocess.run([compiler, *flags, '-c', str(PORT / f'{module}.c'),
                            '-o', str(obj)], check=True)
            command = [objcopy]
            for source, target in aliases.items():
                command.extend(['--redefine-sym', f'{source}={target}'])
            subprocess.run([*command, str(obj)], check=True)
            objects.append(str(obj))
        sources = [HAL / f'{name}.c' for name in
                   ('i2cbus', 'spibus', 'i2cown', 'spiown', 'buslife', 'pinpool')]
        sources += [ROOT / 'tests/target' / f'{name}.c' for name in ('busfake', 'busmain')]
        cls.binary = directory / ('bus-faults.exe' if sys.platform == 'win32' else 'bus-faults')
        subprocess.run([compiler, *flags, *objects, *map(str, sources),
                        '-o', str(cls.binary)], check=True)

    def run_scenarios(self, scenarios):
        for scenario, name in scenarios.items():
            with self.subTest(scenario=name):
                result = subprocess.run([str(self.binary), str(scenario)],
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_i2c_wire_clock_and_yield_faults_recover(self):
        self.run_scenarios({1: 'nack', 2: 'arbitration', 3: 'hardware timeout',
                            4: 'deadline', 5: 'clock failure', 6: 'yield failure'})

    def test_i2c_held_clock_and_fifo_failures_recover(self):
        self.run_scenarios({7: 'pause timeout', 8: 'pause nack', 9: 'pause deadline',
                            10: 'short fifo'})

    def test_i2c_boundary_reads_preserve_final_nack(self):
        self.run_scenarios({11: 'held-clock read', 12: 'one byte', 13: 'full fifo'})

    def test_spi_stalls_clock_and_lock_failures_release_cs_and_recover(self):
        self.run_scenarios({20: 'configuration stall', 21: 'update stall',
                            22: 'transfer stall', 23: 'second chunk stall',
                            24: 'clock failure', 25: 'yield failure', 27: 'lock timeout'})

    def test_spi_multichunk_transfer_keeps_single_cs_window(self):
        self.run_scenarios({26: '130-byte transfer'})


if __name__ == '__main__':
    unittest.main()
