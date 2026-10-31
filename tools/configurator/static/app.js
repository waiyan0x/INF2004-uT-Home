"use strict";
import { renderProfile, renderTelemetry, setupWorkspace } from './dashboard.js';
import { renderDiscovery } from './discovery.js';
import { renderDatasheet } from './datasheet.js';
const el = id => document.getElementById(id);
const token = document.querySelector('meta[name="session"]').content;
const showWorkspace = setupWorkspace();
let connected = false, busy = false, observation = null, reviewed = null, examples = {};
let discoveryFeatures = 0, recoveryRequired = false, discovered = null;
let discoveryTimer = null, discoveryGeneration = 0, connections = [];
let catalogueDescriptors = [], datasheetSelection = null, datasheetBusy = false, datasheetResult = null;
function clearDiscovery(message) {
  discovered = null; discoveryGeneration++; clearTimeout(discoveryTimer);
  renderDiscovery(null, null, message);
}
function connectionDetails() {
  const preset = connections.find(p => p.preset_id === el('discovery-preset').value);
  if (!preset) return;
  el('discovery-wiring').textContent = preset.wiring;
  el('detect').textContent = preset.method === 'manual' ? 'Prepare manual selection' : 'Detect';
  el('wiring-confirmed').checked = false;
  clearDiscovery(); controls();
}
function notice(text, error=false) { el("notice").textContent=text; el("notice").classList.toggle("error",error); }
function controls() {
  el("connection").textContent=connected?"Connected · USB":"Offline review";
  for (const id of ["disconnect","query","save"]) el(id).disabled=!connected||busy;
  el('recover').disabled = !connected || busy || !recoveryRequired;
  const preset = connections.find(p => p.preset_id === el('discovery-preset').value);
  const supported = preset?.method === 'manual' || !!(discoveryFeatures & (1 << preset?.wire_code));
  el('detect').disabled = !connected || busy || !supported || !el('wiring-confirmed').checked || !el('discovery-preset').value;
  for (const id of ['discovery-preset', 'wiring-confirmed']) el(id).disabled = busy;
  for (const button of el('discovery-results').querySelectorAll('button')) button.disabled = !connected || busy || !discovered?.discovery_id || button.dataset.ready === 'no';
  el("connect").disabled=connected||busy; el("review").disabled=busy;
  el("apply").disabled=!connected||!reviewed||busy;
  el('datasheet-analyse').disabled = datasheetBusy || !el('datasheet-file').files.length;
  el('datasheet-generate').disabled = datasheetBusy || busy || !connected || !datasheetSelection || !el('datasheet-confirm').checked;
}
function invalidate() { clearDiscovery(); reviewed=null; el("review-summary").textContent="Profile changed. Validate and review again."; el("plan").replaceChildren(); el("rule-plan").replaceChildren(); el('datasheet-generate-status').textContent='A draft cannot install a driver or change the board. Review and apply separately.'; renderProfile(el("profile").value); controls(); }
async function rpc(action, body={}) {
  const response=await fetch("/api/"+action,{method:"POST",headers:{"Content-Type":"application/json","X-Session":token},body:JSON.stringify(body)});
  const data=await response.json();
  if (!response.ok) { if(data.connected===false) connected=false; throw new Error(data.error||"Request failed"); }
  return data;
}
async function run(action, onError) {
  if(busy) return; busy=true; controls();
  try { if (observation) await observation; await action(); } catch(error) { reviewed=null; recoveryRequired=false; clearDiscovery('The request failed. Check the message above, then detect again.'); notice(error.message,true); if (onError) onError(error); }
  finally { busy=false; controls(); }
}
function state(data) {
  recoveryRequired = !!(data.flags & 32);
  el("state").textContent=`${data.message||"Observed state"} · active revision ${data.active_revision} · saved revision ${data.saved_revision} · ${data.flags&16?"saved state known":"saved state unknown"}${data.flags&32?" · recovery required":""}`;
}
function table(container, headings, rows) {
  const table=document.createElement("table"), head=table.createTHead().insertRow();
  for(const label of headings) { const th=document.createElement("th"); th.textContent=label; head.append(th); }
  const body=table.createTBody();
  for(const row of rows) { const tr=body.insertRow(); for(const value of row) tr.insertCell().textContent=String(value); }
  container.replaceChildren(table);
}
el("profile").addEventListener("input",invalidate);
el('discovery-preset').onchange = connectionDetails;
el('wiring-confirmed').onchange = () => { clearDiscovery(); controls(); };
el('datasheet-confirm').onchange = controls;
function resetDatasheetMatch() {
  datasheetSelection = null; el('datasheet-confirm').checked = false;
  el('datasheet-enable-wrap').hidden = true; el('datasheet-enable').value = '';
  el('datasheet-address').replaceChildren(); controls();
}
el('datasheet-name').oninput = resetDatasheetMatch;
for (const id of ['datasheet-device-id', 'datasheet-address', 'datasheet-sda', 'datasheet-scl', 'datasheet-enable']) {
  el(id).addEventListener('change', () => { el('datasheet-confirm').checked = false; controls(); });
}
el('datasheet-file').onchange = () => {
  datasheetSelection = null; datasheetResult = null; el('datasheet-download').disabled = true; el('datasheet-confirm').checked = false;
  el('datasheet-enable-wrap').hidden = true; el('datasheet-enable').value = '';
  el('datasheet-facts').replaceChildren(); el('datasheet-uncertainties').replaceChildren();
  el('datasheet-driver').textContent = 'A name match is only a candidate; confirm the actual module.';
  el('datasheet-status').textContent = 'Choose Analyse to send this PDF to Google Gemini.'; controls();
};
el('datasheet-analyse').onclick = async () => {
  const file = el('datasheet-file').files[0];
  if (!file || datasheetBusy) return;
  datasheetResult = null; el('datasheet-download').disabled = true; resetDatasheetMatch();
  el('datasheet-facts').replaceChildren(); el('datasheet-uncertainties').replaceChildren();
  datasheetBusy = true; controls();
  el('datasheet-status').textContent = 'Analysing the PDF. Board monitoring can continue…';
  try {
    if (file.size > 2 * 1024 * 1024) throw new Error('Choose a PDF no larger than 2 MiB.');
    const dataUrl = await new Promise((resolve, reject) => {
      const reader = new FileReader(); reader.onload = () => resolve(reader.result);
      reader.onerror = () => reject(new Error('Could not read the PDF.')); reader.readAsDataURL(file);
    });
    const result = await rpc('datasheet', {pdf_base64: dataUrl.split(',', 2)[1]});
    if (el('datasheet-file').files[0] !== file) return;
    renderDatasheet(result, el('datasheet-facts'), el('datasheet-uncertainties'));
    el('datasheet-name').value = result.draft.device_name || '';
    datasheetResult = result; el('datasheet-download').disabled = false;
    datasheetSelection = null; el('datasheet-confirm').checked = false; el('datasheet-address').replaceChildren();
    el('datasheet-status').textContent = `Unreviewed AI draft · ${result.pages} pages · source ${result.source_sha256.slice(0, 12)}…`;
    el('datasheet-driver').textContent = 'Check the reviewed catalogue and connected firmware before generating a profile.';
  } catch (error) {
    el('datasheet-status').textContent = error.message;
  } finally { datasheetBusy = false; controls(); }
};
el('datasheet-download').onclick = () => {
  if (!datasheetResult) return;
  const url = URL.createObjectURL(new Blob([JSON.stringify(datasheetResult, null, 2) + '\n'], {type: 'application/json'}));
  const link = document.createElement('a'); link.href = url; link.download = 'device-facts-draft.json'; link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
};
el('datasheet-match').onclick = () => run(async () => {
  datasheetSelection = null; el('datasheet-enable-wrap').hidden = true; el('datasheet-enable').value = ''; el('datasheet-address').replaceChildren();
  const name = el('datasheet-name').value.trim();
  const result = await rpc('datasheet_match', {device_name: name});
  if (el('datasheet-name').value.trim() !== name) return;
  const available = result.matches.filter(match => match.driver_available === true);
  if (available.length !== 1) {
    el('datasheet-driver').textContent = result.matches.length ?
      (result.connected ? 'The connected firmware has no unique available driver for this selection.' : 'Connect the board to check driver availability.') :
      'No exact reviewed catalogue entry found. A maintainer must review this device before use.';
    controls(); return;
  }
  const match = available[0];
  const descriptor = catalogueDescriptors.find(row => row.descriptor_id === match.descriptor_id && row.descriptor_version === match.descriptor_version);
  const addresses = descriptor?.instance_schema?.properties?.interface?.properties?.address?.enum;
  if (descriptor?.protocol !== 'i2c' || descriptor?.execution?.kind !== 'periodic' || !Array.isArray(addresses)) {
    el('datasheet-driver').textContent = 'This reviewed device is not supported by the periodic I2C draft builder. Use the existing device flow.';
    controls(); return;
  }
  for (const address of addresses) { const option = document.createElement('option'); option.value = address; option.textContent = address; el('datasheet-address').append(option); }
  const needsEnable = descriptor.instance_schema.properties.interface.required?.includes('enable_gpio') === true;
  datasheetSelection = {...match, needsEnable}; el('datasheet-enable-wrap').hidden = !needsEnable; el('datasheet-confirm').checked = false;
  el('datasheet-driver').textContent = `Candidate ${match.descriptor_id} · ${match.driver_id} is installed. Confirm the physical module and connections.`;
  controls();
});
el('datasheet-generate').onclick = () => run(async () => {
  if (!datasheetSelection || !el('datasheet-confirm').checked) throw new Error('Check the device and wiring first.');
  const sda = el('datasheet-sda').value, scl = el('datasheet-scl').value;
  if (sda === '' || scl === '') throw new Error('Select SDA and SCL GPIO pins.');
  const enable = el('datasheet-enable').value;
  if (datasheetSelection.needsEnable && enable === '') throw new Error('Select the enable GPIO pin.');
  const profile = el('profile').value;
  const result = await rpc('datasheet_prepare_i2c', {profile, descriptor_id: datasheetSelection.descriptor_id,
    descriptor_version: datasheetSelection.descriptor_version, device_id: el('datasheet-device-id').value,
    address: el('datasheet-address').value, sda_gpio: Number(sda), scl_gpio: Number(scl),
    ...(datasheetSelection.needsEnable ? {enable_gpio: Number(enable)} : {}),
    wiring_confirmed: true, identity_confirmed: true});
  if (profile !== el('profile').value) throw new Error('The profile changed; prepare the draft again.');
  el('profile').value = JSON.stringify(result.profile, null, 2); invalidate();
  const message = 'Device added to the draft. Validate and review the whole profile before applying.';
  el('datasheet-generate-status').textContent = message; notice(message);
}, error => { el('datasheet-generate-status').textContent = error.message; });
el('detect').onclick = () => run(async () => {
  reviewed = null; clearDiscovery('Checking the reviewed connection…');
  const generation = discoveryGeneration, profile = el('profile').value;
  const data = await rpc('discover', { profile, preset_id: el('discovery-preset').value,
    wiring_confirmed: el('wiring-confirmed').checked });
  if (generation !== discoveryGeneration || profile !== el('profile').value) return;
  showDiscovery(data); state(data.state);
});
function showDiscovery(data) {
  clearTimeout(discoveryTimer); discovered = data;
  renderDiscovery(data, addDiscovered, undefined, confirmDiscovered);
  if (data.discovery_id) discoveryTimer = setTimeout(() => {
    clearDiscovery('These results have expired. Detect again before adding a device.'); controls();
  }, data.expires_in_ms);
}
function confirmDiscovered(index, identity_confirmed, source_queue) {
  return run(async () => {
    const profile = el('profile').value, generation = discoveryGeneration;
    const data = await rpc('confirm_discovered', { profile, index, identity_confirmed, source_queue,
      discovery_id: discovered?.discovery_id, preset_id: el('discovery-preset').value,
      wiring_confirmed: el('wiring-confirmed').checked });
    if (generation !== discoveryGeneration || profile !== el('profile').value) return;
    showDiscovery(data);
  });
}
function addDiscovered(index) {
  return run(async () => {
    const profile = el('profile').value, generation = discoveryGeneration;
    const data = await rpc('add_discovered', { profile, index, discovery_id: discovered?.discovery_id,
      preset_id: el('discovery-preset').value, wiring_confirmed: el('wiring-confirmed').checked });
    if (generation !== discoveryGeneration || profile !== el('profile').value) {
      clearDiscovery('The draft changed while adding. Detect again.'); return;
    }
    el('profile').value = JSON.stringify(data.profile, null, 2); invalidate();
    clearDiscovery('Device added to your draft. Validate and review before applying. Detect again to add another device.');
    notice('Device added to the draft. The running setup is unchanged. Validate and review the whole profile next.');
  });
}
el("examples").onchange=()=>{ if(examples[el("examples").value]) {el("profile").value=JSON.stringify(examples[el("examples").value],null,2);invalidate();} };
el("file").onchange=()=>run(async()=>{const file=el("file").files[0];if(!file)return;if(file.size>32768)throw new Error("Profile file exceeds 32 KiB");el("profile").value=await file.text();invalidate();});
el("format").onclick=()=>run(async()=>{el("profile").value=JSON.stringify(JSON.parse(el("profile").value),null,2);invalidate();});
el("download").onclick=()=>run(async()=>{const data=JSON.stringify(JSON.parse(el("profile").value),null,2);const url=URL.createObjectURL(new Blob([data+"\n"],{type:"application/json"}));const a=document.createElement("a");a.href=url;a.download="node-profile.json";a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
el("connect").onclick=()=>run(async()=>{const data=await rpc("connect",{port:el("port").value.trim()});connected=true;resetDatasheetMatch();discoveryFeatures=data.capabilities.features;el('wiring-confirmed').checked=false;invalidate();state(data.state);if(!(discoveryFeatures&6))clearDiscovery('This firmware cannot detect devices. Manual selections are available; update firmware for detection.');const c=data.capabilities;el("capabilities").textContent=`${c.board} · schema ${c.node_schema} · compiled: ${c.drivers.join(", ")} · ${c.hardware_qualified?"hardware qualified":"hardware qualification pending"}`;notice("Connected. Review your profile against this firmware before deployment.");});
el("disconnect").onclick=()=>run(async()=>{await rpc("disconnect");connected=false;resetDatasheetMatch();invalidate();renderTelemetry(null);el("readings").replaceChildren();el("timestamp").textContent="Disconnected";el("health").textContent="No live observations";el("state").textContent="Disconnected; last board state is no longer current.";el("capabilities").textContent="Offline review uses the host reference catalogue.";notice("Disconnected. The board keeps its active configuration.");});
el("review").onclick=()=>run(async()=>{clearDiscovery();const text=el("profile").value;const data=await rpc("review",{profile:text});if(el("profile").value!==text){invalidate();return;}reviewed=data.review_id;const r=data.plan.resources;el("review-summary").textContent=`${data.bytes} bytes · ${r.active_tasks} active workers · ${r.device_stack_bytes} bytes peripheral stacks · ${data.connected?"reviewed against connected firmware":"offline review; connect and review again to deploy"}`;table(el("plan"),["Instance","Driver","Enabled","Period"],data.plan.devices.map(d=>[d.device_id,d.driver_id,d.enabled?"Yes":"No",d.period_ms?d.period_ms+" ms":"stream"]));if(data.plan.rules.length)table(el("rule-plan"),["Source / condition","Output","Policy"],data.plan.rules.map(r=>[`${r.source}: ${r.metric} ${r.comparison} ${r.threshold}`,`${r.target}: ${r.duty_permille/10}%`,`hysteresis ${r.hysteresis}; cooldown ${r.cooldown_ms} ms; max age ${r.max_age_ms} ms`]));else el("rule-plan").replaceChildren();if(data.state)state(data.state);notice("Profile validated. Check the wiring, device list and rules before applying.");showWorkspace("review",true);});
el("apply").onclick=()=>run(async()=>{clearDiscovery();const id=reviewed;reviewed=null;const data=await rpc("apply",{review_id:id});state(data);notice(data.message,data.status!==1);});
for(const action of ["query","save","recover"]) el(action).onclick=()=>run(async()=>{clearDiscovery();reviewed=null;const data=await rpc(action);state(data);notice(data.message,![0,1,8].includes(data.status));});
function value(sample) {
  const v=sample.value;if(!v)return "Unavailable";
  if(sample.type===1)return `${v.temperature_c.toFixed(2)} °C · ${v.pressure_pa.toFixed(0)} Pa`;
  if(sample.type===3)return `${v.distance_mm} mm · echo ${v.echo_us} µs`;
  if(sample.type===2)return v.targets.map((t,i)=>t.present?`${i+1}: (${t.x_mm}, ${t.y_mm}) mm, ${t.speed_cm_s} cm/s`:`${i+1}: absent`).join("; ");
  if(sample.type===5)return `${(v.millilux/1000).toFixed(3)} lux`;
  return `Last event ${v.event} · count ${v.occurrences}`;
}
async function poll() {
  if(connected&&!busy&&!document.hidden) {
    observation=(async()=>{const data=await rpc("telemetry");if(discovered?.discovery_id&&data.discovery_id!==discovered.discovery_id)clearDiscovery('Board state or detection freshness changed. Detect again.');renderTelemetry(data);if(data.result){recoveryRequired=false;el("timestamp").textContent="Snapshot unavailable";el("readings").replaceChildren();el("health").textContent="No current health observation";el("state").textContent="Board state unavailable; refresh to query again.";el("diagnostics").textContent="";return;}el("timestamp").textContent=`Board uptime ${(data.timestamp_ms/1000).toFixed(1)} s`;state(data.configuration);el("health").textContent=`${data.health.total_events} recorded health events · ${data.runtime.failed_workers} failed workers · service status ${data.runtime.service_error}`;table(el("readings"),["Source","Quality","Value","Age"],data.samples.map(s=>[s.name,s.quality,value(s),s.age_ms===null?"—":s.age_ms+" ms"]));el("diagnostics").textContent=JSON.stringify({runtime:data.runtime,queues:data.queues},null,2);})();
    try { await observation; } catch(error) { reviewed=null; recoveryRequired=false; clearDiscovery('The request failed. Check the message above, then detect again.'); notice(error.message,true); controls(); }
    finally { observation=null; }
  }
  if(!connected){renderTelemetry(null);el("timestamp").textContent="No live connection";el("readings").replaceChildren();el("health").textContent="No live observations";el("diagnostics").textContent="";}
  setTimeout(poll,1000);
}
fetch("/api/catalogue").then(r=>r.json()).then(data=>{connections=data.connections;catalogueDescriptors=data.descriptors;for(const preset of connections){const option=document.createElement('option');option.value=preset.preset_id;option.textContent=preset.label;el('discovery-preset').append(option);}connectionDetails();examples=data.examples;for(const name of Object.keys(examples)){const option=document.createElement("option");option.value=name;option.textContent=name;el("examples").append(option);}el("catalogue").textContent=JSON.stringify(data.descriptors,null,2);if(examples["distance-only"]){el("examples").value="distance-only";el("examples").onchange();}}).catch(e=>notice(e.message,true));
controls();poll();
