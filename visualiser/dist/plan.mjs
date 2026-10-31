// Coordinates describe the proposed NEW placement, not a detected physical setup.
export const REVISION = "two-board-photo-v2";
export const COLORS = { ground: "#1879b6", v33: "#c43752", v5: "#c07813", signal: "#087d76" };
export const ESP_PINS = [
  { label: "5", row: 29, column: "c", access: "b29" },
  { label: "6", row: 30, column: "c", access: "b30" },
  { label: "7", row: 31, column: "c", access: "b31" },
  { label: "8", row: 32, column: "c", access: "b32", reserved: true },
  { label: "9", row: 33, column: "c", access: "b33", reserved: true },
  { label: "10", row: 34, column: "c", access: "b34" },
  { label: "20", row: 35, column: "c", access: "b35" },
  { label: "21", row: 36, column: "c", access: "b36" },
  { label: "5V", row: 29, column: "h", access: "j29" },
  { label: "G", row: 30, column: "h", access: "j30" },
  { label: "3.3", row: 31, column: "h", access: "j31" },
  { label: "4", row: 32, column: "h", access: "j32" },
  { label: "3", row: 33, column: "h", access: "j33" },
  { label: "2", row: 34, column: "h", access: "j34", reserved: true },
  { label: "1", row: 35, column: "h", access: "j35" },
  { label: "0", row: 36, column: "h", access: "j36" },
];
export function parseEndpoint(id) {
  const grid = /^([AB]):([a-j])(0|[1-9]\d?)$/.exec(id);
  if (grid) {
    const row = Number(grid[3]), valid = grid[1] === "A" ? row >= 1 && row <= 63 : row <= 60;
    if (valid) return { type: "grid", board: grid[1], column: grid[2], row };
  }
  const rail = /^([AB]):([LR])([+-])([1-9]\d?)$/.exec(id);
  if (rail && Number(rail[4]) <= 50) return { type: "rail", board: rail[1], side: rail[2], sign: rail[3], index: Number(rail[4]) };
  const port = /^(radar|bmp|oled|distance):([A-Z0-9]+)$/.exec(id);
  if (port && MODULES[port[1]].ports.includes(port[2])) return { type: "module", module: port[1], pin: port[2] };
  throw new Error(`Unknown endpoint: ${id}`);
}
export function address(id) {
  const e = parseEndpoint(id);
  if (e.type === "module") return `${MODULES[e.module].short} · ${e.pin}`;
  return `${e.board} · ${id.split(":")[1].replace("-", "−")}`;
}
export function endpointDescription(id) {
  const e = parseEndpoint(id);
  if (e.type === "module") return `Use the module's printed ${e.pin} label. Illustrated port order is schematic.`;
  if (e.type === "rail") return `Board ${e.board}, ${e.side === "R" ? "right" : "left"} ${e.sign === "-" ? "blue −" : "red +"} rail. Count hole ${e.index} from the top, skipping gaps.`;
  if (e.board === "B") return `Board B, bottom column ${e.column.toUpperCase()}, RIGHT-hand printed row ${e.row}. The left edge shows ${60 - e.row} at this height. Two unnumbered rows sit above row 0.`;
  const pin = e.board === "A" && ESP_PINS.find(p => p.row === e.row && ((p.column < "f") === (e.column < "f")));
  return `Board ${e.board}, column ${e.column}, printed row ${e.row}.${pin ? ` Same five-hole strip as ESP ${pin.label === "G" ? "ground (G)" : pin.label === "3.3" ? "3.3 V" : pin.label === "5V" ? "5 V" : `GPIO${pin.label}`}.` : ""}`;
}
// Board B uses the bottom A–J letters and right-edge 0–60 numbers in the photo.
// Its first two physical rows are unnumbered and deliberately unused.
export function physicalGridRow(board, row) {
  return board === "B" ? row + 3 : row;
}
export function railRow(index) {
  if (!Number.isInteger(index) || index < 1 || index > 50) throw new Error("Rail index must be 1..50");
  return 3 + 6 * Math.floor((index - 1) / 5) + (index - 1) % 5;
}
export function connectedStrip(id) {
  const e = parseEndpoint(id);
  if (e.type === "grid") return `${e.board}:${e.column <= "e" ? "ae" : "fj"}${e.row}`;
  // Conservative electrical model: every rail may be split at its midpoint.
  if (e.type === "rail") return `${e.board}:${e.side}${e.sign}:${e.index <= 25 ? "upper" : "lower"}`;
  return id;
}
export const MODULES = {
  radar: { short: "LD2450", name: "HLK-LD2450 radar", ports: ["5V", "GND", "TX", "RX"], x: 56, y: 32, w: 252, h: 100, location: "Above Board A · face the open test area" },
  bmp: { short: "BMP388", name: "BMP388", ports: ["VIN", "GND", "SDA", "SCL"], x: 390, y: 32, w: 224, h: 100, location: "Above Board B · on a nonconductive support" },
  oled: { short: "OLED", name: "Six-pin SPI OLED", ports: ["GND", "VCC", "SCL", "SDA", "RES", "DC"], x: 374, y: 866, w: 268, h: 104, location: "Below Board B · facing you" },
  distance: { short: "HC-SR04", name: "HC-SR04 · 5 V", ports: ["VCC", "TRIG", "ECHO", "GND"], x: 390, y: 32, w: 224, h: 100, location: "Above Board B · transducers face the target" },
};
const check = (id, title, instruction, test, options = {}) => ({
  id, title, instruction, check: test, phase: "Prepare", kind: "check", ...options,
});
const wire = (id, title, from, to, net, instruction, options = {}) => ({
  id, title, from, to, net, instruction, kind: "wire", phase: "Power & ground",
  check: "Both ends are fully seated in the highlighted holes. USB and any external supply remain unplugged.",
  ...options,
});
const foundation = [
  check("orientation", "Match your photo", "Disconnect all power and remove all jumpers. Place Board A on the left with a–j across the top. Place Board B on the right with J–A across the top and A–J along the bottom, as in your photo.", "Board B has two rows of holes above the right-hand 0. Its right numbers increase downward; its left numbers decrease downward. Each board has 63 rows of holes and 50 holes per rail.", { note: "For every Board B grid coordinate, use the BOTTOM letter and RIGHT-hand row number. Leave its two unnumbered top rows unused. Board A uses its usual printed coordinates.", showEsp: false }),
  check("rails", "Read the rail labels", "Main-grid labels use letters and printed rows, such as j30. For rails, count holes from the top: L means left, R means right, + means red, and − means blue.", "Using jumpers and the meter in Ω mode, check adjacent holes on each used rail. Touch probes to the jumpers' exposed metal ends.", { note: "R−23 is the 23rd hole in the right blue column, not row 23. Rail columns on opposite sides are separate. Split rails are bridged explicitly below.", showEsp: false }),
  check("esp", "Seat the ESP across the gap", "Put the ESP on Board A with USB pointing up. Its left header occupies c29–c36 and its right header h29–h36. Leave b29–b36 and j29–j36 accessible.", "The printed 5V, G and 3.3 pins align with rows 29, 30 and 31 on the right. GPIO20 and GPIO21 align with rows 35 and 36 on the left.", { note: "Printed ESP labels take priority. Stop if this placement does not match your board. The illustration shows pin positions, not a product photograph.", focusEsp: true }),
  check("empty", "Confirm an empty startup profile", "With the ESP alone, connect USB, apply and save the empty profile, and verify matching active/saved revisions with no retained peripheral instances. Then unplug USB again.", "The current empty profile is confirmed saved, and USB is now unplugged.", { power: "controlled", note: "Revision 8 was last verified on 20 October 2026. That historical result is not a live check. Use the existing Configuration Desk or ask for a board check; this guide cannot send commands.", focusEsp: true }),
  wire("gnd-a", "Ground the first board", "A:j30", "A:R-23", "ground", "Connect ESP ground at j30 to the right blue rail, hole 23."),
  wire("gnd-a-bridge", "Bridge Board A's ground rail", "A:R-25", "A:R-26", "ground", "Connect holes 25 and 26 in Board A's right blue rail, across the middle gap.", { note: "Both ends stay in the same blue column. This is harmless if that rail is already continuous." }),
  wire("gnd-b", "Carry ground to Board B", "A:R-22", "B:L-22", "ground", "Connect Board A's right blue rail, hole 22, to Board B's left blue rail, hole 22."),
  wire("gnd-b-bridge", "Bridge Board B's ground rail", "B:L-25", "B:L-26", "ground", "Connect holes 25 and 26 in Board B's left blue rail."),
  check("gnd-test", "Check the complete ground path", "Place spare test jumpers at A · i30 and B · L−40. In Ω mode, measure between their free metal ends. Remove the test jumpers afterward.", "Resistance is close to the probe-and-wire baseline and stays stable. OL means the path is open; inspect the jumpers before continuing.", { phase: "Power & ground", endpoints: ["A:i30", "B:L-40"] }),
  wire("v33-a", "Add the 3.3 V supply", "A:j31", "A:R+23", "v33", "Connect the ESP's 3.3 V pin at j31 to Board A's right red rail, hole 23.", { note: "Label this red rail 3.3 V. Red does not automatically mean 5 V." }),
  wire("v33-a-bridge", "Bridge Board A's 3.3 V rail", "A:R+25", "A:R+26", "v33", "Connect holes 25 and 26 in Board A's right red rail."),
  wire("v33-b", "Carry 3.3 V to Board B", "A:R+22", "B:L+22", "v33", "Connect Board A's right red rail, hole 22, to Board B's left red rail, hole 22."),
  wire("v33-b-bridge", "Bridge Board B's 3.3 V rail", "B:L+25", "B:L+26", "v33", "Connect holes 25 and 26 in Board B's left red rail. Label this rail 3.3 V."),
  check("v33-test", "Check the 3.3 V path", "With USB unplugged, use test jumpers at A · i31 and B · L+40. Check continuity between their metal ends. Check that no jumper joins a red rail to a blue rail.", "The supply path is continuous. A persistent near-zero reading between 3.3 V and ground needs investigation before power; ESP circuitry may otherwise give a changing resistance.", { phase: "Power & ground", endpoints: ["A:i31", "B:L+40"] }),
  check("voltage-test", "Measure the empty rails", "With sensors still disconnected and wiring checked, attach spare test jumpers to B · L−39 and B · L+39. Set the meter to DC volts with leads in COM and V/Ω. Connect USB, black probe to blue, red probe to red. Unplug USB before removing the test jumpers.", "The red rail reads approximately +3.3 V relative to blue. USB is unplugged again before continuing.", { phase: "Power & ground", endpoints: ["B:L-39", "B:L+39"], power: "controlled", note: "Keep exposed jumper ends apart. Never use Ω or current mode across powered rails." }),
];
const fiveVolt = [
  check("budget", "Check the 5 V source", "Use the ESP's USB-derived 5 V pin only after confirming this board's USB power path and the supply/cable budget for the selected module. Keep power off while wiring.", "The 5 V source has been checked. For the LD2450, allow more than 200 mA for the radar, plus the ESP and other loads.", { phase: "5 V supply", note: "Do not join an external supply's positive output to a USB-powered ESP 5 V pin. This plan assumes one verified USB source. If a separate supply is needed, revise the wiring plan first." }),
  wire("v5-a", "Create a separate 5 V rail", "A:j29", "A:L+23", "v5", "Connect ESP 5V at j29 to Board A's LEFT red rail, hole 23. Label this rail 5 V.", { phase: "5 V supply", note: "The right red rails remain 3.3 V. Do not join the left and right red rails." }),
  wire("v5-bridge", "Bridge the 5 V rail", "A:L+25", "A:L+26", "v5", "Bridge holes 25 and 26 in Board A's left red rail.", { phase: "5 V supply" }),
  check("v5-test", "Measure the separate 5 V rail", "With no sensors connected, place test jumpers at A · L+39 and A · R−39. After checking wiring, set DC volts, connect USB and measure red probe on L+39, black on R−39. Unplug USB before removing jumpers.", "Approximately +5 V is present on the LEFT red rail; the right red rail remains approximately +3.3 V.", { phase: "5 V supply", power: "controlled", endpoints: ["A:L+39", "A:R-39"] }),
];
const radar = [
  check("radar-place", "Place the radar above Board A", "Secure the radar on a nonconductive support above Board A, facing the test area. Leave its cable relaxed so walking past cannot move the board.", "The actual module's 5V, GND, TX and RX labels are identified. The guide's port order is schematic, not connector order.", { phase: "Radar", module: "radar" }),
  wire("radar-gnd", "Radar ground", "A:R-15", "radar:GND", "ground", "Connect Board A's right blue rail, hole 15, to the radar's labelled GND.", { phase: "Radar", module: "radar" }),
  wire("radar-power", "Radar supply", "A:L+15", "radar:5V", "v5", "Connect Board A's left red 5 V rail, hole 15, to the radar's labelled 5V.", { phase: "Radar", module: "radar" }),
  wire("radar-tx", "Radar TX → ESP receive", "radar:TX", "A:b35", "signal", "Connect the radar's TX to b35 on Board A, the same strip as ESP GPIO20.", { phase: "Radar", module: "radar", note: "TX sends data. It connects to the ESP's receive pin, GPIO20." }),
  wire("radar-rx", "Radar RX → ESP transmit", "radar:RX", "A:b36", "signal", "Connect the radar's RX to b36 on Board A, the same strip as ESP GPIO21.", { phase: "Radar", module: "radar" }),
];
const bmp = [
  check("bmp-place", "Place the BMP388 above Board B", "Place the BMP388 on a nonconductive support above Board B. Use its existing four-wire cable after confirming each signal.", "The actual VIN, GND, SDA and SCL connections are confirmed. Wire colours are not assumed.", { phase: "BMP388", module: "bmp", note: "This plan retains the tested 3.3 V supply and I2C mode. Do not change existing address or interface straps." }),
  wire("bmp-gnd", "BMP388 ground", "B:L-10", "bmp:GND", "ground", "Connect Board B's left blue rail, hole 10, to BMP388 GND.", { phase: "BMP388", module: "bmp" }),
  wire("bmp-vin", "BMP388 supply", "B:L+10", "bmp:VIN", "v33", "Connect Board B's left red 3.3 V rail, hole 10, to BMP388 VIN.", { phase: "BMP388", module: "bmp" }),
  wire("bmp-sda", "BMP388 data", "A:j36", "bmp:SDA", "signal", "Connect j36 on Board A, ESP GPIO0, to BMP388 SDA.", { phase: "BMP388", module: "bmp" }),
  wire("bmp-scl", "BMP388 clock", "A:j35", "bmp:SCL", "signal", "Connect j35 on Board A, ESP GPIO1, to BMP388 SCL.", { phase: "BMP388", module: "bmp" }),
];
const oled = [
  check("oled-place", "Place the OLED below Board B", "Place the six-pin OLED on a nonconductive support below Board B, facing you. Follow its printed GND, VCC, SCL, SDA, RES and DC labels.", "This is the tested six-pin SPI panel. GPIO4 stays physically unconnected and reserved in the profile.", { phase: "OLED", module: "oled", note: "SDA/SCL labels on this panel refer to SPI data and clock. Keep this display alone on its SPI bus." }),
  wire("oled-gnd", "OLED ground", "B:L-44", "oled:GND", "ground", "Connect Board B's left blue rail, hole 44, to OLED GND.", { phase: "OLED", module: "oled" }),
  wire("oled-vcc", "OLED supply", "B:L+44", "oled:VCC", "v33", "Connect Board B's left red 3.3 V rail, hole 44, to OLED VCC.", { phase: "OLED", module: "oled" }),
  wire("oled-scl", "OLED clock", "A:b31", "oled:SCL", "signal", "Connect b31 on Board A, ESP GPIO7, to OLED SCL.", { phase: "OLED", module: "oled" }),
  wire("oled-sda", "OLED data", "A:b30", "oled:SDA", "signal", "Connect b30 on Board A, ESP GPIO6, to OLED SDA.", { phase: "OLED", module: "oled" }),
  wire("oled-res", "OLED reset", "A:b34", "oled:RES", "signal", "Connect b34 on Board A, ESP GPIO10, to OLED RES.", { phase: "OLED", module: "oled" }),
  wire("oled-dc", "OLED command / data", "A:j33", "oled:DC", "signal", "Connect j33 on Board A, ESP GPIO3, to OLED DC.", { phase: "OLED", module: "oled" }),
];
const distance = [
  check("distance-place", "Place the HC-SR04 above Board B", "Use the 5 V HC-SR04 on a nonconductive support above Board B. Disconnect the OLED and remove its profile entry for this separate test.", "VCC, TRIG, ECHO and GND are confirmed from the module labels. Two measured resistors, 10 kΩ and 15 kΩ, are available.", { phase: "HC-SR04", module: "distance", note: "This is a separate setup, not an addition to the OLED combination. The module's 5 V Echo must not connect directly to an ESP pin." }),
  wire("distance-gnd", "HC-SR04 ground", "B:L-12", "distance:GND", "ground", "Connect Board B's left blue rail, hole 12, to HC-SR04 GND.", { phase: "HC-SR04", module: "distance" }),
  wire("distance-vcc", "HC-SR04 supply", "A:L+12", "distance:VCC", "v5", "Connect Board A's left red 5 V rail, hole 12, to HC-SR04 VCC.", { phase: "HC-SR04", module: "distance" }),
  wire("divider-top", "Fit the 10 kΩ resistor", "B:e14", "B:e18", "signal", "Insert the 10 kΩ resistor between e14 and e18 on Board B. Bend the leads so they fit without touching other metal.", { phase: "HC-SR04", resistor: 10000 }),
  wire("divider-bottom", "Fit the 15 kΩ resistor", "B:d18", "B:d22", "signal", "Insert the 15 kΩ resistor between d18 and d22 on Board B. d18 and e18 share a connected five-hole strip.", { phase: "HC-SR04", resistor: 15000 }),
  wire("divider-gnd", "Ground the divider", "B:a22", "B:L-14", "ground", "Connect a22 to Board B's left blue rail, hole 14.", { phase: "HC-SR04" }),
  check("divider-check", "Check the divider before the ESP", "With the sensor and ESP signal disconnected, verify the resistor values and connections. A separate reviewed static 5 V test can check that the row-18 midpoint is about 3.0 V. Remove test power afterward.", "10 kΩ is above the midpoint; 15 kΩ is below it to ground. The midpoint voltage has been verified before attaching GPIO5.", { phase: "HC-SR04", endpoints: ["B:a14", "B:a18"], note: "5 × 15 ÷ (10 + 15) = 3.0 V. The hardware guide's 10 kΩ / 15 kΩ circuit is used here; an older catalogue string says 20 kΩ. Do not silently substitute resistor values." }),
  wire("distance-echo", "Echo → divider input", "distance:ECHO", "B:a14", "signal", "Connect HC-SR04 ECHO to a14 on Board B, at the top of the 10 kΩ resistor.", { phase: "HC-SR04", module: "distance" }),
  wire("distance-input", "Divider midpoint → ESP input", "B:a18", "A:b29", "signal", "Connect a18 on Board B to b29 on Board A, ESP GPIO5. This is the reduced-voltage midpoint.", { phase: "HC-SR04" }),
  wire("distance-trig", "ESP trigger → HC-SR04", "A:j32", "distance:TRIG", "signal", "Connect j32 on Board A, ESP GPIO4, to HC-SR04 TRIG.", { phase: "HC-SR04", module: "distance" }),
];
function finish(id, description) {
  return check(`${id}-finish`, "Review, then run the matching profile", "Inspect every endpoint and voltage before power-up. Connect USB only after these checks, then review and apply the matching profile through Configuration Desk.", description, { phase: "Bench test", power: "controlled", note: "A diagram is not acceptance evidence. Stop on recovery errors, wrong voltages or recurring communication faults. Save an empty profile and unplug all power before changing this wiring." });
}
export const PLANS = {
  foundation: { name: "01 · Empty boards & power", description: "Start here. Place the ESP and connect ground and 3.3 V across both boards.", modules: [], steps: foundation },
  radar: { name: "02 · Radar only", description: "Isolate the radar first. Add a separately labelled 5 V rail, then four radar connections.", modules: ["radar"], steps: [...foundation, ...fiveVolt, ...radar, finish("radar", "Valid moving-target reports, then valid zero-target reports, then movement again. Hardware stays stationary. Current radar reliability remains unresolved.")] },
  bmp: { name: "03 · BMP388 only", description: "A separate sensor test using 3.3 V, GPIO0 and GPIO1.", modules: ["bmp"], steps: [...foundation, ...bmp, finish("bmp", "Fresh temperature and pressure readings with the correct address. Bench starting values: 0x77, 100 kHz, 500 ms interval, 100 ms timeout.")] },
  display: { name: "04 · BMP388 + OLED", description: "Check the sensor and six-pin SPI display together. GPIO4 remains reserved and unwired.", modules: ["bmp", "oled"], steps: [...foundation, ...bmp, ...oled, finish("display", "Fresh BMP readings and visibly updating OLED. Retain the tested 4 MHz SPI speed, 250 ms refresh and 20 ms timeout.")] },
  combined: { name: "05 · BMP388 + OLED + radar", description: "Combine only after individual tests. No HC-SR04 in this configuration.", modules: ["radar", "bmp", "oled"], steps: [...foundation, ...fiveVolt, ...radar, ...bmp, ...oled, finish("combined", "All three devices work under combined load without recurring I/O faults or resets. Physical acceptance is still required.")] },
  distance: { name: "06 · HC-SR04, separate setup", description: "A separate 5 V sensor test with a 10 kΩ / 15 kΩ Echo divider. No OLED in this plan.", modules: ["distance"], steps: [...foundation, ...fiveVolt, ...distance, finish("distance", "Use known targets and verify distance readings. Check the Echo high level with a suitable scope; a meter's average cannot establish pulse peak voltage.")] },
};
export const EQUIPMENT = [
  ["ESP32-C3 SuperMini", "Board A · across centre gap", "Pin labels must match placement"],
  ["BMP388", "Above Board B · I2C", "Driver supported; identify through approved chip-ID reads"],
  ["Six-pin OLED", "Below Board B · SPI", "Manual confirmation; GPIO4 reserved"],
  ["HLK-LD2450", "Above Board A · UART", "Intermittent faults still unresolved"],
  ["HC-SR04 · 5 V", "Above Board B · separate setup", "Driver supported; manual identity; Echo divider required"],
  ["FC-51 + HC-020K", "Keep off the connected boards", "Pin/supply verification and new drivers still required"],
];
