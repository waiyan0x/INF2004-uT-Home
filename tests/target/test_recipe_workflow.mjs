import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {test} from 'node:test';
import vm from 'node:vm';
import {parseRecipeFields, describeRecipe} from '../../tools/configurator/static/recipes.js';

async function openDesk(drivers = ['i2c_recipe']) {
  class Element {
    value = ''; checked = false; disabled = false; children = []; files = [];
    textContent = ''; listeners = {}; classList = {toggle() {}};
    addEventListener(event, callback) { this.listeners[event] = callback; }
    replaceChildren(...items) { this.children = items; }
    append(...items) { this.children.push(...items); }
  }
  const elements = new Map(), requests = [];
  const get = id => {
    if (!elements.has(id)) elements.set(id, new Element());
    return elements.get(id);
  };
  let complete;
  const response = data => ({ok: true, json: async () => data});
  const document = {hidden: true, getElementById: get, querySelector: () => ({content: 'test'}),
    querySelectorAll: () => [], createElement: () => new Element()};
  const fetch = async (url, options) => {
    requests.push({url, body: options && JSON.parse(options.body)});
    if (url === '/api/catalogue') return response({connections: [], examples: {}});
    if (url === '/api/connect') return response({capabilities: {features: 0, drivers}, state: {}});
    if (url === '/api/recipe-prepare') return new Promise(resolve => {
      complete = data => resolve(response(data));
    });
    throw new Error(`Unexpected API request ${url}`);
  };
  const source = (await readFile(new URL('../../tools/configurator/static/app.js', import.meta.url), 'utf8'))
    .replace(/^import .*;\r?\n/gm, '');
  vm.runInNewContext(source, {document, fetch, setTimeout() {}, clearTimeout() {},
    renderProfile() {}, renderTelemetry() {}, renderBoardState() {}, renderReviewPlan() {},
    updateDiscoveryControls() {}, renderDiscovery() {}, renderDatasheet() {},
    parseRecipeFields, describeRecipe, setupWorkspace: () => () => {}});
  await new Promise(resolve => setImmediate(resolve));
  await get('connect').onclick();
  for (const [id, value] of Object.entries({'recipe-device': 'reader', 'recipe-bus': 'i2c0',
    'recipe-address': '0x77', 'recipe-register': '0x00', 'recipe-read': '1', 'recipe-pause': '0',
    'recipe-offset': '0', 'recipe-length': '1', 'recipe-interval': '500', 'recipe-timeout': '100'})) {
    get(id).value = value;
  }
  get('profile').value = JSON.stringify({node_id: 'original', devices: [], interfaces: []});
  get('recipe-confirm').checked = true;
  get('recipe-confirm').onchange();
  return {get, requests, complete: data => complete(data)};
}

test('unsupported firmware leaves recipe preparation disabled', async () => {
  const {get, requests} = await openDesk([]);
  assert.equal(get('recipe-add').disabled, true);
  assert.match(get('recipe-support').textContent, /cannot run recipes/);
  assert.equal(requests.some(row => row.url === '/api/recipe-prepare'), false);
});

test('recipe preparation changes only the draft and requires a new board review', async () => {
  const {get, requests, complete} = await openDesk();
  assert.equal(get('recipe-add').disabled, false);
  const pending = get('recipe-add').onclick();
  complete({profile: {node_id: 'prepared', devices: [], interfaces: []}, summary: 'Read 1 byte'});
  await pending;
  assert.equal(JSON.parse(get('profile').value).node_id, 'prepared');
  assert.match(get('recipe-status').textContent, /Draft only/);
  assert.equal(get('apply').disabled, true);
  assert.equal(requests.some(row => row.url === '/api/apply'), false);
  assert.equal(requests.find(row => row.url === '/api/recipe-prepare').body.recipe.register, 0);
});

for (const change of ['profile', 'recipe-register']) {
  test(`delayed preparation cannot overwrite a newer ${change} edit`, async () => {
    const {get, complete} = await openDesk();
    const pending = get('recipe-add').onclick();
    if (change === 'profile') get(change).value = '{"node_id":"newer"}';
    else get(change).value = '0x03';
    get(change).listeners.input();
    const current = get('profile').value;
    complete({profile: {node_id: 'obsolete'}, summary: 'Old result'});
    await pending;
    assert.equal(get('profile').value, current);
    assert.match(get('recipe-status').textContent, /changed/);
    assert.equal(get('apply').disabled, true);
  });
}
