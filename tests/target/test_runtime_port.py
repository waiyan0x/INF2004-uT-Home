"""Inject kernel failures into the real adapter; execution is tested on board."""
from pathlib import Path
import shutil
import json
import os
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


class RuntimePortFailureTests(unittest.TestCase):
    def test_ownership_survives_kernel_failures(self):
        compiler = shutil.which("gcc")
        self.assertIsNotNone(compiler, "GCC is required")
        with tempfile.TemporaryDirectory(prefix="uthome-port-faults-") as folder:
            binary = Path(folder) / "port_faults.exe"
            command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                       "-DUTHOME_RUNTIME_UTKERNEL", "-I", str(ROOT / "tests/target/fake_kernel"),
                       "-I", str(ROOT / "firmware/include"), "-I", str(ROOT / "firmware/src/runtime"),
                       "-I", str(ROOT / "firmware/hal/include"),
                       "-I", str(ROOT / "firmware/hal/ports/host_sim"),
                       "-I", str(ROOT / "firmware/drivers/bmp388"),
                       "-I", str(ROOT / "firmware/drivers/hc_sr04"),
                       "-I", str(ROOT / "firmware/drivers/pwm_output"),
                       "-I", str(ROOT / "firmware/drivers/ssd1306_spi"),
                       "-I", str(ROOT / "firmware/drivers/hlk_ld2450"),
                       "-I", str(ROOT / "firmware/third_party/bmp3"),
                       "-I", str(ROOT / "firmware/src/config"),
                       "-I", str(ROOT / "firmware/src/discovery"),
                       "-I", str(ROOT / "firmware/src/services"),
                       "-I", str(ROOT / "firmware/src/automation"),
                       "-I", str(ROOT / "firmware/third_party/cjson"),
                       "-DCJSON_NESTING_LIMIT=8",
                       str(ROOT / "tests/target/tkfake_main.c"),
                       str(ROOT / "tests/target/periph.c"),
                       str(ROOT / "tests/target/oledtest.c"),
                       str(ROOT / "tests/target/owntest.c"),
                       str(ROOT / "tests/target/spowntest.c"),
                       str(ROOT / "tests/target/uarttest.c"),
                       str(ROOT / "tests/target/gpiotest.c"),
                       str(ROOT / "tests/target/bindtest.c"),
                       str(ROOT / "tests/target/disctest.c"),
                       str(ROOT / "firmware/src/discovery/discuart.c"),
                       str(ROOT / "tests/target/uartdisc.c"),
                       str(ROOT / "firmware/src/discovery/disci2c.c"),
                       str(ROOT / "firmware/src/discovery/cfgdisc.c"),
                       str(ROOT / "firmware/src/config/cfgpump.c"),
                       str(ROOT / "tests/target/servtest.c"),
                       str(ROOT / "firmware/hal/pulseio.c"),
                       str(ROOT / "firmware/hal/pwmio.c"),
                       str(ROOT / "firmware/hal/ports/host_sim/gpiofake.c"),
                       str(ROOT / "firmware/drivers/hc_sr04/hc_sr04.c"),
                       str(ROOT / "firmware/drivers/hc_sr04/hcinst.c"),
                       str(ROOT / "firmware/drivers/pwm_output/pwminst.c"),
                       str(ROOT / "firmware/hal/uartio.c"),
                       str(ROOT / "firmware/hal/ports/host_sim/uartfake.c"),
                       str(ROOT / "firmware/drivers/hlk_ld2450/ldparse.c"),
                       str(ROOT / "firmware/drivers/hlk_ld2450/ld2450.c"),
                       str(ROOT / "firmware/drivers/hlk_ld2450/ldinst.c"),

                       str(ROOT / "firmware/hal/pinpool.c"),
                       str(ROOT / "firmware/hal/spibus.c"),
                       str(ROOT / "firmware/hal/spiown.c"),
                       str(ROOT / "firmware/hal/ports/host_sim/spifake.c"),
                       str(ROOT / "firmware/drivers/ssd1306_spi/ssd1306.c"),
                       str(ROOT / "firmware/drivers/ssd1306_spi/oledinst.c"),
                       str(ROOT / "firmware/hal/i2cbus.c"),
                       str(ROOT / "firmware/hal/i2cown.c"),
                       str(ROOT / "firmware/hal/ports/host_sim/i2cfake.c"),
                       str(ROOT / "firmware/drivers/bmp388/bmp388.c"),
                       str(ROOT / "firmware/drivers/bmp388/bmpinst.c"),
                       str(ROOT / "firmware/third_party/bmp3/bmp3.c"),
                       str(ROOT / "firmware/src/config/drvreg.c"),
                       str(ROOT / "firmware/src/runtime/tkport.c"),
                       str(ROOT / "firmware/src/runtime/activate.c"),
                       str(ROOT / "firmware/src/runtime/shared.c"),
                       str(ROOT / "firmware/src/services/sampcache.c"),
                       str(ROOT / "firmware/src/services/statusmon.c"),
                       str(ROOT / "firmware/src/services/telemetry.c"),
                       str(ROOT / "firmware/src/services/oledview.c"),
                       str(ROOT / "firmware/src/runtime/component.c"),
                       str(ROOT / "firmware/src/messaging/msgbus.c"),
                       str(ROOT / "firmware/src/health/health.c"),
                       str(ROOT / "firmware/src/health/watchdog.c"),
                       str(ROOT / "firmware/src/config/jsonscan.c"),
                       str(ROOT / "firmware/src/config/cfgjson.c"),
                       str(ROOT / "firmware/src/config/cfgplan.c"),
                       str(ROOT / "firmware/src/config/cfgdev.c"),
                       str(ROOT / "firmware/src/config/cfgrule.c"),
                       str(ROOT / "firmware/src/automation/rulecalc.c"),
                       str(ROOT / "firmware/src/config/cfgbind.c"),
                       str(ROOT / "firmware/src/config/cfgbytes.c"),
                       str(ROOT / "firmware/src/config/cfgwire.c"),
                       str(ROOT / "firmware/src/config/cfgstore.c"),
                       str(ROOT / "firmware/src/config/cfgctrl.c"),
                       str(ROOT / "firmware/third_party/cjson/cJSON.c"),
                       "-lm",
                       "-o", str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            profile = json.loads((ROOT / "configs/nodes/distance-only.json").read_bytes())
            prototype = json.loads((ROOT / "configs/nodes/first-prototype.json").read_bytes())
            profile["interfaces"] = [prototype["interfaces"][1]]
            display = prototype["devices"][1]
            display["interface"]["cs_gpio"] = 0
            display["behaviour"]["sources"] = [profile["queues"][0]["id"]]
            profile["devices"].append(display)
            profile_path = Path(folder) / "display.json"
            profile_path.write_text(json.dumps(profile, separators=(",", ":")), encoding="utf-8")
            rules = json.loads((ROOT / "configs/nodes/distance-controlled-output.json").read_bytes())
            rules["interfaces"] = profile["interfaces"]
            rules["devices"].append(display)
            rule_path = Path(folder) / "rules.json"
            rule_path.write_text(json.dumps(rules,separators=(",",":")),encoding="utf-8")
            alternate = json.loads((ROOT / "configs/nodes/two-environment-sensors.json").read_bytes())
            alternate["schema_version"] = "2.1.0"
            alternate["devices"] = [alternate["devices"][0], rules["devices"][1]]
            alternate["devices"][1]["interface"]["output_gpio"] = 6
            alternate["rules"] = [dict(rules["rules"][0], source="room_1", metric="pressure_pa",
                                        comparison="above",threshold=0,duty_permille=200)]
            alt_path = Path(folder) / "alternate.json"
            alt_path.write_text(json.dumps(alternate,separators=(",",":")),encoding="utf-8")
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10,
                env=dict(os.environ, UTHOME_DISPLAY_PROFILE=str(profile_path),
                         UTHOME_RULE_PROFILE=str(rule_path), UTHOME_ALT_RULE_PROFILE=str(alt_path)))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("I2C permanent-lock ownership checks passed.", result.stdout)
            self.assertIn("SPI permanent-lock ownership checks passed.", result.stdout)

            from tools.config_transfer.capabilities import decode_capabilities
            from tools.config_transfer.discovery import decode_discovery, discovery_request
            discovery_frame = next(bytes.fromhex(line.split(":", 1)[1]) for line in result.stdout.splitlines()
                                   if line.startswith("DISCOVERY:"))
            discovery = decode_discovery(discovery_frame, request_id=27, revision=0,
                                         payload=discovery_request(123, wiring_confirmed=True))
            self.assertEqual(discovery.result, "complete")
            self.assertEqual([record["value"] for record in discovery.records], [0x50, 0x60])
            uart_frame = next(bytes.fromhex(line.split(':', 1)[1]) for line in result.stdout.splitlines()
                              if line.startswith('UARTDISCOVERY:'))
            uart = decode_discovery(uart_frame, request_id=27, revision=0,
                                    payload=discovery_request(123, wiring_confirmed=True, preset_code=2))
            self.assertEqual(uart.result, 'complete')
            self.assertEqual(uart.records[0]['valid_reports'], 3)
            self.assertEqual(uart.records[0]['received_bytes'], 90)
            self.assertEqual(uart.records[0]['window_ms'], 1000)
            caps_frame = next(bytes.fromhex(line.split(":",1)[1]) for line in result.stdout.splitlines()
                              if line.startswith("CAPABILITIES:"))
            caps = decode_capabilities(caps_frame, request_id=27)
            self.assertEqual(caps.features, 7)
            self.assertEqual(set(caps.drivers), {"bmp388", "ssd1306_spi", "hlk_ld2450", "hc_sr04", "pwm_output"})
            self.assertFalse(caps.hardware_qualified)
            self.assertEqual((caps.board,caps.node_schema), ("esp32-c3-supermini", "2.1.0"))
            from tools.config_transfer.telemetry import decode_telemetry
            frames = [bytes.fromhex(line.split(":", 1)[1]) for line in result.stdout.splitlines()
                      if line.startswith("TELEMETRY:")]
            self.assertEqual(len(frames), 2)
            first, second = [decode_telemetry(frame, request_id=27) for frame in frames]
            self.assertEqual([s["quality"] for s in first.samples], ["stale", "missing", "fresh", "fresh"])
            self.assertEqual(first.samples[0]["value"]["distance_mm"], 321)
            self.assertEqual(first.samples[2]["value"], {"pressure_pa": 100000, "temperature_c": -12.5})
            target = first.samples[3]["value"]["targets"][0]
            self.assertEqual((target["x_mm"], target["y_mm"], target["speed_cm_s"]), (-123, 456, -7))
            self.assertEqual(second.samples[-1]["source_index"], 65535)
            self.assertEqual(second.samples[-1]["value"], {"event": 5, "occurrences": 1})
            self.assertEqual(second.health["event_counts"][5], 2)
