# Two-breadboard wiring visualiser

A local, offline guide for rebuilding the user's ESP32-C3 SuperMini bench.
It does not connect to USB/serial, discover devices, change firmware or apply
profiles. It is separate from Member 5's Configuration Desk.

## Open locally

Node.js is the only requirement; there are no packages to install.

```powershell
cd visualiser
node serve.mjs
```

Open **http://127.0.0.1:4173** in a browser. The server binds only to loopback
and serves an explicit allowlist of files inside `dist/`. Stop it with Ctrl+C.
If port 4173 is already occupied, open the existing guide or use a different
port with `$env:VISUALISER_PORT = '4174'` before starting it.

For VS Code, open **visualiser.code-workspace**. Run **Terminal → Run Task →
Serve wiring guide**, then open the URL in a browser or VS Code's **Simple
Browser: Show** command. Alternatively, with no server already running, press
F5 using **Open wiring guide in Edge**. This launches the server task and Edge.
The root project can also run `node visualiser/serve.mjs` without changing
directories. The npm shortcut is `npm start`.

## Follow the guide

Choose a setup, then select a step. The current wire is bright; previous wires
are dimmed. **All wires** shows later connections as faint dashed lines; it is
a planning view, not a claim that they have been fitted. **Focus on step**
enlarges each endpoint, or the component placement. Rail-hole guide numbers
appear in close-ups; endpoint panels stack in narrow windows. Select a hole to see its
address. The connection list provides the same endpoints without relying on
colour. Checkmarks and your selected view are stored in this browser's local
storage; they are user notes, never measured verification.

The first setup covers placement, ground, and 3.3 V. Other setups cover radar
alone, BMP388 alone, BMP388/OLED, the original three-module combination, and
HC-SR04 separately. Those plans start from empty wiring. The **Add SEN0390 to
existing setup** plan instead starts with the existing BMP388 + OLED + radar
connections as references and guides the new leads. Verify that your actual
assembly and free holes match the reference before using it.

The SEN0390 plan is provisional: its driver, combined profile, required EN
startup behaviour and hardware acceptance are not complete. It proposes GPIO5
for EN and a 10 kΩ pull-down to ground; the existing EN lead may currently be
tied to 3.3 V. Remove all power before moving it, and never connect it to GPIO5
and 3.3 V at the same time. Confirm
the cable conductor-to-pin mapping, the 3.3 V rail and shared I2C pull-ups first.
To prepare for a demonstration, keep unused connector ends insulated and label
the plug; connect or disconnect peripherals only after removing USB and any
external power. The plan does not claim powered hot-plug or automatic discovery.

Use **Print this plan** for the connection list and selected view. For a wiring
overview, choose Overview and All wires first. The browser's print-to-PDF is
also suitable for carrying the guide to the bench.

## Coordinate and electrical contract

- Board orientation matches the supplied empty-board photo. Board A is on
  the left with a–j across the top. Board B has J–A across its top and A–J
  across its bottom; its left row numbers descend while its right numbers ascend.
  Board B coordinates always use the **bottom letters and right-hand numbers**.
  The user confirmed two physical rows above the right-hand printed 0.
  These unnumbered rows are drawn but remain unused; B row 0 is physical row 3
  and B row 60 is physical row 63. For example, `B:a14` uses bottom A and
  right-hand 14; the left edge reads 46 at that height.
- Main grids are 63 rows. Each rail has 50 holes counted from top to bottom,
  ignoring gaps, in ten groups of five. If the actual breadboard differs,
  stop and revise the model before using rail addresses.
- `A:j30` is printed main-grid column j, row 30 on Board A.
  `B:L-22` is the 22nd hole from the top in Board B's left blue rail.
  Rail indices are **guide labels, not printed main-grid row numbers**.
- The photo-layout revision uses new saved-progress storage, so checks from
  the earlier orientation do not carry over to the revised hole positions.
- The ESP is illustrated in c29–c36 and h29–h36, USB up. Check its printed
  pin labels against the guide before wiring. The ESP body covers some grid
  holes; the plan uses exposed b and j columns.
- Board A's right blue and Board B's left blue rails are ground. Their adjacent
  red rails are **3.3 V**. Board A's left red rail is **5 V** only in setups that
  need it. Other rails stay unused. No red-to-red link joins 3.3 V and 5 V.
- Every used rail is conservatively modelled as two separate halves. Explicit
  25-to-26 bridges connect them, even if the actual rail is already continuous.
  Verify continuity; the diagram cannot detect a bad wire or contact.
- Module symbols are placed **outside the breadboards on nonconductive
  supports**. Their labelled pads are logical connections, not a claim about
  physical header order, dimensions or component appearance. Trace actual
  module pin labels and cables; do not assume wire colours.
- HC-SR04 uses the hardware guide's 10 kΩ upper / 15 kΩ lower Echo divider.
  The older `bench-distance.json` text mentions 20 kΩ. This guide deliberately
  follows `CONTRIBUTING.md` and flags the discrepancy; it changes no catalogue
  or firmware file.

## Bench status and limits

The project [hardware guide](../CONTRIBUTING.md) is the main acceptance
procedure. [Discovery documentation](../docs/README.md) explains identification
limits. The visualiser does not replace electrical checks or a reviewed profile.

Earlier board-state and dismantling records are in the
[history log](../docs/COMPLETED_WORK_LOG.md). Browser checkmarks and a wiring plan
never establish the actual circuit, installed firmware or successful repair.
Check live capabilities/state before use and follow the hardware acceptance guide.

FC-51 and HC-020K are deliberately listed without a connectable plan: their
electrical variants still need verification, and their drivers/catalogue
support do not yet exist here. The 5 V HC-SR04 is supported through manual
identification, not automatic model recognition. Query the actual firmware
for I2C/UART discovery feature bits. Suggested
settings come from reviewed defaults and bench observations, not a deployed
AI model.

## Maintain and check

```powershell
node --test tests/*.test.mjs
```

- `dist/plan.mjs`: component positions, coordinates, wiring steps and setup data.
- `dist/diagram.mjs`: breadboard geometry, paths and endpoint close-ups.
- `dist/app.mjs`: navigation, checkmarks, connection table and saved view.
- `dist/style.css`: responsive appearance and printing.
- `serve.mjs`: loopback-only static server.

Tests validate coordinates, separate rail halves and bridges, hole occupancy,
power-net isolation, reserved pins, driver signal mappings against the
catalogue, and the HC-SR04 divider. They validate the proposed plan, not the
physical circuit. After changing layout or controls, inspect both overview and
focus views in a browser, including a narrow viewport.
