/** Profile source toolbar. The textarea itself is owned by core/editor.js. */

import { $ } from '../core/dom.js';
import { loadSampleProfile, importProfile, importRecommendation, markProfileEdited, formatProfile, validateProfile } from '../core/actions.js';

export default {
  mount() {
    $('#loadSample').addEventListener('click', loadSampleProfile);
    $('#format').addEventListener('click', formatProfile);
    $('#validate').addEventListener('click', validateProfile);
    $('#editor').addEventListener('input', markProfileEdited);

    $('#fileInput').addEventListener('change', async (event) => {
      const [file] = event.target.files;
      if (file) await importProfile(file);
      event.target.value = '';
    });
    $('#recommendationInput').addEventListener('change', async (event) => {
      const [file] = event.target.files;
      if (file) await importRecommendation(file);
      event.target.value = '';
    });
  },

  render() {},
};
