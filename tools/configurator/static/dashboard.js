/** Recovered dashboard presentation; profiles describe requests, not live hardware. */
/** @typedef {{message?: string, flags: number, active_revision: number, saved_revision: number}} BoardState
 * @typedef {{present: boolean, x_mm: number, y_mm: number, speed_cm_s: number}} RadarTarget
 * @typedef {{name: string, quality: string, age_ms: number|null}} SampleIdentity
 * @typedef {SampleIdentity & (
 *   {type: 1, value: {temperature_c: number, pressure_pa: number}|null} |
 *   {type: 2, value: {targets: RadarTarget[]}|null} |
 *   {type: 3, value: {distance_mm: number, echo_us: number}|null} |
 *   {type: 4, value: {event: number, occurrences: number}|null} |
 *   {type: 5, value: {millilux: number}|null} |
 *   {type: 6, value: {bytes: number[]}|null})} Sample
 * @typedef {{result?: 0, timestamp_ms: number, configuration: BoardState,
 *   runtime: {failed_workers: number, service_error: number},
 *   health: {total_events: number}, samples: Sample[], queues: object[]}} AvailableTelemetry
 * @typedef {AvailableTelemetry | {result: 1|2} | null} TelemetrySnapshot
 */
const el = id => document.getElementById(id);
const svgNS = 'http://www.w3.org/2000/svg';
const drivers = {
  bmp388: ['Bosch BMP388', 'thermometer', 'orange'],
  ssd1306_spi: ['SSD1306 OLED', 'monitor', 'blue'],
  hlk_ld2450: ['HLK-LD2450', 'radar', 'violet'],
  hc_sr04: ['HC-SR04', 'ruler', 'cyan'],
  pwm_output: ['PWM output', 'gpio', 'teal'],
  sen0390: ['SEN0390 light sensor', 'gauge', 'amber'],
  i2c_recipe: ['I²C register-read recipe', 'i2c', 'violet'],
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

/** Display values are converted to text; object/null values render as invalid.
 * @param {unknown} title
 * @param {unknown} detail
 * @param {string} glyph
 * @param {string} accent
 * @returns {HTMLElement}
 */
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

/** @param {string} text Untrusted JSON draft; malformed input renders empty configuration. */
export function renderProfile(text) {
  let profile;
  try { profile = JSON.parse(text); } catch { profile = null; }
  if (!profile || typeof profile !== 'object' || Array.isArray(profile)) profile = {};
  el('appbarNode').textContent = typeof profile.node_id === 'string' ? profile.node_id : 'no valid profile loaded';
  el('profileMeta').textContent = profile.board?.variant
    ? `${display(profile.board.variant)} · schema ${display(profile.schema_version ?? '—')}` : 'JSON draft';

  const devices = rows(profile.devices);
  el('devicesMeta').textContent = `${devices.filter(d => d.enabled === true).length}/${devices.length} enabled`;
  tiles('devices', devices.map(device => {
    const [name, glyph, accent] = typeof device.driver === 'string' && Object.hasOwn(drivers, device.driver)
      ? drivers[device.driver] : ['Unrecognized driver', 'package', 'slate'];
    return tile(device.device_id ?? 'Unnamed device',
      `${name} · ${device.enabled === true ? 'enabled' : 'disabled'}`, glyph, accent);
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

/** Render shared controller metadata for commands and live observations.
 * @param {BoardState} data */
export function renderBoardState(data) {
  el('state').textContent = `${data.message || 'Observed state'} · active revision ${data.active_revision} · saved revision ${data.saved_revision} · ${data.flags & 16 ? 'saved state known' : 'saved state unknown'}${data.flags & 32 ? ' · recovery required' : ''}`;
}

/** @param {HTMLElement} container
 * @param {string[]} headings
 * @param {unknown[][]} records */
export function renderTable(container, headings, records) {
  const table = document.createElement('table');
  const head = table.createTHead().insertRow();
  for (const label of headings) {
    const th = document.createElement('th');
    th.textContent = label;
    head.append(th);
  }
  const body = table.createTBody();
  for (const record of records) {
    const row = body.insertRow();
    for (const value of record) row.insertCell().textContent = String(value);
  }
  container.replaceChildren(table);
}

/** Render an already validated resource plan; the controller owns review freshness.
 * @param {{resources: {active_tasks: number, device_stack_bytes: number},
 *   devices: {device_id: string, driver_id: string, enabled: boolean, period_ms: number|null}[],
 *   rules: {source: string, metric: string, comparison: string, threshold: number,
 *     target: string, duty_permille: number, hysteresis: number, cooldown_ms: number,
 *     max_age_ms: number}[]}} plan
 * @param {boolean} connected
 */
export function renderReviewPlan(plan, connected) {
  const resources = plan.resources;
  el('review-summary').textContent = `${resources.active_tasks} active workers · ${resources.device_stack_bytes} bytes peripheral stacks · ${connected ? 'reviewed against connected firmware' : 'offline review; connect and review again to deploy'}`;
  renderTable(el('plan'), ['Instance', 'Driver', 'Enabled', 'Period'], plan.devices.map(device => [
    device.device_id, device.driver_id, device.enabled ? 'Yes' : 'No',
    device.period_ms ? device.period_ms + ' ms' : 'stream',
  ]));
  if (plan.rules.length) {
    renderTable(el('rule-plan'), ['Source / condition', 'Output', 'Policy'], plan.rules.map(rule => [
      `${rule.source}: ${rule.metric} ${rule.comparison} ${rule.threshold}`,
      `${rule.target}: ${rule.duty_permille / 10}%`,
      `hysteresis ${rule.hysteresis}; cooldown ${rule.cooldown_ms} ms; max age ${rule.max_age_ms} ms`,
    ]));
  } else {
    el('rule-plan').replaceChildren();
  }
}

/** @param {Sample} sample */
function formatSampleValue(sample) {
  const value = sample.value;
  if (!value) return 'Unavailable';
  if (sample.type === 1) return `${value.temperature_c.toFixed(2)} °C · ${value.pressure_pa.toFixed(0)} Pa`;
  if (sample.type === 3) return `${value.distance_mm} mm · echo ${value.echo_us} µs`;
  if (sample.type === 2) return value.targets.map((target, index) => target.present
    ? `${index + 1}: (${target.x_mm}, ${target.y_mm}) mm, ${target.speed_cm_s} cm/s`
    : `${index + 1}: absent`).join('; ');
  if (sample.type === 5) return `${(value.millilux / 1000).toFixed(3)} lux`;
  if (sample.type === 6) return value.bytes.map(byte => byte.toString(16).padStart(2, '0').toUpperCase()).join(' ');
  return `Last event ${value.event} · count ${value.occurrences}`;
}

/** Render all observation surfaces; null means disconnected, nonzero result unavailable.
 * @param {TelemetrySnapshot} snapshot */
export function renderTelemetry(snapshot) {
  if (!snapshot || snapshot.result) {
    el('pills').replaceChildren(node('p', 'hint', snapshot
      ? 'Snapshot unavailable · no current readings' : 'No live readings · connect to the application firmware to observe sensors.'));
    el('healthTiles').replaceChildren();
    el('readings').replaceChildren();
    el('timestamp').textContent = snapshot ? 'Snapshot unavailable' : 'No live connection';
    el('health').textContent = snapshot ? 'No current health observation' : 'No live observations';
    el('state').textContent = snapshot ? 'Board state unavailable; refresh to query again.'
      : 'Disconnected; last board state is no longer current.';
    el('diagnostics').textContent = '';
    return;
  }
  el('timestamp').textContent = `Board uptime ${(snapshot.timestamp_ms / 1000).toFixed(1)} s`;
  renderBoardState(snapshot.configuration);
  el('health').textContent = `${snapshot.health.total_events} recorded health events · ${snapshot.runtime.failed_workers} failed workers · service status ${snapshot.runtime.service_error}`;
  renderTable(el('readings'), ['Source', 'Quality', 'Value', 'Age'], snapshot.samples.map(sample => [
    sample.name, sample.quality, formatSampleValue(sample), sample.age_ms === null ? '—' : sample.age_ms + ' ms',
  ]));
  el('diagnostics').textContent = JSON.stringify({runtime: snapshot.runtime, queues: snapshot.queues}, null, 2);
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
    } else if (sample.type === 5) {
      pills.push(pill(sample, 'Light', `${(value.millilux / 1000).toFixed(3)} lux`, 'gauge', 'amber'));
    } else if (sample.type === 6) {
      pills.push(pill(sample, 'Raw bytes', formatSampleValue(sample), 'braces', 'violet'));
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

/** Switch views without changing the draft, board state or pending review.
 * @returns {(view: string, focus?: boolean) => void} Unknown views leave selection unchanged.
 */
export function setupWorkspace() {
  const tabs = [...document.querySelectorAll('[data-workspace-tab]')];
  function select(view, focus = false) {
    const selected = tabs.find(tab => tab.dataset.workspaceTab === view);
    if (!selected) return;
    for (const tab of tabs) {
      const active = tab === selected;
      tab.setAttribute('aria-selected', String(active));
      tab.tabIndex = active ? 0 : -1;
      el(tab.getAttribute('aria-controls')).hidden = !active;
    }
    if (focus) selected.focus();
  }
  for (const [index, tab] of tabs.entries()) {
    tab.addEventListener('click', () => select(tab.dataset.workspaceTab));
    tab.addEventListener('keydown', event => {
      const target = { ArrowRight: (index + 1) % tabs.length,
        ArrowLeft: (index + tabs.length - 1) % tabs.length,
        Home: 0, End: tabs.length - 1 }[event.key];
      if (target === undefined) return;
      event.preventDefault();
      select(tabs[target].dataset.workspaceTab, true);
    });
  }
  for (const link of document.querySelectorAll('[data-workspace-link]')) {
    link.addEventListener('click', () => select(link.dataset.workspaceLink, true));
  }
  return select;
}
