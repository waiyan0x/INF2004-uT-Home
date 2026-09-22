import { html } from '../core/dom.js';
import { STATUS_ACCENT } from '../domain/catalog.js';

export const badge = ({ tone = 'idle', label }) =>
  html`<span class="badge" data-accent="${STATUS_ACCENT[tone]}">${label}</span>`;
