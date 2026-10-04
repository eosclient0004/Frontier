/* ============================================================================
   Frontier — Texture Paint Studio
   A layered PBR material authoring tool. Reuses the Slate "Fluid" visual
   language but is purpose-built for painting: a layer stack, a robust
   multi-channel material system, and SVG / text decals.
   ========================================================================== */

import { icon, applyIcons } from "./icons.js";

/* ----------------------------------------------------------------- fatal guard */
function showFatalError(msg) {
  let el = document.getElementById("fatal-error");
  if (!el) {
    el = document.createElement("div");
    el.id = "fatal-error";
    el.style.cssText =
      "position:fixed;inset:0;z-index:9999;background:#0b0b0b;color:#ff9a9a;" +
      "font:13px/1.6 DM Sans,ui-sans-serif,sans-serif;padding:24px;white-space:pre-wrap;overflow:auto";
    (document.body || document.documentElement).appendChild(el);
  }
  el.textContent = "Frontier failed to start:\n\n" + msg;
}
window.addEventListener("error", (e) =>
  showFatalError((e.error && e.error.stack) || e.message || String(e))
);
window.addEventListener("unhandledrejection", (e) =>
  showFatalError("Unhandled promise rejection:\n" + ((e.reason && e.reason.stack) || e.reason))
);

/* ----------------------------------------------------------------- constants */
const DEFAULT_RES = 1024;

const CHANNEL_DEFS = [
  { id: "baseColor", label: "Base Color", short: "RGB", type: "rgb", category: "color", baseline: [22, 22, 26] },
  { id: "roughness", label: "Roughness", short: "R", type: "scalar", category: "scalar", baseline: [128, 128, 128] },
  { id: "metallic", label: "Metallic", short: "R", type: "scalar", category: "scalar", baseline: [0, 0, 0] },
  { id: "normal", label: "Normal", short: "RGB", type: "normal", category: "data", baseline: [128, 128, 255] },
  { id: "height", label: "Height", short: "R", type: "scalar", category: "data", baseline: [128, 128, 128] },
  { id: "emissive", label: "Emissive", short: "RGB", type: "rgb", category: "color", baseline: [0, 0, 0] },
];

const BLEND_MODES = [
  ["normal", "Normal"],
  ["multiply", "Multiply"],
  ["screen", "Screen"],
  ["overlay", "Overlay"],
  ["lighten", "Lighten"],
  ["darken", "Darken"],
  ["add", "Add"],
  ["difference", "Difference"],
];

const BLEND_OP = {
  normal: "source-over",
  multiply: "multiply",
  screen: "screen",
  overlay: "overlay",
  lighten: "lighten",
  darken: "darken",
  add: "lighter",
  difference: "difference",
};

const BRUSH_PRESETS = [
  { name: "Soft Round", size: 90, hardness: 0.2, opacity: 1, flow: 0.6, spacing: 0.12 },
  { name: "Hard Round", size: 42, hardness: 0.9, opacity: 1, flow: 1, spacing: 0.04 },
  { name: "Airbrush", size: 140, hardness: 0.05, opacity: 0.5, flow: 0.18, spacing: 0.05 },
  { name: "Chalk", size: 64, hardness: 0.6, opacity: 0.9, flow: 0.5, spacing: 0.22 },
  { name: "Marker", size: 52, hardness: 0.82, opacity: 1, flow: 0.9, spacing: 0.02 },
];

const DECAL_PRESETS = [
  { name: "Arrow", svg: "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><path d='M18 50 H68 M52 34 L74 50 L52 66' fill='none' stroke='#e8e8e8' stroke-width='11' stroke-linecap='round' stroke-linejoin='round'/></svg>" },
  { name: "Star", svg: "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><path d='M50 8 L61 38 L93 38 L67 58 L77 90 L50 70 L23 90 L33 58 L7 38 L39 38 Z' fill='#e8e8e8'/></svg>" },
  { name: "Warning", svg: "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><path d='M50 14 L90 84 H10 Z' fill='none' stroke='#e8e8e8' stroke-width='9' stroke-linejoin='round'/><path d='M50 42 V64' stroke='#e8e8e8' stroke-width='9' stroke-linecap='round'/><circle cx='50' cy='76' r='4.5' fill='#e8e8e8'/></svg>" },
  { name: "Ring", svg: "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><circle cx='50' cy='50' r='34' fill='none' stroke='#e8e8e8' stroke-width='12'/></svg>" },
];

/* -------------------------------------------------------------------- state */
let uidCounter = 0;
const uid = () => `L${(uidCounter++).toString(36)}${Math.random().toString(36).slice(2, 5)}`;

const state = {
  material: { name: "Rusty Plate", res: DEFAULT_RES, tiling: 1 },
  channelEnabled: Object.fromEntries(CHANNEL_DEFS.map((c) => [c.id, true])),
  activeChannel: "baseColor",
  channelFilter: "all",
  channelSearch: "",
  layers: [],
  selectedLayerId: null,
  tool: "brush",
  brush: { size: 90, hardness: 0.2, opacity: 1, flow: 0.6, spacing: 0.12, color: "#c8692f", value: 0.5 },
  view: { mode: "channel", zoom: 1, grid: true },
  undo: [],
  redo: [],
  dirty: false,
};

let shadedCache = null;
let fit = { ox: 0, oy: 0, dw: 0, dh: 0, scale: 1 };
let painting = false;
let strokeLast = null;
let strokeTarget = null;
let strokeRGB = null;
let strokeErase = false;
let checkerPattern = null;

/* --------------------------------------------------------------- dom lookup */
const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));
const paintSurface = $("#paint-surface");
const viewportEl = $("#viewport");
const brushCursor = $("#brush-cursor");

/* -------------------------------------------------------------- math helpers */
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const lerp = (a, b, t) => a + (b - a) * t;
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const normalize = (v) => {
  const l = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / l, v[1] / l, v[2] / l];
};
const channelDef = (id) => CHANNEL_DEFS.find((c) => c.id === id);
const selectedLayer = () => state.layers.find((l) => l.id === state.selectedLayerId) || null;

function hexToRgb(hex) {
  const h = hex.replace("#", "");
  return {
    r: parseInt(h.slice(0, 2), 16),
    g: parseInt(h.slice(2, 4), 16),
    b: parseInt(h.slice(4, 6), 16),
  };
}
function makeCanvas(w, h) {
  const c = document.createElement("canvas");
  c.width = w;
  c.height = h;
  return c;
}

/* ===================================================================== engine */
function getBuffer(layer, channelId) {
  if (!layer.buffers[channelId]) {
    const c = makeCanvas(state.material.res, state.material.res);
    layer.buffers[channelId] = c;
  }
  return layer.buffers[channelId];
}

function compositeChannel(channelId, withBaseline) {
  const res = state.material.res;
  const out = makeCanvas(res, res);
  const ctx = out.getContext("2d");
  if (withBaseline) {
    const b = channelDef(channelId).baseline;
    ctx.fillStyle = `rgb(${b[0]},${b[1]},${b[2]})`;
    ctx.fillRect(0, 0, res, res);
  }
  const cx = res / 2;
  const cy = res / 2;
  for (const layer of state.layers) {
    if (!layer.visible) continue;
    const buf = layer.buffers[channelId];
    if (!buf) continue;
    ctx.save();
    ctx.globalAlpha = layer.opacity;
    ctx.globalCompositeOperation = BLEND_OP[layer.blend] || "source-over";
    const t = layer.transform;
    ctx.translate(cx + t.x, cy + t.y);
    ctx.rotate((t.rotation * Math.PI) / 180);
    ctx.scale(t.scale, t.scale);
    ctx.drawImage(buf, -res / 2, -res / 2);
    ctx.restore();
  }
  return out;
}

function tint(data, idx) {
  return [data[idx] / 255, data[idx + 1] / 255, data[idx + 2] / 255];
}

function computeShadedPreview() {
  const res = state.material.res;
  const tiling = Math.max(1, state.material.tiling | 0);
  const base = compositeChannel("baseColor", true).getContext("2d").getImageData(0, 0, res, res);
  const rough = compositeChannel("roughness", true).getContext("2d").getImageData(0, 0, res, res);
  const metal = compositeChannel("metallic", true).getContext("2d").getImageData(0, 0, res, res);
  const norm = compositeChannel("normal", true).getContext("2d").getImageData(0, 0, res, res);
  const emis = compositeChannel("emissive", true).getContext("2d").getImageData(0, 0, res, res);

  const out = makeCanvas(res, res);
  const octx = out.getContext("2d");
  const odata = octx.createImageData(res, res);

  const L = normalize([0.42, 0.5, 0.78]);
  const H = normalize([L[0], L[1], L[2] + 1]);

  const bd = base.data, rd = rough.data, md = metal.data, nd = norm.data, ed = emis.data, od = odata.data;
  for (let py = 0; py < res; py++) {
    for (let px = 0; px < res; px++) {
      const sx = ((px * tiling) % res + res) % res;
      const sy = ((py * tiling) % res + res) % res;
      const si = (sy * res + sx) * 4;
      const di = (py * res + px) * 4;

      let nx = (nd[si] / 255) * 2 - 1;
      let ny = (nd[si + 1] / 255) * 2 - 1;
      let nz = (nd[si + 2] / 255) * 2 - 1;
      const nl = Math.hypot(nx, ny, nz) || 1;
      nx /= nl; ny /= nl; nz /= nl;

      const ndl = Math.max(0, nx * L[0] + ny * L[1] + nz * L[2]);
      const ndh = Math.max(0, nx * H[0] + ny * H[1] + nz * H[2]);
      const r = rd[si] / 255;
      const m = md[si] / 255;
      const shin = lerp(4, 220, 1 - r);
      const spec = Math.pow(ndh, shin) * (1 - r * 0.85);

      const ar = bd[si] / 255, ag = bd[si + 1] / 255, ab = bd[si + 2] / 255;
      const ambient = 0.16;
      const kd = [ar * (1 - m), ag * (1 - m), ab * (1 - m)];
      const ks0 = lerp(0.04, ar, m), ks1 = lerp(0.04, ag, m), ks2 = lerp(0.04, ab, m);
      const er = ed[si] / 255, eg = ed[si + 1] / 255, eb = ed[si + 2] / 255;

      od[di] = clamp((kd[0] * (ambient + ndl) + ks0 * spec + er) * 255, 0, 255);
      od[di + 1] = clamp((kd[1] * (ambient + ndl) + ks1 * spec + eg) * 255, 0, 255);
      od[di + 2] = clamp((kd[2] * (ambient + ndl) + ks2 * spec + eb) * 255, 0, 255);
      od[di + 3] = 255;
    }
  }
  octx.putImageData(odata, 0, 0);
  return out;
}

function getShaded() {
  if (!shadedCache) shadedCache = computeShadedPreview();
  return shadedCache;
}
function invalidateShaded() {
  shadedCache = null;
}

/* -------------------------------------------------------------- render viewport */
function renderViewport(forceChannel = false) {
  const cw = viewportEl.clientWidth;
  const ch = viewportEl.clientHeight;
  if (!cw || !ch) return;
  const dpr = window.devicePixelRatio || 1;
  const needW = Math.round(cw * dpr);
  const needH = Math.round(ch * dpr);
  if (paintSurface.width !== needW || paintSurface.height !== needH) {
    paintSurface.width = needW;
    paintSurface.height = needH;
  }
  const ctx = paintSurface.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cw, ch);

  const res = state.material.res;
  const pad = 30;
  const baseScale = Math.min((cw - pad * 2) / res, (ch - pad * 2) / res);
  const scale = baseScale * state.view.zoom;
  const dw = res * scale;
  const dh = res * scale;
  const ox = (cw - dw) / 2;
  const oy = (ch - dh) / 2;
  fit = { ox, oy, dw, dh, scale };

  // checkerboard backdrop inside the texture rect (cached pattern)
  if (!checkerPattern) {
    const pc = makeCanvas(20, 20);
    const pctx = pc.getContext("2d");
    pctx.fillStyle = "#171717";
    pctx.fillRect(0, 0, 20, 20);
    pctx.fillStyle = "#212121";
    pctx.fillRect(0, 0, 10, 10);
    pctx.fillRect(10, 10, 10, 10);
    checkerPattern = pc;
  }
  ctx.fillStyle = ctx.createPattern(checkerPattern, "repeat");
  ctx.fillRect(ox, oy, dw, dh);

  // source image
  let src;
  if (state.view.mode === "shaded" && !forceChannel) src = getShaded();
  else src = compositeChannel(state.activeChannel, false);
  ctx.imageSmoothingEnabled = state.view.zoom < 2.2;
  ctx.drawImage(src, ox, oy, dw, dh);

  // grid
  if (state.view.grid) drawGrid(ctx, ox, oy, dw, dh, res);

  // frame
  ctx.strokeStyle = "rgba(255,255,255,0.12)";
  ctx.lineWidth = 1;
  ctx.strokeRect(ox + 0.5, oy + 0.5, dw - 1, dh - 1);
}

function drawGrid(ctx, ox, oy, dw, dh, res) {
  const divs = 16;
  ctx.lineWidth = 1;
  for (let i = 0; i <= divs; i++) {
    const t = i / divs;
    const major = i % 4 === 0;
    ctx.strokeStyle = major ? "rgba(255,255,255,0.10)" : "rgba(255,255,255,0.04)";
    const x = ox + dw * t;
    const y = oy + dh * t;
    ctx.beginPath();
    ctx.moveTo(x, oy);
    ctx.lineTo(x, oy + dh);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(ox, y);
    ctx.lineTo(ox + dw, y);
    ctx.stroke();
  }
  // centre cross
  ctx.strokeStyle = "rgba(255,255,255,0.16)";
  ctx.beginPath();
  ctx.moveTo(ox + dw / 2, oy);
  ctx.lineTo(ox + dw / 2, oy + dh);
  ctx.moveTo(ox, oy + dh / 2);
  ctx.lineTo(ox + dw, oy + dh / 2);
  ctx.stroke();
}

/* --------------------------------------------------------------- layer thumb */
function renderLayerThumb(layer, canvasEl) {
  const w = canvasEl.width;
  const h = canvasEl.height;
  const ctx = canvasEl.getContext("2d");
  ctx.clearRect(0, 0, w, h);
  const buf = layer.buffers[state.activeChannel];
  if (buf) {
    ctx.drawImage(buf, 0, 0, w, h);
  } else {
    const def = channelDef(state.activeChannel);
    ctx.fillStyle = `rgb(${def.baseline[0]},${def.baseline[1]},${def.baseline[2]})`;
    ctx.fillRect(0, 0, w, h);
  }
}

/* ====================================================================== paint */
function drawDab(ctx, x, y, radius, hardness, rgb, alpha, erase) {
  ctx.save();
  ctx.globalCompositeOperation = erase ? "destination-out" : "source-over";
  const inner = clamp(hardness, 0, 0.999);
  const c = erase ? "0,0,0" : `${rgb.r},${rgb.g},${rgb.b}`;
  const g = ctx.createRadialGradient(x, y, 0, x, y, radius);
  g.addColorStop(0, `rgba(${c},${alpha})`);
  g.addColorStop(inner, `rgba(${c},${alpha})`);
  g.addColorStop(1, `rgba(${c},0)`);
  ctx.fillStyle = g;
  ctx.beginPath();
  ctx.arc(x, y, Math.max(0.5, radius), 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}

function paintColorFor(channelType) {
  if (channelType === "scalar") {
    const v = clamp(state.brush.value, 0, 1) * 255;
    return { r: v, g: v, b: v };
  }
  if (channelType === "normal") return { r: 128, g: 128, b: 255 };
  return hexToRgb(state.brush.color);
}

function toTexture(clientX, clientY) {
  const rect = paintSurface.getBoundingClientRect();
  const px = clientX - rect.left;
  const py = clientY - rect.top;
  return { x: (px - fit.ox) / fit.scale, y: (py - fit.oy) / fit.scale };
}

function snapshotOf(layer, channelId) {
  const buf = layer.buffers[channelId];
  if (!buf) return null;
  const img = buf.getContext("2d").getImageData(0, 0, state.material.res, state.material.res);
  return new ImageData(new Uint8ClampedArray(img.data), img.width, img.height);
}

function applySnapshot(layer, channelId, snap) {
  if (snap) {
    const buf = getBuffer(layer, channelId);
    buf.getContext("2d").putImageData(snap, 0, 0);
  } else {
    delete layer.buffers[channelId];
  }
}

function pushUndo(layer, channelId) {
  state.undo.push({ layerId: layer.id, channelId, before: snapshotOf(layer, channelId), after: null });
  if (state.undo.length > 20) state.undo.shift();
  state.redo.length = 0;
}

function beginStroke(point) {
  const layer = selectedLayer();
  if (!layer) return;
  if (!state.channelEnabled[state.activeChannel]) {
    flashChannelDisabled();
    return;
  }
  strokeTarget = layer;
  strokeErase = state.tool === "eraser";
  const def = channelDef(state.activeChannel);
  strokeRGB = paintColorFor(def.type);
  pushUndo(layer, state.activeChannel);
  painting = true;
  strokeLast = point;
  const radius = state.brush.size / 2;
  const alpha = state.brush.opacity * state.brush.flow;
  const ctx = getBuffer(layer, state.activeChannel).getContext("2d");
  drawDab(ctx, point.x, point.y, radius, state.brush.hardness, strokeRGB, alpha, strokeErase);
  refreshAfterPaint(layer);
}

function strokeTo(point) {
  if (!painting || !strokeTarget) return;
  const ctx = getBuffer(strokeTarget, state.activeChannel).getContext("2d");
  const radius = state.brush.size / 2;
  const alpha = state.brush.opacity * state.brush.flow;
  const spacing = Math.max(0.4, state.brush.spacing * radius);
  const dx = point.x - strokeLast.x;
  const dy = point.y - strokeLast.y;
  const dist = Math.hypot(dx, dy);
  const steps = Math.max(1, Math.floor(dist / spacing));
  for (let i = 1; i <= steps; i++) {
    const t = i / steps;
    const x = strokeLast.x + dx * t;
    const y = strokeLast.y + dy * t;
    drawDab(ctx, x, y, radius, state.brush.hardness, strokeRGB, alpha, strokeErase);
  }
  strokeLast = point;
  refreshAfterPaint(strokeTarget);
}

function endStroke() {
  if (!painting) return;
  painting = false;
  const l = strokeTarget;
  const ch = state.activeChannel;
  const u = state.undo[state.undo.length - 1];
  if (u && u.layerId === (l && l.id) && u.channelId === ch) u.after = snapshotOf(l, ch);
  invalidateShaded();
  setDirty(true);
  if (state.view.mode === "shaded") renderViewport();
  strokeTarget = null;
  if (l) refreshAfterPaint(l);
}

function refreshAfterPaint(layer) {
  renderViewport(true);
  updateLayerThumb(layer);
}

function floodFill(layer, channelId, x, y) {
  const res = state.material.res;
  const buf = getBuffer(layer, channelId);
  const ctx = buf.getContext("2d");
  const img = ctx.getImageData(0, 0, res, res);
  const data = img.data;
  const ix = clamp(Math.floor(x), 0, res - 1);
  const iy = clamp(Math.floor(y), 0, res - 1);
  const start = (iy * res + ix) * 4;
  const tr = data[start], tg = data[start + 1], tb = data[start + 2], ta = data[start + 3];
  const def = channelDef(channelId);
  let nr, ng, nb;
  if (def.type === "scalar") {
    const v = clamp(state.brush.value, 0, 1) * 255;
    nr = ng = nb = v;
  } else if (def.type === "normal") {
    nr = 128; ng = 128; nb = 255;
  } else {
    const c = hexToRgb(state.brush.color);
    nr = c.r; ng = c.g; nb = c.b;
  }
  if (tr === nr && tg === ng && tb === nb && ta === 255) return;
  const tol = 24;
  const match = (i) =>
    Math.abs(data[i] - tr) <= tol &&
    Math.abs(data[i + 1] - tg) <= tol &&
    Math.abs(data[i + 2] - tb) <= tol &&
    Math.abs(data[i + 3] - ta) <= tol;
  const stack = [[ix, iy]];
  const seen = new Uint8Array(res * res);
  while (stack.length) {
    const [cx, cy] = stack.pop();
    if (cx < 0 || cy < 0 || cx >= res || cy >= res) continue;
    const p = cy * res + cx;
    if (seen[p]) continue;
    const idx = p * 4;
    if (!match(idx)) continue;
    seen[p] = 1;
    data[idx] = nr; data[idx + 1] = ng; data[idx + 2] = nb; data[idx + 3] = 255;
    stack.push([cx + 1, cy], [cx - 1, cy], [cx, cy + 1], [cx, cy - 1]);
  }
  ctx.putImageData(img, 0, 0);
  invalidateShaded();
  setDirty(true);
  renderViewport();
  updateLayerThumb(layer);
}

function eyedrop(point) {
  const res = state.material.res;
  const ix = clamp(Math.floor(point.x), 0, res - 1);
  const iy = clamp(Math.floor(point.y), 0, res - 1);
  const c = compositeChannel(state.activeChannel, false).getContext("2d").getImageData(ix, iy, 1, 1).data;
  const def = channelDef(state.activeChannel);
  if (def.type === "rgb") {
    state.brush.color = `#${[c[0], c[1], c[2]].map((v) => v.toString(16).padStart(2, "0")).join("")}`;
    buildBrushSettings();
  } else if (def.type === "scalar") {
    state.brush.value = c[0] / 255;
    buildBrushSettings();
  }
}

function flashChannelDisabled() {
  const pill = $("#live-pill");
  pill.textContent = "MAP DISABLED";
  pill.style.color = "#ff8a8a";
  setTimeout(() => {
    pill.innerHTML = "<i></i> EDITING";
    pill.style.color = "";
  }, 900);
}

/* =================================================================== layers ops */
function addLayer(name, opts = {}) {
  const layer = {
    id: uid(),
    name: name || `Layer ${state.layers.length + 1}`,
    visible: true,
    opacity: 1,
    blend: "normal",
    transform: { x: 0, y: 0, scale: 1, rotation: 0 },
    buffers: {},
    ...opts,
  };
  state.layers.push(layer);
  state.selectedLayerId = layer.id;
  setDirty(true);
  invalidateShaded();
  buildLayerStack();
  renderViewport();
  updateStats();
  return layer;
}

function deleteLayer(id) {
  const idx = state.layers.findIndex((l) => l.id === id);
  if (idx < 0) return;
  state.layers.splice(idx, 1);
  if (state.selectedLayerId === id) {
    const next = state.layers[Math.min(idx, state.layers.length - 1)];
    state.selectedLayerId = next ? next.id : null;
  }
  setDirty(true);
  invalidateShaded();
  buildLayerStack();
  renderViewport();
  updateStats();
}

function moveLayer(id, dir) {
  const idx = state.layers.findIndex((l) => l.id === id);
  const ni = idx + dir;
  if (idx < 0 || ni < 0 || ni >= state.layers.length) return;
  const [l] = state.layers.splice(idx, 1);
  state.layers.splice(ni, 0, l);
  setDirty(true);
  invalidateShaded();
  buildLayerStack();
  renderViewport();
}

function toggleLayerVis(id) {
  const l = state.layers.find((x) => x.id === id);
  if (!l) return;
  l.visible = !l.visible;
  setDirty(true);
  invalidateShaded();
  buildLayerStack();
  renderViewport();
}

function renameLayer(id, name) {
  const l = state.layers.find((x) => x.id === id);
  if (!l) return;
  l.name = name;
  setDirty(true);
}

/* ============================================================ channel / material */
function setActiveChannel(id) {
  state.activeChannel = id;
  $("#viewport-object").textContent = channelDef(id).label;
  const sel = $("#channel-quick");
  if (sel) sel.value = id;
  buildChannelTree();
  buildBrushSettings();
  buildLayerStack();
  renderViewport();
}

function toggleChannelEnabled(id) {
  state.channelEnabled[id] = !state.channelEnabled[id];
  setDirty(true);
  invalidateShaded();
  buildChannelTree();
  renderViewport();
  updateStats();
}

function setResolution(res) {
  if (res === state.material.res) return;
  if (!confirm(`Switching to ${res}×${res} resets all painted layers. Continue?`)) return;
  state.material.res = res;
  for (const l of state.layers) l.buffers = {};
  invalidateShaded();
  setDirty(true);
  buildLayerStack();
  renderViewport();
  updateStats();
}

function setMaterialName(name) {
  state.material.name = name;
  const dn = $("#document-name");
  if (dn && dn.value !== name) dn.value = name;
  setDirty(true);
}

/* ===================================================================== decals */
function decalTransformForPoint(point) {
  const res = state.material.res;
  return {
    x: point ? point.x - res / 2 : 0,
    y: point ? point.y - res / 2 : 0,
    scale: 1,
    rotation: 0,
  };
}

function addTextDecal(opts = {}, point = null) {
  const res = state.material.res;
  const text = (opts.text || "FRONTIER").toString();
  const font = opts.font || "'DM Sans', sans-serif";
  const weight = opts.weight || 700;
  const size = opts.size || Math.round(res * 0.1);
  const color = opts.color || "#e8e8e8";
  const layer = addLayer(`Text: ${text.slice(0, 16) || "Text"}`, {
    transform: decalTransformForPoint(point),
  });
  const buf = getBuffer(layer, "baseColor");
  const ctx = buf.getContext("2d");
  ctx.clearRect(0, 0, res, res);
  ctx.fillStyle = color;
  ctx.font = `${weight} ${size}px ${font}`;
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText(text, res / 2, res / 2);
  invalidateShaded();
  setActiveChannel("baseColor");
  renderViewport();
  return layer;
}

function svgBox(svg) {
  const m = /viewBox\s*=\s*["']\s*([\d.eE+-]+)\s+([\d.eE+-]+)\s+([\d.eE+-]+)\s+([\d.eE+-]+)\s*["']/.exec(svg);
  if (m) return { w: parseFloat(m[3]), h: parseFloat(m[4]) };
  return null;
}

function addSvgDecal(svgMarkupOrUrl, name, point = null) {
  const url = svgMarkupOrUrl.startsWith("data:")
    ? svgMarkupOrUrl
    : "data:image/svg+xml;charset=utf-8," + encodeURIComponent(svgMarkupOrUrl);
  const img = new Image();
  img.onload = () => {
    const res = state.material.res;
    const layer = addLayer(name || "SVG decal", {
      transform: decalTransformForPoint(point),
    });
    const buf = getBuffer(layer, "baseColor");
    const ctx = buf.getContext("2d");
    ctx.clearRect(0, 0, res, res);
    const max = res * 0.5;
    let w = img.naturalWidth || img.width || 0;
    let h = img.naturalHeight || img.height || 0;
    const vb = svgBox(svgMarkupOrUrl);
    if (!w || !h) {
      if (vb) { w = vb.w; h = vb.h; }
      else { w = 256; h = 256; }
    }
    const s = Math.min(max / w, max / h, 1);
    w *= s; h *= s;
    ctx.drawImage(img, res / 2 - w / 2, res / 2 - h / 2, w, h);
    invalidateShaded();
    setActiveChannel("baseColor");
    renderViewport();
  };
  img.onerror = () => alert("Could not load SVG decal.");
  img.src = url;
}

function importSvgFile(file) {
  const reader = new FileReader();
  reader.onload = () => addSvgDecal(reader.result, `SVG: ${file.name}`, null);
  reader.readAsText(file);
}

/* ====================================================================== export */
function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 4000);
}

function exportChannelPNG(channelId) {
  const c = compositeChannel(channelId, true);
  c.toBlob((blob) => {
    if (blob) downloadBlob(blob, `${sanitize(state.material.name)}_${channelId}.png`);
  }, "image/png");
}

function exportAllMaps() {
  const ids = CHANNEL_DEFS.filter((c) => state.channelEnabled[c.id]).map((c) => c.id);
  ids.forEach((id, i) => setTimeout(() => exportChannelPNG(id), i * 280));
}

function sanitize(name) {
  return (name || "material").replace(/[^a-z0-9_-]+/gi, "_").toLowerCase();
}

function saveProject() {
  const res = state.material.res;
  const data = {
    app: "frontier-texture-painter",
    version: 1,
    material: { ...state.material },
    channelEnabled: { ...state.channelEnabled },
    activeChannel: state.activeChannel,
    layers: state.layers.map((l) => ({
      id: l.id,
      name: l.name,
      visible: l.visible,
      opacity: l.opacity,
      blend: l.blend,
      transform: { ...l.transform },
      buffers: Object.fromEntries(
        Object.entries(l.buffers).map(([k, c]) => [k, c.toDataURL("image/png")])
      ),
    })),
  };
  const blob = new Blob([JSON.stringify(data)], { type: "application/json" });
  downloadBlob(blob, `${sanitize(state.material.name)}.texture.json`);
  setDirty(false);
}

function loadImageToCanvas(dataUrl) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => {
      const c = makeCanvas(img.width, img.height);
      c.getContext("2d").drawImage(img, 0, 0);
      resolve(c);
    };
    img.onerror = reject;
    img.src = dataUrl;
  });
}

async function loadProject(file) {
  try {
    const text = await file.text();
    const data = JSON.parse(text);
    state.material = { ...state.material, ...data.material };
    state.channelEnabled = { ...state.channelEnabled, ...data.channelEnabled };
    state.activeChannel = data.activeChannel || "baseColor";
    state.layers = [];
    for (const ld of data.layers) {
      const layer = {
        id: ld.id || uid(),
        name: ld.name,
        visible: ld.visible,
        opacity: ld.opacity,
        blend: ld.blend,
        transform: { ...ld.transform },
        buffers: {},
      };
      state.layers.push(layer);
      for (const [k, url] of Object.entries(ld.buffers || {})) {
        layer.buffers[k] = await loadImageToCanvas(url);
      }
    }
    state.selectedLayerId = state.layers.length ? state.layers[state.layers.length - 1].id : null;
    setDirty(false);
    invalidateShaded();
    state.undo.length = 0;
    state.redo.length = 0;
    $("#document-name").value = state.material.name;
    buildChannelTree();
    buildBrushSettings();
    buildLayerStack();
    setActiveChannel(state.activeChannel);
    renderViewport();
    updateStats();
  } catch (e) {
    alert("Failed to open material file.");
  }
}

/* ====================================================================== dirty */
function setDirty(v) {
  state.dirty = v;
  const ind = $("#dirty-indicator");
  if (ind) ind.classList.toggle("clean", !v);
}

/* =============================================================== UI : channel tree */
function buildChannelTree() {
  const tree = $("#channel-tree");
  const q = state.channelSearch.toLowerCase();
  const rows = CHANNEL_DEFS.filter((c) => {
    if (state.channelFilter !== "all" && c.category !== state.channelFilter) return false;
    if (q && !c.label.toLowerCase().includes(q)) return false;
    return true;
  });
  tree.innerHTML = "";
  for (const c of rows) {
    const enabled = state.channelEnabled[c.id];
    const active = state.activeChannel === c.id;
    const row = document.createElement("div");
    row.className = "scene-row" + (active ? " selected" : "");
    row.dataset.category = c.category;
    row.dataset.channel = c.id;
    row.setAttribute("role", "treeitem");
    row.innerHTML = `
      <span class="row-icon" data-icon="${c.category === "color" ? "paint" : c.category === "scalar" ? "droplet" : "box"}"></span>
      <span class="scene-label">${c.label}</span>
      <span class="row-badge">${c.short}</span>
      <button class="row-toggle icon-button ${enabled ? "" : "off"}" data-act="enable" data-icon="${enabled ? "eye" : "eye-off"}" title="Toggle map"></button>
    `;
    row.addEventListener("click", (e) => {
      if (e.target.closest('[data-act="enable"]')) return;
      setActiveChannel(c.id);
    });
    row.querySelector('[data-act="enable"]').addEventListener("click", (e) => {
      e.stopPropagation();
      toggleChannelEnabled(c.id);
    });
    tree.appendChild(row);
  }
  applyIcons(tree);
}

/* =============================================================== UI : layer stack */
function buildLayerStack() {
  const stack = $("#layer-stack");
  stack.innerHTML = "";
  if (state.layers.length === 0) {
    stack.innerHTML = `<div class="empty-hint">No layers yet. Add one to start painting.</div>`;
  }
  // top of stack visually = end of array
  for (let i = state.layers.length - 1; i >= 0; i--) {
    const l = state.layers[i];
    const row = document.createElement("div");
    row.className = "layer-row" + (l.id === state.selectedLayerId ? " selected" : "");
    row.dataset.layer = l.id;
    const sub = `${l.blend} · ${Math.round(l.opacity * 100)}%`;
    row.innerHTML = `
      <button class="layer-vis icon-button ${l.visible ? "" : "off"}" data-act="vis" data-icon="${l.visible ? "eye" : "eye-off"}" title="Visibility"></button>
      <div class="layer-thumb"><canvas width="68" height="68"></canvas></div>
      <div class="layer-meta">
        <div class="layer-name">${escapeHtml(l.name)}</div>
        <div class="layer-sub">${sub}</div>
      </div>
      <div class="layer-actions">
        <button data-act="up" data-icon="chevron-up" title="Move up"></button>
        <button data-act="down" data-icon="chevron-down" title="Move down"></button>
        <button class="danger" data-act="del" data-icon="trash" title="Delete"></button>
      </div>
    `;
    row.addEventListener("click", (e) => {
      const act = e.target.closest("[data-act]");
      if (act) {
        e.stopPropagation();
        const a = act.dataset.act;
        if (a === "vis") toggleLayerVis(l.id);
        else if (a === "up") moveLayer(l.id, 1);
        else if (a === "down") moveLayer(l.id, -1);
        else if (a === "del") deleteLayer(l.id);
        return;
      }
      state.selectedLayerId = l.id;
      buildLayerStack();
      buildLayerProps();
    });
    stack.appendChild(row);
    updateLayerThumb(l, row.querySelector("canvas"));
  }
  buildLayerProps();
  applyIcons(stack);
}

function updateLayerThumb(layer, canvasEl) {
  if (!canvasEl) {
    const row = $(`.layer-row[data-layer="${layer.id}"]`);
    if (row) canvasEl = row.querySelector("canvas");
  }
  if (canvasEl) renderLayerThumb(layer, canvasEl);
}

function buildLayerProps() {
  const box = $("#layer-props");
  const l = selectedLayer();
  if (!l) {
    box.innerHTML = `<div class="empty-hint">Select a layer to edit its blend, opacity and decal transform.</div>`;
    return;
  }
  box.innerHTML = `
    <div class="field">
      <label>Name</label>
      <div class="control"><input type="text" id="lp-name" value="${escapeHtml(l.name)}" /></div>
    </div>
    <div class="field">
      <label>Blend</label>
      <div class="control">
        <select id="lp-blend" class="field-control">
          ${BLEND_MODES.map(([v, label]) => `<option value="${v}" ${v === l.blend ? "selected" : ""}>${label}</option>`).join("")}
        </select>
      </div>
    </div>
    <div class="field">
      <label>Opacity</label>
      <div class="control">
        <input type="range" id="lp-opacity" min="0" max="1" step="0.01" value="${l.opacity}" />
        <span class="value" id="lp-opacity-v">${Math.round(l.opacity * 100)}%</span>
      </div>
    </div>
    <div class="divider"></div>
    <div class="field"><label>Position</label><div class="control"><span class="note">X / Y offset for decals</span></div></div>
    <div class="field">
      <label>X</label>
      <div class="control"><input type="range" id="lp-x" min="${-state.material.res}" max="${state.material.res}" step="1" value="${l.transform.x}" /><span class="value" id="lp-x-v">${Math.round(l.transform.x)}</span></div>
    </div>
    <div class="field">
      <label>Y</label>
      <div class="control"><input type="range" id="lp-y" min="${-state.material.res}" max="${state.material.res}" step="1" value="${l.transform.y}" /><span class="value" id="lp-y-v">${Math.round(l.transform.y)}</span></div>
    </div>
    <div class="field">
      <label>Scale</label>
      <div class="control"><input type="range" id="lp-scale" min="0.05" max="4" step="0.01" value="${l.transform.scale}" /><span class="value" id="lp-scale-v">${l.transform.scale.toFixed(2)}×</span></div>
    </div>
    <div class="field">
      <label>Rotation</label>
      <div class="control"><input type="range" id="lp-rot" min="-180" max="180" step="1" value="${l.transform.rotation}" /><span class="value" id="lp-rot-v">${Math.round(l.transform.rotation)}°</span></div>
    </div>
    <div class="btn-row">
      <button class="button" id="lp-reset-t">Reset transform</button>
      <button class="button danger" id="lp-clear">Clear paint</button>
    </div>
  `;
  applyIcons(box);
  const recomp = () => {
    invalidateShaded();
    renderViewport();
    buildLayerStack();
  };
  $("#lp-name").addEventListener("input", (e) => renameLayer(l.id, e.target.value));
  $("#lp-blend").addEventListener("change", (e) => {
    l.blend = e.target.value;
    setDirty(true);
    recomp();
  });
  $("#lp-opacity").addEventListener("input", (e) => {
    l.opacity = parseFloat(e.target.value);
    $("#lp-opacity-v").textContent = Math.round(l.opacity * 100) + "%";
    setDirty(true);
    recomp();
  });
  const tbind = (id, key, fmt) => {
    $(id).addEventListener("input", (e) => {
      l.transform[key] = parseFloat(e.target.value);
      $(id + "-v").textContent = fmt(l.transform[key]);
      setDirty(true);
      recomp();
    });
  };
  tbind("#lp-x", "x", (v) => Math.round(v));
  tbind("#lp-y", "y", (v) => Math.round(v));
  tbind("#lp-scale", "scale", (v) => v.toFixed(2) + "×");
  tbind("#lp-rot", "rotation", (v) => Math.round(v) + "°");
  $("#lp-reset-t").addEventListener("click", () => {
    l.transform = { x: 0, y: 0, scale: 1, rotation: 0 };
    buildLayerProps();
    recomp();
  });
  $("#lp-clear").addEventListener("click", () => {
    for (const k of Object.keys(l.buffers)) {
      l.buffers[k].getContext("2d").clearRect(0, 0, state.material.res, state.material.res);
    }
    invalidateShaded();
    setDirty(true);
    buildLayerStack();
    renderViewport();
  });
}

/* =============================================================== UI : brush library */
function brushSwatch(preset) {
  const c = makeCanvas(38, 38);
  const ctx = c.getContext("2d");
  const r = 16;
  const inner = clamp(preset.hardness, 0, 0.999);
  const g = ctx.createRadialGradient(19, 19, 0, 19, 19, r);
  g.addColorStop(0, "rgba(224,224,224,1)");
  g.addColorStop(inner, "rgba(224,224,224,1)");
  g.addColorStop(1, "rgba(224,224,224,0)");
  ctx.fillStyle = g;
  ctx.beginPath();
  ctx.arc(19, 19, r, 0, Math.PI * 2);
  ctx.fill();
  return c.toDataURL();
}

function buildBrushLibrary() {
  const list = $("#brush-library");
  list.innerHTML = "";
  BRUSH_PRESETS.forEach((p) => {
    const card = document.createElement("button");
    card.className = "preset-card";
    card.innerHTML = `
      <span class="preset-swatch"><img src="${brushSwatch(p)}" alt=""></span>
      <span class="preset-copy"><strong>${p.name}</strong><small>${p.size}px · ${Math.round(p.hardness * 100)}% hard</small></span>
    `;
    card.addEventListener("click", () => {
      Object.assign(state.brush, {
        size: p.size,
        hardness: p.hardness,
        opacity: p.opacity,
        flow: p.flow,
        spacing: p.spacing,
      });
      setTool("brush");
      buildBrushSettings();
    });
    list.appendChild(card);
  });
  $("#brush-count").textContent = BRUSH_PRESETS.length;
}

/* =============================================================== UI : decal library */
function buildDecalLibrary() {
  const list = $("#decal-library");
  list.innerHTML = "";
  DECAL_PRESETS.forEach((d) => {
    const card = document.createElement("button");
    card.className = "preset-card";
    card.innerHTML = `
      <span class="preset-swatch">${d.svg}</span>
      <span class="preset-copy"><strong>${d.name}</strong><small>SVG decal</small></span>
    `;
    card.addEventListener("click", () => addSvgDecal(d.svg, `SVG: ${d.name}`, null));
    list.appendChild(card);
  });
  $("#decal-count").textContent = DECAL_PRESETS.length;
  applyIcons(list);
}

/* =============================================================== UI : brush settings */
function buildBrushSettings() {
  const box = $("#brush-settings");
  const def = channelDef(state.activeChannel);
  const isRgb = def.type === "rgb";
  const isScalar = def.type === "scalar";
  box.innerHTML = `
    <div class="field">
      <label>Tool</label>
      <div class="control"><span class="note" id="bs-tool">${state.tool}</span></div>
    </div>
    <div class="field">
      <label>Painting</label>
      <div class="control"><span class="note">${def.label} (${def.type})</span></div>
    </div>
    <div class="field">
      <label>Size</label>
      <div class="control"><input type="range" id="bs-size" min="1" max="400" step="1" value="${state.brush.size}" /><span class="value" id="bs-size-v">${state.brush.size}</span></div>
    </div>
    <div class="field">
      <label>Hardness</label>
      <div class="control"><input type="range" id="bs-hard" min="0" max="1" step="0.01" value="${state.brush.hardness}" /><span class="value" id="bs-hard-v">${Math.round(state.brush.hardness * 100)}%</span></div>
    </div>
    <div class="field">
      <label>Opacity</label>
      <div class="control"><input type="range" id="bs-op" min="0" max="1" step="0.01" value="${state.brush.opacity}" /><span class="value" id="bs-op-v">${Math.round(state.brush.opacity * 100)}%</span></div>
    </div>
    <div class="field">
      <label>Flow</label>
      <div class="control"><input type="range" id="bs-flow" min="0.02" max="1" step="0.01" value="${state.brush.flow}" /><span class="value" id="bs-flow-v">${Math.round(state.brush.flow * 100)}%</span></div>
    </div>
    <div class="field">
      <label>Spacing</label>
      <div class="control"><input type="range" id="bs-space" min="0.02" max="1" step="0.01" value="${state.brush.spacing}" /><span class="value" id="bs-space-v">${Math.round(state.brush.spacing * 100)}%</span></div>
    </div>
    ${isRgb ? `
    <div class="divider"></div>
    <div class="field">
      <label>Color</label>
      <div class="control"><input type="color" id="bs-color" value="${state.brush.color}" /></div>
    </div>` : ""}
    ${isScalar ? `
    <div class="divider"></div>
    <div class="field">
      <label>Value</label>
      <div class="control"><input type="range" id="bs-value" min="0" max="1" step="0.01" value="${state.brush.value}" /><span class="value" id="bs-value-v">${state.brush.value.toFixed(2)}</span></div>
    </div>` : ""}
    ${def.type === "normal" ? `<div class="note">Normal map painting writes tangent-space blue (128,128,255) — use decals or sculpt for direction.</div>` : ""}
  `;
  const bind = (id, key, fmt, after) => {
    const el = $(id);
    if (!el) return;
    el.addEventListener("input", (e) => {
      state.brush[key] = parseFloat(e.target.value);
      const v = $(id + "-v");
      if (v) v.textContent = fmt(state.brush[key]);
      if (after) after();
    });
  };
  bind("#bs-size", "size", (v) => Math.round(v));
  bind("#bs-hard", "hardness", (v) => Math.round(v * 100) + "%");
  bind("#bs-op", "opacity", (v) => Math.round(v * 100) + "%");
  bind("#bs-flow", "flow", (v) => Math.round(v * 100) + "%");
  bind("#bs-space", "spacing", (v) => Math.round(v * 100) + "%");
  bind("#bs-value", "value", (v) => v.toFixed(2));
  const col = $("#bs-color");
  if (col) col.addEventListener("input", (e) => (state.brush.color = e.target.value));
}

/* =============================================================== UI : material settings */
function buildMaterialSettings() {
  const box = $("#material-settings");
  box.innerHTML = `
    <div class="field">
      <label>Name</label>
      <div class="control"><input type="text" id="ms-name" value="${escapeHtml(state.material.name)}" /></div>
    </div>
    <div class="field">
      <label>Resolution</label>
      <div class="control">
        <select id="ms-res" class="field-control">
          ${[512, 1024, 2048].map((r) => `<option value="${r}" ${r === state.material.res ? "selected" : ""}>${r} × ${r}</option>`).join("")}
        </select>
      </div>
    </div>
    <div class="field">
      <label>Tiling</label>
      <div class="control"><input type="range" id="ms-tile" min="1" max="8" step="1" value="${state.material.tiling}" /><span class="value" id="ms-tile-v">${state.material.tiling}×</span></div>
    </div>
    <div class="divider"></div>
    <label class="toggle ${CHANNEL_DEFS.every((c) => state.channelEnabled[c.id]) ? "on" : ""}" id="ms-all">
      <span class="tick"></span>Export all maps
    </label>
    <div class="map-export-grid" id="ms-maps">
      ${CHANNEL_DEFS.map((c) => `<button class="button" data-map="${c.id}">${c.label}<small>${c.short}</small></button>`).join("")}
    </div>
    <div class="btn-row" style="margin-top:10px">
      <button class="button accent" id="ms-export-all">Export all PNG</button>
      <button class="button" id="ms-save">Save project</button>
    </div>
    <div class="note">Each map is exported as a 16-bit-free PNG named &lt;material&gt;_&lt;map&gt;.png. “Save project” bundles every layer and map into a single .texture.json you can reopen.</div>
  `;
  applyIcons(box);
  $("#ms-name").addEventListener("input", (e) => setMaterialName(e.target.value));
  $("#ms-res").addEventListener("change", (e) => setResolution(parseInt(e.target.value, 10)));
  $("#ms-tile").addEventListener("input", (e) => {
    state.material.tiling = parseInt(e.target.value, 10);
    $("#ms-tile-v").textContent = state.material.tiling + "×";
    invalidateShaded();
    if (state.view.mode === "shaded") renderViewport();
  });
  $("#ms-all").addEventListener("click", () => {
    const turnOn = !CHANNEL_DEFS.every((c) => state.channelEnabled[c.id]);
    CHANNEL_DEFS.forEach((c) => (state.channelEnabled[c.id] = turnOn));
    $("#ms-all").classList.toggle("on", turnOn);
    buildChannelTree();
    renderViewport();
    updateStats();
  });
  box.querySelectorAll("[data-map]").forEach((b) =>
    b.addEventListener("click", () => exportChannelPNG(b.dataset.map))
  );
  $("#ms-export-all").addEventListener("click", exportAllMaps);
  $("#ms-save").addEventListener("click", saveProject);
}

/* =============================================================== UI : decal settings */
function buildDecalSettings() {
  const box = $("#decal-settings");
  const res = state.material.res;
  box.innerHTML = `
    <div class="decal-preview" id="ds-preview"></div>
    <div class="field">
      <label>Text</label>
      <div class="control"><input type="text" id="ds-text" value="FRONTIER" maxlength="40" /></div>
    </div>
    <div class="field">
      <label>Font</label>
      <div class="control">
        <select id="ds-font" class="field-control">
          ${["'DM Sans', sans-serif", "Arial, sans-serif", "Georgia, serif", "'Courier New', monospace", "Impact, sans-serif", "Verdana, sans-serif"].map((f) => `<option value="${f}">${f.split(",")[0].replace(/'/g, "")}</option>`).join("")}
        </select>
      </div>
    </div>
    <div class="field">
      <label>Weight</label>
      <div class="control">
        <select id="ds-weight" class="field-control">
          ${[300, 400, 500, 700].map((w) => `<option value="${w}" ${w === 700 ? "selected" : ""}>${w}</option>`).join("")}
        </select>
      </div>
    </div>
    <div class="field">
      <label>Size</label>
      <div class="control"><input type="range" id="ds-size" min="20" max="${res}" step="2" value="${Math.round(res * 0.12)}" /><span class="value" id="ds-size-v">${Math.round(res * 0.12)}</span></div>
    </div>
    <div class="field">
      <label>Color</label>
      <div class="control"><input type="color" id="ds-color" value="#e8e8e8" /></div>
    </div>
    <div class="btn-row">
      <button class="button accent" id="ds-add-text">Add text layer</button>
    </div>
    <div class="divider"></div>
    <div class="field">
      <label>SVG file</label>
      <div class="control">
        <input type="file" id="ds-svg-file" accept=".svg,image/svg+xml" />
      </div>
    </div>
    <div class="btn-row">
      <button class="button" id="ds-add-svg">Import SVG decal</button>
    </div>
    <div class="note">Tip: pick the Text or SVG tool in the viewport and click to drop the decal exactly where you want it. Move, scale and rotate it from the selected layer’s transform controls.</div>
  `;
  applyIcons(box);
  const preview = () => {
    const text = $("#ds-text").value || "FRONTIER";
    const size = parseInt($("#ds-size").value, 10);
    const color = $("#ds-color").value;
    const pv = $("#ds-preview");
    pv.innerHTML = "";
    const c = makeCanvas(256, 96);
    const ctx = c.getContext("2d");
    ctx.fillStyle = color;
    ctx.font = `${$("#ds-weight").value} ${Math.round((size / res) * c.height * 2.2)}px ${$("#ds-font").value}`;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(text, c.width / 2, c.height / 2);
    c.style.width = "100%";
    c.style.height = "100%";
    pv.appendChild(c);
  };
  const bindSize = () => {
    $("#ds-size").addEventListener("input", (e) => {
      $("#ds-size-v").textContent = e.target.value;
      preview();
    });
  };
  ["#ds-text", "#ds-color", "#ds-weight", "#ds-font"].forEach((s) =>
    $(s).addEventListener("input", preview)
  );
  bindSize();
  preview();

  $("#ds-add-text").addEventListener("click", () => {
    addTextDecal({
      text: $("#ds-text").value,
      font: $("#ds-font").value,
      weight: parseInt($("#ds-weight").value, 10),
      size: parseInt($("#ds-size").value, 10),
      color: $("#ds-color").value,
    });
  });
  $("#ds-svg-file").addEventListener("change", (e) => {
    if (e.target.files[0]) importSvgFile(e.target.files[0]);
    e.target.value = "";
  });
  $("#ds-add-svg").addEventListener("click", () => $("#ds-svg-file").click());
}

/* ======================================================================= stats */
function updateStats() {
  $("#channel-count").textContent = CHANNEL_DEFS.length;
  $("#enabled-count").textContent = CHANNEL_DEFS.filter((c) => state.channelEnabled[c.id]).length;
  $("#layer-count").textContent = state.layers.length;
  $("#hud-res").textContent = state.material.res;
}

/* ======================================================================= tools */
function setTool(tool) {
  state.tool = tool;
  $$("#tool-dock [data-tool]").forEach((b) => b.classList.toggle("active", b.dataset.tool === tool));
  if (tool === "text" || tool === "svg") {
    $("#inspector").scrollTo({ top: $("#inspector").scrollHeight, behavior: "smooth" });
  }
  updateCursor();
}

function updateCursor() {
  const show = state.tool !== "eyedropper" && state.tool !== "fill";
  brushCursor.style.display = show ? "block" : "none";
}

/* ======================================================================= events */
function wireEvents() {
  // tool dock
  $$("#tool-dock [data-tool]").forEach((b) =>
    b.addEventListener("click", () => setTool(b.dataset.tool))
  );

  // viewport pointer painting
  let pointerActive = false;
  const getPoint = (e) => toTexture(e.clientX, e.clientY);
  paintSurface.addEventListener("pointerdown", (e) => {
    paintSurface.setPointerCapture(e.pointerId);
    pointerActive = true;
    const p = getPoint(e);
    if (state.tool === "fill") {
      const l = selectedLayer();
      if (l) floodFill(l, state.activeChannel, p.x, p.y);
      pointerActive = false;
      return;
    }
    if (state.tool === "eyedropper") {
      eyedrop(p);
      pointerActive = false;
      return;
    }
    if (state.tool === "text") {
      addTextDecal(
        {
          text: $("#ds-text").value,
          font: $("#ds-font").value,
          weight: parseInt($("#ds-weight").value, 10),
          size: parseInt($("#ds-size").value, 10),
          color: $("#ds-color").value,
        },
        p
      );
      pointerActive = false;
      return;
    }
    if (state.tool === "svg") {
      const d = DECAL_PRESETS[0];
      addSvgDecal(d.svg, `SVG: ${d.name}`, p);
      pointerActive = false;
      return;
    }
    beginStroke(p);
  });
  paintSurface.addEventListener("pointermove", (e) => {
    const p = getPoint(e);
    updateBrushCursor(e);
    $("#hud-cursor").textContent = `${Math.round(p.x)}, ${Math.round(p.y)}`;
    if (!pointerActive) return;
    if (painting) strokeTo(p);
  });
  const endPointer = () => {
    if (painting) endStroke();
    pointerActive = false;
  };
  paintSurface.addEventListener("pointerup", endPointer);
  paintSurface.addEventListener("pointercancel", endPointer);
  paintSurface.addEventListener("pointerleave", () => {
    brushCursor.style.display = "none";
  });
  paintSurface.addEventListener("pointerenter", () => updateCursor());

  // brush cursor follow
  function updateBrushCursor(e) {
    if (state.tool === "eyedropper" || state.tool === "fill") {
      brushCursor.style.display = "none";
      return;
    }
    const rect = paintSurface.getBoundingClientRect();
    const d = (state.brush.size * fit.scale);
    brushCursor.style.width = d + "px";
    brushCursor.style.height = d + "px";
    brushCursor.style.left = e.clientX - rect.left + "px";
    brushCursor.style.top = e.clientY - rect.top + "px";
    brushCursor.style.display = "block";
  }

  // view mode segmented
  $$("#view-mode button").forEach((b) =>
    b.addEventListener("click", () => {
      state.view.mode = b.dataset.mode;
      $$("#view-mode button").forEach((x) => x.classList.toggle("active", x === b));
      invalidateShaded();
      renderViewport();
    })
  );
  $("#grid-toggle").addEventListener("click", () => {
    state.view.grid = !state.view.grid;
    $("#grid-toggle").classList.toggle("active", state.view.grid);
    renderViewport();
  });
  const zoomBy = (f) => {
    state.view.zoom = clamp(state.view.zoom * f, 0.1, 8);
    updateZoomHud();
    renderViewport();
  };
  $("#zoom-in").addEventListener("click", () => zoomBy(1.25));
  $("#zoom-out").addEventListener("click", () => zoomBy(1 / 1.25));
  $("#zoom-reset").addEventListener("click", () => {
    state.view.zoom = 1;
    updateZoomHud();
    renderViewport();
  });

  // channel quick switch
  const quick = $("#channel-quick");
  quick.innerHTML = CHANNEL_DEFS.map((c) => `<option value="${c.id}">${c.label}</option>`).join("");
  quick.value = state.activeChannel;
  quick.addEventListener("change", () => setActiveChannel(quick.value));

  // left panel filters + search
  $$("#channel-filters button").forEach((b) =>
    b.addEventListener("click", () => {
      state.channelFilter = b.dataset.filter;
      $$("#channel-filters button").forEach((x) => {
        const on = x === b;
        x.classList.toggle("active", on);
        x.setAttribute("aria-pressed", on);
      });
      buildChannelTree();
    })
  );
  $("#channel-search").addEventListener("input", (e) => {
    state.channelSearch = e.target.value;
    buildChannelTree();
  });

  // collection toggle (collapse channel tree)
  $("#collection-toggle").addEventListener("click", () => {
    const expanded = $("#collection-toggle").getAttribute("aria-expanded") === "true";
    $("#collection-toggle").setAttribute("aria-expanded", String(!expanded));
    $("#channel-tree").hidden = expanded;
  });
  $("#compact-outliner").addEventListener("click", () => {
    const pressed = $("#compact-outliner").getAttribute("aria-pressed") === "true";
    $("#compact-outliner").setAttribute("aria-pressed", String(!pressed));
    $$(".library").forEach((el) => (el.style.display = pressed ? "flex" : "none"));
  });
  $("#add-channel").addEventListener("click", () => addLayer());
  $("#add-layer").addEventListener("click", () => addLayer());

  // header
  $("#export-button").addEventListener("click", saveProject);
  $("#import-button").addEventListener("click", () => $("#import-file").click());
  $("#import-file").addEventListener("change", (e) => {
    if (e.target.files[0]) loadProject(e.target.files[0]);
    e.target.value = "";
  });
  $("#document-name").addEventListener("input", (e) => setMaterialName(e.target.value));
  $("#help-button").addEventListener("click", showHelp);
  $("#workspace-button").addEventListener("click", () => {
    $("#workspace-button").classList.add("active");
    $("#library-button").classList.remove("active");
  });
  $("#library-button").addEventListener("click", () => {
    $("#library-button").classList.add("active");
    $("#workspace-button").classList.remove("active");
    $("#inspector").scrollTo({ top: $("#inspector").scrollHeight, behavior: "smooth" });
  });

  // keyboard
  window.addEventListener("keydown", (e) => {
    const tag = (e.target.tagName || "").toLowerCase();
    if (tag === "input" || tag === "textarea" || tag === "select") return;
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") {
      e.preventDefault();
      undo();
      return;
    }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "y") {
      e.preventDefault();
      redo();
      return;
    }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") {
      e.preventDefault();
      saveProject();
      return;
    }
    switch (e.key.toLowerCase()) {
      case "b": setTool("brush"); break;
      case "e": setTool("eraser"); break;
      case "g": setTool("fill"); break;
      case "i": setTool("eyedropper"); break;
      case "t": setTool("text"); break;
      case "s": setTool("svg"); break;
      case "[": state.brush.size = clamp(state.brush.size - 8, 1, 400); buildBrushSettings(); break;
      case "]": state.brush.size = clamp(state.brush.size + 8, 1, 400); buildBrushSettings(); break;
      case "/": e.preventDefault(); $("#channel-search").focus(); break;
    }
  });

  // resize
  const ro = new ResizeObserver(() => renderViewport());
  ro.observe(viewportEl);
}

function updateZoomHud() {
  $("#hud-zoom").textContent = Math.round(state.view.zoom * 100) + "%";
}

/* ======================================================================= undo */
function undo() {
  const u = state.undo.pop();
  if (!u) return;
  const layer = state.layers.find((l) => l.id === u.layerId) || selectedLayer();
  if (!layer) return;
  const current = snapshotOf(layer, u.channelId);
  applySnapshot(layer, u.channelId, u.before);
  state.redo.push({ layerId: u.layerId, channelId: u.channelId, before: current, after: u.after });
  invalidateShaded();
  setDirty(true);
  renderViewport();
  buildLayerStack();
}
function redo() {
  const u = state.redo.pop();
  if (!u) return;
  const layer = state.layers.find((l) => l.id === u.layerId) || selectedLayer();
  if (!layer) return;
  const current = snapshotOf(layer, u.channelId);
  applySnapshot(layer, u.channelId, u.after);
  state.undo.push({ layerId: u.layerId, channelId: u.channelId, before: current, after: u.after });
  invalidateShaded();
  setDirty(true);
  renderViewport();
  buildLayerStack();
}

/* ======================================================================= help */
function showHelp() {
  const scrim = document.createElement("div");
  scrim.className = "modal-scrim";
  scrim.innerHTML = `
    <div class="modal">
      <h2>Texture Paint Studio</h2>
      <p class="sub">Frontier experimental · layered PBR material authoring</p>
      <ul>
        <li><b>Material maps</b> Pick a map in the left outliner (Base Color, Roughness, Metallic, Normal, Height, Emissive) to paint into it. The material system composites every enabled map.</li>
        <li><b>Layer stack</b> Stack paint layers with per-layer opacity, blend mode and transform. Decals are just layers you can move, scale and rotate.</li>
        <li><b>Decals</b> Drop SVG files or type text. Use the Text / SVG tools in the viewport to place them, then nudge them in the layer transform.</li>
        <li><b>Material view</b> Toggle “Material” in the viewport bar for a live shaded PBR preview (tiling-aware).</li>
        <li><b>Export</b> Save each map as PNG, or bundle the whole project to a .texture.json.</li>
      </ul>
      <ul>
        <li><b>B / E / G / I</b> brush · eraser · fill · eyedropper</li>
        <li><b>T / S</b> text decal · svg decal</li>
        <li><b>[ ]</b> brush size &nbsp; <b>Ctrl+Z</b> undo &nbsp; <b>Ctrl+S</b> save</li>
      </ul>
      <button class="button accent" id="help-close">Got it</button>
    </div>
  `;
  document.body.appendChild(scrim);
  scrim.addEventListener("click", (e) => {
    if (e.target === scrim || e.target.id === "help-close") scrim.remove();
  });
}

/* ======================================================================= utils */
function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

/* -------------------------------------------------------------------- seed */
function seedDefaultContent(layer) {
  const res = state.material.res;
  const ctx = getBuffer(layer, "baseColor").getContext("2d");
  ctx.fillStyle = "#1a1410";
  ctx.fillRect(0, 0, res, res);
  const g = ctx.createRadialGradient(res * 0.5, res * 0.44, res * 0.04, res * 0.5, res * 0.5, res * 0.72);
  g.addColorStop(0, "#d9772f");
  g.addColorStop(0.45, "#8a3f1d");
  g.addColorStop(1, "#19120d");
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, res, res);

  const rb = getBuffer(layer, "roughness").getContext("2d");
  rb.fillStyle = "#6e6e6e";
  rb.fillRect(0, 0, res, res);
  const rg = rb.createRadialGradient(res * 0.5, res * 0.5, res * 0.08, res * 0.5, res * 0.5, res * 0.72);
  rg.addColorStop(0, "#d2d2d2");
  rg.addColorStop(1, "#3c3c3c");
  rb.fillStyle = rg;
  rb.fillRect(0, 0, res, res);
}

/* ======================================================================= init */
function init() {
  applyIcons(document);
  // seed a starting paint layer with a visible starter material
  const base = addLayer("Base layer");
  seedDefaultContent(base);
  buildChannelTree();
  buildBrushLibrary();
  buildDecalLibrary();
  buildBrushSettings();
  buildMaterialSettings();
  buildDecalSettings();
  buildLayerStack();
  updateStats();
  updateZoomHud();
  setActiveChannel("baseColor");
  wireEvents();
  setTool("brush");
  requestAnimationFrame(() => renderViewport());
}

try {
  init();
} catch (e) {
  showFatalError((e && e.stack) || String(e));
  throw e;
}
