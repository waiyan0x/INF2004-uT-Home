"""Compare Python workload features with independently admitted target plans."""
from copy import deepcopy
import ctypes as c
import json
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

from ai import workload
from ai.tests import test_protocol
from ai.timing import export_profile, prepare

ROOT = Path(__file__).resolve().parents[2]


class FeatureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        gcc = shutil.which('gcc')
        if gcc is None:
            raise AssertionError('GCC required')
        cls.temp = tempfile.TemporaryDirectory(prefix='uthome-adfeat-')
        cls.addClassCleanup(cls.temp.cleanup)
        dll = Path(cls.temp.name) / 'adfeat.dll'
        includes = ['firmware/src/config', 'firmware/src/services', 'firmware/src/automation',
                    'firmware/third_party/cjson', 'firmware/include']
        sources = ['firmware/src/config/' + name + '.c' for name in
                   ('jsonscan', 'cfgjson', 'cfgplan', 'cfgdev', 'cfgrule')]
        sources += ['firmware/third_party/cjson/cJSON.c', 'firmware/src/services/adfeat.c',
                    'tests/ai/adfmain.c']
        command = [gcc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2',
                   '-DCJSON_NESTING_LIMIT=8', '-shared', '-Wl,--export-all-symbols']
        for name in includes:
            command += ['-I', str(ROOT / name)]
        subprocess.run(command + [str(ROOT / name) for name in sources] + ['-lm', '-o', str(dll)],
                       check=True, capture_output=True)
        cls.lib = c.CDLL(str(dll))
        if sys.platform == 'win32':
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        cls.run_plan = cls.lib.adfmain_run
        cls.run_plan.argtypes = [c.c_char_p, c.c_size_t, c.c_size_t, c.c_uint32, c.POINTER(c.c_uint32)]
        cls.run_plan.restype = c.c_int
        # Only synthetic fixtures and configuration inputs are used here; no
        # hardware recordings, model labels or active-study evaluation data.
        test_protocol.ProtocolTests.setUpClass()
        cls.study = test_protocol.ProtocolTests.study

    def read(self, profile, target=0, goal=800):
        raw = json.dumps(profile, separators=(',', ':')).encode('ascii')
        vector = (c.c_uint32 * 18)(*([0xDEADBEEF] * 18))
        result = self.run_plan(raw, len(raw), target, goal, vector)
        return result, list(vector)

    def check_experiment(self, experiment, interval):
        profile = export_profile(experiment, interval)
        target = next(i for i, device in enumerate(profile['devices'])
                      if device['device_id'] == experiment['target']['instance_id'])
        result, actual = self.read(profile, target, experiment['scenario']['goal']['limit_ms'])
        self.assertEqual(result, 0)
        self.assertEqual(actual, workload.features(experiment, interval))

    def test_all_synthetic_protocol_workloads_match_python(self):
        for case in self.study['cases']:
            experiment = case['experiment']
            for trial in experiment['trials']:
                with self.subTest(family=experiment['scenario']['family_id'], interval=trial['interval_ms']):
                    self.check_experiment(experiment, trial['interval_ms'])

    def test_priority_goal_and_fractional_rate_rounding_match(self):
        rng = random.Random(9127)
        original = next(case['experiment'] for case in self.study['cases']
                        if case['experiment']['scenario']['family_id'] == 'all-current')
        for _ in range(32):
            experiment = deepcopy(original)
            experiment['scenario']['goal']['limit_ms'] = rng.randint(1, 60000)
            for device in experiment['profile']['devices']:
                device['task']['priority'] = rng.randint(5, 19)
            experiment['profile']['devices'][1]['behaviour']['refresh_interval_ms'] = rng.choice([173, 501, 997])
            experiment = prepare(experiment['profile'], experiment['build_manifest'], experiment['scenario'])
            self.check_experiment(experiment, 500)

    def test_unknown_supported_gpio_abstains_after_successful_admission(self):
        profile = json.loads((ROOT / 'configs/nodes/distance-only.json').read_text())
        result, vector = self.read(profile)
        self.assertEqual(result, -1)  # -2 would mean normal configuration failed.
        self.assertEqual(vector, [0] * 18)

    def test_nonproducer_target_and_invalid_goals_clear_output(self):
        experiment = next(case['experiment'] for case in self.study['cases']
                          if case['experiment']['scenario']['family_id'] == 'all-current')
        profile = export_profile(experiment, 500)
        for target, goal in [(1, 800), (2, 800), (8, 800), (0, 0), (0, 60001), (0, 0xFFFFFFFF)]:
            with self.subTest(target=target, goal=goal):
                result, vector = self.read(profile, target, goal)
                self.assertEqual(result, -1)
                self.assertEqual(vector, [0] * 18)

    def test_declared_uart_shape_is_admitted_before_features(self):
        experiment = next(case['experiment'] for case in self.study['cases']
                          if case['experiment']['scenario']['family_id'] == 'all-current')
        profile = export_profile(experiment, 500)
        profile['devices'][2]['behaviour']['frame']['size_bytes'] = 31
        self.assertEqual(self.read(profile)[0], -2)
        profile['devices'][2]['behaviour']['frame']['size_bytes'] = 30
        result, vector = self.read(profile)
        self.assertEqual(result, 0)
        self.assertEqual(vector[16], 300)


if __name__ == '__main__':
    unittest.main()
