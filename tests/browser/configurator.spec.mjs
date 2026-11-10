import { test, expect } from '@playwright/test';
import { readFile } from 'node:fs/promises';
import { extname } from 'node:path';

const draft = {node_id: 'original', devices: [], interfaces: [], queues: []};
const board = {flags: 16, active_revision: 1, saved_revision: 1};
const catalogue = {examples: {'distance-only': draft}, descriptors: [], connections: [
  {preset_id: 'bench_manual', label: 'Reviewed connection', method: 'manual', wiring: 'Reviewed pins'},
]};
const reviewed = {review_id: 'review-token', connected: true,
  plan: {resources: {active_tasks: 0, device_stack_bytes: 0}, devices: [], rules: []}};
const report = selected => ({discovery_id: 'discovery-token', expires_in_ms: 60000,
  preset_id: 'bench_manual', preset_version: '1', state: board,
  capture: {result: 'manual', active_revision: 1}, devices: [{
    observation: {method: 'manual', protocol: 'gpio'},
    identity: {status: 'candidate', selected: selected ? {id: 'hc_sr04', version: '1'} : null,
      candidates: [{id: 'hc_sr04', version: '1'}]},
    capabilities: {support: 'available', hardware_qualified: false},
    recommendation: selected ? {status: 'awaiting_review', instance: {}, settings: []}
      : {status: 'awaiting_identity'},
  }]});
const snapshot = () => ({result: 0, discovery_id: 'discovery-token', timestamp_ms: 2500,
  configuration: {...board, flags: 48}, health: {total_events: 2},
  runtime: {failed_workers: 0, service_error: 0}, queues: [],
  samples: [{type: 3, name: 'sonar', quality: 'fresh', age_ms: 0,
    value: {distance_mm: 120, echo_us: 700}}]});

function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return {promise, resolve};
}

async function openDesk(page, overrides = {}) {
  const requests = [];
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  const handlers = {
    catalogue: () => catalogue,
    connect: () => ({capabilities: {features: 0, drivers: [], board: 'test', node_schema: '2.1.0'}, state: board}),
    telemetry: () => ({result: 1, discovery_id: 'discovery-token'}),
    discover: () => report(false),
    confirm_discovered: () => report(true),
    add_discovered: () => ({profile: {...draft, node_id: 'added'}}),
    review: () => reviewed,
    apply: () => ({...board, status: 1, message: 'Saved'}),
    ...overrides,
  };
  await page.route('http://uthome.test/**', async route => {
    const pathname = new URL(route.request().url()).pathname;
    if (pathname.startsWith('/api/')) {
      const action = pathname.slice(5);
      const body = route.request().method() === 'POST' ? route.request().postDataJSON() : null;
      requests.push({action, body});
      if (!handlers[action]) throw new Error(`Unexpected API action: ${action}`);
      const response = await handlers[action](body, route);
      if (response !== undefined) await route.fulfill({json: response});
      return;
    }
    const file = pathname === '/' ? 'index.html' : pathname.slice(1);
    const url = new URL(`../../tools/configurator/static/${file}`, import.meta.url);
    const types = {'.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.woff2': 'font/woff2'};
    await route.fulfill({body: await readFile(url), contentType: types[extname(file)] || 'application/octet-stream'});
  });
  await page.clock.install({time: new Date('2026-11-02T08:00:00Z')});
  await page.goto('http://uthome.test/');
  await page.getByText('Edit profile JSON', {exact: true}).click();
  await page.getByText('Discover or select hardware', {exact: true}).click();
  return {requests, errors, handlers};
}

async function connectAndDetect(page) {
  await page.locator('#connect').click();
  await page.locator('#wiring-confirmed').check();
  await page.locator('#detect').click();
  await expect(page.getByRole('button', {name: 'Confirm module'})).toBeVisible();
}

test('datasheet matching uses server requirements while catalogue loading is pending', async ({page}) => {
  const pending = deferred();
  const desk = await openDesk(page, {catalogue: () => pending.promise,
    datasheet_match: () => ({connected: true, matches: [{
      descriptor_id: 'sen0390', descriptor_version: '1.0.0', driver_id: 'sen0390', driver_available: true,
      draft_builder: {eligible: true, addresses: ['0x4a'], needs_enable: true},
    }]}),
  });
  await page.locator('#connect').click();
  await page.getByText('Driver availability and configuration', {exact: true}).click();
  await page.locator('#datasheet-name').fill('sen0390');
  await page.locator('#datasheet-match').click();
  await expect(page.locator('#datasheet-address')).toHaveValue('0x4a');
  await expect(page.locator('#datasheet-enable-wrap')).toBeVisible();
  await expect(page.locator('#datasheet-driver')).toContainText('sen0390 is installed');
  pending.resolve(catalogue);
  expect(desk.errors).toEqual([]);
});

test('an import cannot replace edits made while File.text is pending', async ({page}) => {
  await page.addInitScript(() => {
    const read = File.prototype.text;
    File.prototype.text = async function () {
      await new Promise(resolve => { globalThis.releaseImport = resolve; });
      return read.call(this);
    };
  });
  const desk = await openDesk(page);
  await expect(page.locator('#profile')).toHaveValue(JSON.stringify(draft, null, 2));
  await page.locator('#file').setInputFiles({name: 'profile.json', mimeType: 'application/json',
    buffer: Buffer.from('{"node_id":"imported"}')});
  const newer = '{"node_id":"edited while loading"}';
  await page.locator('#profile').fill(newer);
  await page.evaluate(() => globalThis.releaseImport());
  await expect(page.locator('#notice')).toContainText('draft changed while reading');
  await expect(page.locator('#profile')).toHaveValue(newer);
  expect(desk.errors).toEqual([]);
});

test('a failed local import preserves a valid review and recovery state', async ({page}) => {
  const desk = await openDesk(page, {telemetry: snapshot});
  await page.locator('#connect').click();
  await page.clock.runFor(1000);
  await expect(page.locator('#recover')).toBeEnabled();
  await page.locator('#review').click();
  await expect(page.locator('#apply')).toBeEnabled();
  await page.locator('#tab-profile').click();
  await page.locator('#file').setInputFiles({name: 'large.json', mimeType: 'application/json',
    buffer: Buffer.alloc(32769)});
  await expect(page.locator('#notice')).toHaveText('Profile file exceeds 32 KiB');
  await expect(page.locator('#apply')).toBeEnabled();
  await expect(page.locator('#recover')).toBeEnabled();
  expect(desk.errors).toEqual([]);
});

for (const action of ['catalogue', 'connect']) {
  for (const status of [200, 503]) {
    test(`${action} keeps HTTP ${status} context when JSON decoding fails`, async ({page}) => {
      const desk = await openDesk(page, {[action]: async (_body, route) => {
        await route.fulfill({status, body: '<html>Upstream failure</html>', contentType: 'text/html'});
      }});
      if (action === 'connect') await page.locator('#connect').click();
      await expect(page.locator('#notice')).toContainText(`HTTP ${status}`);
      await expect(page.locator('#notice')).toContainText('response was not valid JSON');
      expect(desk.errors).toEqual([]);
    });
  }
}

test('real modules wire confirmation, busy controls, addition and review navigation', async ({page}) => {
  const confirmation = deferred();
  const desk = await openDesk(page, {confirm_discovered: () => confirmation.promise});
  await expect(page.locator('#profile')).toHaveValue(JSON.stringify(draft, null, 2));
  await connectAndDetect(page);
  const check = page.locator('#discovery-results input[type=checkbox]');
  const confirm = page.getByRole('button', {name: 'Confirm module'});
  await expect(confirm).toBeDisabled();
  await check.check();
  await confirm.click();
  await expect(confirm).toBeDisabled();
  await check.uncheck();
  await check.check();
  await expect(confirm).toBeDisabled();
  confirmation.resolve(report(true));
  await page.getByRole('button', {name: /Add .* to profile/}).click();
  await expect(page.locator('#profile')).toHaveValue(JSON.stringify({...draft, node_id: 'added'}, null, 2));
  await page.locator('#review').click();
  await expect(page.locator('#tab-review')).toHaveAttribute('aria-selected', 'true');
  await expect(page.locator('#workspace-review')).toBeVisible();
  await expect(page.locator('#plan table')).toBeVisible();
  expect(desk.requests.filter(row => row.action === 'confirm_discovered')).toHaveLength(1);
  expect(desk.requests.filter(row => row.action === 'add_discovered')).toHaveLength(1);
  expect(desk.errors).toEqual([]);
});

for (const mode of ['edit', 'import']) {
  test(`a delayed catalogue preserves an intervening ${mode}`, async ({page}) => {
    const pending = deferred();
    const desk = await openDesk(page, {catalogue: () => pending.promise});
    const content = JSON.stringify({...draft, node_id: mode});
    if (mode === 'edit') await page.locator('#profile').fill(content);
    else await page.locator('#file').setInputFiles({name: 'profile.json', mimeType: 'application/json', buffer: Buffer.from(content)});
    await expect(page.locator('#profile')).toHaveValue(content);
    pending.resolve(catalogue);
    await expect(page.locator('#examples option')).toHaveCount(2);
    await expect(page.locator('#profile')).toHaveValue(content);
    expect(desk.errors).toEqual([]);
  });
}

test('catalogue HTTP failures retain the server explanation', async ({page}) => {
  const desk = await openDesk(page, {catalogue: async (_body, route) => {
    await route.fulfill({status: 503, json: {error: 'Catalogue temporarily unavailable'}});
  }});
  await expect(page.locator('#notice')).toHaveText('Catalogue temporarily unavailable');
  expect(desk.errors).toEqual([]);
});

for (const failure of ['network', 'json', 'unavailable']) {
  test(`a ${failure} telemetry failure clears every current observation and recovery control`, async ({page}) => {
    const desk = await openDesk(page, {telemetry: snapshot});
    await page.locator('#connect').click();
    await page.clock.runFor(1000);
    await expect(page.locator('#pills')).toContainText('120 mm');
    await expect(page.locator('#readings')).toContainText('120 mm');
    await expect(page.locator('#readings')).toContainText('echo 700 µs');
    await expect(page.locator('#recover')).toBeEnabled();
    desk.handlers.telemetry = async (_body, route) => {
      if (failure === 'network') await route.abort('failed');
      else if (failure === 'json') await route.fulfill({body: '{', contentType: 'application/json'});
      else return {result: 1};
    };
    await page.clock.runFor(1000);
    await expect(page.locator('#timestamp')).toHaveText('Snapshot unavailable');
    await expect(page.locator('#pills')).not.toContainText('120 mm');
    await expect(page.locator('#readings')).toBeEmpty();
    await expect(page.locator('#healthTiles')).toBeEmpty();
    await expect(page.locator('#diagnostics')).toBeEmpty();
    await expect(page.locator('#state')).toContainText('Board state unavailable');
    await expect(page.locator('#recover')).toBeDisabled();
    if (failure === 'network') await expect(page.locator('#notice')).toContainText('telemetry failed:');
    expect(desk.errors).toEqual([]);
  });
}

const i2cMatch = needsEnable => ({connected: true, matches: [{
  descriptor_id: 'sen0390', descriptor_version: '1.0.0', driver_id: 'sen0390', driver_available: true,
  draft_builder: {eligible: true, addresses: ['0x4a'], needs_enable: needsEnable},
}]});

async function selectI2cDraft(page, needsEnable = true) {
  await page.locator('#connect').click();
  await page.getByText('Driver availability and configuration', {exact: true}).click();
  await page.locator('#datasheet-name').fill('sen0390');
  await page.locator('#datasheet-match').click();
  await expect(page.locator('#datasheet-address')).toHaveValue('0x4a');
  await page.locator('#datasheet-sda').fill('4');
  await page.locator('#datasheet-scl').fill('5');
  if (needsEnable) await page.locator('#datasheet-enable').fill('6');
  await page.locator('#datasheet-confirm').check();
}

for (const needsEnable of [true, false]) {
  test(`datasheet draft sends reviewed inputs and consumes review, enable=${needsEnable}`, async ({page}) => {
    const prepared = {...draft, node_id: 'prepared'};
    const desk = await openDesk(page, {datasheet_match: () => i2cMatch(needsEnable),
      datasheet_prepare_i2c: () => ({profile: prepared})});
    await selectI2cDraft(page, needsEnable);
    await page.locator('#review').click();
    await expect(page.locator('#apply')).toBeEnabled();
    await page.locator('#tab-profile').click();
    await page.locator('#datasheet-generate').click();
    await expect(page.locator('#profile')).toHaveValue(JSON.stringify(prepared, null, 2));
    await expect(page.locator('#apply')).toBeDisabled();
    expect(desk.requests.find(row => row.action === 'datasheet_prepare_i2c').body).toEqual({
      profile: JSON.stringify(draft, null, 2), descriptor_id: 'sen0390', descriptor_version: '1.0.0',
      device_id: 'i2c_sensor', address: '0x4a', sda_gpio: 4, scl_gpio: 5,
      ...(needsEnable ? {enable_gpio: 6} : {}), wiring_confirmed: true, identity_confirmed: true,
    });
    expect(desk.errors).toEqual([]);
  });
}

for (const missing of ['sda', 'enable']) {
  test(`missing local ${missing} preserves a valid review without sending a draft request`, async ({page}) => {
    const desk = await openDesk(page, {datasheet_match: () => i2cMatch(true)});
    await selectI2cDraft(page);
    await page.locator('#review').click();
    await expect(page.locator('#apply')).toBeEnabled();
    await page.locator('#tab-profile').click();
    await page.locator(`#datasheet-${missing}`).fill('');
    await page.locator(`#datasheet-${missing}`).blur();
    await page.locator('#datasheet-confirm').check();
    await page.locator('#datasheet-generate').click();
    await expect(page.locator('#datasheet-generate-status')).toContainText('Select');
    await expect(page.locator('#apply')).toBeEnabled();
    expect(desk.requests.filter(row => row.action === 'datasheet_prepare_i2c')).toEqual([]);
    expect(desk.errors).toEqual([]);
  });
}

test('a delayed datasheet draft cannot replace a profile edited and then restored', async ({page}) => {
  const pending = deferred();
  const desk = await openDesk(page, {datasheet_match: () => i2cMatch(true),
    datasheet_prepare_i2c: () => pending.promise});
  await selectI2cDraft(page);
  await page.locator('#datasheet-generate').click();
  await expect.poll(() => desk.requests.filter(row => row.action === 'datasheet_prepare_i2c').length).toBe(1);
  const original = await page.locator('#profile').inputValue();
  await page.locator('#profile').fill('{"node_id":"newer"}');
  await page.locator('#profile').fill(original);
  pending.resolve({profile: {...draft, node_id: 'stale'}});
  await expect(page.locator('#notice')).toContainText('profile changed');
  await expect(page.locator('#profile')).toHaveValue(original);
  expect(desk.errors).toEqual([]);
});

for (const success of [true, false]) {
  test(`apply waits for outstanding telemetry and follows its result, success=${success}`, async ({page}) => {
    const pending = deferred();
    const desk = await openDesk(page, {telemetry: async (_body, route) => {
      await pending.promise;
      if (success) return snapshot();
      await route.abort('failed');
    }});
    await page.locator('#connect').click();
    await page.locator('#review').click();
    await expect(page.locator('#apply')).toBeEnabled();
    await page.clock.runFor(1000);
    await expect.poll(() => desk.requests.filter(row => row.action === 'telemetry').length).toBe(1);
    await page.locator('#apply').click();
    await expect(page.locator('#review')).toBeDisabled();
    await expect(page.locator('#file')).toBeDisabled();
    expect(desk.requests.filter(row => row.action === 'apply')).toEqual([]);
    pending.resolve();
    await expect(page.locator('#review')).toBeEnabled();
    await expect(page.locator('#file')).toBeEnabled();
    expect(desk.requests.filter(row => row.action === 'apply').map(row => row.body))
      .toEqual(success ? [{review_id: 'review-token'}] : []);
    if (!success) await expect(page.locator('#notice')).toContainText('telemetry failed:');
    await expect(page.locator('#apply')).toBeDisabled();
    expect(desk.errors).toEqual([]);
  });
}

test('catalogue network failure identifies its operation', async ({page}) => {
  const desk = await openDesk(page, {catalogue: async (_body, route) => { await route.abort('failed'); }});
  await expect(page.locator('#notice')).toContainText('Catalogue failed:');
  expect(desk.errors).toEqual([]);
});

for (const action of ['discover', 'review']) {
  test(`editing while ${action} is pending rejects its stale response`, async ({page}) => {
    const pending = deferred();
    const desk = await openDesk(page, {[action]: () => pending.promise});
    await page.locator('#connect').click();
    if (action === 'discover') await page.locator('#wiring-confirmed').check();
    await page.locator(action === 'discover' ? '#detect' : '#review').click();
    await expect.poll(() => desk.requests.some(row => row.action === action)).toBe(true);
    const edited = JSON.stringify({...draft, node_id: 'edited'});
    await page.locator('#profile').fill(edited);
    pending.resolve(action === 'discover' ? report(false) : reviewed);
    await expect(page.locator('#review')).toBeEnabled();
    await expect(page.locator('#profile')).toHaveValue(edited);
    await expect(page.locator('#apply')).toBeDisabled();
    await expect(page.locator('#discovery-results button')).toHaveCount(0);
    expect(desk.errors).toEqual([]);
  });
}

test('expired discovery cannot add and a rejected apply consumes its review token', async ({page}) => {
  const pending = deferred();
  const desk = await openDesk(page, {discover: () => report(true), apply: async (_body, route) => {
    await pending.promise;
    await route.fulfill({status: 409, json: {error: 'Review expired'}});
  }});
  await page.locator('#connect').click();
  await page.locator('#wiring-confirmed').check();
  await page.locator('#detect').click();
  await expect(page.getByRole('button', {name: /Add .* to profile/})).toBeEnabled();
  await page.clock.runFor(60001);
  await expect(page.locator('#discovery-results button')).toHaveCount(0);
  expect(desk.requests.filter(row => row.action === 'add_discovered')).toHaveLength(0);
  await page.locator('#review').click();
  await page.locator('#apply').click();
  await expect(page.locator('#apply')).toBeDisabled();
  pending.resolve();
  await expect(page.locator('#notice')).toHaveText('Review expired');
  await expect(page.locator('#apply')).toBeDisabled();
  expect(desk.requests.filter(row => row.action === 'apply').map(row => row.body)).toEqual([{review_id: 'review-token'}]);
  expect(desk.errors).toEqual([]);
});


test('connecting during catalogue loading still initializes an untouched empty draft', async ({page}) => {
  const pending = deferred();
  const desk = await openDesk(page, {catalogue: () => pending.promise});
  await page.locator('#connect').click();
  pending.resolve(catalogue);
  await expect(page.locator('#profile')).toHaveValue(JSON.stringify(draft, null, 2));
  expect(desk.errors).toEqual([]);
});
