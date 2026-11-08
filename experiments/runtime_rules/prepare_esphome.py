"""Stage an ESPHome experiment from the same C sources; never change vendors."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, default=ROOT/"build/esphome-runtime")
    p.add_argument("--secrets", type=Path,
                   help="Private ESPHome secrets.yaml with wifi_ssid, wifi_password, mqtt_broker")
    p.add_argument("--build-path", type=Path)
    args = p.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    component = out/"components/runtime_rules"
    component.mkdir(parents=True, exist_ok=True)
    sources = list((HERE/"component").glob("*"))
    sources += [ROOT/("firmware/src/services/transaction/"+name)
                for name in ("txrule.c", "txrule.h", "txwire.c", "txwire.h")]
    manifest = {}
    for source in sources:
        if source.is_file():
            shutil.copyfile(source, component/source.name)
            manifest[str(source.relative_to(ROOT))] = hashlib.sha256(source.read_bytes()).hexdigest()
    if args.secrets:
        shutil.copyfile(args.secrets, out/"secrets.yaml")
    elif not (out/"secrets.yaml").exists():
        (out/"secrets.yaml").write_text(
            'wifi_ssid: "uthome-build-only.invalid"\n'
            'wifi_password: "build-only-password"\n'
            'mqtt_broker: "192.0.2.1"\n', encoding="utf-8")
    build_path = (args.build_path or out/"build").resolve().as_posix()
    config = f"""esphome:
  name: uthome_rule_lab
  build_path: "{build_path}"
esp32:
  variant: esp32c3
  board: esp32-c3-devkitm-1
  flash_size: 4MB
  framework:
    type: esp-idf
logger:
  baud_rate: 115200
  hardware_uart: USB_SERIAL_JTAG
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  reboot_timeout: 0s
mqtt:
  broker: !secret mqtt_broker
  discovery: false
  reboot_timeout: 0s
i2c:
  id: lab_bus
  sda: GPIO0
  scl: GPIO1
  frequency: 100kHz
  scan: false
external_components:
  - source:
      type: local
      path: components
runtime_rules:
  i2c_id: lab_bus
  topic_prefix: uthome/rule-lab
"""
    (out/"device.yaml").write_text(config, encoding="utf-8")
    (out/"source-manifest.json").write_text(json.dumps(manifest, indent=2))
    print(out/"device.yaml")


if __name__ == "__main__":
    main()
