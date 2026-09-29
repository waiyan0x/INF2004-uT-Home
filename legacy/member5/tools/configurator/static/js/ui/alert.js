import { html } from '../core/dom.js';
import { icon } from './icons.js';
import { STATUS_ACCENT } from '../domain/catalog.js';

const GLYPH = { ok: 'check', warn: 'alert', bad: 'x', neutral: 'info' };

/** @param {'ok'|'warn'|'bad'|'neutral'} tone */
export function alert({ tone = 'neutral', title, text }) {
  const accent = STATUS_ACCENT[tone];
  return html`
    <div class="alert ${tone === 'neutral' ? 'alert--neutral' : ''}" data-accent="${accent ?? ''}">
      ${icon(GLYPH[tone])}
      <div>
        <div class="alert__title">${title}</div>
        ${text ? html`<div class="alert__text">${text}</div>` : ''}
      </div>
    </div>`;
}
