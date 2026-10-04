/* Headless smoke harness: stub DOM + Canvas2D, run the real TexturePanel. */
import fs from "node:fs";

import { fileURLToPath } from "node:url";
import path from "node:path";
const ROOT = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const INDEX = fs.readFileSync(path.join(ROOT, "index.html"), "utf8");

/* ---------- 2D context stub ---------- */
function makeImageData(w, h) {
  return { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) };
}
class GradientStub {
  addColorStop() {}
}
class Context2DStub {
  constructor(canvas) {
    this.canvas = canvas;
    this.fillStyle = "#000";
    this.strokeStyle = "#000";
    this.globalAlpha = 1;
    this.globalCompositeOperation = "source-over";
    this.lineWidth = 1;
    this.font = "10px sans-serif";
    this.textAlign = "left";
    this.textBaseline = "alphabetic";
    this.imageSmoothingEnabled = true;
    this.imageSmoothingQuality = "high";
    this.filter = "none";
    this.shadowColor = "transparent";
    this.shadowBlur = 0;
  }
  save() {} restore() {} clearRect() {} fillRect() {} strokeRect() {}
  drawImage() {} fillText() {} strokeText() {}
  beginPath() {} arc() {} ellipse() {} moveTo() {} lineTo() {} rect() {} clip() {} fill() {} stroke() {}
  setTransform() {} translate() {} rotate() {} scale() {}
  setLineDash() {} getLineDash() { return []; }
  measureText() { return { width: 42 }; }
  getImageData(x, y, w, h) { return makeImageData(w, h); }
  putImageData() {}
  createImageData(w, h) { return makeImageData(w, h); }
  createLinearGradient() { return new GradientStub(); }
  createRadialGradient() { return new GradientStub(); }
}

/* ---------- Element stub ---------- */
const registry = [];
function deregister(el) {
  const i = registry.indexOf(el);
  if (i >= 0) registry.splice(i, 1);
  for (const child of el.children) deregister(child);
}

function parseAttrs(src) {
  const attrs = {};
  const re = /([a-zA-Z-]+)(?:=(?:"([^"]*)"|'([^']*)'|([^\s>]+)))?/g;
  let m;
  while ((m = re.exec(src))) {
    attrs[m[1].toLowerCase()] = m[2] ?? m[3] ?? m[4 ?? ""];
    if (attrs[m[1].toLowerCase()] === undefined) attrs[m[1].toLowerCase()] = "";
  }
  return attrs;
}

class ClassList {
  constructor() { this.set = new Set(); }
  add(...c) { c.forEach((x) => this.set.add(x)); }
  remove(...c) { c.forEach((x) => this.set.delete(x)); }
  toggle(c, force) {
    if (force === undefined) { this.set.has(c) ? this.set.delete(c) : this.set.add(c); }
    else { force ? this.set.add(c) : this.set.delete(c); }
    return this.set.has(c);
  }
  contains(c) { return this.set.has(c); }
}

function matchToken(el, token) {
  token = token.trim();
  if (!token) return false;
  let tag = null, id = null, cls = null, attr = null, attrVal = undefined;
  const attrMatch = token.match(/\[([^\]=]+)(?:="?([^"\]]*)"?)?\]/);
  if (attrMatch) {
    attr = attrMatch[1];
    attrVal = attrMatch[2];
    token = token.replace(attrMatch[0], "");
  }
  const idMatch = token.match(/#([A-Za-z0-9\-_]+)/);
  if (idMatch) { id = idMatch[1]; token = token.replace(idMatch[0], ""); }
  const clsMatch = token.match(/\.([A-Za-z0-9\-_]+)/);
  if (clsMatch) { cls = clsMatch[1]; token = token.replace(clsMatch[0], ""); }
  token = token.trim();
  if (token) tag = token.toUpperCase();
  if (tag && el.tagName !== tag) return false;
  if (id && el.id !== id) return false;
  if (cls && !el.classList.contains(cls)) return false;
  if (attr) {
    if (attr.startsWith("data-")) {
      const key = attr.slice(5).replace(/-([a-z])/g, (_, c) => c.toUpperCase());
      if (!(key in el.dataset)) return false;
      if (attrVal !== undefined && el.dataset[key] !== attrVal) return false;
    } else {
      return false;
    }
  }
  return true;
}

class ElementStub {
  constructor(tag) {
    this.tagName = String(tag).toUpperCase();
    this.children = [];
    this.listeners = {};
    this.classList = new ClassList();
    this.dataset = {};
    this.style = { setProperty() {} };
    this.id = "";
    this._innerHTML = "";
    this.textContent = "";
    this.value = "";
    this.checked = false;
    this.disabled = false;
    this.hidden = false;
    this.title = "";
    this.width = 0;
    this.height = 0;
    this.parent = null;
    registry.push(this);
  }
  set innerHTML(html) {
    for (const child of this.children) deregister(child);
    this.children = [];
    this._innerHTML = String(html);
    const VOIDS = new Set(["input", "br", "hr", "img", "link", "meta", "source", "wbr", "path", "circle", "rect", "ellipse", "line", "progress"]);
    const re = /<\/?([a-zA-Z][a-zA-Z0-9]*)((?:\s+[a-zA-Z][a-zA-Z0-9\-]*(?:=(?:"[^"]*"|'[^']*'|[^\s>]+))?)*)\s*(\/?)>/g;
    const stack = [this];
    let m;
    while ((m = re.exec(this._innerHTML))) {
      const full = m[0];
      const tag = m[1].toLowerCase();
      if (full.startsWith("</")) {
        if (stack.length > 1) stack.pop();
        continue;
      }
      const parent = stack[stack.length - 1];
      const child = new ElementStub(m[1]);
      const attrs = parseAttrs(m[2] || "");
      if (attrs.id) child.id = attrs.id;
      if (attrs.class) attrs.class.split(/\s+/).filter(Boolean).forEach((c) => child.classList.add(c));
      for (const [k, v] of Object.entries(attrs)) {
        if (k.startsWith("data-")) {
          const key = k.slice(5).replace(/-([a-z])/g, (_, c) => c.toUpperCase());
          child.dataset[key] = v;
        }
        if (k === "value") child.value = v;
        if (k === "selected") child.selected = true;
        if (k === "checked") child.checked = true;
      }
      child.parent = parent;
      parent.children.push(child);
      const selfClosing = m[3] === "/" || VOIDS.has(tag);
      if (!selfClosing) stack.push(child);
    }
  }
  get innerHTML() { return this._innerHTML; }
  appendChild(child) { child.parent = this; this.children.push(child); return child; }
  remove() { deregister(this); }
  replaceWith(other) {
    if (!this.parent) return;
    const i = this.parent.children.indexOf(this);
    if (i >= 0) this.parent.children[i] = other;
    other.parent = this.parent;
    deregister(this);
  }
  _findById(id) {
    let found = null;
    const walk = (el) => {
      for (const child of el.children) {
        if (child.id === id) { found = child; return; }
        walk(child);
        if (found) return;
      }
    };
    walk(this);
    return found;
  }
  _scope(sel) {
    let tokens = sel.trim().split(/\s+/);
    let scope = [this];
    if (tokens[0].startsWith("#")) {
      const found = this._findById(tokens[0].slice(1));
      if (!found) return { scope: [], last: "" };
      scope = [found];
      tokens = tokens.slice(1);
      if (!tokens.length) return { scope: [found.parent || this], last: "#" + found.id };
    }
    return { scope, last: tokens[tokens.length - 1] };
  }
  querySelector(sel) {
    const { scope, last } = this._scope(sel);
    if (!last) return null;
    let result = null;
    const walk = (el) => {
      for (const child of el.children) {
        if (matchToken(child, last)) { result = child; return; }
        walk(child);
        if (result) return;
      }
    };
    for (const s of scope) { walk(s); if (result) return result; }
    if (sel.trim().startsWith("#") && !sel.includes(" ")) return this._findById(sel.trim().slice(1));
    return result;
  }
  querySelectorAll(sel) {
    const { scope, last } = this._scope(sel);
    const out = [];
    if (!last) return out;
    const walk = (el) => {
      for (const child of el.children) {
        if (matchToken(child, last)) out.push(child);
        walk(child);
      }
    };
    for (const s of scope) walk(s);
    return out;
  }
  addEventListener(type, fn) { (this.listeners[type] = this.listeners[type] || []).push(fn); }
  removeEventListener(type, fn) {
    if (!this.listeners[type]) return;
    this.listeners[type] = this.listeners[type].filter((f) => f !== fn);
  }
  _fire(type, props = {}) {
    const evt = Object.assign({
      type, target: this, currentTarget: this, button: 0,
      clientX: 100, clientY: 100, deltaY: 0, key: "", pressure: 0.5,
      pointerType: "mouse", pointerId: 1, buttons: 1,
      ctrlKey: false, shiftKey: false, altKey: false, metaKey: false,
      preventDefault() {}, stopPropagation() {},
    }, props);
    for (const fn of this.listeners[type] || []) fn(evt);
    return evt;
  }
  getBoundingClientRect() { return { width: 900, height: 700, left: 0, top: 0, right: 900, bottom: 700 }; }
  setPointerCapture() {}
  click() { this._fire("click"); }
  focus() {} blur() {} select() {}
  showModal() { this._open = true; }
  close() { this._open = false; }
  get open() { return !!this._open; }
  closest() { return null; }
  setAttribute(name, value) {
    name = String(name).toLowerCase();
    if (name === "aria-pressed" || name === "aria-expanded" || name === "aria-selected" || name === "aria-label" || name === "role") return;
    if (name === "disabled") this.disabled = true;
    if (name === "hidden") this.hidden = true;
  }
  getAttribute(name) {
    name = String(name).toLowerCase();
    if (name === "aria-expanded") return this._ariaExpanded || "true";
    if (name === "aria-pressed") return "false";
    return null;
  }
  removeAttribute() {}
  toggleAttribute(name, force) {
    if (name === "hidden") this.hidden = force !== undefined ? force : !this.hidden;
  }
  hasAttribute() { return false; }
  getContext(kind) {
    if (this.tagName !== "CANVAS") return null;
    if (kind === "2d") return new Context2DStub(this);
    return null; // no webgl in harness
  }
  toDataURL() { return "data:image/png;base64,"; }
}

/* ---------- document / window ---------- */
const root = new ElementStub("div");
root.innerHTML = INDEX;

const documentStub = {
  querySelector: (sel) => {
    if (sel.trim().startsWith("#") && !sel.includes(" ") && !sel.includes(".") && !sel.includes("[")) {
      const id = sel.trim().slice(1);
      return registry.find((el) => el.id === id) || null;
    }
    const tokens = sel.trim().split(/\s+/);
    const last = tokens[tokens.length - 1];
    return registry.find((el) => matchToken(el, last)) || null;
  },
  querySelectorAll: (sel) => {
    const tokens = sel.trim().split(/\s+/);
    const last = tokens[tokens.length - 1];
    return registry.filter((el) => matchToken(el, last));
  },
  createElement: (tag) => new ElementStub(tag),
  body: null,
  fonts: { ready: Promise.resolve() },
  activeElement: null,
  addEventListener: () => {},
};
documentStub.body = documentStub.querySelector("body") || new ElementStub("body");

class ImageStub {
  constructor() { this._src = ""; this.naturalWidth = 64; this.naturalHeight = 64; this.onload = null; this.onerror = null; }
  set src(v) {
    this._src = v;
    setTimeout(() => { if (this.onload) this.onload(); }, 0);
  }
  get src() { return this._src; }
}

let rafCallback = null;
globalThis.requestAnimationFrame = (cb) => { rafCallback = cb; return 1; };
globalThis.document = documentStub;
globalThis.window = {
  devicePixelRatio: 1,
  Image: ImageStub,
  Blob: class {}, URL: { createObjectURL: () => "blob:x", revokeObjectURL: () => {} },
  addEventListener: () => {},
};
globalThis.ResizeObserver = class { constructor() {} observe() {} disconnect() {} };
globalThis.Blob = globalThis.window.Blob;
globalThis.URL = globalThis.window.URL;
globalThis.Image = ImageStub;

function pumpFrames(panel, n) {
  let t = 1000;
  for (let i = 0; i < n; i++) {
    t += 32;
    const cb = rafCallback;
    rafCallback = null;
    if (!cb) break;
    cb(t);
  }
}

/* ---------- Run ---------- */
const errors = [];
process.on("uncaughtException", (e) => { console.log("FATAL:\n" + (e && e.stack || e)); process.exit(1); });

const { TexturePanel } = await import(path.join(ROOT, "src", "TexturePanel.js"));
console.log("import OK");

const panel = new TexturePanel();
console.log("construct OK, docs:", panel.documents.length, "layers:", panel.doc.layers.length);
if (panel.view.mode !== "3d") throw new Error("expected 3D default view, got " + panel.view.mode);
panel.setViewMode("2d");
pumpFrames(panel, 2);
console.log("default 3D + switch to 2D OK");
pumpFrames(panel, 4);
console.log("frames OK, compositeVersion:", panel.compositeVersion);

// Exercise tools
for (const tool of ["paint", "eraser", "smudge", "fill", "eyedropper", "shape", "move", "pan"]) {
  panel.setTool(tool);
}
panel.setTool("paint");
console.log("tools OK");

// Inspector tabs
for (const tab of ["tool", "layer", "material"]) {
  panel.inspectorTab = tab;
  panel.renderInspector();
}
console.log("inspector tabs OK");

// Add every layer kind
for (const kind of ["paint", "fill", "svg", "text", "image", "adjust"]) {
  panel.addLayerOfKind(kind);
  pumpFrames(panel, 2);
}
console.log("add layers OK, total:", panel.doc.layers.length);

// Select each layer + render inspector layer tab
panel.inspectorTab = "layer";
for (const layer of panel.doc.layers) {
  panel.selectLayer(layer.id);
  pumpFrames(panel, 1);
}
console.log("select+inspect OK");

// Library presets
for (const tab of ["brush", "material", "decal"]) {
  panel.libraryTab = tab;
  panel.renderLibrary();
  for (const item of panel.libraryItems()) panel.applyPreset(item);
  pumpFrames(panel, 2);
}
console.log("presets OK, layers:", panel.doc.layers.length);

// Paint a stroke through the engine
{
  const doc = panel.doc;
  const paintLayer = doc.layers.find((l) => l.kind === "paint") || doc.layers[0];
  doc.setActiveLayer(paintLayer.id);
  const eng = panel.engine;
  eng.tool = "paint";
  eng.beginStroke(doc, paintLayer, 100, 100, 1);
  for (let i = 1; i <= 20; i++) eng.strokeTo(100 + i * 8, 100 + Math.sin(i) * 30, 0.8);
  eng.endStroke();
  eng.tool = "smudge";
  eng.beginStroke(doc, paintLayer, 200, 200, 1);
  for (let i = 1; i <= 10; i++) eng.strokeTo(200 + i * 5, 200 + i * 3, 1);
  eng.endStroke();
  eng.tool = "shape";
  eng.shapeKind = "ellipse";
  eng.beginStroke(doc, paintLayer, 300, 300, 1);
  eng.strokeTo(420, 380, 1);
  eng.endStroke();
  eng.tool = "paint";
  eng.floodFill(doc, paintLayer, 50, 50);
  doc.renderComposite(true);
  doc.renderNormal(true);
  console.log("strokes OK, history:", doc.history.length);
}

// Masks
{
  const doc = panel.doc;
  const layer = doc.activeLayer;
  doc.toggleMask(layer);
  pumpFrames(panel, 2);
  doc.removeMask(layer);
  console.log("mask OK");

// Extended mask ops: invert / fill / feather / density + masked fill composite
{
  const doc = panel.doc;
  const layer = doc.activeLayer;
  if (!layer.channels.mask) doc.toggleMask(layer);
  doc.invertMask(layer);
  doc.fillMask(layer, false);
  doc.fillMask(layer, true);
  const snap = document.createElement("canvas");
  snap.width = doc.width; snap.height = doc.height;
  snap.getContext("2d").drawImage(layer.channels.mask, 0, 0);
  doc.featherPreview(layer, snap, 4);
  doc.commitMaskFeather(layer, snap);
  doc.setLayerProps(layer, { maskDensity: 0.5 }, "density test");
  // Mask on a FILL layer exercises applyMaskToCanvas; multi-channel masked
  // paint (metal/rough) exercises fresh-snapshot maskedSource.
  const fill = doc.layers.find((l) => l.kind === "fill");
  if (fill) {
    doc.setActiveLayer(fill.id);
    doc.toggleMask(fill);
  }
  doc.renderComposite(true);
  panel.view.showMaskOverlay = true;
  pumpFrames(panel, 2);
  panel.view.showMaskOverlay = false;
  console.log("mask ops OK, history:", doc.history.length);
}

// Quick Mask: enter, paint every tool, commit, discard
{
  const doc = panel.doc;
  const paint = doc.layers.find((l) => l.kind === "paint" && !l.locked) || doc.activeLayer;
  doc.setActiveLayer(paint.id);
  panel.toggleQuickMask();
  if (!doc.quickMaskActive) throw new Error("quick mask did not activate");
  pumpFrames(panel, 2); // overlay render path
  const eng = panel.engine;
  eng.tool = "paint";
  eng.beginStroke(doc, paint, 60, 60, 1);
  eng.strokeTo(160, 160, 1);
  eng.endStroke();
  eng.tool = "eraser";
  eng.beginStroke(doc, paint, 100, 100, 1);
  eng.strokeTo(140, 140, 1);
  eng.endStroke();
  eng.tool = "smudge";
  eng.beginStroke(doc, paint, 120, 120, 1);
  eng.strokeTo(150, 150, 1);
  eng.endStroke();
  eng.tool = "shape";
  eng.shapeKind = "rect";
  eng.beginStroke(doc, paint, 200, 200, 1);
  eng.strokeTo(300, 280, 1);
  eng.endStroke();
  eng.tool = "paint";
  eng.floodFill(doc, paint, 400, 400);
  const before = doc.history.length;
  panel.toggleQuickMask(); // commit
  if (doc.quickMaskActive) throw new Error("quick mask did not commit");
  if (!paint.channels.mask) throw new Error("commit produced no mask");
  if (doc.history.length !== before + 1) throw new Error("commit history mismatch");
  doc.renderComposite(true);
  pumpFrames(panel, 2);
  // discard path
  panel.toggleQuickMask();
  eng.beginStroke(doc, paint, 60, 60, 1);
  eng.strokeTo(90, 90, 1);
  eng.endStroke();
  doc.discardQuickMask();
  if (doc.quickMaskActive) throw new Error("discard failed");
  console.log("quick mask OK");
}
}

// Undo/redo everything
{
  const doc = panel.doc;
  let n = 0;
  while (doc.canUndo && n++ < 200) { doc.undo(); }
  pumpFrames(panel, 2);
  let m = 0;
  while (doc.canRedo && m++ < 200) { doc.redo(); }
  pumpFrames(panel, 2);
  console.log(`undo ${n} / redo ${m} OK`);
}

// Structural ops
{
  const doc = panel.doc;
  panel.inspectorTab = "layer";
  doc.duplicateActiveLayer();
  doc.moveActiveLayer(-1);
  doc.moveActiveLayer(1);
  const r = doc.mergeActiveDown();
  doc.rasterizeActiveLayer();
  doc.clearActiveLayer();
  doc.deleteActiveLayer();
  pumpFrames(panel, 2);
  console.log("structural OK, merge result:", r, "layers:", doc.layers.length);
}

// Material tab + export + save
{
  panel.inspectorTab = "material";
  panel.renderInspector();
  panel.pushMaterialHistory({ metallic: 0 }, { metallic: 0.5 }, "test");
  panel.doc.renderComposite(true);
  panel.doc.renderNormal(true);
  panel.runExport();
  panel.saveProject();
  console.log("material/export/save OK");
}

// Serialize round-trip
{
  const data = panel.doc.serialize();
  const json = JSON.stringify(data);
  console.log("serialize OK, bytes:", json.length);
  const { TextureDocument } = await import(path.join(ROOT, "src", "Document.js"));
  const doc2 = await TextureDocument.deserialize(JSON.parse(json));
  doc2.renderComposite(true);
  console.log("deserialize OK, layers:", doc2.layers.length);
}

// Second document + templates
for (const t of ["checker", "testgrid", "label"]) {
  panel.createDocument("T-" + t, 512, "dark", t);
  pumpFrames(panel, 3);
}
console.log("templates OK, docs:", panel.documents.length);
panel.closeDocument(1);
console.log("close OK, docs:", panel.documents.length);

// View modes + channels
for (const mode of ["2d", "split", "3d"]) {
  panel.setViewMode(mode);
  pumpFrames(panel, 2);
}
panel.setViewMode("2d");
for (const ch of ["composite", "albedo", "metallic", "roughness", "emissive", "height", "normal", "mask"]) {
  panel.view.channel = ch;
  pumpFrames(panel, 1);
}
console.log("views OK");

// Software renderer: stub GL resolves null, so boot must land on software.
const { SoftwarePreview } = await import(path.join(ROOT, "src", "PreviewRenderer.js"));
if (panel.preview.mode !== "software") throw new Error("expected software fallback in harness, got " + panel.preview.mode);
if (!panel.preview.ready) throw new Error("software preview not ready after boot");
// Real math on canned maps: lit red center, dark backdrop corner.
const SWCanvas = document.createElement("canvas");
SWCanvas.width = 300;
SWCanvas.height = 300;
const SWView = new SoftwarePreview(SWCanvas);
SWView.setQuality("low");
if (!SWView.initialize()) throw new Error("software init failed");
const MS = 256, MN = MS * MS;
const solid = (R, G, B) => {
  const D = new Uint8ClampedArray(MN * 4);
  for (let I = 0; I < MN; I++) {
    D[I * 4] = R; D[I * 4 + 1] = G; D[I * 4 + 2] = B; D[I * 4 + 3] = 255;
  }
  return { d: D, w: MS, h: MS };
};
SWView.maps = {
  albedo: solid(200, 40, 40),
  metallic: solid(0, 0, 0),
  roughness: solid(128, 128, 128),
  normal: solid(128, 128, 255),
  emissive: solid(0, 0, 0),
};
let captured = null, puts = 0;
SWView.bctx.putImageData = (Image) => { captured = Image; puts++; };
SWView.render(0, 0.016);
if (!captured) throw new Error("software produced no frame");
const at = (X, Y) => {
  const I = (Y * SWView.internal + X) * 4;
  return [captured.data[I], captured.data[I + 1], captured.data[I + 2]];
};
const C = at(SWView.internal >> 1, SWView.internal >> 1);
const K = at(4, 4);
console.log("  software center:", C.join(","), "corner:", K.join(","));
if (!(C[0] > 60 && C[0] > C[2] + 30)) throw new Error("center pixel not lit red: " + C);
if (!(K[0] < 60 && K[1] < 60 && K[2] < 70)) throw new Error("corner pixel not background: " + K);
SWView.yaw += 0.5;
SWView.dirty = true;
SWView.lastFrame = 0;
SWView.render(1, 0.016);
if (puts < 2) throw new Error("second software frame missing");
SWView.setQuality("high");
if (SWView.internal !== 384) throw new Error("quality switch failed");
SWView.resize(2);
if (panel.preview.onFallback === undefined) throw new Error("panel did not wire onFallback");
console.log("software renderer OK");

// Renderer pacing: idle costs nothing, turntable is throttled, uploads collapse.
{
  panel.setViewMode("3d", true);
  const SW = panel.preview.software;
  SW.lastUploadMs = 0;
  panel.preview.uploadTextures(panel.doc.composite, panel.compositeVersion + 5000);
  if (!SW.maps) throw new Error("software maps missing after forced upload");
  // Pick: canvas center hits the sphere, far corner misses.
  const Hit = panel.preview.pickSphere(450, 350);
  if (!Hit || Hit.u < 0 || Hit.u > 1 || Hit.v < 0 || Hit.v > 1) throw new Error("center pick missed: " + JSON.stringify(Hit));
  const Miss = panel.preview.pickSphere(5, 5);
  if (Miss !== null) throw new Error("corner pick should miss: " + JSON.stringify(Miss));
  if (!panel.previewPaintable()) throw new Error("software sphere should be paintable");
  console.log("  pick center:", Hit.u.toFixed(3), Hit.v.toFixed(3));
  // Idle with turntable off: one settled frame, then silence.
  panel.preview.turntable = false;
  SW.dirty = true;
  panel.preview.render(9.9, 0.016); // settles resolution (may reallocate)
  let puts = 0;
  SW.bctx.putImageData = () => { puts++; };
  SW.dirty = true;
  panel.preview.render(10, 0.016);
  if (puts < 1) throw new Error("expected a settled re-render");
  panel.preview.render(10.02, 0.016);
  panel.preview.render(10.04, 0.016);
  if (puts !== 1) throw new Error("idle frames retraced the sphere, puts=" + puts);
  // Turntable slow path: a burst of frames renders at most once.
  panel.preview.turntable = true;
  SW.lastFrame = 0;
  panel.preview.render(10.05, 0.016); // drops to dynamic res (reallocates)
  puts = 0;
  SW.bctx.putImageData = () => { puts++; };
  SW.lastFrame = 0;
  SW.dirty = true;
  panel.preview.render(10.06, 0.016);
  panel.preview.render(10.07, 0.016);
  panel.preview.render(10.08, 0.016);
  if (puts !== 1) throw new Error("turntable burst mis-throttled, puts=" + puts);
  // Texture upload throttle: rapid re-stamps collapse into one upload.
  SW.lastUploadMs = 0;
  const down0 = SW.stamp;
  panel.preview.uploadTextures(panel.doc.composite, down0 + 1001);
  panel.preview.uploadTextures(panel.doc.composite, down0 + 1002);
  if (SW.stamp !== down0 + 1001) throw new Error("upload throttle failed, stamp=" + SW.stamp);
  delete SW.bctx.putImageData;
  console.log("renderer pacing OK");
}

// 3D paint-on-model through the real wired handlers.
{
  panel.createDocument("paint-3d", 256, "dark", "blank");
  panel.setViewMode("3d", true);
  panel.setTool("paint");
  const Doc3 = panel.doc;
  const PL = Doc3.layers.find((l) => l.kind === "paint" && !l.locked);
  if (!PL) throw new Error("fresh doc has no paint layer");
  panel.selectLayer(PL.id);
  const PV = document.querySelector("#preview-canvas");
  if (PV.style.cursor !== "crosshair") throw new Error("preview cursor not crosshair for brush");
  const H0 = Doc3.history.length;
  PV._fire("pointerdown", { clientX: 450, clientY: 350, button: 0, buttons: 1, pointerId: 1 });
  if (!panel.engine.painting) throw new Error("3D stroke did not start");
  if (!panel.previewPainting) throw new Error("previewPainting state missing");
  PV._fire("pointermove", { clientX: 470, clientY: 360, buttons: 1, pointerId: 1 });
  PV._fire("pointermove", { clientX: 490, clientY: 372, buttons: 1, pointerId: 1 });
  PV._fire("pointerup", { pointerId: 1 });
  if (panel.engine.painting) throw new Error("3D stroke did not end");
  if (Doc3.history.length <= H0) throw new Error("3D stroke left no history");
  panel.setTool("fill");
  const H1 = Doc3.history.length;
  PV._fire("pointerdown", { clientX: 450, clientY: 350, button: 0, buttons: 1, pointerId: 1 });
  if (panel.engine.painting) throw new Error("fill should not start a stroke");
  if (Doc3.history.length < H1) throw new Error("3D fill corrupted history");
  panel.setTool("eyedropper");
  PV._fire("pointerdown", { clientX: 450, clientY: 350, button: 0, buttons: 1, pointerId: 1 });
  if (!panel.previewPainting) throw new Error("3D sample did not start");
  PV._fire("pointerup", {});
  panel.setTool("pan");
  PV._fire("pointerdown", { clientX: 450, clientY: 350, button: 0 });
  if (!panel.orbiting) throw new Error("pan tool should orbit in 3D");
  PV._fire("pointerup", {});
  if (panel.orbiting) throw new Error("orbit did not end");
  if (PV.style.cursor !== "grab") throw new Error("preview cursor not grab for pan");
  panel.setTool("paint");
  PV._fire("pointerdown", { clientX: 450, clientY: 350, button: 0, altKey: true });
  if (!panel.orbiting) throw new Error("Alt-drag should orbit with a brush selected");
  PV._fire("pointerup", {});
  console.log("3D paint OK");
}

// Keyboard shortcuts (simulate)
{
  const fire = (key, extra = {}) => {
    // call handler directly with fake event
    panel.onKeyDown(Object.assign({
      key, target: { tagName: "DIV" }, ctrlKey: false, shiftKey: false,
      altKey: false, metaKey: false, preventDefault() {},
    }, extra));
  };
  for (const k of ["b", "e", "u", "g", "i", "r", "v", "h", "x", "f", "c", "t", "d", "[", "]", "1", "5", "0"]) fire(k);
  fire("q"); // enter quick mask
  if (!panel.doc.quickMaskActive) throw new Error("q did not enter quick mask");
  panel.onKeyDown({ key: "Enter", target: { tagName: "DIV" }, ctrlKey: false, shiftKey: false, altKey: false, metaKey: false, preventDefault() {} });
  if (panel.doc.quickMaskActive) throw new Error("Enter did not commit quick mask");
  fire("q");
  panel.onKeyDown({ key: "Escape", target: { tagName: "DIV" }, ctrlKey: false, shiftKey: false, altKey: false, metaKey: false, preventDefault() {} });
  if (panel.doc.quickMaskActive) throw new Error("Escape did not discard quick mask");
  fire("z", { ctrlKey: true });
  fire("y", { ctrlKey: true });
  fire("s", { ctrlKey: true });
  console.log("keys OK");
}

pumpFrames(panel, 3);
if (errors.length) {
  console.log("ERRORS:\n" + errors.join("\n"));
  process.exit(1);
}
console.log("ALL SMOKE TESTS PASSED");
