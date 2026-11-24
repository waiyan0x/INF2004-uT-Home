"use strict";

const el = id => document.getElementById('runtime-' + id);
const statusNames = {ready: 'Ready', not_ready: 'Not ready', update_ok: 'Rule loaded',
  execute_ok: 'Execution complete', result_expired: 'Result expired', request_pending: 'Request pending',
  outcome_unknown: 'Outcome unknown', esp_disconnected: 'Disconnected', status_expired: 'Status unavailable',
  controller_blocked: 'Session blocked'};

export function setupRuntime() {
  const token = document.querySelector('meta[name="session"]').content;
  let descriptions = [], catalogueLoaded = false, cataloguePending = false;
  let active = false, view = null, received = 0;
  let pending = false, sending = false, epoch = 0, nextPoll = 0;
  let message = '', transportUnknown = false, checking = false, snapshotFailed = false;

  async function request(body) {
    const started = performance.now();
    const response = await fetch('/api/runtime', {method: 'POST', signal: AbortSignal.timeout(3000),
      headers: {'Content-Type': 'application/json', 'X-Session': token}, body: JSON.stringify(body)});
    let data;
    try { data = await response.json(); }
    catch (cause) { throw new Error(`Runtime request failed (HTTP ${response.status}): invalid JSON`, {cause}); }
    if (!response.ok) throw new Error(data.error || `Runtime request failed (HTTP ${response.status})`);
    return {data, started};
  }

  function selection() {
    const item = descriptions.find(entry => entry.id === el('description').value);
    const operation = item?.operations.find(entry => entry.name === el('operation').value);
    return {item, operation, rule: operation?.rules[el('address').value]};
  }

  function options(select, entries, placeholder) {
    select.replaceChildren(new Option(placeholder, ''), ...entries.map(([value, label]) => new Option(label, value)));
  }

  function preview() {
    const {operation, rule} = selection();
    let settings = '';
    if (rule?.protocol === 'uart') settings = `${rule.rate} baud`;
    else if (rule) {
      settings = `${rule.rate / 1000} kHz`;
      if (rule.protocol === 'spi') {
        const dc = rule.dc_pin === 255 ? 'unused' : `GPIO${rule.dc_pin}=${rule.dc_level}`;
        settings += ` / Mode ${rule.mode} / CS GPIO${rule.cs_pin} / D/C ${dc}`;
      }
    }
    el('preview').textContent = rule
      ? `${rule.protocol.toUpperCase()} / TX ${rule.write_hex || 'none'} / Read ${rule.read_length} ${rule.read_length === 1 ? 'byte' : 'bytes'} / ${settings} / ${rule.timeout_ms} ms`
      : operation?.reason || 'Select a reviewed description, operation and address.';
    el('notes').textContent = operation?.notes || '';
  }

  function render() {
    const {item, operation, rule} = selection();
    const elapsed = performance.now() - received;
    const current = view && elapsed < 2000;
    const state = current ? view.state : null;
    const display = current ? view.display : null;
    const busy = sending || !!view?.busy;
    const age = state?.display.age_ms == null ? null : state.display.age_ms + elapsed;
    const available = !!display?.data_available && !busy && !transportUnknown && age !== null && age < 5000;
    el('node').textContent = view?.target?.node || 'Runtime session';
    el('connection').textContent = !current ? 'Status unavailable' : !view.configured ? 'Not configured'
      : state?.ready ? 'MQTT ready' : state?.connected ? 'MQTT connected · not ready'
        : view.enabled ? 'MQTT reconnecting' : 'Not connected';
    el('ha').textContent = !view?.home_assistant.enabled ? 'HA publisher disabled'
      : current && view.home_assistant.connected ? 'HA publisher connected' : 'HA publisher disconnected';
    el('connect').textContent = view?.enabled ? 'Disconnect MQTT' : 'Connect MQTT';
    el('connect').disabled = busy || !current || !view.configured;
    el('description').disabled = busy || !catalogueLoaded;
    el('operation').disabled = busy || !item;
    el('address').disabled = busy || !item?.addresses.length || !!operation?.reason;
    el('load').disabled = busy || transportUnknown || !rule || !state?.ready
      || (state.selected !== null && !state.can_load);
    el('execute').disabled = busy || transportUnknown || !state?.can_execute;
    el('active').textContent = display?.active_available
      ? `${display.details.operation || state.active.rule.device} / revision ${state.active.rule.revision}` : 'Unavailable';
    const status = transportUnknown ? 'Outcome unknown' : busy ? 'Request pending'
      : available ? 'Execution complete' : display?.status === 'execute_ok' ? 'Result expired'
        : statusNames[display?.status] || display?.status?.replaceAll('_', ' ') || 'Unavailable';
    el('status').textContent = status;
    const receipt = available && state?.mqtt_receipt;
    el('receipt').hidden = !receipt;
    el('receipt').textContent = receipt
      ? `MQTT/TLS reply #${receipt.request_id} · ${receipt.bytes} bytes · ${receipt.topic}` : '';
    el('raw').textContent = available ? (display.raw_hex || '(empty)') : 'Unavailable';
    el('tx').textContent = available ? String(display.tx_accepted) : 'Unavailable';
    el('rx').textContent = available ? String(display.rx_received) : 'Unavailable';
    el('age').textContent = available ? `${(age / 1000).toFixed(1)} s` : 'Unavailable';
    el('recovery').hidden = !transportUnknown && !state?.pending;
    el('check').disabled = busy || !state?.connected;
    el('recover').hidden = !state?.pending;
    el('recover').disabled = busy || !state?.connected || !state.status_fresh;
    el('notice').textContent = message || view?.error || state?.issue || state?.error
      || (!view?.configured ? 'Start the Desk with MQTT settings to connect. Descriptions can be previewed offline.'
        : 'Load a reviewed rule, then execute it once. An idle peripheral plan is required.');
  }

  function receive({data, started}) {
    if (snapshotFailed) { message = ''; snapshotFailed = false; }
    view = data;
    received = started; // Include HTTP round-trip time when judging freshness.
    if (checking && !view.busy) {
      if (!view.error && view.state?.status_fresh) transportUnknown = false;
      checking = false;
    }
    preview();
    render();
  }

  async function snapshot() {
    if (pending || sending) return;
    const version = epoch;
    pending = true;
    try {
      const data = await request({action: 'snapshot'});
      if (version === epoch) receive(data);
    } catch (error) {
      if (version === epoch) {
        view = null;
        message = `Snapshot unavailable: ${error.message}`;
        snapshotFailed = true;
        render();
      }
    } finally {
      pending = false;
      nextPoll = performance.now() + 500;
    }
  }

  async function act(action) {
    if (sending || view?.busy) return;
    const {item, operation, rule} = selection();
    const body = {action};
    if (action === 'load') {
      if (!rule) return;
      Object.assign(body, {description: item.id, operation: operation.name,
        address: item.addresses.length ? Number(el('address').value) : null, sha256: item.sha256});
    }
    ++epoch;
    sending = true;
    message = '';
    checking = action === 'status' || action === 'recover';
    render();
    try {
      receive(await request(body));
    } catch (error) {
      message = error.message;
      if (['load', 'execute', 'recover'].includes(action)) transportUnknown = true;
      view = null;
      checking = false;
    } finally {
      sending = false;
      nextPoll = 0;
      render();
    }
  }

  async function loadCatalogue() {
    if (catalogueLoaded || cataloguePending) return;
    cataloguePending = true;
    try {
      const response = await fetch('/api/runtime-catalogue');
      if (!response.ok) throw new Error(`Descriptions unavailable (HTTP ${response.status})`);
      descriptions = (await response.json()).descriptions;
      options(el('description'), descriptions.map(item => [item.id, item.device]), 'Choose description');
      catalogueLoaded = true;
    } catch (error) { message = error.message; }
    finally { cataloguePending = false; render(); }
  }

  el('description').onchange = () => {
    const {item} = selection();
    options(el('operation'), item?.operations.map(operation => [operation.name,
      operation.name + (operation.reason ? ' (unsupported)' : '')]) || [], 'Choose operation');
    options(el('address'), item?.addresses.map(address => [String(address), '0x' + address.toString(16)]) || [], item && !item.addresses.length ? 'Not applicable' : 'Choose address');
    if (item?.addresses.length === 1) el('address').value = String(item.addresses[0]);
    preview();
    render();
  };
  for (const name of ['operation', 'address']) el(name).onchange = () => { preview(); render(); };
  for (const action of ['load', 'execute', 'recover']) el(action).onclick = () => act(action);
  el('connect').onclick = () => act(view?.enabled ? 'disconnect' : 'connect');
  el('check').onclick = () => act('status');

  function tick() {
    if (active && !document.hidden) {
      render();
      if (performance.now() >= nextPoll) snapshot();
    }
    setTimeout(tick, 100);
  }
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden && active) { received = -Infinity; nextPoll = 0; render(); }
  });
  tick();
  return {activate(value) {
    active = value;
    if (active) {
      received = -Infinity;
      nextPoll = 0;
      render();
      loadCatalogue();
      snapshot();
    }
  }};
}
