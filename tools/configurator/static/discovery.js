/** Present observations separately from configured devices; never invent live state. */
import { tile } from './dashboard.js';
const el = id => document.getElementById(id);
function text(tag, value, className = '') {
  const node = document.createElement(tag);
  node.textContent = value;
  node.className = className;
  return node;
}

export function renderDiscovery(report, add, message = 'Connect, confirm the wiring, then check the connection. Results do not change the running setup.', confirm = null) {
  const container = el('discovery-results');
  container.replaceChildren();
  el('discovery-status').textContent = message;
  if (!report) return;
  const result = report.capture.result;
  const outcomes = {
    complete: 'Check the results below. Adding changes only your draft; validate and review before applying. Results expire after 60 seconds.',
    manual: report.devices.some(device => device.identity.selected) ?
      'User confirmation recorded. Review the settings before adding to your draft. No hardware detection or output test was performed.' :
      'Manual selection: no hardware detection or output test was performed. Confirm the module below, then review its settings.',
    busy: 'The board already has configured hardware or reserved connections. Detection left it running. See “Preparing an idle board” below.',
    fault: 'The connection check failed. Check the wiring with power off, then reconnect and detect again.',
    incomplete: 'The check did not finish. No device can be added from these results.',
    stale: 'The board setup changed during detection. Detect again.',
    invalid: 'The firmware rejected this connection check.',
  };
  el('discovery-status').textContent = outcomes[result] || 'Detection unavailable.';
  for (const [index, device] of report.devices.entries()) {
    const observation = device.observation;
    const selected = device.identity.selected;
    if (observation.address === undefined) {
      renderConfirmed(container, report, device, index, add, confirm);
      continue;
    }
    const address = '0x' + observation.address.toString(16).padStart(2, '0');
    let label = observation.status === 'not_responding' ? 'No response' :
      observation.status !== 'responding' ? 'Check incomplete' :
      device.identity.status === 'ambiguous' ? 'More than one match' : selected ? 'Identified' : 'Unknown device';
    if (selected && device.capabilities.support === 'unavailable') label = 'Identified · driver unavailable';
    const item = tile(`${selected ? selected.id : address} · ${label}`,
      `${address} · ${observation.status.replaceAll('_', ' ')} · not configured by this check`, 'i2c', 'violet');
    const details = text('details', '');
    details.append(text('summary', 'Evidence and recommended settings'));
    const value = observation.value === null ? 'no chip ID' : 'chip ID 0x' + observation.value.toString(16).padStart(2, '0');
    details.append(text('p', `${value} · register 0x${observation.register.toString(16).padStart(2, '0')} · ${device.identity.status} catalogue match`, 'hint'));
    if (selected) details.append(text('p', `Descriptor ${selected.id} v${selected.version} · driver ${device.capabilities.driver} · hardware qualification ${device.capabilities.hardware_qualified ? 'recorded' : 'pending'}`, 'hint'));
    const recommendation = device.recommendation;
    if (recommendation.status === 'awaiting_review' && recommendation.instance) {
      const list = text('dl', '', 'discovery-settings');
      for (const setting of recommendation.settings) {
        list.append(text('dt', setting.label), text('dd', `${setting.value} — ${setting.reason} (${setting.source.replaceAll('_', ' ')})`));
      }
      details.append(list);
      const button = text('button', 'Add to profile', 'btn');
      button.type = 'button';
      button.setAttribute('aria-label', `Add ${selected.id} at ${address} to profile`);
      button.disabled = !report.discovery_id;
      button.onclick = () => add(index);
      item.append(details, button);
    } else {
      details.append(text('p', 'No settings to add. A unique identity and a supported driver are required.', 'hint'));
      item.append(details);
    }
    container.append(item);
  }
  const evidence = text('details', '', 'discovery-evidence');
  evidence.append(text('summary', 'Capture details'), text('p',
    result === 'manual' ? `Preset ${report.preset_id} v${report.preset_version}; board revision ${report.capture.active_revision}; user confirmation only, no measured identity.` :
      `Preset ${report.preset_id} v${report.preset_version}; request ${report.capture.request_id}; board revision ${report.capture.active_revision}; board time ${report.capture.started_ms} ms; elapsed ${report.capture.elapsed_ms} ms; firmware ${report.capture.build_sha256}.`, 'hint'));
  container.append(evidence);
}

function renderConfirmed(container, report, device, index, add, confirm) {
  const manual = device.observation.method === 'manual';
  const selected = device.identity.selected, candidates = device.identity.candidates;
  const name = selected?.id || (candidates.length === 1 ? candidates[0].id : 'UART connection');
  const label = selected ? 'User confirmed' : manual ? 'Manual selection' :
    device.identity.status === 'candidate' ? 'Candidate — confirm module' :
    device.identity.status === 'ambiguous' ? 'More than one match' : 'No reliable match';
  const item = tile(`${name} · ${label}`, 'Not configured by this check', manual ? device.observation.protocol || 'gpio' : 'uart', 'violet');
  const details = text('details', '');
  details.append(text('summary', 'Evidence and recommended settings'));
  details.append(text('p', manual ? 'No readback is available for this selection. User confirmation is not proof that the device operates.' :
    `${device.observation.valid_reports ?? 0} complete reports; ${device.observation.window_ms ?? 0} ms window; ${device.observation.receive_errors ?? 0} receive errors; ${device.observation.discarded_bytes ?? 0} discarded bytes. Silence does not prove disconnection; reports with zero targets still count.`, 'hint'));
  for (const caution of device.cautions || []) details.append(text('p', caution, 'hint'));
  const recommendation = device.recommendation;
  if (selected) {
    details.append(text('p', `Descriptor ${selected.id} v${selected.version} · driver ${device.capabilities.support} · hardware qualification ${device.capabilities.hardware_qualified ? 'recorded' : 'pending'}`, 'hint'));
    if (recommendation.status === 'awaiting_review' && recommendation.instance) {
      const list = text('dl', '', 'discovery-settings');
      for (const setting of recommendation.settings) list.append(text('dt', setting.label), text('dd', `${Array.isArray(setting.value) ? setting.value.join(', ') : setting.value} — ${setting.reason} (${setting.source.replaceAll('_', ' ')})`));
      details.append(list);
      const button = text('button', 'Add to profile', 'btn'); button.type = 'button';
      button.disabled = !report.discovery_id; button.onclick = () => add(index);
      item.append(details, button);
    } else {
      details.append(text('p', 'This firmware has no available driver for the selection.', 'hint')); item.append(details);
    }
  } else if (candidates.length === 1 && (manual || device.identity.status === 'candidate')) {
    item.append(details);
    let sources = null;
    if (device.source_queues) {
      const sourceLabel = text('label', 'Existing readings queue', 'field__label');
      sources = text('select', '', 'input'); sources.setAttribute('aria-label', 'Existing readings queue');
      for (const id of device.source_queues) { const option = text('option', id); option.value = id; sources.append(option); }
      sourceLabel.append(sources); item.append(sourceLabel);
      if (!device.source_queues.length) item.append(text('p', 'Add a sensor and its readings queue to the draft first.', 'hint'));
    }
    const check = text('input', ''); check.type = 'checkbox';
    const labelNode = text('label', '', 'discovery-confirm');
    labelNode.append(check, text('span', device.confirmation_text || `I checked that the connected module is ${name} and matches this wiring.`));
    const button = text('button', 'Confirm module', 'btn'); button.type = 'button'; button.disabled = true;
    button.dataset.ready = 'no';
    check.onchange = () => {
      button.dataset.ready = check.checked && (!sources || sources.value) ? 'yes' : 'no';
      button.disabled = !report.discovery_id || button.dataset.ready !== 'yes';
    };
    button.onclick = () => confirm?.(index, check.checked, sources?.value);
    item.append(labelNode, button);
  } else {
    details.append(text('p', 'No settings to add. Check the confirmed connection and retry; do not infer a device from an incomplete or noisy stream.', 'hint'));
    item.append(details);
  }
  container.append(item);
}
