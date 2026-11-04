"""Protect the comparison policy shared by reviews, discovery and captures."""
from copy import deepcopy
import unittest

from tools.config_transfer.identity import configuration_key, capability_key


class BoardIdentityTests(unittest.TestCase):
    def test_configuration_fields_and_each_state_flag_remain_significant(self):
        value = dict(active_revision=1, saved_revision=2, active_crc=3, saved_crc=4,
                     flags=0x70, request_id=5, status=0)
        original = deepcopy(value)
        expected = configuration_key(value)
        for key in ('active_revision', 'saved_revision', 'active_crc', 'saved_crc'):
            with self.subTest(key=key):
                self.assertNotEqual(expected, configuration_key(value | {key: value[key] + 1}))
        for flag in (0x10, 0x20, 0x40):
            with self.subTest(flag=flag):
                self.assertNotEqual(expected, configuration_key(value | {'flags': value['flags'] ^ flag}))
        self.assertEqual(expected, configuration_key(value | {'request_id': 99, 'status': 8, 'flags': 0x7f}))
        self.assertEqual(original, value)

    def test_only_request_id_is_excluded_from_capability_comparison(self):
        value = {'request_id': 1, 'board': 'esp32-c3-supermini', 'drivers': ('bmp388',),
                 'node_schema': '2.1.0', 'features': 7, 'max_workers': 4,
                 'max_devices': 8, 'max_profile_bytes': 8192, 'allowed_gpio_mask': 0x3fffff,
                 'hardware_qualified': False}
        original = deepcopy(value)
        expected = capability_key(value)
        self.assertEqual(expected, capability_key(value | {'request_id': 99}))
        for key in value.keys() - {'request_id'}:
            with self.subTest(key=key):
                self.assertNotEqual(expected, capability_key(value | {key: None}))
        self.assertNotEqual(expected, capability_key(value | {'future_capability': 1}))
        self.assertEqual(original, value)
