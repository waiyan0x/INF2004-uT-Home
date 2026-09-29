/** Bus tiles: protocol, pin assignments, and speed. */

import { $, html, render } from '../core/dom.js';
import { tile } from '../ui/tile.js';
import { empty } from '../ui/empty.js';
import { describeProtocol } from '../domain/catalog.js';
import { formatPins, formatBusSpeed } from '../core/format.js';

export default {
  render({ profile }) {
    const interfaces = profile?.interfaces ?? [];
    const meta = $('#interfacesMeta');

    if (!interfaces.length) {
      render($('#interfaces'), empty({
        icon: 'route',
        title: 'No interfaces',
        text: 'Load a profile to list its buses.',
      }));
      if (meta) meta.textContent = '';
      return;
    }

    render($('#interfaces'), interfaces.map((bus) => {
      const spec = describeProtocol(bus.protocol);
      const pins = formatPins(bus);
      const speed = formatBusSpeed(bus);
      return tile({
        name: bus.id,
        icon: spec.icon,
        accent: spec.accent,
        wrap: true,
        state: html`${spec.label}${pins ? html` · <code>${pins}</code>` : ''}${speed ? ` · ${speed}` : ''}`,
      });
    }));

    if (meta) meta.textContent = `${interfaces.length} configured`;
  },
};
