/** Approve / reject / export, and the decision record. */

import { $, render, setDisabled } from '../core/dom.js';
import { alert } from '../ui/alert.js';
import { badge } from '../ui/badge.js';
import { decide, exportReviewed } from '../core/actions.js';

export default {
  mount() {
    $('#approve').addEventListener('click', () =>
      decide('approve', $('#reviewer').value, $('#note').value));
    $('#reject').addEventListener('click', () =>
      decide('reject', $('#reviewer').value, $('#note').value));
    $('#export').addEventListener('click', exportReviewed);
  },

  render({ candidate, reviewed, review }) {
    const decided = Boolean(reviewed);
    setDisabled('#approve', !candidate || decided);
    setDisabled('#reject', !candidate || decided);
    setDisabled('#export', !decided);

    const meta = $('#reviewMeta');
    if (meta) {
      if (decided) {
        render(meta, badge({ tone: reviewed.status === 'approved' ? 'ok' : 'bad', label: reviewed.status }));
      } else if (candidate) {
        meta.textContent = 'awaiting decision';
      } else {
        meta.textContent = '';
      }
    }

    render($('#reviewStatus'), review ? alert(review) : '');
  },
};
