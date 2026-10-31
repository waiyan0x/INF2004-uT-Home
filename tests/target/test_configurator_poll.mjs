import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { test } from 'node:test';
import vm from 'node:vm';

test('a user review waits for an active observation without disabling the button', async () => {
  class Element {
    value = ''; checked = false; disabled = false; dataset = {}; children = [];
    files = []; textContent = ''; classList = { toggle() {} };
    addEventListener() {}
    querySelectorAll() { return []; }
    replaceChildren(...children) { this.children = children; }
    append(...children) { this.children.push(...children); }
    createTHead() { return new Element(); }
    createTBody() { return new Element(); }
    insertRow() { return new Element(); }
    insertCell() { return new Element(); }
  }
  const elements = new Map();
  const get = id => {
    if (!elements.has(id)) elements.set(id, new Element());
    return elements.get(id);
  };
  const timers = [], requests = [];
  let finishObservation, duplicate = false;
  const response = data => ({ ok: true, json: async () => data });
  const fetch = async url => {
    requests.push(url);
    if (url === '/api/catalogue') return response({ examples: {}, connections: [], descriptors: [{
      descriptor_id: 'sen0390', descriptor_version: '1.0.0', protocol: 'i2c', execution: { kind: 'periodic' },
      instance_schema: { properties: { interface: { required: ['enable_gpio'],
        properties: { address: { enum: ['0x4a'] } } } } },
    }] });
    if (url === '/api/connect') return response({ capabilities: { features: 0, drivers: [], board: 'test', node_schema: '2.1.0' }, state: {} });
    if (url === '/api/telemetry') return new Promise(resolve => { finishObservation = () => resolve(response({ result: 1 })); });
    if (url === '/api/review') return response({ review_id: 'review', bytes: 2, connected: true,
      plan: { resources: { active_tasks: 0, device_stack_bytes: 0 }, devices: [], rules: [] } });
    if (url === '/api/datasheet_match') return response({ connected: true, matches: [{
      descriptor_id: 'sen0390', descriptor_version: '1.0.0', driver_id: 'sen0390', driver_available: true,
    }] });
    if (url === '/api/datasheet_prepare_i2c') return duplicate
      ? { ok: false, json: async () => ({ error: 'This device name is already in the profile' }) }
      : response({ profile: {} });
    throw new Error(`Unexpected request: ${url}`);
  };
  const document = {
    hidden: false, getElementById: get, querySelector: () => ({ content: 'token' }),
    querySelectorAll: () => [], createElement: () => new Element(),
  };
  const source = (await readFile(new URL('../../tools/configurator/static/app.js', import.meta.url), 'utf8'))
    .replace(/^import .*;\r?\n/gm, '');
  const context = vm.createContext({ document, fetch, setTimeout: fn => timers.push(fn), clearTimeout() {},
    renderProfile() {}, renderTelemetry() {}, renderDiscovery() {}, renderDatasheet() {},
    setupWorkspace: () => () => {}, console });
  vm.runInContext(source, context);
  await new Promise(resolve => setImmediate(resolve));
  await get('connect').onclick();
  assert.equal(get('review').disabled, false);
  const observing = timers.shift()();
  assert.equal(typeof finishObservation, 'function');
  assert.equal(get('review').disabled, false, 'background telemetry must not disable review');
  const reviewing = get('review').onclick();
  assert.equal(get('review').disabled, true, 'an active user action disables review');
  assert.equal(requests.includes('/api/review'), false, 'review waits for the serial observation');
  finishObservation();
  await observing;
  await reviewing;
  assert.ok(requests.indexOf('/api/review') > requests.indexOf('/api/telemetry'));
  assert.equal(get('review').disabled, false);
  assert.match(get('notice').textContent, /Profile validated/);
  get('datasheet-name').value = 'SEN0390';
  await get('datasheet-match').onclick();
  assert.match(get('datasheet-driver').textContent, /sen0390 is installed/);
  assert.equal(get('datasheet-enable-wrap').hidden, false);
  get('datasheet-confirm').checked = true;
  get('datasheet-sda').value = '0'; get('datasheet-scl').value = '1';
  get('datasheet-enable').value = '5'; get('profile').value = '{}';
  await get('datasheet-generate').onclick();
  assert.match(get('datasheet-generate-status').textContent, /Device added to the draft/);
  duplicate = true;
  await get('datasheet-generate').onclick();
  assert.match(get('datasheet-generate-status').textContent, /already in the profile/);
});
