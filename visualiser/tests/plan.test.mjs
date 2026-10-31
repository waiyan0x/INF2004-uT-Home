import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { PLANS, ESP_PINS, parseEndpoint, connectedStrip, railRow, endpointDescription } from "../dist/plan.mjs";
import { point } from "../dist/diagram.mjs";

function netGraph(plan) {
  const parents = new Map();
  const find = id => {
    id = connectedStrip(id);
    if (!parents.has(id)) parents.set(id, id);
    let root = id;
    while (parents.get(root) !== root) root = parents.get(root);
    return root;
  };
  for (const wire of plan.steps.filter(s => s.kind === "wire" && !s.resistor)) {
    parents.set(find(wire.from), find(wire.to));
  }
  return { same: (a, b) => find(a) === find(b) };
}
test("coordinates cover the real grid and rail counting, with invalid endpoints rejected", () => {
  assert.equal(railRow(1), 3);
  assert.equal(railRow(25), 31);
  assert.equal(railRow(26), 33);
  assert.equal(railRow(50), 61);
  assert.equal(connectedStrip("A:i30"), connectedStrip("A:j30"));
  assert.notEqual(connectedStrip("A:e30"), connectedStrip("A:f30"));
  assert.notEqual(connectedStrip("A:R-25"), connectedStrip("A:R-26"));
  for (const id of ["A:j64", "B:R+51", "A:k30", "C:j30", "A:j0", "B:a61", "B:j63", "radar:BOGUS"]) {
    assert.throws(() => parseEndpoint(id));
  }
});
test("photo layout: Board B uses bottom letters, right numbers, and two holes above zero", () => {
  assert.equal(parseEndpoint("B:a0").row, 0);
  assert.equal(point("B:a0").y, point("A:a3").y);
  assert.equal(point("B:j60").y, point("A:j63").y);
  assert.equal(point("B:a14").y, point("A:a17").y);
  assert.ok(point("B:a14").x < point("B:e14").x);
  assert.ok(point("B:e14").x < point("B:f14").x);
  assert.ok(point("B:f14").x < point("B:j14").x);
  assert.match(endpointDescription("B:a14"), /bottom column A, RIGHT-hand printed row 14/);
  assert.match(endpointDescription("B:a14"), /left edge shows 46/);
  assert.equal(connectedStrip("B:a14"), connectedStrip("B:e14"));
  assert.notEqual(connectedStrip("B:e14"), connectedStrip("B:f14"));
});
for (const [name, plan] of Object.entries(PLANS)) {
  test(`${name}: holes are valid, no two leads occupy one hole, and ESP body holes are avoided`, () => {
    const occupied = new Set(), ids = new Set();
    for (const step of plan.steps) {
      assert.ok(!ids.has(step.id), `Duplicate step ID ${step.id}`); ids.add(step.id);
      for (const id of step.endpoints || []) parseEndpoint(id);
      if (step.kind !== "wire") continue;
      for (const id of [step.from, step.to]) {
        const e = parseEndpoint(id);
        assert.ok(!occupied.has(id), `Two leads share ${id}`); occupied.add(id);
        if (e.type === "grid" && e.board === "A" && e.row >= 29 && e.row <= 36) {
          assert.ok(["a", "b", "i", "j"].includes(e.column), `Lead hidden under ESP at ${id}`);
        }
      }
    }
  });
  test(`${name}: both rail halves work; power nets stay separate; no reserved GPIO gets a wire`, () => {
    const graph = netGraph(plan);
    for (const id of ["A:R-1", "A:R-50", "B:L-1", "B:L-50"]) assert.ok(graph.same("A:j30", id), `Ground not connected to ${id}`);
    for (const id of ["A:R+1", "A:R+50", "B:L+1", "B:L+50"]) assert.ok(graph.same("A:j31", id), `3.3 V not connected to ${id}`);
    assert.ok(!graph.same("A:j30", "A:j31"), "Ground shorted to 3.3 V");
    assert.ok(!graph.same("A:j29", "A:j31"), "5 V shorted to 3.3 V");
    assert.ok(!graph.same("A:j29", "A:j30"), "5 V shorted to ground");
    if (plan.steps.some(s => s.net === "v5")) {
      for (const id of ["A:L+1", "A:L+50"]) assert.ok(graph.same("A:j29", id));
    }
    for (const step of plan.steps.filter(s => s.kind === "wire")) {
      for (const id of [step.from, step.to]) {
        for (const pin of ESP_PINS.filter(p => p.reserved)) {
          assert.notEqual(connectedStrip(id), connectedStrip(`A:${pin.access}`), `Reserved GPIO${pin.label} used`);
        }
      }
    }
    for (const p of ESP_PINS.filter(p => /^\d+$/.test(p.label))) {
      assert.ok(!graph.same(`A:${p.access}`, "A:j29"), `5 V reaches GPIO${p.label}`);
    }
  });
}
test("sensor connections match the reviewed catalogue GPIOs and supply domains", () => {
  const preset = name => JSON.parse(readFileSync(new URL(`../../catalogue/connections/bench-${name}.json`, import.meta.url)));
  const pin = gpio => `A:${ESP_PINS.find(p => p.label === String(gpio)).access}`;
  const graph = netGraph(PLANS.combined);
  const i2c = preset("i2c"), uart = preset("uart"), oled = preset("oled");
  for (const [port, gpio] of [
    ["bmp:SDA", i2c.interface.sda_gpio], ["bmp:SCL", i2c.interface.scl_gpio],
    ["radar:TX", uart.interface.rx_gpio], ["radar:RX", uart.interface.tx_gpio],
    ["oled:SCL", oled.interface.sclk_gpio], ["oled:SDA", oled.interface.mosi_gpio],
    ["oled:DC", oled.device_interface.dc_gpio], ["oled:RES", oled.device_interface.reset_gpio],
  ]) assert.ok(graph.same(port, pin(gpio)), `Incorrect ${port} → GPIO${gpio}`);
  for (const id of ["bmp:GND", "radar:GND", "oled:GND"]) assert.ok(graph.same(id, "A:j30"));
  for (const id of ["bmp:VIN", "oled:VCC"]) assert.ok(graph.same(id, "A:j31"));
  assert.ok(graph.same("radar:5V", "A:j29"));
  assert.ok(!PLANS.combined.steps.some(s => [s.from, s.to].includes(pin(4))), "OLED CS must remain physically unconnected");
});
test("HC-SR04 echo has the 10k/15k divider and no direct 5 V route to GPIO5", () => {
  const plan = PLANS.distance, graph = netGraph(plan);
  assert.deepEqual(plan.steps.filter(s => s.resistor).map(s => [s.from, s.to, s.resistor]), [
    ["B:e14", "B:e18", 10000], ["B:d18", "B:d22", 15000],
  ]);
  assert.ok(graph.same("distance:ECHO", "B:e14"));
  assert.ok(graph.same("A:b29", "B:e18"));
  assert.ok(graph.same("A:j30", "B:d22"));
  assert.ok(!graph.same("distance:ECHO", "A:b29"));
  assert.ok(!plan.modules.includes("oled"));
  assert.ok(graph.same("distance:TRIG", "A:j32"));
  assert.ok(graph.same("distance:VCC", "A:j29"));
});
test("provisional SEN0390 addition shares I2C and only uses the 3.3 V supply", () => {
  const plan = PLANS.light, graph = netGraph(plan);
  assert.equal(plan.steps[plan.initialStep].id, "light-prepare");
  assert.ok(plan.steps.slice(0, plan.initialStep).every(step => step.phase === "Existing wiring reference"));
  assert.ok(graph.same("light:SCL", "bmp:SCL"));
  assert.ok(graph.same("light:SDA", "bmp:SDA"));
  assert.ok(graph.same("light:SCL", "A:j35"));
  assert.ok(graph.same("light:SDA", "A:j36"));
  assert.ok(graph.same("light:VCC", "A:j31"));
  assert.ok(graph.same("light:EN", "A:b29"));
  assert.deepEqual(plan.steps.filter(s => s.resistor).map(s => [s.from, s.to, s.resistor]), [["A:a29", "A:a24", 10000]]);
  assert.ok(graph.same("A:a29", "light:EN"));
  assert.ok(graph.same("A:a24", "A:j30"));
  assert.ok(graph.same("light:GND", "A:j30"));
  assert.ok(!graph.same("light:VCC", "A:j29"));
  assert.ok(!graph.same("light:EN", "A:j31"));
  assert.match(plan.steps.at(-1).note, /not yet|no SEN0390 driver/i);
});
test("all wire changes are power-off steps; unknown modules receive no wiring", () => {
  for (const plan of Object.values(PLANS)) {
    for (const step of plan.steps.filter(s => s.kind === "wire")) assert.equal(step.power, undefined);
    assert.ok(plan.modules.every(m => ["radar", "bmp", "oled", "distance", "light"].includes(m)));
  }
});
