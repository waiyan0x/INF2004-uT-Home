import js from '@eslint/js';
import globals from 'globals';
import { defineConfig } from 'eslint/config';

export default defineConfig([
  { ignores: ['build/**', 'firmware/third_party/**', 'legacy/**'] },
  {
    files: ['tools/configurator/static/**/*.js', 'tools/wiring_visualiser/**/*.mjs', 'tests/target/**/*.mjs', 'tests/browser/**/*.mjs'],
    extends: [js.configs.recommended],
  },
  {
    files: ['tools/configurator/static/**/*.js'],
    languageOptions: { sourceType: 'module', globals: globals.browser },
  },
  {
    files: ['tools/wiring_visualiser/static/**/*.mjs'],
    languageOptions: { globals: globals.browser },
  },
  {
    files: ['tools/wiring_visualiser/serve.mjs', 'tools/wiring_visualiser/tests/**/*.mjs', 'tests/target/**/*.mjs', 'tests/browser/**/*.mjs'],
    languageOptions: { globals: globals.node },
  },
]);
