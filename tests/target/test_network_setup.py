"""Private Wi-Fi TX-power provisioning bounds and immutable build inputs."""
import json
from pathlib import Path
import tempfile
import unittest

from tools.target import network_setup


def configuration(folder):
    """Syntax-only PEM fixtures; never valid deployment credentials."""
    (folder / 'certificate.pem').write_bytes(
        b'-----BEGIN CERTIFICATE-----\nfixture\n-----END CERTIFICATE-----\n')
    (folder / 'key.pem').write_bytes(
        b'-----BEGIN PRIVATE KEY-----\nfixture\n-----END PRIVATE KEY-----\n')
    values = dict(ssid='lab\u2019s AP', password='test-password', token='t' * 32,
                  certificate_file='certificate.pem', key_file='key.pem')
    path = folder / 'settings.json'
    path.write_text(json.dumps(values))
    return path, values


class NetworkSetupPowerTests(unittest.TestCase):
    def test_omission_preserves_default_and_explicit_integer_caps(self):
        with tempfile.TemporaryDirectory() as name:
            path, values = configuration(Path(name))
            header, _ = network_setup.generate(path)
            self.assertRegex(header, rb'#define UTHOME_SETUP_TX_POWER +\(0U\)')
            for power in (8, 34, 42, 84):
                with self.subTest(power=power):
                    path.write_text(json.dumps(dict(values, tx_power_quarter_dbm=power)))
                    header, _ = network_setup.generate(path)
                    self.assertRegex(header, rb'#define UTHOME_SETUP_TX_POWER +\('
                                     + str(power).encode('ascii') + rb'U\)')

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


if __name__ == '__main__':
    unittest.main()
