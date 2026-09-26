/** Icon helper. Path data lives in the sprite at the top of index.html so the
 *  first paint needs no JavaScript; this only emits references to it. */

import { html } from '../core/dom.js';

export const icon = (name) =>
  html`<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
        stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><use href="#i-${name}"/></svg>`;
