/** Queue tiles with an occupancy meter, filled once a snapshot is loaded. */

import { $, html, render } from '../core/dom.js';
import { tile } from '../ui/tile.js';
import { empty } from '../ui/empty.js';

export default {
  render({ profile, health }) {
    const queues = profile?.queues ?? [];
    const meta = $('#queuesMeta');

    if (!queues.length) {
      render($('#queues'), empty({
        icon: 'layers',
        title: 'No queues',
        text: 'Load a profile to list its bounded mailboxes.',
      }));
      if (meta) meta.textContent = '';
      return;
    }

    const usedById = new Map((health?.queues ?? []).map((queue) => [queue.id, queue.used]));

    render($('#queues'), queues.map((queue) => {
      const depth = queue.depth || 1;
      const used = usedById.get(queue.id);
      const known = typeof used === 'number';
      const fill = known ? Math.min(1, used / depth) : 0;

      return tile({
        name: queue.id,
        icon: 'layers',
        accent: !known ? 'slate' : fill >= 0.9 ? 'rose' : 'amber',
        fill,
        state: known
          ? html`${used}/${depth} used · ${queue.overflow_policy}`
          : html`depth ${depth} · ${queue.overflow_policy}`,
      });
    }));

    if (meta) meta.textContent = `${queues.length} declared`;
  },
};
