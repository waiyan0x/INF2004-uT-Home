import assert from 'node:assert/strict';
import { test } from 'node:test';
import { renderProfile, renderTelemetry } from '../../tools/configurator/static/dashboard.js';

// Minimal DOM contract for presentation-only tests; no browser or USB access.
class Element {
  children = [];
  dataset = {};
  attributes = {};
  ownText = '';
  set textContent(value) { this.ownText = String(value); this.children = []; }
  get textContent() { return this.ownText + this.children.map(c => c.textContent).join(''); }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.ownText = ''; this.children = children; }
  setAttribute(key, value) { this.attributes[key] = value; }
}
const elements = new Map();
globalThis.document = {
  getElementById(id) {
    if (!elements.has(id)) elements.set(id, new Element());
    return elements.get(id);
  },
  createElement: () => new Element(),
  createElementNS: () => new Element(),
};
const text = id => document.getElementById(id).textContent;

test('profile tiles describe configuration, never simulated hardware status', () => {
  renderProfile(JSON.stringify({ node_id: 'test-node', devices: [
    { device_id: 'sonar', driver: 'hc_sr04', enabled: true },
    { device_id: 'motor', driver: 'pwm_output', enabled: false },
  ], interfaces: [{ id: 'bus', protocol: 'i2c', sda_gpio: 8, clock_hz: 100000 }],
  queues: [{ id: 'samples', depth: 4, overflow_policy: 'drop_oldest' }] }));
  assert.equal(text('devicesMeta'), '1/2 enabled');
  assert.match(text('devices'), /configured; not hardware-verified/);
  assert.match(text('devices'), /disabled/);
  assert.match(text('interfaces'), /SDA 8/);
  assert.match(text('queues'), /Capacity 4/);
  renderProfile('{');
  assert.equal(text('appbarNode'), 'no valid profile loaded');
  assert.doesNotMatch(text('devices'), /sonar/);
});

test('untrusted drafts stay plain text and malformed fields do not crash', () => {
  for (const driver of ['__proto__', 'toString', {}, null]) {
    renderProfile(JSON.stringify({ devices: [null, [], { driver, device_id: '<img onerror=alert(1)>', enabled: true }],
      interfaces: [{ id: {}, protocol: {} }], queues: [{ id: {}, depth: {} }] }));
    assert.match(text('devices'), /Unrecognized driver/);
    assert.match(text('devices'), /<img onerror=alert\(1\)>/);
    assert.match(text('interfaces'), /invalid value/);
  }
  for (const draft of ['null', '[]', '42', '"hello"']) assert.doesNotThrow(() => renderProfile(draft));
});

test('live pills retain source identity and freshness, then clear on disconnect', () => {
  renderTelemetry({ samples: [
    { type: 1, name: 'bmp', quality: 'stale', value: { temperature_c: 25, pressure_pa: 101325 } },
    { type: 3, name: 'sonar', quality: 'fresh', value: { distance_mm: 120 } },
    { type: 2, name: 'radar', quality: 'fresh', value: { targets: [{ present: true }, { present: false }] } },
  ], runtime: { failed_workers: 0 }, health: { total_events: 2 },
  configuration: { active_revision: 4, saved_revision: 3 } });
  assert.match(text('pills'), /bmp · Temperature25.0 °Cstale/);
  assert.match(text('pills'), /1013.3 hPa/);
  assert.match(text('pills'), /sonar · Distance120 mm/);
  assert.match(text('pills'), /radar · Presence1 targets/);
  assert.match(text('healthTiles'), /Active 4 · saved 3/);
  renderTelemetry(null);
  assert.doesNotMatch(text('pills'), /25.0|120 mm/);
  assert.equal(text('healthTiles'), '');
  renderTelemetry({ result: 1 });
  assert.match(text('pills'), /Snapshot unavailable/);
});
