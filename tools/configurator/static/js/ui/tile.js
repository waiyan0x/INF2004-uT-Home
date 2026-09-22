/** A Lovelace-style tile: tinted glyph, name, state line, optional meter. */

import { html } from '../core/dom.js';
import { icon } from './icons.js';

/**
 * @param {object} options
 * @param {string} options.name    primary label
 * @param {*}      options.state   secondary line (string, or html`` markup)
 * @param {string} options.icon    sprite icon name
 * @param {string} options.accent  accent key from the token ramp
 * @param {number} [options.fill]  0..1 occupancy; renders a meter when present
 * @param {boolean} [options.wrap] allow the state line to wrap
 */
export function tile({ name, state, icon: iconName, accent = 'slate', fill, wrap = false }) {
  return html`
    <div class="tile ${wrap ? 'tile--wrap' : ''}" data-accent="${accent}">
      <div class="tile__row">
        <div class="icon-badge">${icon(iconName)}</div>
        <div class="tile__body">
          <div class="tile__name" title="${name}">${name}</div>
          <div class="tile__state">${state}</div>
        </div>
      </div>
      ${fill === undefined ? '' : html`
        <div class="meter"><div class="meter__fill" style="width:${Math.round(fill * 100)}%"></div></div>`}
    </div>`;
}
