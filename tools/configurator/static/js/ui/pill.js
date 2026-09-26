import { html } from '../core/dom.js';
import { icon } from './icons.js';

export const pill = ({ icon: iconName, label, accent = 'slate' }) => html`
  <span class="pill" data-accent="${accent}">
    <span class="icon-badge icon-badge--sm">${icon(iconName)}</span>
    <span class="pill__label">${label}</span>
  </span>`;
