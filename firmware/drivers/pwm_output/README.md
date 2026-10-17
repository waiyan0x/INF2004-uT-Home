# Generic PWM output

`pwminst` exposes `pwm_output` through component configure/write operations.
It borrows a `pwmio` port; the C3 backend owns an independently timed LEDC output.
Setup and cleanup request zero duty. No actuator-specific behavior is included.
See [binding, electrical boundaries and validation](../hc_sr04/INTEGRATION.md).


## Optional L298N + TT motor: fixed direction only

Keep motor power disconnected until the board's power/jumper documentation and
the TT motor's rated voltage/current are known. The motor supply must suit the
motor and driver, including startup current. A terminal labelled `12V` on an
L298N board does **not** mean a TT motor should receive 12 V.

After the LED test, this is the intended signal mapping for **layout B**, with
HC-SR04 unchanged. It assumes a conventional L298N module with labelled ENA,
IN1/IN2 and OUT1/OUT2; verify that this is your board before wiring.

| L298N/module connection | Intended connection |
|---|---|
| ENA | GPIO1; remove the ENA always-enabled jumper; fit 10 kΩ ENA-to-GND pulldown |
| IN1 / IN2 | Fixed 3.3 V / GND respectively, once compatible input circuitry is verified |
| OUT1 / OUT2 | The two TT motor terminals; neither motor terminal goes directly to ESP GND |
| GND | Motor supply negative and ESP common GND |
| Motor supply / Vs / terminal sometimes labelled `12V` | Appropriate external motor supply positive; never an ESP GPIO/3V3 rail |
| Logic 5 V and `5V-EN` regulator jumper | Follow the exact module's instructions; do not connect power here until its input/output role is established |
| Unused channel B | No motor; disable ENB according to the module instructions |

**ENA and 5V-EN are different jumpers.** Never leave ENA shorted to 5 V while
connecting it to an ESP GPIO. Never join a module's regulator output to an
external 5 V output. Verify fitted flyback protection and supply decoupling;
the [ST L298 datasheet](https://www.st.com/resource/en/datasheet/l298.pdf) describes
the chip's TTL control, separate logic/motor supplies and bridge behaviour,
but does not define your breakout's jumpers. Its voltage drop also reduces the
voltage reaching a low-voltage TT motor.

Once the supply arrangement is verified, secure the unloaded motor with its
shaft/wheels clear, power down to replace the LED with ENA, and use the same
`07-distance-led.json` PWM rule. Verify ENA low before motor power is enabled,
then briefly test the threshold sequence. Record start/stop, supply current and
driver temperature; stop if the motor stalls or wiring/driver overheats. Zero
enable allows the motor to coast, so an immediate physical stop is not promised.
Do not increase supply voltage simply because 50% duty fails to start it.
Powered motor acceptance stays **NOT TESTED** until those supply/module details
are confirmed; the IR encoder is not part of this open-loop test.

Use the [hardware guide](../../../CONTRIBUTING.md) for common-ground, power-off rewiring and the initial LED test.
