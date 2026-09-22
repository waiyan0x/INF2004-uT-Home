import { html } from '../core/dom.js';
import { icon } from './icons.js';

export const empty = ({ icon: iconName, title, text }) => html`
  <div class="empty">
    ${icon(iconName)}
    <span class="empty__title">${title}</span>
    <span class="empty__text">${text}</span>
  </div>`;
