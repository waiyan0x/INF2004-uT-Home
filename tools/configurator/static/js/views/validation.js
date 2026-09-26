/** Validator outcome: one alert plus the ordered list of findings. */

import { $, html, render } from '../core/dom.js';
import { alert } from '../ui/alert.js';

export default {
  render({ validation }) {
    const meta = $('#validationMeta');

    if (!validation) {
      render($('#validationStatus'), '');
      render($('#validationErrors'), '');
      if (meta) meta.textContent = '';
      return;
    }

    render($('#validationStatus'), alert(validation));

    const errors = validation.errors ?? [];
    render($('#validationErrors'), errors.map((error) => html`
      <div class="error">
        <div class="error__head">
          <span class="error__code">${error.code}</span>
          <span class="error__path">${error.path}</span>
        </div>
        <div class="error__message">${error.message}</div>
      </div>`));

    if (meta) meta.textContent = errors.length ? `${errors.length} ${errors.length === 1 ? 'finding' : 'findings'}` : '';
  },
};
