# Hardware acceptance guide

The board is confirmed as an **ESP32-C3 SuperMini**. The wiring, profiles and
firmware in this guide target that board.
Start with one module, then test a supported combination using the same firmware.
This is the physical acceptance procedure. Offline AI work can proceed
separately, but it does not qualify the hardware.
The latest application has passed software/build checks. Record physical results for each session. Earlier partial results are in the
[evidence index](firmware/qualification/README.md#peripheral-bench-observations);
this procedure does not certify the hardware.

For the bench: check the inventory and power first, use the wiring tables in
sections 3–4, then follow the profile-change procedure and test table in
sections 6–7. Use the [installation procedure](tools/target/README.md#install-the-configuration-application)
if needed and [generate bench profiles](configs/README.md#bench-profiles).

## 1. Bring these items and identify the modules

- ESP32-C3 SuperMini, a **USB data cable**, laptop, breadboard and jumper wires.
- BMP388 breakout, **128×64 SSD1306 in 4-wire SPI mode**, HLK-LD2450 and HC-SR04.
- External LED, 1 kΩ resistor, 10 kΩ and 15 kΩ resistors for the echo divider,
  and another 10 kΩ resistor for the LED output pulldown.
- Multimeter, ruler/flat target, and preferably an oscilloscope or logic analyser.
  A logic analyser must accept the voltage at the point being probed.
- Suitable regulated 5 V supply for the 5 V modules. The LD2450 supply must have
  more than 200 mA capacity; allow additional capacity for other loads.
  See [Hi-Link's protocol document, section 1.2](https://make.net.za/wp-content/datasheets/HLK%20LD2450%20Serial%20Communication%20Protocol%20v1.03.pdf).

**Before applying power:** record each breakout's exact model and printed pin
labels. Header order and wire colours vary. The tables below identify signals,
not positions counted from the edge of a board. Confirm that the BMP388 and OLED
breakouts support a 3.3 V supply and 3.3 V signals. A pin labelled `VIN`, `VCC`
or `3V3` must be interpreted using that breakout's documentation.

The usual seven-pin SPI OLED exposes clock, data, CS, DC and reset. The supplied
six-pin panel exposes GND, VCC, SCL, SDA, RES and DC; see its bench wiring note
below. Its exact controller marking is unconfirmed, but it rendered live
128x64 output with the existing SSD1306 driver during this session.
A four-pin I2C-only display, SH1106 display or 128×32 panel needs different
firmware support. Do not connect it using the SPI table. BMP280/BMP390 and
LD2410 are also not substitutes for the configured drivers.

Use an external LED for today's first output test; it was not in the supplied
inventory, so bring one or use a scope to check PWM. The optional L298N/TT motor test
needs its supply ratings and module jumper arrangement confirmed first.
GPIO is a control signal, not motor power.

### How the supplied inventory maps to today's firmware

| Item available | Today's use |
|---|---|
| BMP388 | Supported through I2C; use that path, not SPI |
| 0.96-inch OLED, currently 4-wire SPI | Use SPI after confirming SSD1306 controller and 128×64 geometry |
| HLK-LD2450 | Supported UART target reports |
| HC-SR04 | Supported GPIO trigger/echo with level conversion |
| L298N + yellow TT motor | Optional fixed-direction enable/PWM test after LED/scope and power checks; no software direction or speed-feedback control |
| B-LUX, possibly B-LUX-V30B | No compiled driver; verify model and leave unwired for this acceptance session |
| W25QXX external flash | No compiled driver or MISO binding; leave unwired. Configuration persistence currently uses a partition of the ESP's onboard flash |
| IR optical encoder/speed sensor | No configured pulse-count/speed component; leave unwired for this session |
| MH Sensor Series module | Exact sensing function, output type and voltage are unknown; leave unwired until identified |

General-purpose operation means selecting supported drivers and profiles within
the board limits. Having an I2C/SPI/GPIO connector does not make an unknown
module automatically supported. SPI sharing also needs compatible bus settings,
separate CS signals and any required MISO support; spare wires alone are insufficient.

## 2. Power and board preparation

1. Put the SuperMini across the breadboard centre gap, with each header row on
   separate strips. Check for shorts and split power rails with power off.
2. Use **printed GPIO numbers**: `0` means GPIO0, not the first header pin.
   `G`/`GND`, `3.3`/`3V3` and `5V` are power pins, not GPIOs.
3. Make one common ground: ESP GND, module grounds and any external supply's
   negative terminal. Keep the 3.3 V and 5 V positive rails separate.
4. Power the ESP through USB. Power verified 3.3 V BMP/OLED breakouts from its
   3V3 rail within the board regulator's capacity. Power HC-SR04/LD2450 from 5 V.
   Using the board's 5 V pin is acceptable only after checking its USB power path
   and the cable/board current budget. Otherwise use a separate regulated supply
   for those modules, sharing ground with the ESP.
5. **Do not connect an external 5 V positive supply to the USB-powered board's
   5 V pin** unless that exact board's power arrangement has been verified.
   Never connect 5 V to 3V3 or directly to an ESP GPIO. ESP32-C3 input levels
   are specified relative to its 3.3 V supply in
   [Espressif's datasheet, section 5.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf).
6. Disconnect USB **and** external power before changing wires. Avoid leaving
   powered module signal outputs connected to an unpowered ESP. Start with
   short jumpers and have another person check supply polarity and signal paths.

The current backend permits ordinary GPIO **0, 1, 3, 4, 5, 6, 7, 10** and fixed
UART **RX20/TX21**. Leave GPIO2, 8/9, USB18/19 and flash/unavailable pins alone.
These are the project's [compiled binding limits](firmware/src/config/README.md#admission),
not a claim about every possible use of the chip.

## 3. Wiring layout A: BMP388, OLED and radar

Add modules one at a time using the test order below. These connections can
coexist; HC-SR04 is tested separately in layout B.

### BMP388 over I2C

| BMP388 breakout signal | Connect to |
|---|---|
| Documented 3.3 V-compatible power input | ESP 3V3 |
| GND | Common GND |
| SDA / SDI | GPIO0 |
| SCL / SCK | GPIO1 |
| CS / CSB, if exposed and not already strapped | 3.3 V logic supply to select I2C |
| SDO / address select, if exposed | GND for address `0x76`; 3.3 V for `0x77` |
| INT | Leave unconnected for this polling test |

Check existing straps before adding CS/SDO connections; do not short an opposing
strap. Do not leave address select floating. SDA/SCL need pullups to **3.3 V**:
use the breakout's fitted pullups, or fit about 4.7 kΩ from each line to 3.3 V
if absent. Do not add duplicate pullups unnecessarily. These signal names,
address choices and I2C connections follow the
[Bosch BMP388 datasheet, sections 5–6](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp388-ds001.pdf).

The generated lab profiles use `0x77`, a 100 kHz bus, a 500 ms sample period
and a 100 ms I/O timeout. These settings started the supplied BMP388 successfully
on the ESP32-C3 during the 2026-09-29 bench session; the earlier 20 ms timeout
failed activation. The timeout also covers sensor initialization, not just reads.
For a breakout strapped to `0x76`, change the address in **every** BMP profile
you use. Address agreement is required; an address is not auto-detected.

For the supplied purple breakout's four-wire cable, the user measured continuity
as **green = GND, yellow = VIN, red = SDA, black = SCL**. The red SDA lead was
extended with purple to GPIO0; black SCL went to GPIO1. VIN/GND measured about
3.28 V when powered from ESP 3V3. This identifies that tested cable only;
recheck continuity with all power disconnected if it is replaced or rewired.

### SSD1306 OLED over SPI

| OLED breakout signal | Connect to |
|---|---|
| Documented 3.3 V-compatible power input | ESP 3V3 |
| GND | Common GND |
| D0 / SCK / CLK / SCL | GPIO7 |
| D1 / MOSI / DIN / SDA | GPIO6 |
| CS | **GPIO4** |
| DC / D-C / A0 | GPIO3 |
| RES / RST | GPIO10 |

There is no MISO connection. Verify the display is configured for **4-wire SPI**;
some boards select the interface with solder straps. Labels such as SDA/SCL can
appear on SPI displays. Use the exact breakout's instructions for straps and
power; [Adafruit's SSD1306 wiring guide](https://learn.adafruit.com/monochrome-oled-breakouts/wiring-128x64-oleds)
illustrates why board variants must be checked.

Lab profiles use SPI host 2 at **4 MHz**, 250 ms refresh and a 20 ms transfer
timeout. The initial 1 MHz setting repeatedly timed out during refresh on this
board. Basic live output and a saved-profile restart were observed at 4 MHz;
full acceptance remains open. See the [bench results](firmware/qualification/README.md#peripheral-bench-observations).

**Supplied six-pin OLED:** R3/R4 appear fitted, matching its printed 4SPI
selection. Connect GND to GND, VCC to 3V3, SCL to GPIO7, SDA to GPIO6, RES to
GPIO10 and DC to GPIO3. There is no exposed CS connection: leave GPIO4
physically unconnected but reserved by the profile. Internal chip selection
is inferred from successful operation, not a verified schematic. Keep this
OLED as the only device on these SPI wires; this test does not qualify bus
sharing. Leave the mode resistors unchanged.

The screen intentionally rotates through configured sources every two seconds.
The environment page shows temperature and pressure; the health page shows
the last recorded event, not a current all-clear indicator. EVENT 2 means a
device timeout; EVENT 4 records a queue overwrite. During this test, EVENT 4
counts exactly matched the configured drop-oldest queues replacing old records.
The display reads cached samples and does not drain those queues.
**Do not copy GPIO2 CS from `configs/nodes/first-prototype.json`.** That historical
profile is rejected by the current target. Keep DC/reset under GPIO control.

### HLK-LD2450 radar over UART

| LD2450 labelled signal | Connect to |
|---|---|
| 5V | Regulated 5 V module supply |
| GND | Common GND |
| TX | **GPIO20: ESP RX** |
| RX | **GPIO21: ESP TX** |

TX and RX cross between devices. Use the labelled normal connector, not
unidentified programming pads. LD2450 uses 5 V power and 3.3 V UART logic;
its default baud rate is 256000. The profile uses 8 data bits, no parity and
one stop bit. No HC-SR04-style divider is needed on this 3.3 V UART.
See [Hi-Link's protocol document](https://make.net.za/wp-content/datasheets/HLK%20LD2450%20Serial%20Communication%20Protocol%20v1.03.pdf).

Face the antenna towards an open test area. Confirm a previously configured
module still has the expected baud rate/report mode. Move a person or target
through the area; absence of motion alone is not proof that the link has failed.

**Open bench finding:** The earlier empty-scene test showed unavailable readings
after the boards/cables were moved. Keep the hardware stationary for a repeat.
A valid no-target report should stay FRESH with three ABSENT slots; unavailable
means a fault. Startup timeouts also remain unexplained. Full observations and
counts are retained in the [bench summary](firmware/qualification/README.md#peripheral-bench-observations).

### Separate radar-to-LED test using layout A's pin assignments

Use **GPIO5** and the LED circuit shown under layout B with `05-radar-led.json`.
This profile enables radar and PWM only; BMP/OLED are omitted. For the isolated
test, use the power-off procedure to remove those modules before adding the LED.
The radar/LED profile
turns on 50% PWM when `presence_sensor.target_count > 0`. It uses a 200 ms cooldown
and a 500 ms rule age limit, subject to the source's own freshness limit.

## 4. Wiring layout B: HC-SR04 and LED output

First use the safe profile-change procedure in section 6. Disconnect layout A's
modules before using this layout: **GPIO4 was OLED CS and GPIO1 was BMP SCL.**

### HC-SR04 distance sensor

This wiring is for the classic **5 V HC-SR04**. Check the actual module; a 3.3 V
variant may need different level conversion. The classic module uses a TTL
trigger and an echo pulse, with measurement cycles longer than 60 ms recommended
by its [datasheet](https://cdn.sparkfun.com/datasheets/Sensors/Proximity/HCSR04.pdf).
The lab profiles use 1000 ms for the isolated test and 100 ms for the output test.

| HC-SR04 signal | Connect to |
|---|---|
| VCC | Regulated 5 V module supply |
| GND | Common GND |
| TRIG | GPIO4 |
| ECHO | Divider input below; **never directly to GPIO5** |

```text
HC-SR04 ECHO ----[ 10 kΩ ]----+---- GPIO5 (ESP echo input)
                             |
                          [ 15 kΩ ]
                             |
                         Common GND
```

The upper resistor is 10 kΩ; the lower is 15 kΩ. A 5 V echo becomes
`5 × 15 / (10 + 15) = 3.0 V`. Do not reverse the resistors or substitute a
single series resistor. Build/check the divider before connecting it to GPIO5.
Verify the pulse high level with a suitable scope; a multimeter's average
reading of pulses does not verify their peak voltage. A static 5 V divider
check can be done separately with the sensor and ESP signal disconnected.

GPIO4 supplies the 3.3 V trigger. If the actual variant does not accept that
logic level, use a suitable 3.3-to-5 V buffer for TRIG; do not raise ESP voltage.

### External LED as the first actuator substitute

```text
GPIO1 ----[ 1 kΩ ]---- LED anode (+) >| cathode (-) ---- GND
  |
[ 10 kΩ ]   output pulldown
  |
 GND
```

On an ordinary through-hole LED the long lead is usually the anode; verify
polarity if leads were trimmed. Use **GPIO5 instead of GPIO1 for the radar/LED test**.
Use an external LED, not the SuperMini's GPIO8 onboard LED.

The distance/output profile uses 1 kHz PWM and 50% duty below 400 mm. Once on,
it turns off at 450 mm or farther; between 400 and 450 mm it retains its state.
Allow for the 200 ms cooldown and sampling. Missing/faulty/stale input requests
zero on the next output step. Confirm electrical timing with a scope; the
browser's one-second polling cannot prove PWM frequency or response latency.
See the [output contract](firmware/src/automation/README.md).

For the optional motor test, see the [PWM guide](firmware/drivers/pwm_output/README.md#optional-l298n--tt-motor-fixed-direction-only). Test an LED/scope first.

## 5. Which combinations fit?

| Setup | GPIO use | Active peripheral workers |
|---|---|---:|
| A: BMP + OLED + radar | BMP 0/1; OLED 3/4/6/7/10; UART20/21 | 3 |
| Separate radar + LED | UART20/21, PWM5; no BMP/OLED | 2 |
| B: HC-SR04 + LED | TRIG4, divided ECHO5, PWM1 | 2 |
| Optional: two BMP388s | Shared SDA0/SCL1; separate `0x76`/`0x77` | 2 |

BMP + OLED + HC-SR04 would require **nine ordinary GPIOs**, but this backend
exposes eight. The original all-sensor prototype therefore cannot be wired as
one simultaneous setup under the current bindings. Use A and B separately.
Do not bypass pin validation, tie OLED control pins to rails or repurpose USB.

There is also a parser allocation limit: the attempted A+LED profile passed the
Python resource planner but exceeded the C parser's **32 KiB allocation budget**.
The supplied lab profiles therefore separate the radar/LED test from combined A.
All eight generated profiles passed the actual C admission harness as well as the
Python planner during guide preparation; that is not a hardware result.

Combined A uses 10,240 bytes of peripheral task stacks, within the 16 KiB/four-worker
limit. This calculation does not measure remaining RAM. Disabled devices still
reserve pins: remove unused device and bus entries rather than merely disabling them.

## 6. Prepare software and change wiring safely

**Start with the board alone.** Verify the installed **configuration application**
against its build evidence; the acceptance fixture cannot run these profiles.
Use the [installation procedure](tools/target/README.md#install-the-configuration-application) if needed.

From the `Code` repository root:

```powershell
python -m pip install -r tools/requirements.txt -r tools/config_transfer/requirements.txt
python -B -m tools.configurator --port 8080
```

Open `http://127.0.0.1:8080`. Close Arduino/serial monitors before connecting.
Select the actual Device Manager port; **COM8 was the previously identified
port**, but it can change. Generate [matching bench profiles](configs/README.md#bench-profiles).

For **every physical wiring change**, including the first attachment:

1. With the existing wiring still intact, connect to the application. Load
   `00-empty.json`, choose **Validate and review**, then **Apply & save**.
2. Confirm successful saving, matching active/saved revisions and no active
   peripheral workers. **Stop/recover alone is insufficient:** it leaves the
   saved profile intact, and that profile can return at the next boot.
3. Disconnect in the UI, remove USB and module power, then change/check wiring.
   Restore power only after the new circuit is checked. The saved empty plan
   prevents an old plan from driving the newly assigned pins at boot.
4. Reconnect, load the matching new profile, review against the **connected
   board's capabilities**, and apply/save. Confirm readings and both revisions.

If the board cannot respond, disconnect all power and peripherals and diagnose
it alone before reconnecting a new circuit. On a timeout or uncertain reply,
reconnect and query active/saved state before retrying. An active-but-unsaved
plan is not proof of persistence. The hardware-qualified flag remains false
until a later qualification review; it does not become true simply by running
these tests. Details: [configurator](tools/configurator/README.md).

## 7. Run today's acceptance tests in this order

Use a fresh record for each test: profile, wiring photo, voltages, active/saved
revisions, observations, evidence path and **PASS / FAIL / NOT TESTED**.
The durations/tolerances below are proposed bench criteria, not sensor accuracy
certification. Change them explicitly in the record if the team agrees otherwise.

| Test | Procedure | Pass evidence |
|---|---|---|
| USB/application | Board alone; connect, read capabilities, apply/save empty profile | Correct C3 board and compiled drivers; stable connection and matching revisions |
| BMP388 | Layout A's BMP only; `01-bmp.json`; run 2 minutes | Fresh readings identified as `environment_sensor`; temperature/pressure plausible against a local reference; no recurring I/O faults |
| OLED | Add OLED; `02-bmp-oled.json`; run 2 minutes | Legible, changing environmental/health display, correct orientation and no corrupted rows; inspect the panel physically |
| Radar | Wire radar alone using A's UART pins; `03-radar.json`; walk into/across/out of the field | Fresh valid target reports and changing positions/counts; observations agree with movement; record any blind areas |
| Distance | Layout B's HC-SR04 only; `06-distance.json`; use a flat target at 200, 400 and 800 mm | At least ten samples at each position; suggested error within ±20 mm, stable identification as `front_range`; no unexplained timeouts |
| Distance → LED | Add LED on GPIO1; `07-distance-led.json`; move target 300 → 420 → 500 → 420 → 300 mm, holding each for 2 seconds | LED sequence on → on → off → off → on; scope shows about 1 kHz/50% when on and zero when off |
| Fault → zero | With B/output active, aim into an area that produces an actual timeout/invalid sample; observe health and output | When a fault is reported, output goes to zero; record measured delay. If echoes remain valid, fault injection was not achieved |
| Radar → LED | Radar plus LED5 only; `05-radar-led.json`; move into/out of the field | Valid target count above zero enables PWM, valid zero disables it; no firmware change from the distance/output test |
| Combined load | Rebuild A; `04-combined.json`; run 10 minutes | BMP/radar/display/UI remain responsive, no reset or repeated I/O errors; record queue drops and explain them |
| Profile changes | On unchanged A wiring, apply ten valid changes to sample/display periods within allowed limits; finish by saving the original A profile | Correct periods/source identities and active/saved revisions after each change; no unexplained retained workers or faults |
| Reboot restore | Leave wiring unchanged and reboot three times after a confirmed save; reconnect each time | Saved configuration returns; outputs start inactive until fresh data/rules allow activation; record revision and readings |
| Rejected profile | Edit a copy to use reserved GPIO2 or duplicate an occupied pin, then validate | Rejected; live profile/revisions and readings unchanged. UI rejection alone does not demonstrate firmware-side rejection |
| Activation rollback | With one known-working BMP at `0x76`, request absent `0x77` without changing wires (reverse if appropriate) | If admission succeeds but activation fails, previous working profile is restored; capture error, revisions and recovered readings |

Check the empty-save sequence before each added/removed module. Normal profile
changes that leave the physical wiring unchanged do not need a power cycle.
An absent target is not necessarily stale data: a fresh radar report with zero
targets is valid. Similarly, successful SPI writes do not prove an OLED is
connected; visual inspection is part of that test.

Optional general-purpose check: with two BMP388s, wire both SDA/SCL lines to 0/1,
set one address to `0x76` and the other to `0x77`, and load
[two-environment-sensors.json](configs/nodes/two-environment-sensors.json).
Verify separate source IDs and the configured 500/5000 ms periods. Review bus
pullups when two breakout boards share a bus. No LED may remain connected to
GPIO1 in that setup. Missing a second sensor means this test is **NOT TESTED**.

### Recovery/resource checks before signing off all of checkpoint 8

- Disconnect/reconnect the **UI** while the board stays powered; sensor/output
  work should continue. Pulling the only USB power cable is a power-loss test,
  not a communications-only disconnect test.
- Test loss of sensor data separately from a valid zero-target reading. Never
  hot-unplug module power as an improvised fault injection. Use a reviewed signal
  isolation arrangement with the receiver held at a valid idle level, or record
  that physical stale-data testing is still outstanding.
- A controlled interrupted-save test requires a recorded known-good profile,
  LED-only loads and a reviewed way to remove **all** power without backfeeding.
  On reboot, the previous or fully committed new profile should recover; no
  partially written profile may activate. Do not erase flash to simulate this.
  Record this as outstanding if no controlled interruption setup is available.
- Measure response latency under combined load and saving, free heap and task
  stack high-water usage. The current UI does **not** expose all these metrics;
  static build sizes and worker counts cannot replace runtime measurements.
  Agree instrumentation before claiming memory/timing acceptance or AI headroom.

Passing the wiring tests is useful progress; full checkpoint 8 sign-off also
needs those recovery/resource results and any real actuator acceptance.

## 8. Troubleshooting at the bench

| Symptom | Check first |
|---|---|
| Board lights up, no COM port | Data-capable cable, direct USB port, Device Manager; board alone; hold BOOT, tap/release RESET, then release BOOT for download mode |
| COM exists, configurator cannot connect | Another process owns it, wrong port, download mode, or old fixture image; release BOOT and reset into the application |
| BMP fails to start | Power, common ground, SDA/SCL, pullups, CS I2C strap, address and actual chip model |
| OLED blank despite no SPI error | Power/interface straps, actual controller/geometry, CS4/DC3/RESET10, clock7/data6; SPI has no panel acknowledgement |
| Radar has no valid frames | 5 V capacity, common ground, crossed TX/RX, 256000 baud and reporting configuration |
| Distance always times out | Echo divider and GPIO assignment, target angle/range, common ground and trigger acceptance; do not bypass the divider |
| LED always off | LED polarity/resistor, correct layout pin, fresh source ID and rule threshold; measure PWM rather than trusting browser state |
| Resets or erratic readings | Supply sag, accidental rail shorts, loose grounds, long wires, invalid mixed layouts; return to one module |

## 9. Session record and sign-off

Keep a dated record and photos with the actual JSON files and build hashes.
Do not overwrite historical qualification results or mark unperformed checks
as passing. Suggested record:

| Field | Fill in at the bench |
|---|---|
| Date/time; operator; wiring reviewer | |
| Board/COM port; module models and interface straps | |
| Firmware build record/hash; backup location | |
| Layout/photo; measured 3.3 V/5 V rails and echo peak | |
| Profile file; active/saved revisions | |
| Tests performed; readings, timings and evidence paths | |
| Failures, omitted tests and corrective action | |
| Wiring/peripheral acceptance: PASS / FAIL / INCOMPLETE | |
| Full checkpoint 8 readiness: PASS / FAIL / INCOMPLETE | |
| Team reviewer and date | |

Any C changes made while fixing hardware issues must still follow
[BARR-C:2018 project requirements](C_CODING_STANDARD.md) and [AGENTS.md](AGENTS.md):
mandatory pinned standard gate, manual review, affected tests and a fresh real
target build when applicable. Do not weaken checks or reuse deviations for new
code. Record new evidence before repeating the affected acceptance tests.
