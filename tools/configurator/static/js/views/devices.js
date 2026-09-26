/** Device tiles, one per configured sensor or display. */

import { $, html, render } from '../core/dom.js';
import { tile } from '../ui/tile.js';
import { empty } from '../ui/empty.js';
import { describeDriver, describeBehaviour, requiresVoltageDivider } from '../domain/catalog.js';

/** Merge the configured device with whatever the health snapshot reports. */
function stateLine(device, reported) {
  const spec = describeDriver(device.driver);

  if (device.enabled === false) return html`${spec.label} · disabled`;
  if (reported?.error) return html`${spec.label} · ${reported.error}`;
  if (reported) return html`${spec.label} · ${reported.state}`;
  return html`${spec.label} · ${describeBehaviour(device.behaviour?.profile)}`;
}

function accentFor(device, reported) {
  if (device.enabled === false) return 'slate';
  if (reported?.error) return 'rose';
  if (requiresVoltageDivider(device.driver)
      && device.interface?.echo_voltage_divider_required !== true) return 'amber';
  return describeDriver(device.driver).accent;
}

export default {
  render({ profile, health }) {
    const devices = profile?.devices ?? [];
    const meta = $('#devicesMeta');

    if (!devices.length) {
      render($('#devices'), empty({
        icon: 'package',
        title: 'No devices',
        text: 'Load a profile to list its sensors.',
      }));
      if (meta) meta.textContent = '';
      return;
    }

    const reportedById = new Map((health?.devices ?? []).map((device) => [device.device_id, device]));

    render($('#devices'), devices.map((device) => {
      const reported = reportedById.get(device.device_id);
      return tile({
        name: device.device_id,
        state: stateLine(device, reported),
        icon: describeDriver(device.driver).icon,
        accent: accentFor(device, reported),
      });
    }));

    const enabled = devices.filter((device) => device.enabled !== false).length;
    if (meta) meta.textContent = `${enabled}/${devices.length} enabled`;
  },
};
