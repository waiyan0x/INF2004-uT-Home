"""Private Wi-Fi TX-power provisioning bounds and immutable build inputs."""
import json
from pathlib import Path
import tempfile
import unittest

from tools.target import network_setup


def configuration(folder):
    """Private setup syntax fixture; never valid deployment credentials."""
    values = dict(ssid='lab\u2019s AP', password='test-password')
    path = folder / 'settings.json'
    path.write_text(json.dumps(values))
    return path, values


class NetworkSetupPowerTests(unittest.TestCase):
    def test_mqtt_only_setup_has_no_server_keys_and_rejects_old_https_fields(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            path, values = configuration(folder)
            ca = folder / 'broker-ca.pem'
            ca.write_bytes(b'-----BEGIN CERTIFICATE-----\nfixture\n-----END CERTIFICATE-----\n')
            values['mqtt'] = dict(address='127.0.0.1', server_name='broker.test', port=8883,
                                  user='device', password='password', ca_file=ca.name)
            path.write_text(json.dumps(values))
            header, record = network_setup.generate(path)
            self.assertIn(b'g_setup_mqtt_ca', header)
            self.assertIn('mqtt_ca_sha256', record)
            for symbol in (b'g_setup_certificate', b'g_setup_key', b'g_setup_token', b'UTHOME_SETUP_PORT'):
                self.assertNotIn(symbol, header)
            for field, value in (('token', 't' * 32), ('certificate_file', 'server.pem'),
                                 ('key_file', 'server.key'), ('port', 8443)):
                path.write_text(json.dumps(dict(values, **{field: value})))
                with self.assertRaisesRegex(ValueError, 'invalid setup fields'):
                    network_setup.generate(path)

    def test_single_ca_encoding_preserves_bundle_and_input_identity(self):
        pem = b'-----BEGIN CERTIFICATE-----\nMAIBAA==\n-----END CERTIFICATE-----\n'
        rewrapped = pem.replace(b'MAIBAA==', b'MAIB\nAA==').replace(b'\n', b'\r\n')
        for single in (pem, rewrapped):
            self.assertEqual(network_setup.mqtt_ca_blob(single), (b'\x30\x02\x01\x00', True))
        for other in (pem + pem, b'0 prefaced bundle\n' + pem + pem, pem + b'extra', pem.replace(b'AA==', b'!')):
            self.assertEqual(network_setup.mqtt_ca_blob(other), (other + b'\0', False))
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            source, values = configuration(folder)
            ca = folder / 'broker-ca.pem'
            ca.write_bytes(pem)
            values['mqtt'] = dict(address='127.0.0.1', server_name='broker.test', port=8883,
                                  user='device', password='password', ca_file=ca.name)
            source.write_text(json.dumps(values))
            (folder / 'app').mkdir()
            (folder / 'source-manifest.json').write_text('{"sources":{}}')
            network_setup.stage(folder, source)
            record = json.loads((folder / 'source-manifest.json').read_text())['network_setup']
            self.assertEqual(record['mqtt_ca_sha256'], network_setup.digest(pem + b'\0'))
            ca.write_bytes(rewrapped)
            self.assertEqual(network_setup.generate(source)[1]['generated_sha256'], record['generated_sha256'])
            with self.assertRaisesRegex(ValueError, 'private inputs changed'):
                network_setup.verify(folder, record)

    def test_omission_preserves_default_and_explicit_integer_caps(self):
        with tempfile.TemporaryDirectory() as name:
            path, values = configuration(Path(name))
            header, record = network_setup.generate(path)
            self.assertRegex(header, rb'#define UTHOME_SETUP_TX_POWER +\(0U\)')
            self.assertEqual(record['phy_max_wifi_tx_power'], 20)
            for power, startup in ((8,10), (34,10), (40,10), (42,10), (44,11), (80,20), (84,20)):
                with self.subTest(power=power):
                    path.write_text(json.dumps(dict(values, tx_power_quarter_dbm=power)))
                    header, record = network_setup.generate(path)
                    self.assertRegex(header, rb'#define UTHOME_SETUP_TX_POWER +\('
                                     + str(power).encode('ascii') + rb'U\)')
                    self.assertEqual(record['phy_max_wifi_tx_power'], startup)

    def test_optional_cap_rejects_wrong_types_bounds_and_duplicates(self):
        with tempfile.TemporaryDirectory() as name:
            path, values = configuration(Path(name))
            for power in (None, True, False, '34', 34.0, 0, 7, 85, -1, 2**63, [], {}):
                with self.subTest(power=power):
                    path.write_text(json.dumps(dict(values, tx_power_quarter_dbm=power)))
                    with self.assertRaises(ValueError):
                        network_setup.generate(path)
            path.write_text(json.dumps(values)[:-1]
                            + ', "tx_power_quarter_dbm": 34, "tx_power_quarter_dbm": 84}')
            with self.assertRaisesRegex(ValueError, 'duplicate setup field'):
                network_setup.generate(path)

    def test_cap_change_requires_fresh_build_inputs(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            source, values = configuration(folder)
            (folder / 'app').mkdir()
            (folder / 'source-manifest.json').write_text('{"sources":{}}')
            network_setup.stage(folder, source)
            record = json.loads((folder / 'source-manifest.json').read_text())['network_setup']
            network_setup.verify(folder, record)
            source.write_text(json.dumps(dict(values, tx_power_quarter_dbm=34)))
            with self.assertRaisesRegex(ValueError, 'private inputs changed'):
                network_setup.verify(folder, record)

    def test_startup_cap_is_staged_and_cannot_be_overridden_after_review(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            source, values = configuration(folder)
            source.write_text(json.dumps(dict(values, tx_power_quarter_dbm=40)))
            (folder / 'app').mkdir()
            (folder / 'source-manifest.json').write_text('{"sources":{}}')
            network_setup.stage(folder, source)
            record = json.loads((folder / 'source-manifest.json').read_text())['network_setup']
            defaults = folder / 'network-power.defaults'
            self.assertEqual(defaults.read_bytes(), b'CONFIG_ESP_PHY_MAX_WIFI_TX_POWER=10\n')
            network_setup.verify(folder, record)
            compiled = folder / 'sdkconfig'
            compiled.write_text('CONFIG_ESP_PHY_MAX_WIFI_TX_POWER=10\n')
            network_setup.verify(folder, record, sdkconfig=compiled)
            for value in ('', 'CONFIG_ESP_PHY_MAX_WIFI_TX_POWER=20\n',
                          'CONFIG_ESP_PHY_MAX_WIFI_TX_POWER=10\n' * 2):
                compiled.write_text(value)
                with self.assertRaisesRegex(ValueError, 'compiled Wi-Fi startup power'):
                    network_setup.verify(folder, record, sdkconfig=compiled)
            defaults.write_bytes(b'CONFIG_ESP_PHY_MAX_WIFI_TX_POWER=20\n')
            with self.assertRaisesRegex(ValueError, 'private inputs changed'):
                network_setup.verify(folder, record)


if __name__ == '__main__':
    unittest.main()
