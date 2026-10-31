import { PLANS, EQUIPMENT, REVISION, COLORS, address, endpointDescription, parseEndpoint } from "./plan.mjs";
import { renderDiagram } from "./diagram.mjs";

const $ = id => document.getElementById(id);
const storageKey = `uthome-wiring-${REVISION}`;
let state = { plan: "foundation", step: 0, mode: "overview", allWires: false, done: {} };
try {
  const saved = JSON.parse(localStorage.getItem(storageKey));
  if (saved && Object.hasOwn(PLANS, saved.plan)) {
    state.plan = saved.plan;
    state.step = Number.isInteger(saved.step) ? Math.max(0, Math.min(saved.step, PLANS[saved.plan].steps.length - 1)) : 0;
    state.mode = saved.mode === "focus" ? "focus" : "overview";
    state.allWires = saved.allWires === true;
    if (saved.done && typeof saved.done === "object" && !Array.isArray(saved.done)) state.done = saved.done;
  }
} catch { /* Private browser mode or corrupt notes: start clean. */ }
let selectedHole = null;
function save() {
  try { localStorage.setItem(storageKey, JSON.stringify(state)); } catch { /* Viewing still works without persistence. */ }
}
function doneKey(step) { return `${state.plan}:${step.id}`; }
function node(tag, className, text) {
  const n = document.createElement(tag);
  if (className) n.className = className;
  if (text !== undefined) n.textContent = text;
  return n;
}
for (const [id, plan] of Object.entries(PLANS)) {
  const option = node("option", "", plan.name); option.value = id; $("setup").append(option);
}
for (const [name, location, readiness] of EQUIPMENT) {
  const item = node("div", "equipment-item");
  item.append(node("strong", "", name), node("span", "", location), node("span", "readiness", readiness));
  $("equipment-list").append(item);
}
function go(index) {
  state.step = Math.max(0, Math.min(index, PLANS[state.plan].steps.length - 1));
  selectedHole = null;
  $("hole-detail").textContent = "Select a hole to see its address and electrical connection.";
  save(); render();
}
function render() {
  const plan = PLANS[state.plan], step = plan.steps[state.step];
  $("setup").value = state.plan;
  $("setup-description").textContent = plan.description;
  $("step-count").textContent = `STEP ${state.step + 1} OF ${plan.steps.length}`;
  const completed = plan.steps.filter(s => state.done[doneKey(s)] === true).length;
  $("review-count").textContent = `${completed} checked`;
  $("progress-fill").style.width = `${100 * completed / plan.steps.length}%`;
  const list = $("step-list"); list.replaceChildren();
  let phase = "";
  plan.steps.forEach((s, i) => {
    if (s.phase !== phase) { list.append(node("div", "phase-label", s.phase.toUpperCase())); phase = s.phase; }
    const done = state.done[doneKey(s)] === true;
    const button = node("button", `step-item${i === state.step ? " active" : ""}${done ? " completed" : ""}`);
    button.type = "button"; button.dataset.step = i;
    if (i === state.step) button.setAttribute("aria-current", "step");
    button.append(node("span", "step-number", done ? "✓" : String(i + 1)), node("span", "", s.title));
    button.addEventListener("click", () => go(i)); list.append(button);
  });
  $("step-kicker").textContent = `${step.phase.toUpperCase()} / ${String(state.step + 1).padStart(2, "0")}`;
  $("step-title").textContent = step.title;
  $("step-instruction").textContent = step.instruction;
  $("detail-note").textContent = step.note || "";
  $("detail-note").hidden = !step.note;
  $("check-text").textContent = step.check;
  $("step-done").checked = state.done[doneKey(step)] === true;
  $("done-label").textContent = step.kind === "wire" ? "I fitted and checked this connection" : "I completed this check";
  $("previous").disabled = state.step === 0;
  $("next").disabled = state.step === plan.steps.length - 1;
  $("view-caption").textContent = state.mode === "focus" ? "Close-up of this step" : "Photo layout · Board B: bottom letters, right numbers";
  $("all-wires").checked = state.allWires;
  for (const mode of ["overview", "focus"]) {
    $(mode).classList.toggle("active", state.mode === mode);
    $(mode).setAttribute("aria-pressed", state.mode === mode ? "true" : "false");
  }
  const controlled = step.power === "controlled";
  $("power-state").textContent = controlled ? "Power only for this check" : "USB unplugged";
  $("power-note").textContent = controlled ? "Follow the check, then unplug before changing any wire." : "Wiring steps are performed with all power off.";
  document.querySelector(".power-banner").classList.toggle("measure", controlled);
  const endpoints = step.kind === "wire" ? [step.from, step.to] : step.endpoints;
  $("endpoints").replaceChildren();
  if (endpoints) endpoints.forEach((id, i) => {
    const block = node("div", "endpoint");
    block.append(node("span", "endpoint-caption", step.kind === "wire" ? (i ? "TO" : "FROM") : (i ? "TEST POINT B" : "TEST POINT A")),
      node("strong", "", address(id)), node("small", "", endpointDescription(id)));
    $("endpoints").append(block);
  });
  renderDiagram($("bench"), plan, state.step, state.mode, state.allWires, selectedHole);
  const rows = $("wire-table"); rows.replaceChildren();
  plan.steps.forEach((s, index) => {
    if (s.kind !== "wire") return;
    const row = node("tr", index === state.step ? "selected-row" : "");
    const first = node("td"); const jump = node("button", "wire-link", s.title);
    jump.addEventListener("click", () => { go(index); document.querySelector(".workspace").scrollIntoView({ behavior: "smooth", block: "start" }); });
    first.append(jump);
    row.append(first);
    for (const id of [s.from, s.to]) {
      const cell = node("td"); cell.append(node("span", "", address(id)), node("small", "", endpointDescription(id))); row.append(cell);
    }
    const purpose = node("td"), chip = node("span", "wire-chip");
    chip.style.background = COLORS[s.net];
    purpose.append(chip, document.createTextNode(s.resistor ? `${s.resistor / 1000} kΩ resistor` : ({ ground: "Ground", v33: "3.3 V", v5: "5 V", signal: "Signal" })[s.net]));
    row.append(purpose); rows.append(row);
  });
}
$("setup").addEventListener("change", e => {
  state.plan = e.target.value; state.step = PLANS[state.plan].initialStep ?? 0; selectedHole = null;
  $("hole-detail").textContent = "Select a hole to see its address and electrical connection.";
  state.allWires = false; state.mode = "overview"; save(); render();
});
$("previous").addEventListener("click", () => go(state.step - 1));
$("next").addEventListener("click", () => go(state.step + 1));
$("step-done").addEventListener("change", e => { state.done[doneKey(PLANS[state.plan].steps[state.step])] = e.target.checked; save(); render(); });
for (const mode of ["overview", "focus"]) $(mode).addEventListener("click", () => { state.mode = mode; save(); render(); });
$("all-wires").addEventListener("change", e => { state.allWires = e.target.checked; save(); render(); });
$("help-toggle").addEventListener("click", () => {
  $("help").hidden = !$("help").hidden;
  $("help-toggle").setAttribute("aria-expanded", String(!$("help").hidden));
});
$("print").addEventListener("click", () => window.print());
$("bench").addEventListener("click", e => {
  const hit = e.target.closest("[data-hole]");
  if (!hit) return;
  const id = hit.dataset.hole, p = parseEndpoint(id);
  selectedHole = id;
  const connectivity = p.type === "grid" ? `Connected strip: ${p.column <= "e" ? "a–e" : "f–j"} in ${p.board === "B" ? "right-hand " : ""}row ${p.row}.`
    : p.type === "rail" ? `Assume a midpoint split until measured. This is the ${p.index <= 25 ? "upper" : "lower"} half.`
      : "Module labels are schematic; verify the actual connector.";
  $("hole-detail").textContent = `${address(id)} — ${endpointDescription(id)} ${connectivity}`;
  renderDiagram($("bench"), PLANS[state.plan], state.step, state.mode, state.allWires, selectedHole);
});
render();
let resizeTimer;
let previousWidth = $("bench").clientWidth;
new ResizeObserver(() => {
  const width = $("bench").clientWidth;
  if (Math.abs(width - previousWidth) < 1) return;
  previousWidth = width;
  clearTimeout(resizeTimer);
  resizeTimer = setTimeout(() => renderDiagram($("bench"), PLANS[state.plan], state.step, state.mode, state.allWires, selectedHole), 100);
}).observe($("diagram-wrap"));
