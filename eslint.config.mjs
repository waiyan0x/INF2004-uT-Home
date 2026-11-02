import js from '@eslint/js';
import globals from 'globals';
import { defineConfig } from 'eslint/config';

export default defineConfig([
  { ignores: ['build/**', 'firmware/third_party/**', 'legacy/**', '**/.desloppify/**'] },
  {
    files: ['tools/configurator/static/**/*.js', 'visualiser/**/*.mjs', 'tests/target/**/*.mjs'],
    extends: [js.configs.recommended],
  },
  {
    files: ['tools/configurator/static/**/*.js'],
    languageOptions: { sourceType: 'module', globals: globals.browser },
  },
  {
    files: ['visualiser/dist/**/*.mjs'],
    languageOptions: { globals: globals.browser },
  },
  {
    files: ['visualiser/serve.mjs', 'visualiser/tests/**/*.mjs', 'tests/target/**/*.mjs'],
    languageOptions: { globals: globals.node },
  },
]);
