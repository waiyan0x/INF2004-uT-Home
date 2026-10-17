import assert from 'node:assert/strict';
import { test } from 'node:test';
import { renderProfile, renderTelemetry } from '../../tools/configurator/static/dashboard.js';
import { renderDiscovery } from '../../tools/configurator/static/discovery.js';

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

const discoveryReport = () => ({ preset_id: 'bench_i2c', preset_version: '0.2.0', discovery_id: 'test-token',
  capture: { result: 'complete', request_id: 1, active_revision: 0, started_ms: 1000, elapsed_ms: 5, build_sha256: 'a'.repeat(64) },
  devices: [{ observation: { address: 119, register: 0, status: 'responding', value: 80 },
    identity: { status: 'verified', selected: { id: 'bmp388', version: '1.0.0' } },
    capabilities: { support: 'available', driver: 'bmp388', hardware_qualified: false },
    recommendation: { status: 'awaiting_review', instance: {},
      settings: [{ label: 'Reading interval', value: '500 ms', source: 'bench_observation', reason: 'Bench starting value.' }] } }] });
const descendants = element => [element, ...element.children.flatMap(descendants)];
const addButtons = () => descendants(document.getElementById('discovery-results')).filter(node => node.onclick);

test('radar candidates require a separate confirmation and never claim verified identity', () => {
  const report = discoveryReport();
  report.preset_id = 'bench_uart';
  report.devices[0] = {observation: { valid_reports: 3, window_ms: 1000, receive_errors: 0, discarded_bytes: 0 },
    identity: {status: 'candidate', selected: null, candidates: [{id: 'hlk_ld2450', version: '1.0.0'}]},
    capabilities: {support: 'unchecked', hardware_qualified: false}, recommendation: {status: 'awaiting_identity'}};
  let confirmed;
  renderDiscovery(report, () => assert.fail('Candidate cannot add'), undefined, (...args) => { confirmed = args; });
  assert.match(text('discovery-results'), /Candidate.*confirm module/);
  assert.match(text('discovery-results'), /zero targets still count/);
  const button = addButtons()[0];
  assert.equal(button.textContent, 'Confirm module'); assert.equal(button.disabled, true);
  const checkbox = descendants(document.getElementById('discovery-results')).find(node => node.type === 'checkbox');
  checkbox.checked = true; checkbox.onchange(); button.onclick();
  assert.deepEqual(confirmed, [0, true, undefined]);
  assert.doesNotMatch(text('discovery-results'), /Add to profile/);
});

test('manual OLED selection cannot confirm without an existing source queue', () => {
  const report = discoveryReport(); report.capture.result = 'manual'; report.preset_id = 'bench_oled';
  report.devices[0] = {observation: {method: 'manual', status: 'not_observed'},
    identity: {status: 'unknown', selected: null, candidates: [{id: 'ssd1306_spi', version: '1.0.0'}]},
    capabilities: {support: 'unchecked'}, recommendation: {status: 'awaiting_identity'}, source_queues: []};
  renderDiscovery(report, null);
  assert.match(text('discovery-status'), /no hardware detection/);
  assert.match(text('discovery-results'), /Add a sensor and its readings queue/);
  const checkbox = descendants(document.getElementById('discovery-results')).find(node => node.type === 'checkbox');
  checkbox.checked = true; checkbox.onchange();
  assert.equal(addButtons()[0].disabled, true);
  assert.doesNotMatch(text('discovery-results'), /firmware undefined/);
});

test('discovery reuses tiles, labels sources and only requests a draft addition', () => {
  let selected = null;
  renderDiscovery(discoveryReport(), index => { selected = index; });
  assert.match(text('discovery-results'), /bmp388 · Identified/);
  assert.match(text('discovery-results'), /not configured by this check/);
  assert.match(text('discovery-results'), /500 ms.*bench observation/);
  assert.match(text('discovery-results'), /hardware qualification pending/);
  assert.equal(addButtons().length, 1);
  addButtons()[0].onclick();
  assert.equal(selected, 0);
  renderDiscovery(null, null, 'Detection expired.');
  assert.equal(addButtons().length, 0);
  assert.equal(text('discovery-results'), '');
  assert.equal(text('discovery-status'), 'Detection expired.');
});

test('no response, unknown, ambiguous and missing driver results never offer settings', () => {
  for (const [status, identity, support, expected] of [
    ['not_responding', 'unknown', 'unchecked', 'No response'],
    ['responding', 'unknown', 'unchecked', 'Unknown device'],
    ['responding', 'ambiguous', 'unchecked', 'More than one match'],
    ['responding', 'verified', 'unavailable', 'driver unavailable'],
  ]) {
    const report = discoveryReport(), device = report.devices[0];
    device.observation.status = status;
    device.identity.status = identity;
    if (identity !== 'verified') device.identity.selected = null;
    device.capabilities.support = support;
    device.recommendation = { status: 'blocked', settings: {} };
    renderDiscovery(report, () => assert.fail('must not add'));
    assert.match(text('discovery-results'), new RegExp(expected));
    assert.doesNotMatch(text('discovery-results'), /500 ms/);
    assert.equal(addButtons().length, 0);
  }
  const busy = discoveryReport(); busy.capture.result = 'busy'; busy.devices = [];
  renderDiscovery(busy);
  assert.match(text('discovery-status'), /left it running/);
  assert.equal(addButtons().length, 0);
});

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
