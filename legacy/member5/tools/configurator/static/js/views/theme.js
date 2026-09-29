/** Light/dark toggle. The stored choice wins over the OS preference. */

import { $ } from '../core/dom.js';

const KEY = 'ut-home-theme';
const root = document.documentElement;

const read = () => {
  try { return localStorage.getItem(KEY); } catch { return null; }
};

const write = (value) => {
  try { localStorage.setItem(KEY, value); } catch { /* storage unavailable */ }
};

/** Applied before first paint so the page never flashes the wrong theme. */
export function applyStoredTheme() {
  const stored = read();
  if (stored === 'dark' || stored === 'light') root.setAttribute('data-theme', stored);
}

export default {
  mount() {
    $('#themeToggle').addEventListener('click', () => {
      const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
      const current = root.getAttribute('data-theme') ?? (prefersDark ? 'dark' : 'light');
      const next = current === 'dark' ? 'light' : 'dark';
      root.setAttribute('data-theme', next);
      write(next);
    });
  },

  render() {},
};
