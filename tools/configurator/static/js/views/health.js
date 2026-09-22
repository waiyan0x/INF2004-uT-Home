/** Node health: one alert plus summary tiles from a bounded snapshot. */

import { $, html, render } from '../core/dom.js';
import { tile } from '../ui/tile.js';
import { alert } from '../ui/alert.js';
import { empty } from '../ui/empty.js';
import { formatUptime } from '../core/format.js';
import { loadHealthSnapshot } from '../core/actions.js';

const OVERALL_TONE = { healthy: 'ok', degraded: 'warn', fault: 'bad', offline: 'warn' };

export default {
  mount() {
    $('#loadHealth').addEventListener('click', loadHealthSnapshot);
  },

  render({ health, healthError }) {
    if (!health) {
      render($('#healthStatus'), healthError
        ? alert({ tone: 'bad', title: 'Snapshot rejected', text: healthError })
        : '');
      render($('#health'), empty({
        icon: 'activity',
        title: 'No snapshot loaded',
        text: 'Load a bounded health fixture to inspect node state.',
      }));
      return;
    }

    const tone = OVERALL_TONE[health.overall] ?? 'warn';
    const running = health.tasks.filter((task) => task.state === 'running').length;
    const faulted = health.devices.filter((device) => device.error).length;
    const counters = new Map(health.counters.map((counter) => [counter.name, counter.value]));
    const configActive = health.configuration_state === 'active';

    render($('#healthStatus'), alert({
      tone,
      title: `Node reports ${health.overall}`,
      text: `Snapshot for ${health.node_id}; configuration ${health.configuration_state}.`,
    }));

    render($('#health'), [
      tile({ name: 'Overall', state: health.overall, icon: 'activity', accent: tone === 'ok' ? 'green' : tone === 'bad' ? 'rose' : 'amber' }),
      tile({ name: 'Uptime', state: formatUptime(health.uptime_ms), icon: 'clock', accent: 'cyan' }),
      tile({ name: 'Configuration', state: health.configuration_state, icon: 'braces', accent: configActive ? 'green' : 'amber' }),
      tile({ name: 'Tasks', state: html`${running}/${health.tasks.length} running`, icon: 'cpu', accent: running === health.tasks.length ? 'green' : 'amber' }),
      tile({ name: 'Devices', state: faulted ? html`${faulted} reporting errors` : 'all ready', icon: 'package', accent: faulted ? 'rose' : 'green' }),
      tile({ name: 'Reloads', state: html`${counters.get('configuration_reloads') ?? 0} · ${counters.get('device_errors') ?? 0} errors`, icon: 'refresh', accent: counters.get('device_errors') ? 'rose' : 'slate' }),
    ]);
  },
};
