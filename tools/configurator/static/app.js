"use strict";
import { renderProfile, renderTelemetry, renderBoardState, renderReviewPlan, setupWorkspace } from './dashboard.js';
import { renderDiscovery, updateDiscoveryControls } from './discovery.js';
import { parseRecipeFields, describeRecipe } from './recipes.js';
const el = id => document.getElementById(id);
const token = document.querySelector('meta[name="session"]').content;
const showWorkspace = setupWorkspace();
let connected = false, busy = false, pendingTelemetry = null, reviewId = null, examples = {};
let draftEdited = false, draftGeneration = 0;
let discoveryFeatures = 0, recoveryRequired = false, discovered = null;
let discoveryTimer = null, discoveryGeneration = 0, connections = [];
let datasheetSelection = null;
let recipeSupported = false, recipeExamples = [], editingRecipeId = '', recipeGeneration = 0;
const recipeFields = {device_id: 'recipe-device', bus_ref: 'recipe-bus', address: 'recipe-address',
  register: 'recipe-register', read_length: 'recipe-read', read_pause_ms: 'recipe-pause',
  select_offset: 'recipe-offset', select_length: 'recipe-length', interval_ms: 'recipe-interval',
  timeout_ms: 'recipe-timeout'};
const readRecipe = () => parseRecipeFields(Object.fromEntries(Object.entries(recipeFields)
  .map(([key, id]) => [key, el(id).value])));
function updateRecipeControls() {
  let valid = false;
  try {
    el('recipe-preview').textContent = describeRecipe(readRecipe());
    valid = true;
  } catch (error) {
    el('recipe-preview').textContent = error.message;
  }
  el('recipe-add').disabled = !connected || !recipeSupported || busy || !valid || !el('recipe-confirm').checked;
  el('recipe-add').textContent = editingRecipeId ? 'Update recipe in draft' : 'Add recipe to draft';
  el('recipe-device').readOnly = !!editingRecipeId;
  el('recipe-support').textContent = !connected ? 'Connect to check recipe support. Preparing a draft does not apply it.'
    : recipeSupported ? 'Connected firmware supports I²C register-read recipes.'
      : 'This firmware cannot run recipes. Install a recipe-enabled build before applying one.';
}
function fillRecipe(recipe) {
  for (const [key, id] of Object.entries(recipeFields)) {
    if (recipe[key] !== undefined) el(id).value = String(recipe[key]);
  }
  recipeGeneration++;
  el('recipe-confirm').checked = false;
  updateRecipeControls();
}
function refreshRecipeProfile() {
  let profile;
  try { profile = JSON.parse(el('profile').value); } catch { profile = {}; }
  const buses = Array.isArray(profile?.interfaces) ? profile.interfaces.filter(bus => bus?.protocol === 'i2c') : [];
  const previousBus = el('recipe-bus').value;
  el('recipe-bus').replaceChildren();
  for (const bus of buses) {
    const option = document.createElement('option');
    option.value = bus.id;
    option.textContent = `${bus.id} · SDA ${bus.sda_gpio} / SCL ${bus.scl_gpio}`;
    el('recipe-bus').append(option);
  }
  if (buses.some(bus => bus.id === previousBus)) el('recipe-bus').value = previousBus;
  const devices = Array.isArray(profile?.devices) ? profile.devices.filter(device => device?.driver === 'i2c_recipe') : [];
  el('recipe-existing').replaceChildren();
  for (const device of [{device_id: '', label: 'New recipe'}, ...devices]) {
    const option = document.createElement('option');
    option.value = device.device_id;
    option.textContent = device.label || device.device_id;
    el('recipe-existing').append(option);
  }
  editingRecipeId = devices.find(device => device.device_id === editingRecipeId)?.device_id || devices[0]?.device_id || '';
  el('recipe-existing').value = editingRecipeId;
  const selected = devices.find(device => device.device_id === editingRecipeId);
  if (selected) fillRecipe({...selected.behaviour, device_id: selected.device_id,
    bus_ref: selected.interface?.ref, address: selected.interface?.address});
  else {
    recipeGeneration++;
    el('recipe-confirm').checked = false;
    updateRecipeControls();
  }
}
function clearDiscovery(message) {
  discovered = null;
  discoveryGeneration++;
  clearTimeout(discoveryTimer);
  renderDiscovery(null, null, message);
}
function updateDiscoveryPreset() {
  const preset = connections.find(p => p.preset_id === el('discovery-preset').value);
  if (!preset) return;
  el('discovery-wiring').textContent = preset.wiring;
  el('detect').textContent = preset.method === 'manual' ? 'Prepare manual selection' : 'Detect';
  el('wiring-confirmed').checked = false;
  clearDiscovery();
  updateControls();
}
function notice(text, error = false) {
  el('notice').textContent = text;
  el('notice').classList.toggle('error', error);
}
function updateControls() {
  el('connection').textContent = connected ? 'Connected · USB' : 'Offline review';
  for (const id of ['disconnect', 'query', 'save']) {
    el(id).disabled = !connected || busy;
  }
  el('recover').disabled = !connected || busy || !recoveryRequired;
  const preset = connections.find(p => p.preset_id === el('discovery-preset').value);
  const supported = preset?.method === 'manual' || !!(discoveryFeatures & (1 << preset?.wire_code));
  el('detect').disabled = !connected || busy || !supported || !el('wiring-confirmed').checked || !el('discovery-preset').value;
  for (const id of ['discovery-preset', 'wiring-confirmed']) {
    el(id).disabled = busy;
  }
  updateDiscoveryControls(connected && !busy && !!discovered?.discovery_id);
  el('connect').disabled = connected || busy;
  el('review').disabled = busy;
  el('file').disabled = busy;
  el('apply').disabled = !connected || !reviewId || busy;
  el('datasheet-generate').disabled = busy || !connected || !datasheetSelection || !el('datasheet-confirm').checked;
  updateRecipeControls();
}
function invalidateReviewAndDiscovery() {
  clearDiscovery();
  reviewId = null;
  el('review-summary').textContent = 'Profile changed. Validate and review again.';
  el('plan').replaceChildren();
  el('rule-plan').replaceChildren();
  el('datasheet-generate-status').textContent = 'A draft cannot install a driver or change the board. Review and apply separately.';
  renderProfile(el('profile').value);
  refreshRecipeProfile();
  updateControls();
}
function markDraftChanged() {
  draftEdited = true;
  draftGeneration++;
  invalidateReviewAndDiscovery();
}
for (const id of Object.values(recipeFields)) {
  el(id).addEventListener('input', () => {
    recipeGeneration++;
    el('recipe-confirm').checked = false;
    el('recipe-example').value = '';
    el('recipe-example-note').textContent = 'Custom instructions. Check the exact device datasheet.';
    updateRecipeControls();
  });
}
el('recipe-confirm').onchange = updateRecipeControls;
el('recipe-existing').onchange = () => {
  editingRecipeId = el('recipe-existing').value;
  if (editingRecipeId) refreshRecipeProfile();
  else fillRecipe({device_id: 'raw_sensor'});
};
el('recipe-example').onchange = () => {
  const example = recipeExamples.find(item => item.id === el('recipe-example').value);
  if (!example) return;
  const instructions = Object.fromEntries(Object.entries(example.recipe)
    .filter(([key]) => !['device_id', 'bus_ref', 'address'].includes(key)));
  fillRecipe(instructions);
  el('recipe-example-note').textContent = `${example.description}${example.expected ? ` Expected byte: ${example.expected}.` : ''}`;
};
el('recipe-add').onclick = () => {
  let recipe;
  try { recipe = readRecipe(); } catch (error) { notice(error.message, true); return; }
  if (!el('recipe-confirm').checked) { notice('Check the read instructions and wiring first.', true); return; }
  const draft = draftGeneration, form = recipeGeneration;
  const request = {profile: el('profile').value, recipe, wiring_confirmed: true,
    ...(editingRecipeId ? {replace_device_id: editingRecipeId} : {})};
  return runBoardAction(async () => {
    reviewId = null;
    clearDiscovery();
    const result = await rpc('recipe-prepare', request);
    if (draft !== draftGeneration || form !== recipeGeneration) {
      throw new Error('The draft or recipe changed. Prepare the recipe again.');
    }
    el('profile').value = JSON.stringify(result.profile, null, 2);
    markDraftChanged();
    el('recipe-status').textContent = result.summary + ' Draft only. Validate and review before applying.';
    notice('Recipe prepared in your draft. Validate and review the complete profile next.');
  }, error => { el('recipe-status').textContent = error.message; });
};
async function decodeResponse(response, action) {
  let data;
  try {
    data = await response.json();
  } catch (cause) {
    throw new Error(`${action} failed (HTTP ${response.status}): response was not valid JSON`, {cause});
  }
  if (!response.ok) {
    if (data?.connected === false) connected = false;
    throw new Error(data?.error || `${action} failed (HTTP ${response.status})`);
  }
  return data;
}
async function fetchResponse(url, action, options) {
  try {
    return await fetch(url, options);
  } catch (cause) {
    throw new Error(`${action} failed: ${cause.message}`, {cause});
  }
}
async function rpc(action, body = {}) {
  const response = await fetchResponse('/api/' + action, action, {
    method: 'POST',
    headers: {'Content-Type': 'application/json', 'X-Session': token},
    body: JSON.stringify(body),
  });
  return decodeResponse(response, action);
}
async function runExclusiveAction(action, onError) {
  if (busy) return;
  busy = true;
  updateControls();
  try {
    if (pendingTelemetry) await pendingTelemetry;
    await action();
  } catch (error) {
    notice(error.message, true);
    if (onError) onError(error);
  } finally {
    busy = false;
    updateControls();
  }
}
function boardRequestFailed() {
  reviewId = null;
  recoveryRequired = false;
  clearDiscovery('The request failed. Check the message above, then detect again.');
}
function runBoardAction(action, onError) {
  return runExclusiveAction(action, error => {
    boardRequestFailed();
    if (onError) onError(error);
  });
}
function updateBoardState(data) {
  recoveryRequired = !!(data.flags & 32);
  renderBoardState(data);
}
el('profile').addEventListener('input', markDraftChanged);
el('discovery-preset').onchange = updateDiscoveryPreset;
el('wiring-confirmed').onchange = () => {
  clearDiscovery();
  updateControls();
};
el('datasheet-confirm').onchange = updateControls;
function resetDatasheetMatch() {
  datasheetSelection = null;
  el('datasheet-confirm').checked = false;
  el('datasheet-enable-wrap').hidden = true;
  el('datasheet-enable').value = '';
  el('datasheet-address').replaceChildren();
  updateControls();
}
el('datasheet-name').oninput = resetDatasheetMatch;
for (const id of ['datasheet-device-id', 'datasheet-address', 'datasheet-sda', 'datasheet-scl', 'datasheet-enable']) {
  el(id).addEventListener('change', () => {
    el('datasheet-confirm').checked = false;
    updateControls();
  });
}
el('datasheet-match').onclick = () => runBoardAction(async () => {
  resetDatasheetMatch();
  const name = el('datasheet-name').value.trim();
  const result = await rpc('datasheet_match', {device_name: name});
  if (el('datasheet-name').value.trim() !== name) return;
  const available = result.matches.filter(match => match.driver_available === true);
  if (available.length !== 1) {
    el('datasheet-driver').textContent = result.matches.length ?
      (result.connected ? 'The connected firmware has no unique available driver for this selection.' : 'Connect the board to check driver availability.') :
      'No exact reviewed catalogue entry found. A maintainer must review this device before use.';
    updateControls();
    return;
  }
  const match = available[0];
  const builder = match.draft_builder;
  if (!builder?.eligible) {
    el('datasheet-driver').textContent = 'This reviewed device is not supported by the periodic I2C draft builder. Use the existing device flow.';
    updateControls();
    return;
  }
  for (const address of builder.addresses) {
    const option = document.createElement('option');
    option.value = address;
    option.textContent = address;
    el('datasheet-address').append(option);
  }
  datasheetSelection = {...match, needsEnable: builder.needs_enable};
  el('datasheet-enable-wrap').hidden = !builder.needs_enable;
  el('datasheet-confirm').checked = false;
  el('datasheet-driver').textContent = `Candidate ${match.descriptor_id} · ${match.driver_id} is installed. Confirm the physical module and connections.`;
  updateControls();
});
el('datasheet-generate').onclick = () => {
  let validationError;
  const sda = el('datasheet-sda').value, scl = el('datasheet-scl').value;
  const enable = el('datasheet-enable').value;
  if (!datasheetSelection || !el('datasheet-confirm').checked) {
    validationError = 'Check the device and wiring first.';
  } else if (sda === '' || scl === '') {
    validationError = 'Select SDA and SCL GPIO pins.';
  } else if (datasheetSelection.needsEnable && enable === '') {
    validationError = 'Select the enable GPIO pin.';
  }
  if (validationError) {
    notice(validationError, true);
    el('datasheet-generate-status').textContent = validationError;
    return;
  }
  const generation = draftGeneration;
  const request = {profile: el('profile').value, descriptor_id: datasheetSelection.descriptor_id,
    descriptor_version: datasheetSelection.descriptor_version, device_id: el('datasheet-device-id').value,
    address: el('datasheet-address').value, sda_gpio: Number(sda), scl_gpio: Number(scl),
    ...(datasheetSelection.needsEnable ? {enable_gpio: Number(enable)} : {}),
    wiring_confirmed: true, identity_confirmed: true};
  return runBoardAction(async () => {
    if (generation !== draftGeneration) throw new Error('The profile changed; prepare the draft again.');
    const result = await rpc('datasheet_prepare_i2c', request);
    if (generation !== draftGeneration) throw new Error('The profile changed; prepare the draft again.');
    el('profile').value = JSON.stringify(result.profile, null, 2);
    markDraftChanged();
    const message = 'Device added to the draft. Validate and review the whole profile before applying.';
    el('datasheet-generate-status').textContent = message;
    notice(message);
  }, error => { el('datasheet-generate-status').textContent = error.message; });
};
el('detect').onclick = () => runBoardAction(async () => {
  reviewId = null;
  clearDiscovery('Checking the reviewed connection…');
  const generation = discoveryGeneration, profile = el('profile').value;
  const data = await rpc('discover', { profile, preset_id: el('discovery-preset').value,
    wiring_confirmed: el('wiring-confirmed').checked });
  if (generation !== discoveryGeneration || profile !== el('profile').value) return;
  showDiscovery(data);
  updateBoardState(data.state);
});
function showDiscovery(data) {
  clearTimeout(discoveryTimer);
  discovered = data;
  renderDiscovery(data, addDiscovered, undefined, confirmDiscovered, updateControls);
  if (data.discovery_id) {
    discoveryTimer = setTimeout(() => {
      clearDiscovery('These results have expired. Detect again before adding a device.');
      updateControls();
    }, data.expires_in_ms);
  }
}
function confirmDiscovered(index, identity_confirmed, source_queue) {
  return runBoardAction(async () => {
    const profile = el('profile').value, generation = discoveryGeneration;
    const data = await rpc('confirm_discovered', { profile, index, identity_confirmed, source_queue,
      discovery_id: discovered?.discovery_id, preset_id: el('discovery-preset').value,
      wiring_confirmed: el('wiring-confirmed').checked });
    if (generation !== discoveryGeneration || profile !== el('profile').value) return;
    showDiscovery(data);
  });
}
function addDiscovered(index) {
  return runBoardAction(async () => {
    const profile = el('profile').value, generation = discoveryGeneration;
    const data = await rpc('add_discovered', { profile, index, discovery_id: discovered?.discovery_id,
      preset_id: el('discovery-preset').value, wiring_confirmed: el('wiring-confirmed').checked });
    if (generation !== discoveryGeneration || profile !== el('profile').value) {
      clearDiscovery('The draft changed while adding. Detect again.');
      return;
    }
    el('profile').value = JSON.stringify(data.profile, null, 2);
    markDraftChanged();
    clearDiscovery('Device added to your draft. Validate and review before applying. Detect again to add another device.');
    notice('Device added to the draft. The running setup is unchanged. Validate and review the whole profile next.');
  });
}
el('examples').onchange = () => {
  const example = examples[el('examples').value];
  if (example) {
    el('profile').value = JSON.stringify(example, null, 2);
    markDraftChanged();
  }
};
el('file').onchange = () => runExclusiveAction(async () => {
  const file = el('file').files[0];
  if (!file) return;
  draftEdited = true;
  if (file.size > 32768) throw new Error('Profile file exceeds 32 KiB');
  const generation = draftGeneration;
  const content = await file.text();
  if (generation !== draftGeneration || el('file').files[0] !== file) {
    notice('The draft changed while reading the file. Import again to replace it.');
    return;
  }
  el('profile').value = content;
  markDraftChanged();
});
el('format').onclick = () => runExclusiveAction(() => {
  el('profile').value = JSON.stringify(JSON.parse(el('profile').value), null, 2);
  markDraftChanged();
});
el('download').onclick = () => runExclusiveAction(() => {
  const data = JSON.stringify(JSON.parse(el('profile').value), null, 2);
  const url = URL.createObjectURL(new Blob([data + '\n'], {type: 'application/json'}));
  const link = document.createElement('a');
  link.href = url;
  link.download = 'node-profile.json';
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});
el('connect').onclick = () => runBoardAction(async () => {
  const data = await rpc('connect', {port: el('port').value.trim()});
  connected = true;
  resetDatasheetMatch();
  discoveryFeatures = data.capabilities.features;
  recipeSupported = data.capabilities.drivers.includes('i2c_recipe');
  el('wiring-confirmed').checked = false;
  invalidateReviewAndDiscovery();
  updateBoardState(data.state);
  if (!(discoveryFeatures & 6)) {
    clearDiscovery('This firmware cannot detect devices. Manual selections are available; update firmware for detection.');
  }
  const caps = data.capabilities;
  el('capabilities').textContent = `${caps.board} · schema ${caps.node_schema} · compiled: ${caps.drivers.join(', ')} · ${caps.hardware_qualified ? 'hardware qualified' : 'hardware qualification pending'}`;
  notice('Connected. Review your profile against this firmware before deployment.');
});
el('disconnect').onclick = () => runBoardAction(async () => {
  await rpc('disconnect');
  connected = false;
  recipeSupported = false;
  resetDatasheetMatch();
  invalidateReviewAndDiscovery();
  renderTelemetry(null);
  el('capabilities').textContent = '';
  notice('Disconnected. The board keeps its active configuration.');
});
el('review').onclick = () => runBoardAction(async () => {
  clearDiscovery();
  const profile = el('profile').value;
  const data = await rpc('review', {profile});
  if (el('profile').value !== profile) {
    invalidateReviewAndDiscovery();
    return;
  }
  reviewId = data.review_id;
  renderReviewPlan(data.plan, data.connected);
  if (data.state) updateBoardState(data.state);
  notice('Profile validated. Check the wiring, device list and rules before applying.');
  showWorkspace('review', true);
});
el('apply').onclick = () => runBoardAction(async () => {
  clearDiscovery();
  const id = reviewId;
  reviewId = null;
  const data = await rpc('apply', {review_id: id});
  updateBoardState(data);
  notice(data.message, data.status !== 1);
});
for (const action of ['query', 'save', 'recover']) {
  el(action).onclick = () => runBoardAction(async () => {
    clearDiscovery();
    reviewId = null;
    const data = await rpc(action);
    updateBoardState(data);
    notice(data.message, ![0, 1, 8].includes(data.status));
  });
}
function receiveObservation(data) {
  if (discovered?.discovery_id && data.discovery_id !== discovered.discovery_id) {
    clearDiscovery('Board state or detection freshness changed. Detect again.');
  }
  recoveryRequired = !data.result && !!(data.configuration.flags & 32);
  renderTelemetry(data);
}

async function poll() {
  if (connected && !busy && !document.hidden) {
    pendingTelemetry = rpc('telemetry').then(receiveObservation);
    try {
      await pendingTelemetry;
    } catch (error) {
      boardRequestFailed();
      renderTelemetry({result: 1});
      notice(error.message, true);
    } finally {
      pendingTelemetry = null;
      updateControls();
    }
  }
  if (!connected) renderTelemetry(null);
  setTimeout(poll, 1000);
}

async function loadCatalogue() {
  try {
    const response = await fetchResponse('/api/catalogue', 'Catalogue');
    const data = await decodeResponse(response, 'Catalogue');
    connections = data.connections;
    for (const preset of connections) {
      const option = document.createElement('option');
      option.value = preset.preset_id;
      option.textContent = preset.label;
      el('discovery-preset').append(option);
    }
    updateDiscoveryPreset();
    examples = data.examples;
    recipeExamples = data.recipe_examples || [];
    for (const example of recipeExamples) {
      const option = document.createElement('option');
      option.value = example.id;
      option.textContent = example.label;
      el('recipe-example').append(option);
    }
    for (const name of Object.keys(examples)) {
      const option = document.createElement('option');
      option.value = name;
      option.textContent = name;
      el('examples').append(option);
    }
    const initialExample = ['i2c-recipe-demo', 'c3-protocol-demo', 'distance-only'].find(name => examples[name]);
    if (!draftEdited && !el('profile').value.trim() && initialExample) {
      el('examples').value = initialExample;
      el('examples').onchange();
    }
  } catch (error) {
    notice(error.message, true);
  }
}
loadCatalogue();
updateControls();
poll();
