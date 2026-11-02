import { test, expect } from '@playwright/test';
import { readFile } from 'node:fs/promises';
import { extname } from 'node:path';
import { PLANS, REVISION, address } from '../../visualiser/dist/plan.mjs';

const storageKey = `uthome-wiring-${REVISION}`;

async function openGuide(page, saved) {
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  if (saved !== undefined) {
    await page.addInitScript(({key, value}) => globalThis.localStorage.setItem(key, value),
      {key: storageKey, value: saved});
  }
  await page.route('http://wiring.test/**', async route => {
    const pathname = new URL(route.request().url()).pathname;
    const file = pathname === '/' ? 'index.html' : pathname.slice(1);
    const types = {'.html': 'text/html', '.mjs': 'text/javascript', '.css': 'text/css'};
    await route.fulfill({body: await readFile(new URL(`../../visualiser/dist/${file}`, import.meta.url)),
      contentType: types[extname(file)] || 'application/octet-stream'});
  });
  await page.goto('http://wiring.test/');
  return errors;
}

test('every plan renders its wire endpoints and focus views through the actual modules', async ({page}) => {
  const errors = await openGuide(page);
  await page.setViewportSize({width: 1600, height: 1000});
  for (const [name, plan] of Object.entries(PLANS)) {
    await page.locator('#setup').selectOption(name);
    await expect(page.locator('#wire-table tr')).toHaveCount(plan.steps.filter(step => step.kind === 'wire').length);
    const index = plan.steps.findIndex(step => step.kind === 'wire');
    const wire = plan.steps[index];
    await page.locator(`[data-step="${index}"]`).click();
    await expect(page.locator('#step-title')).toHaveText(wire.title);
    await expect(page.locator('#endpoints')).toContainText(address(wire.from));
    await expect(page.locator('#endpoints')).toContainText(address(wire.to));
    await expect(page.locator('#power-state')).toHaveText('USB unplugged');
    await expect(page.locator('#bench .current-wire')).toHaveCount(1);
    await expect(page.locator(`#bench [data-wire="${wire.id}"]`)).toHaveAttribute('opacity', '1');
    await page.locator('#focus').click();
    await expect(page.locator('#bench')).toHaveAttribute('viewBox', '0 0 700 470');
    await expect(page.locator('#bench > svg')).toHaveCount(2);
    await expect(page.locator('#bench .current-wire')).toHaveCount(2);
    await page.setViewportSize({width: 430, height: 900});
    await expect(page.locator('#bench')).toHaveAttribute('viewBox', '0 0 360 900');
    await page.setViewportSize({width: 1600, height: 1000});
    await page.locator('#overview').click();
    await expect(page.locator('#bench > svg')).toHaveCount(0);
  }
  expect(errors).toEqual([]);
});

test('progress is a persistent user note and plan navigation clamps at both ends', async ({page}) => {
  const errors = await openGuide(page);
  await expect(page.locator('#previous')).toBeDisabled();
  await page.locator('#step-done').check();
  await expect(page.locator('#review-count')).toHaveText('1 checked');
  await page.locator('#next').click();
  await expect(page.locator('#step-done')).not.toBeChecked();
  await page.reload();
  await expect(page.locator('#step-count')).toHaveText(`STEP 2 OF ${PLANS.foundation.steps.length}`);
  await page.locator('#previous').click();
  await expect(page.locator('#step-done')).toBeChecked();
  await expect(page.locator('#previous')).toBeDisabled();
  await page.locator(`[data-step="${PLANS.foundation.steps.length - 1}"]`).click();
  await expect(page.locator('#next')).toBeDisabled();
  expect(errors).toEqual([]);
});

test('future wires are dashed and selected holes describe the correct physical strip', async ({page}) => {
  const errors = await openGuide(page);
  await expect(page.locator('#bench [data-wire]')).toHaveCount(0);
  await page.locator('#all-wires').check();
  const count = PLANS.foundation.steps.filter(step => step.kind === 'wire').length;
  await expect(page.locator('#bench [stroke-dasharray="3 4"]')).toHaveCount(count);
  await page.locator('#bench [data-hole="B:a14"]').click();
  await expect(page.locator('#hole-detail')).toContainText('RIGHT-hand printed row 14');
  await expect(page.locator('#hole-detail')).toContainText('Connected strip:');
  await page.locator('#next').click();
  await expect(page.locator('#hole-detail')).toHaveText('Select a hole to see its address and electrical connection.');
  expect(errors).toEqual([]);
});

for (const saved of ['{', JSON.stringify({plan: 'missing', step: 999}),
  JSON.stringify({plan: 'foundation', step: 999, mode: 'invalid', allWires: 'yes', done: []})]) {
  test(`invalid saved state is bounded: ${saved}`, async ({page}) => {
    const errors = await openGuide(page, saved);
    await expect(page.locator('#setup')).toHaveValue('foundation');
    await expect(page.locator('#overview')).toHaveAttribute('aria-pressed', 'true');
    await expect(page.locator('#all-wires')).not.toBeChecked();
    await expect(page.locator('#review-count')).toHaveText('0 checked');
    await expect(page.locator('#step-title')).not.toBeEmpty();
    expect(errors).toEqual([]);
  });
}

test('the guide remains usable when browser storage is unavailable', async ({page}) => {
  const warnings = [];
  page.on('console', message => {
    if (message.type() === 'warning') warnings.push(message.text());
  });
  await page.addInitScript(() => {
    globalThis.Storage.prototype.getItem = () => { throw new Error('Storage unavailable'); };
    globalThis.Storage.prototype.setItem = () => { throw new Error('Storage unavailable'); };
  });
  const errors = await openGuide(page);
  await page.locator('#next').click();
  await page.locator('#step-done').check();
  await expect(page.locator('#review-count')).toHaveText('1 checked');
  await page.locator('#all-wires').check();
  await expect(page.locator('#bench [data-wire]')).not.toHaveCount(0);
  expect(errors).toEqual([]);
  expect(warnings.filter(message => message.startsWith('Saved wiring progress could not be loaded.'))).toHaveLength(1);
  expect(warnings.filter(message => message.startsWith('Wiring progress could not be saved.'))).toHaveLength(1);
});
