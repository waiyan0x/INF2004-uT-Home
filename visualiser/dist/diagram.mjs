import { COLORS, ESP_PINS, MODULES, address, parseEndpoint, railRow, physicalGridRow } from "./plan.mjs";

const NS = "http://www.w3.org/2000/svg";
const BOARD_X = { A: 32, B: 370 };
const COL_X = { a: 52, b: 66, c: 80, d: 94, e: 108, f: 150, g: 164, h: 178, i: 192, j: 206 };
const RAIL_X = { "L-": 16, "L+": 30, "R-": 246, "R+": 260 };
const Y = row => 200 + (row - 1) * 9.5;

function el(tag, attributes = {}, text) {
  const n = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attributes)) n.setAttribute(k, String(v));
  if (text !== undefined) n.textContent = text;
  return n;
}
function add(parent, tag, attributes, text) {
  const n = el(tag, attributes, text); parent.append(n); return n;
}
export function point(id) {
  const p = parseEndpoint(id);
  if (p.type === "grid") return { x: BOARD_X[p.board] + COL_X[p.column], y: Y(physicalGridRow(p.board, p.row)) };
  if (p.type === "rail") return { x: BOARD_X[p.board] + RAIL_X[p.side + p.sign], y: Y(railRow(p.index)) };
  const m = MODULES[p.module], index = m.ports.indexOf(p.pin);
  return { x: m.x + 24 + index * (m.w - 48) / (m.ports.length - 1), y: m.y + m.h - 10 };
}
function hole(parent, id, x, y, selected = false) {
  add(parent, "rect", { x: x - 2.6, y: y - 2.6, width: 5.2, height: 5.2, rx: .6, class: "hole" });
  if (id) add(parent, "rect", { x: x - 4.5, y: y - 4.5, width: 9, height: 9, "data-hole": id, class: "hole-hit", "aria-label": address(id) });
  if (selected) add(parent, "circle", { cx: x, cy: y, r: 6.8, fill: "none", stroke: "#d17f13", "stroke-width": 1.8, "pointer-events": "none" });
}
function drawBoard(parent, board, uses5V, selectedHole) {
  const x = BOARD_X[board], g = add(parent, "g", { "data-board": board });
  add(g, "text", { x: x + 2, y: 157, class: "board-label" }, `BOARD ${board}`);
  add(g, "text", { x: x + 96, y: 157, class: "board-sub" }, board === "A" ? "Controller" : "Sensor connections");
  add(g, "rect", { x, y: 170, width: 282, height: 658, rx: 11, class: "board-body" });
  add(g, "rect", { x: x + 120, y: 194, width: 18, height: 604, rx: 6, fill: "#e9e9df" });
  add(g, "path", { d: `M${x + 124},194 V798`, fill: "none", stroke: "#dcded2", "stroke-width": 1 });
  for (const [col, cx] of Object.entries(COL_X)) {
    const topLetter = board === "B" ? "JIHGFEDCBA"["abcdefghij".indexOf(col)] : col;
    add(g, "text", { x: x + cx, y: 185, "text-anchor": "middle", class: "grid-label" }, topLetter);
    add(g, "text", { x: x + cx, y: 817, "text-anchor": "middle", class: "grid-label" }, board === "B" ? col.toUpperCase() : col);
    for (let physical = 1; physical <= 63; physical++) {
      const row = board === "B" ? physical - 3 : physical;
      const id = row < 0 ? null : `${board}:${col}${row}`;
      hole(g, id, x + cx, Y(physical), id !== null && selectedHole === id);
    }
  }
  for (let physical = 1; physical <= 63; physical++) {
    const row = board === "B" ? physical - 3 : physical;
    if (row < 0) continue;
    const major = row % 5 === 0 || (board === "A" && (row === 1 || row === 63 || (row >= 28 && row <= 37)));
    for (const cx of [39, 224]) {
      const label = board === "B" && cx === 39 ? 60 - row : row;
      add(g, "text", { x: x + cx, y: Y(physical) + 3, "text-anchor": "middle", class: `grid-label${major ? "" : " detail-grid-label"}` }, label);
    }
  }
  for (const [rail, rx] of Object.entries(RAIL_X)) {
    const negative = rail[1] === "-";
    const stroke = negative ? "#3c93bc" : "#cb5871";
    add(g, "text", { x: x + rx, y: 185, "text-anchor": "middle", class: "rail-label" }, negative ? "−" : "+");
    add(g, "text", { x: x + rx, y: 817, "text-anchor": "middle", class: "rail-label" }, negative ? "−" : "+");
    // A visible midpoint break is conservative, not a claim about the user's rails.
    for (const [first, last] of [[1, 25], [26, 50]]) {
      add(g, "line", { x1: x + rx + (negative ? -7 : 7), x2: x + rx + (negative ? -7 : 7), y1: Y(railRow(first)) - 6, y2: Y(railRow(last)) + 6, stroke, "stroke-width": 1.6 });
    }
    for (let index = 1; index <= 50; index++) {
      const id = `${board}:${rail}${index}`;
      hole(g, id, x + rx, Y(railRow(index)), selectedHole === id);
      if (negative) add(g, "text", { x: x + rx + 7, y: Y(railRow(index)) + 2, "text-anchor": "middle", class: "rail-index", "pointer-events": "none" }, index);
    }
    // Labels sit outside the breadboard, away from holes.
    const isLeft = rail[0] === "L";
    if ((isLeft && rail === "L-") || (!isLeft && rail === "R+")) {
      const tx = x + (isLeft ? -13 : 296);
      const label = isLeft
        ? (board === "A" ? `L− unused / L+ ${uses5V ? "5 V" : "unused"}` : "L− GND / L+ 3.3 V")
        : (board === "A" ? "R− GND / R+ 3.3 V" : "R− unused / R+ unused");
      add(g, "text", { x: tx, y: 340, transform: `rotate(-90 ${tx} 340)`, "text-anchor": "middle", class: "rail-label" }, label);
    }
  }
}
function drawEsp(parent, shown) {
  const left = point("A:c29"), right = point("A:h36");
  const g = add(parent, "g", { opacity: shown ? 1 : .16, "data-component": "esp" });
  add(g, "rect", { x: left.x - 6, y: left.y - 9, width: right.x - left.x + 12, height: right.y - left.y + 20, rx: 3, class: "component-body" });
  add(g, "rect", { x: left.x + 35, y: left.y - 20, width: 31, height: 27, rx: 3, fill: "#c3c8c7", stroke: "#687a7f", "stroke-width": 1 });
  add(g, "rect", { x: left.x + 40, y: left.y - 17, width: 21, height: 5, rx: 2, fill: "#4e646b" });
  add(g, "rect", { x: left.x + 40, y: left.y + 21, width: 23, height: 23, rx: 2, fill: "#52646d", transform: `rotate(45 ${left.x + 51.5} ${left.y + 32.5})` });
  add(g, "text", { x: left.x + 51, y: left.y + 58, "text-anchor": "middle", class: "component-pin-label" }, "ESP32-C3");
  for (const pin of ESP_PINS) {
    const p = point(`A:${pin.column}${pin.row}`);
    add(g, "circle", { cx: p.x, cy: p.y, r: 2.7, class: "port-pad" });
    add(g, "text", { x: p.x + (pin.column === "c" ? 7 : -7), y: p.y + 2.5, "text-anchor": pin.column === "c" ? "start" : "end", class: "component-pin-label", opacity: pin.reserved ? .4 : 1 }, pin.label);
  }
}
function drawModule(parent, id, shown) {
  const m = MODULES[id], g = add(parent, "g", { opacity: shown ? 1 : .15, "data-component": id });
  add(g, "rect", { x: m.x, y: m.y, width: m.w, height: m.h, rx: 10, fill: "#edf3f0", stroke: "#b7cbc2", "stroke-width": 1.3 });
  add(g, "text", { x: m.x + 12, y: m.y + 20, class: "module-label" }, m.name);
  add(g, "text", { x: m.x + 12, y: m.y + 36, class: "module-sub" }, "Labelled connections · not physical pin order");
  if (id === "radar") {
    for (let i = 0; i < 3; i++) {
      add(g, "rect", { x: m.x + 40 + i * 62, y: m.y + 47, width: 39, height: 15, rx: 2, fill: "#bdcfc6" });
    }
  } else if (id === "oled") {
    add(g, "rect", { x: m.x + 55, y: m.y + 43, width: m.w - 110, height: 22, rx: 3, fill: "#193c48" });
    add(g, "text", { x: m.x + m.w / 2, y: m.y + 58, "text-anchor": "middle", fill: "#b7eadc", "font-size": 10 }, "DISPLAY · NO LIVE DATA");
  } else if (id === "distance") {
    [68, m.w - 68].forEach(cx => add(g, "circle", { cx: m.x + cx, cy: m.y + 55, r: 14, fill: "#ced9d5", stroke: "#829891", "stroke-width": 2 }));
  } else {
    add(g, "rect", { x: m.x + m.w / 2 - 12, y: m.y + 44, width: 24, height: 17, rx: 2, fill: "#a7bbb2" });
  }
  for (const pin of m.ports) {
    const p = point(`${id}:${pin}`);
    add(g, "text", { x: p.x, y: p.y - 8, "text-anchor": "middle", class: "module-sub" }, pin);
    add(g, "rect", { x: p.x - 4, y: p.y - 3, width: 8, height: 6, rx: 1, class: "port-pad" });
    add(g, "rect", { x: p.x - 8, y: p.y - 8, width: 16, height: 16, class: "hole-hit", "data-hole": `${id}:${pin}` });
  }
  add(g, "text", { x: m.x + m.w / 2, y: m.y + m.h + 15, "text-anchor": "middle", class: "module-sub" }, m.location);
}
function route(step, index) {
  const a = point(step.from), b = point(step.to);
  if (step.resistor) return `M${a.x},${a.y} L${b.x},${b.y}`;
  const ea = parseEndpoint(step.from), eb = parseEndpoint(step.to);
  if (ea.type === "rail" && eb.type === "rail" && ea.board === eb.board && ea.side === eb.side && ea.sign === eb.sign) {
    return `M${a.x},${a.y} C${a.x + 14},${a.y} ${b.x + 14},${b.y} ${b.x},${b.y}`;
  }
  if (ea.type === "module" || eb.type === "module") {
    const offset = 28 + (index % 5) * 10;
    return `M${a.x},${a.y} C${a.x},${a.y + (a.y < b.y ? offset : -offset)} ${b.x},${b.y + (a.y < b.y ? -offset : offset)} ${b.x},${b.y}`;
  }
  const rise = Math.min(44 + (index % 3) * 12, 70);
  return `M${a.x},${a.y} C${a.x},${a.y - rise} ${b.x},${b.y - rise} ${b.x},${b.y}`;
}
function drawWire(parent, step, index, current, ghost) {
  const color = COLORS[step.net];
  const opacity = current ? 1 : ghost ? .14 : .58;
  const g = add(parent, "g", { opacity, "data-wire": step.id });
  add(g, "path", { d: route(step, index), stroke: "#fff", "stroke-width": current ? 6 : 4.8, class: "wire-path" });
  add(g, "path", { d: route(step, index), stroke: color, "stroke-width": current ? 3.7 : 2.6, class: `wire-path ${current ? "current-wire" : ""}`, ...(ghost ? { "stroke-dasharray": "3 4" } : {}) });
  if (step.resistor) {
    const a = point(step.from), b = point(step.to), mid = (a.y + b.y) / 2;
    add(g, "rect", { x: a.x - 5, y: mid - 12, width: 10, height: 24, rx: 3, fill: "#dfc891", stroke: "#9b7d43", "stroke-width": 1 });
    add(g, "text", { x: a.x + 10, y: mid + 3, class: "module-sub" }, `${step.resistor / 1000} kΩ`);
  }
  if (current) [step.from, step.to].forEach((id, i) => {
    const p = point(id);
    add(g, "circle", { cx: p.x, cy: p.y, r: 7.6, stroke: color, class: "endpoint-ring" });
    add(g, "circle", { cx: p.x, cy: p.y, r: 2.2, fill: color });
    add(g, "text", { x: p.x + 11, y: p.y - 10, class: "focus-label", "paint-order": "stroke", stroke: "#fff", "stroke-width": 3, "stroke-linejoin": "round" }, i ? "TO" : "FROM");
  });
}
function makeScene(plan, stepIndex, allWires, selectedHole) {
  const scene = el("g"), current = plan.steps[stepIndex], uses5V = plan.steps.some(s => s.net === "v5");
  ["A", "B"].forEach(b => drawBoard(scene, b, uses5V, selectedHole));
  drawEsp(scene, stepIndex >= plan.steps.findIndex(s => s.id === "esp"));
  plan.modules.forEach(id => {
    const first = plan.steps.findIndex(s => s.module === id);
    drawModule(scene, id, allWires || stepIndex >= first);
  });
  plan.steps.forEach((s, i) => {
    if (s.kind === "wire" && i !== stepIndex && (i < stepIndex || allWires)) drawWire(scene, s, i, false, i > stepIndex);
  });
  if (current.kind === "wire") drawWire(scene, current, stepIndex, true, false);
  if (current.endpoints) current.endpoints.forEach(id => {
    const p = point(id);
    add(scene, "circle", { cx: p.x, cy: p.y, r: 8, stroke: "#bd791b", class: "endpoint-ring" });
    add(scene, "circle", { cx: p.x, cy: p.y, r: 2, fill: "#bd791b" });
  });
  return scene;
}
export function renderDiagram(svg, plan, stepIndex, mode, allWires, selectedHole) {
  const current = plan.steps[stepIndex], scene = makeScene(plan, stepIndex, allWires, selectedHole);
  svg.replaceChildren(el("title", { id: "diagram-title" }, current.title), el("desc", { id: "diagram-desc" }, current.instruction));
  const targets = current.kind === "wire" ? [current.from, current.to] : current.endpoints;
  const stacked = mode === "focus" && targets?.length === 2 && svg.clientWidth < 560;
  svg.classList.toggle("endpoint-focus", mode === "focus");
  svg.classList.toggle("stacked-focus", stacked);
  if (mode === "focus" && targets?.length === 2) {
    svg.setAttribute("viewBox", stacked ? "0 0 360 900" : "0 0 700 470");
    targets.forEach((id, index) => {
      const x = 18 + (stacked ? 0 : index * 345), y = stacked ? index * 450 : 0;
      const p = point(id), parsed = parseEndpoint(id);
      add(svg, "text", { x: x + 14, y: y + 34, class: "focus-label" }, `${index ? "TO / TEST B" : "FROM / TEST A"}   ${address(id)}`);
      add(svg, "rect", { x, y: y + 48, width: 327, height: 360, rx: 12, class: "view-frame" });
      const w = parsed.type === "module" ? 285 : 115, h = w * 340 / 307;
      const inner = add(svg, "svg", { x: x + 10, y: y + 58, width: 307, height: 340, viewBox: `${p.x - w / 2} ${p.y - h / 2} ${w} ${h}`, overflow: "hidden" });
      inner.append(scene.cloneNode(true));
      add(svg, "text", { x: x + 14, y: y + 437, class: "module-sub" }, parsed.type === "rail"
        ? `${parsed.side === "R" ? "RIGHT" : "LEFT"} ${parsed.sign === "-" ? "BLUE −" : "RED +"} rail · hole ${parsed.index} from the top`
        : parsed.type === "grid" ? (parsed.board === "B"
          ? `BOTTOM ${parsed.column.toUpperCase()} · RIGHT row ${parsed.row} · LEFT ${60 - parsed.row}`
          : `Column ${parsed.column} · printed row ${parsed.row}`)
          : "Use the printed module label");
    });
  } else if (mode === "focus" && current.focusEsp) {
    svg.setAttribute("viewBox", "0 0 700 500");
    add(svg, "text", { x: 25, y: 32, class: "focus-label" }, "BOARD A · ESP placement · USB points up");
    const inner = add(svg, "svg", { x: 25, y: 50, width: 650, height: 420, viewBox: "62 420 230 150" });
    inner.append(scene);
  } else if (mode === "focus" && current.module) {
    const m = MODULES[current.module];
    svg.setAttribute("viewBox", "0 0 700 350");
    const inner = add(svg, "svg", { x: 15, y: 15, width: 670, height: 320, viewBox: `${m.x - 15} ${m.y - 15} ${m.w + 30} ${m.h + 65}` });
    inner.append(scene);
  } else {
    svg.setAttribute("viewBox", `0 0 700 ${plan.modules.includes("oled") ? 1005 : 855}`);
    svg.append(scene);
  }
}
