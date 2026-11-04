import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: '.',
  testMatch: '*.spec.mjs',
  outputDir: '../../build/browser-results',
  workers: 1,
  use: { browserName: 'chromium', headless: true },
});
