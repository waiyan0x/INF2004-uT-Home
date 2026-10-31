# Configuration profiles

Versioned JSON selects supported devices, wiring, timing, queues and rules.
Use UTF-8 with two-space indentation and a `schema_version`. GPIO values are
printed ESP32-C3 GPIO numbers, not header positions. Node schemas 2.0.0/2.1.0
reference reviewed descriptors and board/build limits in the [catalogue](../catalogue/README.md).

## Choose a profile

| Location | Purpose |
|---|---|
| `nodes/c3-protocol-demo.json` | Preserved three-peripheral C3 bench profile: BMP388 I2C, six-pin OLED SPI and LD2450 UART |
| `nodes/two-environment-sensors.json` | Independent instances sharing I2C at distinct addresses |
| `nodes/distance-only.json` | Distance sensor without another sensor/display dependency |
| `nodes/distance-controlled-output.json` | Schema 2.1 typed rule and PWM example |
| `nodes/first-prototype.json` | Historical mixed demonstration; not deployable on the current C3 binding |
| `builds/` | Host fixtures versus compiled/qualified target availability |
| `scenarios/` | Expected demonstration behavior |
| `invalid/` | Profiles validation must reject |

The original prototype uses GPIO2 for OLED CS. **Do not wire that assignment:**
the current backend rejects it. Use the bench profiles below and the
[hardware guide](../CONTRIBUTING.md) instead. Actual driver support and narrower
target parser/resource limits must pass connected review before deployment.

`c3-protocol-demo.json` preserves the actual restored bench settings (compact
CRC 3623560383), including BMP388 address `0x77`, SPI at 4 MHz and UART at
256000 baud. The six-pin OLED has no CS wire; GPIO4 is reserved but left unwired
in this reviewed layout. Follow layout A in the hardware guide; do not infer
wire roles from cable colours. This profile has scoped USB/peripheral evidence,
not wireless, universal module or complete hardware qualification. It is
available as a normal profile in the Desk without a UI change or auto-Apply.

## Target limits

- Ordinary GPIO: 0, 1, 3, 4, 5, 6, 7, 10; UART1 RX20/TX21.
- Preserve native USB18/19, BOOT/LED8/9, GPIO2 and flash/unavailable pins.
- Four active peripheral workers, eight configured instances, four queues of
  depth eight, 16 KiB admitted stack demand. Physical worker reservation is four
  permanent 8 KiB stacks; disabled entries still reserve wiring.
- Profiles are at most 8192 compact bytes. Parsing uses 512 fixed cJSON node
  slots and 12 KiB of text storage; exhaustion rejects the candidate without
  heap fallback. Passing the Python planner alone is insufficient.
- The target-pending manifest intentionally advertises no hardware-qualified
  peripherals. Compiled support reported by a connected build is separate.

Validate the entire candidate before activation. Failed admission preserves the
working profile. Failed cleanup/rollback may require explicit recovery; do not
assume an active-but-unsaved profile will survive reboot. See the
[configuration contract](../firmware/src/config/README.md).

## Bench profiles

Use layout A for BMP388 + OLED + radar and layout B for distance + LED. They
cannot all share the current pin budget. Generate eight profiles (empty, BMP,
BMP/OLED, radar, combined A, radar/LED, distance, distance/LED) with the script
below. It preserves repository examples, creates a temporary output directory
and uses a host preflight manifest; it does not qualify or deploy hardware.
The [hardware guide](../CONTRIBUTING.md) supplies power and wiring instructions.

<details>
<summary>Generate the bench profiles (PowerShell, from the repository root)</summary>

Run this from the repository root after installing the Python requirements.
It writes profiles to a **new temporary directory**, prints that directory, and
leaves the repository examples unchanged. It performs no board operations.
Copy the generated files into the session evidence folder when testing ends.
The local target-limit check is preparation; review again against the connected
board before deployment. Adjust BMP addresses to match your breakout.

```powershell
@'
from copy import deepcopy
from pathlib import Path
import json
import tempfile
from tools.config_validator.planner import Catalogue, build_plan

root = Path.cwd()
def read(name):
    return json.loads((root / name).read_bytes())

base = read("configs/nodes/first-prototype.json")
base["runtime"]["health_report_interval_ms"] = 1000
base["interfaces"][0]["clock_hz"] = 100000
base["interfaces"][1]["clock_hz"] = 4000000
base["devices"][0]["behaviour"]["interval_ms"] = 500
base["devices"][0]["behaviour"]["timeout_ms"] = 100
base["devices"][0]["interface"]["address"] = "0x77"
base["devices"][1]["interface"]["cs_gpio"] = 4
base["devices"][1]["interface"].pop("cs_pullup_ohms", None)
base["devices"][1]["behaviour"]["timeout_ms"] = 20

def subset(drivers):
    doc = deepcopy(base)
    doc["devices"] = [d for d in doc["devices"] if d["driver"] in drivers]
    refs = {d["interface"].get("ref") for d in doc["devices"]}
    doc["interfaces"] = [i for i in doc["interfaces"] if i["id"] in refs]
    destinations = {d["behaviour"].get("destination") for d in doc["devices"]}
    if drivers:
        destinations.add("system.health")
    doc["queues"] = [q for q in doc["queues"] if q["id"] in destinations]
    for dev in doc["devices"]:
        if dev["driver"] == "ssd1306_spi":
            dev["behaviour"]["sources"] = [q["id"] for q in doc["queues"]]
    return doc

combined = subset({"bmp388", "ssd1306_spi", "hlk_ld2450"})
distance_led = read("configs/nodes/distance-controlled-output.json")
radar_led = subset({"hlk_ld2450"})
radar_led["schema_version"] = "2.1.0"
led = deepcopy(distance_led["devices"][1])
led["interface"]["output_gpio"] = 5
radar_led["devices"].append(led)
rule = deepcopy(distance_led["rules"][0])
rule.update(source="presence_sensor", metric="target_count", comparison="above",
            threshold=0, hysteresis=0)
radar_led["rules"] = [rule]

profiles = {
    "00-empty": subset(set()),
    "01-bmp": subset({"bmp388"}),
    "02-bmp-oled": subset({"bmp388", "ssd1306_spi"}),
    "03-radar": subset({"hlk_ld2450"}),
    "04-combined": combined,
    "05-radar-led": radar_led,
    "06-distance": read("configs/nodes/distance-only.json"),
    "07-distance-led": distance_led,
}
manifest = read("configs/builds/host-reference.json")
manifest.update(backend="target", build_id="lab-preflight-not-qualification")
catalogue = Catalogue(root)
folder = Path(tempfile.mkdtemp(prefix="uthome-acceptance-20260929-"))
for name, doc in profiles.items():
    doc["node_id"] = "lab-" + name
    plan = build_plan(doc, manifest, catalogue)
    raw = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    if len(raw) > 8192:
        raise ValueError("Profile exceeds target size: " + name)
    (folder / (name + ".json")).write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    print(name, plan["resources"])
print("Load lab files from:", folder)
'@ | python -B -
```

</details>

## Legacy reference

Baseline 1.0 and old ESP32-S3/prototype-specific decisions are historical, not
current setup instructions. Their full text remains in Git at commit
`37319fbd55ca622779eca498519128e6a33bcaa5`, path
`configs/PROTOTYPE_REFERENCE.md`. The [root guide](../README.md#platform-contract)
defines today's platform baseline. Existing JSON examples are preserved.
