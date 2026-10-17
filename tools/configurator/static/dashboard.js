/** Recovered dashboard presentation; profiles describe requests, not live hardware. */
const el = id => document.getElementById(id);
const svgNS = 'http://www.w3.org/2000/svg';
const drivers = {
  bmp388: ['Bosch BMP388', 'thermometer', 'orange'],
  ssd1306_spi: ['SSD1306 OLED', 'monitor', 'blue'],
  hlk_ld2450: ['HLK-LD2450', 'radar', 'violet'],
  hc_sr04: ['HC-SR04', 'ruler', 'cyan'],
  pwm_output: ['PWM output', 'gpio', 'teal'],
};
const display = value => value === null || typeof value === 'object'
  ? '[invalid value]' : String(value);

function node(tag, className = '', text = '') {
  const element = document.createElement(tag);
  element.className = className;
  element.textContent = display(text);
  return element;
}

function icon(name, small = false) {
  const badge = node('span', `icon-badge${small ? ' icon-badge--sm' : ''}`);
  const svg = document.createElementNS(svgNS, 'svg');
  for (const [key, value] of Object.entries({ viewBox: '0 0 24 24', fill: 'none',
    stroke: 'currentColor', 'stroke-width': '2', 'stroke-linecap': 'round',
    'stroke-linejoin': 'round', 'aria-hidden': 'true' })) svg.setAttribute(key, value);
  const use = document.createElementNS(svgNS, 'use');
  use.setAttribute('href', `#i-${name}`);
  svg.append(use);
  badge.append(svg);
  return badge;
}

export function tile(title, detail, glyph, accent) {
  const result = node('div', 'tile tile--wrap');
  result.dataset.accent = accent;
  const row = node('div', 'tile__row');
  const body = node('div', 'tile__body');
  body.append(node('div', 'tile__name', title), node('div', 'tile__state', detail));
  row.append(icon(glyph), body);
  result.append(row);
  return result;
}

function tiles(id, rows, empty) {
  el(id).replaceChildren(...(rows.length ? rows : [node('p', 'hint', empty)]));
}

const rows = value => Array.isArray(value)
  ? value.filter(item => item && typeof item === 'object' && !Array.isArray(item)) : [];

export function renderProfile(text) {
  let profile;
  try { profile = JSON.parse(text); } catch { profile = null; }
  if (!profile || typeof profile !== 'object' || Array.isArray(profile)) profile = {};
  el('appbarNode').textContent = typeof profile.node_id === 'string' ? profile.node_id : 'no valid profile loaded';
  el('profileMeta').textContent = profile.board?.variant
    ? `${display(profile.board.variant)} · schema ${display(profile.schema_version ?? '—')}` : 'JSON draft';
  el('profileRuntime').textContent = profile.runtime
    ? `Source: ${display(profile.runtime.configuration_source ?? 'unset')} · ${display(profile.runtime.max_device_tasks ?? '—')} task limit · configuration only`
    : 'No valid configuration loaded.';

  const devices = rows(profile.devices);
  el('devicesMeta').textContent = `${devices.filter(d => d.enabled === true).length}/${devices.length} enabled`;
  tiles('devices', devices.map(device => {
    const [name, glyph, accent] = typeof device.driver === 'string' && Object.hasOwn(drivers, device.driver)
      ? drivers[device.driver] : ['Unrecognized driver', 'package', 'slate'];
    return tile(device.device_id ?? 'Unnamed device',
      `${name} · ${device.enabled === true ? 'configured; not hardware-verified' : 'disabled'}`, glyph, accent);
  }), 'Load a profile to list its configured devices.');

  const interfaces = rows(profile.interfaces);
  el('interfacesMeta').textContent = `${interfaces.length} configured`;
  tiles('interfaces', interfaces.map(bus => {
    const protocol = display(bus.protocol ?? 'unknown');
    const pins = Object.entries(bus).filter(([key]) => key.endsWith('_gpio'))
      .map(([key, value]) => `${key.slice(0, -5).toUpperCase()} ${display(value)}`).join(' · ');
    const speed = typeof bus.clock_hz === 'number' ? `${bus.clock_hz.toLocaleString()} Hz`
      : typeof bus.baud_rate === 'number' ? `${bus.baud_rate.toLocaleString()} baud` : '';
    return tile(bus.id ?? 'Unnamed interface', [protocol.toUpperCase(), pins, speed].filter(Boolean).join(' · '),
      ['i2c', 'spi', 'uart', 'gpio'].includes(protocol) ? protocol : 'route', 'violet');
  }), 'No shared bus interfaces in this profile. GPIO connections are declared on devices.');

  const queues = rows(profile.queues);
  el('queuesMeta').textContent = `${queues.length} declared`;
  tiles('queues', queues.map(queue => tile(queue.id ?? 'Unnamed queue',
    `Capacity ${display(queue.depth ?? '—')} · ${display(queue.overflow_policy ?? 'unspecified')}`, 'layers', 'amber')),
  'No configured queues.');
}

function pill(sample, label, value, glyph, accent) {
  const result = node('span', 'pill');
  result.dataset.accent = accent;
  result.append(icon(glyph, true), node('span', 'pill__label', `${sample.name} · ${label}`),
    node('span', '', value));
  if (sample.quality !== 'fresh') result.append(node('span', 'pill__label', sample.quality));
  return result;
}

export function renderTelemetry(snapshot) {
  if (!snapshot || snapshot.result) {
    el('pills').replaceChildren(node('p', 'hint', snapshot
      ? 'Snapshot unavailable · no current readings' : 'No live readings · connect to the application firmware to observe sensors.'));
    el('healthTiles').replaceChildren();
    return;
  }
  const pills = [];
  for (const sample of snapshot.samples) {
    const value = sample.value;
    if (!value) continue;
    if (sample.type === 1) {
      pills.push(pill(sample, 'Temperature', `${value.temperature_c.toFixed(1)} °C`, 'thermometer', 'orange'));
      pills.push(pill(sample, 'Pressure', `${(value.pressure_pa / 100).toFixed(1)} hPa`, 'gauge', 'blue'));
    } else if (sample.type === 3) {
      pills.push(pill(sample, 'Distance', `${value.distance_mm} mm`, 'ruler', 'cyan'));
    } else if (sample.type === 2) {
      pills.push(pill(sample, 'Presence', `${value.targets.filter(t => t.present).length} targets`, 'radar', 'violet'));
    }
  }
  el('pills').replaceChildren(...(pills.length ? pills : [node('p', 'hint', 'No available sensor values in this snapshot.')]));
  const { runtime, health, configuration } = snapshot;
  tiles('healthTiles', [
    tile('Fresh sources', `${snapshot.samples.filter(s => s.quality === 'fresh').length}/${snapshot.samples.length} reported`, 'activity', 'cyan'),
    tile('Failed workers', runtime.failed_workers, 'alert', runtime.failed_workers ? 'rose' : 'green'),
    tile('Health events', `${health.total_events} recorded`, 'shield', 'amber'),
    tile('Revisions', `Active ${configuration.active_revision} · saved ${configuration.saved_revision}`, 'node', 'indigo'),
  ], '');
}

export function setupTheme() {
  const root = document.documentElement;
  try {
    const stored = localStorage.getItem('ut-home-theme');
    if (stored === 'dark' || stored === 'light') root.dataset.theme = stored;
  } catch { /* The OS preference still works if storage is unavailable. */ }
  const button = el('themeToggle');
  function current() {
    return root.dataset.theme ?? (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
  }
  function label() { button.setAttribute('aria-label', `Switch to ${current() === 'dark' ? 'light' : 'dark'} theme`); }
  button.addEventListener('click', () => {
    root.dataset.theme = current() === 'dark' ? 'light' : 'dark';
    try { localStorage.setItem('ut-home-theme', root.dataset.theme); } catch { /* optional preference */ }
    label();
  });
  label();
}
