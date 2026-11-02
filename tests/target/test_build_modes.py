"""Build-mode admission and CLI forwarding without SDK or hardware access."""
from itertools import product
from pathlib import Path
import unittest
from unittest.mock import patch

from tools.target import qualify_kernel
from tools.target.build_modes import BuildMode, BuildSelection, QualificationError, resolve_mode


class BuildModeTests(unittest.TestCase):
    def test_only_documented_flag_combinations_are_admitted(self):
        names = ('runtime_port', 'configuration_app', 'native_uart_fixture', 'static_net_queues',
                 'wifi_probe', 'ble_probe', 'model_probe', 'ble_gatt_probe', 'ble_static_host',
                 'model_load_probe', 'senlux_probe', 'senlux_app_probe')
        runtime = {'runtime_port'}
        application = runtime | {'configuration_app'}
        expected = {(frozenset(), example) for example in (None, 'tasks', 'ble_gatt', 'udp_pingpong_c3')}
        for base in (runtime, application):
            expected.add((frozenset(base), None))
            expected.add((frozenset(base | {'static_net_queues'}), None))
        expected.add((frozenset(runtime | {'native_uart_fixture'}), None))
        for probe in ('wifi_probe', 'model_probe', 'senlux_probe'):
            expected.add((frozenset(runtime | {'static_net_queues', probe}), None))
        for probe in ('model_load_probe', 'senlux_app_probe'):
            expected.add((frozenset(application | {'static_net_queues', probe}), None))
        for options in (set(), {'ble_gatt_probe'}, {'ble_static_host'}, {'ble_gatt_probe', 'ble_static_host'}):
            expected.add((frozenset(runtime | {'static_net_queues', 'ble_probe'} | options), None))

        accepted = set()
        for values in product((False, True), repeat=len(names)):
            flags = dict(zip(names, values))
            for example in (None, 'tasks', 'ble_gatt', 'udp_pingpong_c3', '../escape'):
                try:
                    selection = resolve_mode(**flags, upstream_example=example)
                except QualificationError:
                    continue
                enabled = frozenset(name for name, value in flags.items() if value)
                accepted.add((enabled, example))
                self.assertEqual(selection.runtime_port, flags['runtime_port'])
                self.assertEqual(selection.flags(), {key: flags[key] for key in selection.flags()})
                self.assertEqual(selection.cmake_settings(),
                                 [f'-DUTHOME_{key.upper()}={int(flags[key])}' for key in selection.flags()])
                self.assertEqual(selection.evidence()['configuration_application'], flags['configuration_app'])
                self.assertEqual(selection.evidence()['static_network_queues'], flags['static_net_queues'])
        self.assertEqual(accepted, expected)

    def test_direct_selections_enforce_independent_option_requirements(self):
        for options in (dict(mode=BuildMode.WIFI), dict(mode=BuildMode.UPSTREAM, static_net_queues=True),
                        dict(mode=BuildMode.RUNTIME, ble_gatt_probe=True),
                        dict(mode=BuildMode.CONFIGURATION, upstream_example='ble_gatt')):
            with self.subTest(options=options), self.assertRaises(QualificationError):
                BuildSelection(**options)

    def test_cli_forwards_one_resolved_selection_and_named_patch_paths(self):
        with patch.object(qualify_kernel, 'build', return_value=Path('evidence.json')) as build, \
                patch('builtins.print'):
            result = qualify_kernel.main([
                'build', '--sdk', 'SDK', '--work-dir', 'fresh', '--runtime-port',
                '--configuration-app', '--static-net-queues', '--model-load-probe',
                '--compat-patch', 'compat.patch', '--resource-patch', 'resource.patch'])
        self.assertEqual(result, 0)
        self.assertEqual(len(build.call_args.args), 3)
        options = build.call_args.kwargs
        self.assertEqual(options['selection'].mode, BuildMode.MODEL_LOAD)
        self.assertTrue(options['selection'].static_net_queues)
        self.assertEqual(options['compat_patch'], Path('compat.patch').resolve())
        self.assertEqual(options['resource_patch'], Path('resource.patch').resolve())
