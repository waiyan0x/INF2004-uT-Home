import { test, expect } from '@playwright/test';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { extname } from 'node:path';
import { fileURLToPath } from 'node:url';

const rule = length => ({device: 'bmp388', protocol: 'i2c', address: 119,
  write_hex: length === 1 ? '00' : '04', read_length: length, rate: 100000, timeout_ms: 100});
const descriptions = [{id: 'bmp388.md', device: 'bmp388', sha256: 'a'.repeat(64),
  source: {file: 'bmp388.pdf'}, addresses: [118, 119], operations: [
    {name: 'chip_id', notes: 'Read the chip identity.', pages: [25], reason: '', rules: {'119': rule(1)}},
    {name: 'pressure_raw', notes: 'This read may return zeros before conversion setup.', pages: [26],
      reason: '', rules: {'119': rule(3)}},
    {name: 'deferred', notes: 'Requires a timed sequence.', pages: [27],
      reason: 'Separate command/wait/read sequence required.', rules: {}},
  ]}];

function session({connected = true, status = 'execute_ok', operation = 'chip_id', revision = 1,
  busy = false, pending = null, age = 100, error = null} = {}) {
  const length = operation === 'chip_id' ? 1 : 3;
  const available = connected && status === 'execute_ok' && !busy;
  const active = {rule: {...rule(length), revision}, source: {operation}};
  const last = {outcome: 'completed', result: {raw_hex: length === 1 ? '50' : '00 00 00'}};
  return {configured: true, enabled: connected, busy, error, target: {node: 'uthome-0123456789ab'},
    home_assistant: {enabled: true, connected},
    state: {connected, status_fresh: connected, ready: connected && !pending,
      can_load: connected && !pending, can_execute: connected && !pending,
      selected: active, active, pending, last, display: {age_ms: age}},
    display: {status, active_available: connected, data_available: available,
      details: {operation}, raw_hex: length === 1 ? '50' : '00 00 00', tx_accepted: 1, rx_received: length}};
}

async function desk(page, initial = session()) {
  const model = {view: initial, requests: [], action: null, snapshot: null};
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('http://uthome.test/**', async route => {
    const path = new URL(route.request().url()).pathname;
    if (path === '/api/catalogue') {
      await route.fulfill({json: {examples: {}, descriptors: [], connections: []}});
    } else if (path === '/api/runtime-catalogue') {
      await route.fulfill({json: {descriptions}});
    } else if (path === '/api/runtime') {
      const body = route.request().postDataJSON();
      model.requests.push(body);
      const handler = body.action === 'snapshot' ? model.snapshot : model.action;
      if (handler && await handler(body, route) === false) return;
      await route.fulfill({json: model.view});
    } else {
      const file = path === '/' ? 'index.html' : path.slice(1);
      const types = {'.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.woff2': 'font/woff2'};
      await route.fulfill({body: await readFile(new URL(`../../tools/configurator/static/${file}`, import.meta.url)),
        contentType: types[extname(file)] || 'application/octet-stream'});
    }
  });
  await page.clock.install({time: new Date('2026-11-17T00:00:00+08:00')});
  await page.goto('http://uthome.test/');
  await page.locator('#tab-runtime').click();
  await expect(page.locator('#runtime-description option')).toHaveCount(2);
  await expect(page.locator('#runtime-connection')).not.toHaveText('Status unavailable');
  return {model, errors};
}

async function choose(page, operation = 'pressure_raw') {
  await page.locator('#runtime-description').selectOption('bmp388.md');
  await page.locator('#runtime-operation').selectOption(operation);
  if (operation !== 'deferred') await page.locator('#runtime-address').selectOption('119');
}

test('offline preview and unsupported choices send no peripheral action', async ({page}) => {
  const {model, errors} = await desk(page, session({connected: false, status: 'esp_disconnected'}));
  await choose(page);
  await expect(page.locator('#runtime-preview')).toContainText('TX 04 / Read 3 bytes');
  await expect(page.locator('#runtime-load')).toBeDisabled();
  await expect(page.locator('#runtime-execute')).toBeDisabled();
  await page.locator('#runtime-operation').selectOption('deferred');
  await expect(page.locator('#runtime-preview')).toContainText('command/wait/read');
  expect(model.requests.every(item => item.action === 'snapshot')).toBe(true);
  await expect(page.locator('#usb-connection')).toBeHidden();
  await page.locator('#tab-profile').click();
  await expect(page.locator('#usb-connection')).toBeVisible();
  expect(errors).toEqual([]);
});

test('selection stays a preview until load and execution uses the active rule', async ({page}) => {
  const {model, errors} = await desk(page);
  await choose(page);
  await expect(page.locator('#runtime-active')).toHaveText('chip_id / revision 1');
  model.action = body => {
    if (body.action === 'load') model.view = session({status: 'update_ok', operation: 'pressure_raw', revision: 2});
    if (body.action === 'execute') model.view = session({operation: 'pressure_raw', revision: 2});
  };
  await page.locator('#runtime-load').click();
  await expect(page.locator('#runtime-status')).toHaveText('Rule loaded');
  await expect(page.locator('#runtime-raw')).toHaveText('Unavailable');
  await expect(page.locator('#runtime-active')).toHaveText('pressure_raw / revision 2');
  await page.locator('#runtime-execute').click();
  await expect(page.locator('#runtime-raw')).toHaveText('00 00 00');
  await expect(page.locator('#runtime-rx')).toHaveText('3');
  expect(model.requests.filter(item => item.action === 'execute')).toHaveLength(1);
  expect(model.requests.find(item => item.action === 'load')).toMatchObject({description: 'bmp388.md',
    operation: 'pressure_raw', address: 119, sha256: 'a'.repeat(64)});
  expect(errors).toEqual([]);
});

test('a pending action clears data and cannot be clicked twice', async ({page}) => {
  const {model, errors} = await desk(page);
  await choose(page);
  let release;
  const pending = new Promise(resolve => { release = resolve; });
  model.action = async body => {
    if (body.action === 'execute') await pending;
  };
  await page.locator('#runtime-execute').click();
  await expect(page.locator('#runtime-raw')).toHaveText('Unavailable');
  await expect(page.locator('#runtime-execute')).toBeDisabled();
  await page.locator('#runtime-execute').evaluate(button => button.click());
  expect(model.requests.filter(item => item.action === 'execute')).toHaveLength(1);
  release();
  await expect(page.locator('#runtime-raw')).toHaveText('50');
  expect(errors).toEqual([]);
});

test('a rejected rule keeps the prior active rule without live bytes', async ({page}) => {
  const {model} = await desk(page);
  await choose(page);
  model.action = () => { model.view = session({status: 'update_busy'}); };
  await page.locator('#runtime-load').click();
  await expect(page.locator('#runtime-status')).toHaveText('update busy');
  await expect(page.locator('#runtime-active')).toHaveText('chip_id / revision 1');
  await expect(page.locator('#runtime-raw')).toHaveText('Unavailable');
});

test('a lost HTTP action blocks another execute until explicit status checking', async ({page}) => {
  const {model, errors} = await desk(page);
  await choose(page);
  model.action = async (body, route) => {
    if (body.action === 'execute') { await route.abort(); return false; }
  };
  await page.locator('#runtime-execute').click();
  await expect(page.locator('#runtime-status')).toHaveText('Outcome unknown');
  await page.clock.runFor(600);
  await expect(page.locator('#runtime-execute')).toBeDisabled();
  await expect(page.locator('#runtime-check')).toBeEnabled();
  await page.locator('#runtime-check').click();
  await expect(page.locator('#runtime-execute')).toBeEnabled();
  expect(model.requests.filter(item => item.action === 'execute')).toHaveLength(1);
  expect(errors).toEqual([]);
});

test('expired results and failed snapshots clear current bytes but retain receipt details', async ({page}) => {
  const {model, errors} = await desk(page, session({age: 4900}));
  await page.clock.runFor(200);
  await expect(page.locator('#runtime-status')).toHaveText('Result expired');
  await expect(page.locator('#runtime-raw')).toHaveText('Unavailable');
  model.snapshot = async (_body, route) => { await route.fulfill({status: 503, json: {error: 'Unavailable'}}); return false; };
  await page.clock.runFor(600);
  await expect(page.locator('#runtime-active')).toHaveText('Unavailable');
  await expect(page.locator('#runtime-execute')).toBeDisabled();
  await expect(page.locator('#runtime-details')).toContainText('"raw_hex": "50"');
  model.snapshot = null;
  model.view = session({status: 'result_expired'});
  await page.clock.runFor(600);
  await expect(page.locator('#runtime-active')).toHaveText('chip_id / revision 1');
  await expect(page.locator('#runtime-notice')).not.toContainText('Snapshot unavailable');
  expect(errors).toEqual([]);
});

test('unknown cached completion can be explicitly recovered without another execute', async ({page}) => {
  const {model} = await desk(page, session({status: 'outcome_unknown', pending: {id: 7}}));
  await expect(page.locator('#runtime-execute')).toBeDisabled();
  model.action = body => { if (body.action === 'recover') model.view = session(); };
  await page.locator('#runtime-recover').click();
  await expect(page.locator('#runtime-raw')).toHaveText('50');
  expect(model.requests.some(item => item.action === 'execute')).toBe(false);
});

test('a delayed snapshot cannot make old data current when it finally arrives', async ({page}) => {
  const {model} = await desk(page);
  let release;
  const delayed = new Promise(resolve => { release = resolve; });
  model.snapshot = async () => { await delayed; };
  await page.clock.runFor(600);
  await expect.poll(() => model.requests.filter(item => item.action === 'snapshot').length).toBe(2);
  await page.clock.runFor(2100);
  release();
  await page.waitForResponse(response => response.url().endsWith('/api/runtime'));
  await expect(page.locator('#runtime-active')).toHaveText('Unavailable');
  await expect(page.locator('#runtime-raw')).toHaveText('Unavailable');
  await expect(page.locator('#runtime-execute')).toBeDisabled();
  model.snapshot = null;
  await page.clock.runFor(600);
  await expect(page.locator('#runtime-active')).toHaveText('chip_id / revision 1');
});

test('approved compact layout works at desktop and narrow widths', async ({page}) => {
  await page.setViewportSize({width: 1440, height: 1000});
  const {errors} = await desk(page);
  await choose(page);
  const folder = new URL('../../build/runtime-ui-review-20261117/', import.meta.url);
  await mkdir(folder, {recursive: true});
  await page.screenshot({path: fileURLToPath(new URL('implemented-desktop.png', folder)), fullPage: true});
  const markup = await page.evaluate(() => {
    const copy = globalThis.document.documentElement.cloneNode(true);
    copy.querySelectorAll('script, meta[name="session"]').forEach(item => item.remove());
    copy.querySelectorAll('select').forEach(select => {
      const value = globalThis.document.getElementById(select.id)?.value;
      for (const option of select.options) option.toggleAttribute('selected', option.value === value);
    });
    return '<!DOCTYPE html>\n' + copy.outerHTML;
  });
  await writeFile(new URL('implemented-preview.html', folder), markup);
  await page.setViewportSize({width: 375, height: 900});
  await expect(page.locator('#runtime-load')).toBeVisible();
  await expect(page.locator('#runtime-execute')).toBeVisible();
  expect(await page.evaluate(() => globalThis.document.documentElement.scrollWidth <= globalThis.innerWidth + 1)).toBe(true);
  await page.screenshot({path: fileURLToPath(new URL('implemented-mobile.png', folder)), fullPage: true});
  await page.locator('#tab-runtime').focus();
  await page.keyboard.press('Home');
  await expect(page.locator('#tab-profile')).toBeFocused();
  expect(errors).toEqual([]);
});
