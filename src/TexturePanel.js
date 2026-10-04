/* Frontier Texture Paint — studio controller.
   Document tabs, layer stack, library, inspector, 2D paint view, 3D PBR
   preview, channel strip, history transport, export. Fluid-theme chrome. */

import { TextureDocument, createLayer, applyTemplate, buildORM, getResized, downloadCanvas, defaultMaterial } from "./Document.js";
import { BrushEngine } from "./BrushEngine.js";
import { MaterialPreview } from "./PreviewRenderer.js";
import { FillIcons, SetIcon } from "./Icons.js";
import {
  LAYER_KINDS, BLEND_MODES, CHANNELS, BRUSH_PRESETS, MATERIAL_PRESETS,
  SVG_PRESETS, TEXT_PRESETS, FONT_STACKS, FontFamily, SWATCHES, TIP_LABELS,
} from "./Presets.js";
import { makeCanvas, decalBounds } from "./Decals.js";

const Select = (Selector) => document.querySelector(Selector);
const SelectAll = (Selector) => [...document.querySelectorAll(Selector)];
const Escape = (Text) =>
  String(Text ?? "").replace(/[&<>"']/g, (Character) => (
    { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[Character]
  ));
const Clamp = (Value, Min, Max) => Math.max(Min, Math.min(Max, Value));
const sanitizeName = (Name) => String(Name || "texture").replace(/[^\w\- ]+/g, "").trim().replace(/\s+/g, "-").toLowerCase() || "texture";

const TOOL_LABELS = {
  paint: "Paint brush", eraser: "Eraser", smudge: "Smudge", fill: "Fill",
  eyedropper: "Eyedropper", shape: "Shape", move: "Move decal", pan: "Pan view",
};

const BASE_COLORS = {
  transparent: null,
  dark: "#2a2c2e",
  mid: "#808080",
  light: "#c9c9c9",
};

export class TexturePanel {
  constructor() {
    FillIcons();
    this.engine = new BrushEngine();
    this.documents = [];
    this.docIndex = -1;
    this.view = {
      zoom: 1, panX: 0, panY: 0, fitScale: 1,
      mode: "3d", channel: "composite",
      tiling: false, checker: true, showMaskOverlay: false,
    };
    this.overlayCanvas = null;
    this.inspectorTab = "tool";
    this.libraryTab = "brush";
    this.libraryQuery = "";
    this.layerQuery = "";
    this.layerFilter = "all";
    this.compositeVersion = 0;
    this.previewCanvas = makeCanvas(16, 16);
    this.previewStamp = -1;
    this.channelStamps = new Map();
    this.hover = null;
    this.panning = null;
    this.decalDrag = null;
    this.orbiting = null;
    this.spaceDown = false;
    this.navigatorDrag = false;
    this.lastFrame = performance.now();
    this.toastTimeout = 0;
    this.svgThumbs = new Map();
    this.preview = new MaterialPreview(Select("#preview-canvas"));
    this.preview.onError = (Message) => this.showPreviewError(Message);

    this.paintCanvas = Select("#paint-canvas");
    this.paintContext = this.paintCanvas.getContext("2d");

    this.buildChannelStrip();
    this.connectInterface();
    this.createDocument("Untitled texture", 1024, "dark", "blank", true);
    this.setViewMode("3d", true);
    this.fitView();
    new ResizeObserver(() => this.layoutCanvases()).observe(Select("#viewport"));
    if (document.fonts && document.fonts.ready) {
      document.fonts.ready.then(() => {
        for (const Doc of this.documents) {
          for (const Layer of Doc.layers) {
            if (Layer.kind === "text") Layer.rasterDirty = true;
          }
          Doc.markDirty();
        }
      });
    }
    requestAnimationFrame((Time) => this.frame(Time));
  }

  get doc() {
    return this.documents[this.docIndex] || null;
  }

  /* ================= Documents ================= */

  createDocument(Name, Size, Base, Template, First = false) {
    if (this.documents.length >= 6) {
      this.notify("Tab limit reached — close a texture to open another.");
      return null;
    }
    const Document = new TextureDocument(Name, Size, Size, {});
    if (Base === "transparent") {
      Document.baseMode = "transparent";
    } else {
      Document.baseMode = "material";
      Document.material.albedo = BASE_COLORS[Base] || "#2a2c2e";
    }
    Document.onChange = () => this.refreshAfterAsync();
    applyTemplate(Document, Template || "blank");
    this.documents.push(Document);
    this.docIndex = this.documents.length - 1;
    if (First) this.documents[0].dirty = false;
    this.afterDocumentSwitch();
    return Document;
  }

  switchDocument(Index) {
    if (Index < 0 || Index >= this.documents.length || Index === this.docIndex) return;
    this.engine.cancelStroke();
    this.docIndex = Index;
    this.afterDocumentSwitch();
  }

  afterDocumentSwitch() {
    const Document = this.doc;
    this.previewCanvas = makeCanvas(Document.width, Document.height);
    this.previewStamp = -1;
    this.channelStamps.clear();
    this.view.channel = "composite";
    Select("#render-channel").value = "composite";
    Select("#document-name").value = Document.name;
    this.syncViewModeUI();
    this.fitView();
    this.renderAll();
  }

  closeDocument(Index) {
    if (this.documents.length <= 1) {
      this.notify("A studio needs at least one texture.");
      return;
    }
    this.engine.cancelStroke();
    this.documents.splice(Index, 1);
    if (this.docIndex >= this.documents.length) this.docIndex = this.documents.length - 1;
    if (this.docIndex === Index && Index >= this.documents.length) this.docIndex = this.documents.length - 1;
    this.afterDocumentSwitch();
  }

  renderDocumentTabs() {
    const Tabs = Select("#document-tabs");
    Tabs.innerHTML = "";
    this.documents.forEach((Document, Index) => {
      const Tab = document.createElement("div");
      Tab.className = `document-tab${Index === this.docIndex ? " active" : ""}`;
      Tab.setAttribute("role", "tab");
      Tab.setAttribute("aria-selected", Index === this.docIndex ? "true" : "false");
      Tab.title = `${Document.name}.texpaint`;
      Tab.innerHTML = `<span class="document-label">${Escape(Document.name)}</span>${
        Document.dirty ? '<span class="tab-dirty"></span>' : ""
      }<button class="tab-close" aria-label="Close texture" ${this.documents.length <= 1 ? "disabled" : ""}>✕</button>`;
      Tab.addEventListener("click", (Event) => {
        if (Event.target.closest(".tab-close")) {
          this.closeDocument(Index);
          return;
        }
        this.switchDocument(Index);
      });
      Tab.addEventListener("dblclick", (Event) => {
        if (Event.target.closest(".tab-close")) return;
        const Label = Tab.querySelector(".document-label");
        const Input = document.createElement("input");
        Input.className = "document-rename";
        Input.value = Document.name;
        Input.maxLength = 64;
        Label.replaceWith(Input);
        Input.focus();
        Input.select();
        const Commit = () => {
          const Value = Input.value.trim();
          if (Value) {
            Document.name = Value;
            Document.dirty = true;
          }
          this.renderDocumentTabs();
          Select("#document-name").value = Document.name;
        };
        Input.addEventListener("blur", Commit);
        Input.addEventListener("keydown", (KeyEvent) => {
          if (KeyEvent.key === "Enter") Input.blur();
          if (KeyEvent.key === "Escape") {
            Input.value = Document.name;
            Input.blur();
          }
          KeyEvent.stopPropagation();
        });
      });
      Tabs.appendChild(Tab);
    });
  }

  /* ================= Chrome ================= */

  renderAll() {
    this.renderDocumentTabs();
    this.renderLayerList();
    this.renderLibrary();
    this.renderInspector();
    this.updateChrome();
    this.refreshChannelThumbs(true);
  }

  updateChrome() {
    const Document = this.doc;
    if (!Document) return;
    Select("#dirty-indicator").classList.toggle("clean", !Document.dirty);
    const Visible = Document.layers.filter((Layer) => Layer.visible).length;
    Select("#scene-count").textContent = Document.layers.length;
    Select("#enabled-count").textContent = Visible;
    Select("#disabled-count").textContent = Document.layers.length - Visible;
    const Active = Document.activeLayer;
    if (document.activeElement !== Select("#object-name")) {
      Select("#object-name").value = Active ? Active.name : "No layer";
    }
    Select("#object-type").textContent = Active ? `${LAYER_KINDS[Active.kind].label}${Active.maskSelected ? " · MASK" : ""}${Active.clip ? " · CLIPPED" : ""}`.toUpperCase() : "—";
    if (Active) SetIcon(Select("#object-symbol"), Active.maskSelected ? "mask" : LAYER_KINDS[Active.kind].icon);
    Select("#object-enabled").checked = Active ? Active.visible : false;
    Select("#viewport-object").textContent = Active ? Active.name : "No layer";
    const Channel = CHANNELS.find((Entry) => Entry.id === this.view.channel);
    Select("#viewport-subtitle").textContent = `PBR · ${Document.width}² · ${(Channel || {}).label || "Composite"}`;
    Select("#undo-button").disabled = !Document.canUndo;
    Select("#redo-button").disabled = !Document.canRedo;
    Select("#brush-readout").textContent = `Ø ${Math.round(this.engine.brush.size)} px`;
    Select("#layer-count").textContent = `${Document.layers.length} layer${Document.layers.length === 1 ? "" : "s"}`;
    Select("#doc-size").textContent = `${Document.width} × ${Document.height} px`;
    Select("#doc-hud").textContent = `${Document.width}² RGBA`;
    Select("#left-footer-text").textContent = `${Document.width} × ${Document.height} · ${Document.layers.length} layers · 8-bit sRGB`;
    const Tool = TOOL_LABELS[this.engine.tool] || "Paint";
    Select("#status-ready").innerHTML = `<i></i>${Escape(Tool)} · ${Document.historyIndex + 1}/${Document.history.length} history`;
    Select("#delete-layer").disabled = !Active;
    Select("#clear-button").disabled = !Active;
    Select("#tiling-button").classList.toggle("active", this.view.tiling);
    Select("#overlays-button").classList.toggle("active", this.view.checker);
    Select("#quickmask-button").classList.toggle("active", Document.quickMaskActive);
    if (Document.quickMaskActive) {
      const Pill = Select("#live-pill span");
      if (Pill) Pill.textContent = "QUICK MASK";
    }
    const Scale = this.view.fitScale * this.view.zoom;
    Select("#zoom-level").textContent = `${Math.round(Scale * 100)}%`;
    if (document.activeElement !== Select("#symmetry-select")) {
      Select("#symmetry-select").value = this.engine.symmetry;
    }
    if (document.activeElement !== Select("#shape-kind")) {
      Select("#shape-kind").value = this.engine.shapeKind;
    }
    SelectAll(".viewport-tools [data-tool]").forEach((Button) => {
      Button.classList.toggle("active", Button.dataset.tool === this.engine.tool);
    });
    document.body.classList.toggle("mask-editing", !!(Active && Active.maskSelected));
  }

  notify(Message) {
    clearTimeout(this.toastTimeout);
    const Toast = Select("#toast");
    Toast.textContent = Message;
    Toast.hidden = false;
    this.toastTimeout = setTimeout(() => {
      Toast.hidden = true;
    }, 4200);
  }

  refreshAfterAsync() {
    if (!this.doc) return;
    this.doc.updateThumbs();
    this.renderLayerList();
    this.updateChrome();
    this.refreshChannelThumbs(true);
  }

  /* ================= Layers panel ================= */

  layerVisibleInFilter(Layer) {
    if (this.layerFilter === "paint" && Layer.kind !== "paint") return false;
    if (this.layerFilter === "fill" && Layer.kind !== "fill" && Layer.kind !== "adjust") return false;
    if (this.layerFilter === "decal" && !["svg", "text", "image"].includes(Layer.kind)) return false;
    if (this.layerQuery && !Layer.name.toLowerCase().includes(this.layerQuery)) return false;
    return true;
  }

  renderLayerList() {
    const Document = this.doc;
    const Tree = Select("#scene-tree");
    const Scroll = Tree.scrollTop;
    Tree.innerHTML = "";
    if (!Document || !Document.layers.length) {
      Tree.innerHTML = `<div class="layers-empty">The stack is empty.<button class="button" id="empty-add">Add a layer</button></div>`;
      const Empty = Select("#empty-add");
      if (Empty) Empty.addEventListener("click", () => this.toggleAddMenu(true));
      return;
    }
    const Ordered = [...Document.layers].reverse();
    let Shown = 0;
    for (const Layer of Ordered) {
      if (!this.layerVisibleInFilter(Layer)) continue;
      Shown++;
      const Row = document.createElement("div");
      Row.className = `scene-row layer-row${Layer.id === Document.activeLayerId ? " selected" : ""}${Layer.visible ? "" : " muted"}${Layer.clip ? " clipped" : ""}`;
      Row.dataset.kind = Layer.kind;
      Row.dataset.id = Layer.id;
      Row.tabIndex = 0;
      Row.setAttribute("role", "treeitem");
      Row.draggable = true;
      const Meta = LAYER_KINDS[Layer.kind];
      Row.innerHTML = `
        <div class="layer-thumb"></div>
        <div class="layer-meta">
          <span class="scene-label">${Escape(Layer.name)}</span>
          <span class="layer-sub"><span class="row-badge">${Meta.badge}</span><span>${Escape(Layer.blend)} · ${Math.round(Layer.opacity * 100)}%</span>${
            Layer.channels.mask ? `<span class="mask-dot" title="Has mask">${Layer.maskEnabled ? "◐" : "○"} mask</span>` : ""
          }${Layer.clip ? `<span class="clip-mark">⤵ clip</span>` : ""}</span>
        </div>
        <div class="row-actions">
          <button class="icon-button${Layer.visible ? "" : " off"}" data-action="visibility" title="Toggle visibility" aria-label="Toggle visibility"></button>
          <button class="icon-button${Layer.locked ? " locked" : ""}" data-action="lock" title="Toggle lock" aria-label="Toggle lock"></button>
        </div>`;
      Row.querySelector(".layer-thumb").appendChild(Layer.thumb);
      const [EyeButton, LockButton] = Row.querySelectorAll(".row-actions button");
      SetIcon(EyeButton, Layer.visible ? "eye" : "hidden");
      SetIcon(LockButton, Layer.locked ? "lock" : "unlock");
      EyeButton.addEventListener("click", (Event) => {
        Event.stopPropagation();
        Document.setLayerProps(Layer, { visible: !Layer.visible }, "Toggle visibility");
        this.renderLayerList();
        this.updateChrome();
      });
      LockButton.addEventListener("click", (Event) => {
        Event.stopPropagation();
        Document.setLayerProps(Layer, { locked: !Layer.locked }, Layer.locked ? "Unlock layer" : "Lock layer");
        this.renderLayerList();
        this.updateChrome();
      });
      Row.addEventListener("click", () => this.selectLayer(Layer.id));
      Row.addEventListener("dblclick", (Event) => {
        if (Event.target.closest(".row-actions")) return;
        this.renameLayerInline(Row, Layer);
      });
      Row.addEventListener("keydown", (Event) => {
        if (Event.key === "Enter") this.renameLayerInline(Row, Layer);
      });
      Row.addEventListener("dragstart", (Event) => {
        Event.dataTransfer.setData("text/layer-id", Layer.id);
        Event.dataTransfer.effectAllowed = "move";
        Row.classList.add("dragging");
      });
      Row.addEventListener("dragend", () => {
        Row.classList.remove("dragging");
        SelectAll(".layer-row.drop-above, .layer-row.drop-below").forEach((Entry) => Entry.classList.remove("drop-above", "drop-below"));
      });
      Row.addEventListener("dragover", (Event) => {
        Event.preventDefault();
        const Rect = Row.getBoundingClientRect();
        const Above = Event.clientY < Rect.top + Rect.height / 2;
        Row.classList.toggle("drop-above", Above);
        Row.classList.toggle("drop-below", !Above);
      });
      Row.addEventListener("dragleave", () => Row.classList.remove("drop-above", "drop-below"));
      Row.addEventListener("drop", (Event) => {
        Event.preventDefault();
        const DragId = Event.dataTransfer.getData("text/layer-id");
        const Rect = Row.getBoundingClientRect();
        // Rows render top-first; "above" in the list means higher in the stack.
        const PlaceAbove = Event.clientY < Rect.top + Rect.height / 2;
        Document.reorderLayer(DragId, Layer.id, PlaceAbove);
        this.renderLayerList();
        this.renderInspector();
        this.updateChrome();
      });
      Tree.appendChild(Row);
    }
    if (!Shown) {
      Tree.innerHTML = `<div class="outliner-empty">No layers match this filter.</div>`;
    }
    Tree.scrollTop = Scroll;
  }

  renameLayerInline(Row, Layer) {
    const Label = Row.querySelector(".scene-label");
    const Input = document.createElement("input");
    Input.className = "layer-rename";
    Input.value = Layer.name;
    Input.maxLength = 64;
    Label.replaceWith(Input);
    Input.focus();
    Input.select();
    const Commit = () => {
      const Value = Input.value.trim();
      if (Value && Value !== Layer.name) {
        this.doc.setLayerProps(Layer, { name: Value }, "Rename layer");
      }
      this.renderLayerList();
      this.updateChrome();
    };
    Input.addEventListener("blur", Commit);
    Input.addEventListener("keydown", (Event) => {
      if (Event.key === "Enter") Input.blur();
      if (Event.key === "Escape") {
        Input.value = Layer.name;
        Input.blur();
      }
      Event.stopPropagation();
    });
    Input.addEventListener("click", (Event) => Event.stopPropagation());
  }

  selectLayer(Id) {
    const Document = this.doc;
    if (!Document) return;
    Document.setActiveLayer(Id);
    this.renderLayerList();
    this.renderInspector();
    this.updateChrome();
  }

  /* ================= Library ================= */

  libraryItems() {
    const Query = this.libraryQuery;
    const Matches = (Name, Desc) => {
      if (!Query) return true;
      return `${Name} ${Desc}`.toLowerCase().includes(Query);
    };
    if (this.libraryTab === "brush") {
      return BRUSH_PRESETS.filter((Preset) => Matches(Preset.name, Preset.desc)).map((Preset) => ({
        id: Preset.id, name: Preset.name, desc: Preset.desc, library: "brush", ref: Preset,
      }));
    }
    if (this.libraryTab === "material") {
      return MATERIAL_PRESETS.filter((Preset) => Matches(Preset.name, Preset.desc)).map((Preset) => ({
        id: Preset.id, name: Preset.name, desc: Preset.desc, library: "material", ref: Preset,
      }));
    }
    const Svg = SVG_PRESETS.filter((Preset) => Matches(Preset.name, Preset.desc)).map((Preset) => ({
      id: Preset.id, name: Preset.name, desc: Preset.desc, library: "svg", ref: Preset,
    }));
    const Text = TEXT_PRESETS.filter((Preset) => Matches(Preset.name, Preset.desc)).map((Preset) => ({
      id: Preset.id, name: Preset.name, desc: Preset.desc, library: "text", ref: Preset,
    }));
    return [...Svg, ...Text];
  }

  renderLibrary() {
    const List = Select("#presets-list");
    List.innerHTML = "";
    const Items = this.libraryItems();
    Select("#preset-count").textContent = Items.length;
    Select("#empty-presets").hidden = Items.length > 0;
    for (const Item of Items) {
      const Card = document.createElement("button");
      Card.className = "preset-card";
      Card.dataset.library = Item.library;
      Card.innerHTML = `<span class="preset-swatch"></span><span class="preset-copy"><strong>${Escape(Item.name)}</strong><small>${Escape(Item.desc)}</small></span><span class="preset-arrow">›</span>`;
      const Swatch = Card.querySelector(".preset-swatch");
      if (Item.library === "brush") {
        const Canvas = document.createElement("canvas");
        Canvas.width = 52;
        Canvas.height = 60;
        this.paintBrushSwatch(Canvas, Item.ref);
        Swatch.appendChild(Canvas);
      } else if (Item.library === "material") {
        const Dot = document.createElement("span");
        Dot.className = "material-dot";
        const Rough = Item.ref.roughness;
        Dot.style.background = `radial-gradient(circle at 32% 28%, #ffffff${Rough < 0.4 ? "cc" : "55"} 0%, transparent 46%), linear-gradient(160deg, ${Item.ref.color} 30%, #101010 130%)`;
        Dot.style.boxShadow = Item.ref.emissive > 0 ? `0 0 12px ${Item.ref.emissiveColor}` : "none";
        Swatch.appendChild(Dot);
      } else if (Item.library === "svg") {
        const Canvas = document.createElement("canvas");
        Canvas.width = 52;
        Canvas.height = 60;
        this.paintSvgSwatch(Canvas, Item.ref);
        Swatch.appendChild(Canvas);
      } else {
        Swatch.innerHTML = `<span style="font-family:${FontFamily(Item.ref.font).split(",")[0]};font-weight:${Item.ref.weight};font-style:${Item.ref.italic ? "italic" : "normal"};font-size:20px;color:${Item.ref.color}">Ag</span>`;
      }
      Card.addEventListener("click", () => this.applyPreset(Item));
      List.appendChild(Card);
    }
  }

  paintBrushSwatch(Canvas, Preset) {
    const Context = Canvas.getContext("2d");
    Context.clearRect(0, 0, Canvas.width, Canvas.height);
    Context.save();
    Context.translate(Canvas.width / 2, Canvas.height / 2);
    Context.rotate((Preset.angle * Math.PI) / 180);
    Context.scale(1, Preset.roundness);
    const Radius = 15;
    const Gradient = Context.createRadialGradient(0, 0, 0, 0, 0, Radius);
    Gradient.addColorStop(0, Preset.tool === "eraser" ? "#ff8a80" : Preset.tool === "smudge" ? "#9ec8ff" : "#e8e6e0");
    Gradient.addColorStop(Math.max(0.05, Math.min(0.95, Preset.hardness)), "#c9c9c9");
    Gradient.addColorStop(1, "rgba(200,200,200,0)");
    Context.fillStyle = Gradient;
    Context.beginPath();
    Context.arc(0, 0, Radius, 0, Math.PI * 2);
    Context.fill();
    Context.restore();
  }

  paintSvgSwatch(Canvas, Preset) {
    const Context = Canvas.getContext("2d");
    Context.clearRect(0, 0, Canvas.width, Canvas.height);
    const Cached = this.svgThumbs.get(Preset.id);
    if (Cached) {
      Context.drawImage(Cached, 4, 6, Canvas.width - 8, Canvas.height - 12);
      return;
    }
    Context.fillStyle = "#666";
    Context.font = "18px sans-serif";
    Context.textAlign = "center";
    Context.fillText("◈", Canvas.width / 2, Canvas.height / 2 + 6);
    const Image = new window.Image();
    Image.onload = () => {
      this.svgThumbs.set(Preset.id, Image);
      if (Canvas.isConnected) {
        Context.clearRect(0, 0, Canvas.width, Canvas.height);
        Context.drawImage(Image, 4, 6, Canvas.width - 8, Canvas.height - 12);
      }
    };
    const Blob = new window.Blob([Preset.svg], { type: "image/svg+xml;charset=utf-8" });
    Image.src = URL.createObjectURL(Blob);
  }

  applyPreset(Item) {
    const Document = this.doc;
    if (!Document) return;
    if (Item.library === "brush") {
      const Preset = Item.ref;
      this.setTool(Preset.tool === "smudge" ? "smudge" : Preset.tool);
      Object.assign(this.engine.brush, {
        size: Preset.size, hardness: Preset.hardness, opacity: Preset.opacity,
        flow: Preset.flow, spacing: Preset.spacing, roundness: Preset.roundness,
        angle: Preset.angle, scatter: Preset.scatter, smoothing: Preset.smoothing,
        tip: Preset.tip, smudge: Preset.smudge || 0.65,
      });
      this.engine.tool = Preset.tool === "smudge" ? "smudge" : Preset.tool;
      if (Preset.tool === "eraser") this.setTool("eraser");
      this.renderInspector();
      this.updateChrome();
      this.notify(`${Preset.name} ready — ${TOOL_LABELS[this.engine.tool]}.`);
      return;
    }
    if (Item.library === "material") {
      const Preset = Item.ref;
      this.engine.fgColor = Preset.color;
      Object.assign(this.engine.material, {
        paintMetal: true, metal: Preset.metallic,
        paintRough: true, rough: Preset.roughness,
        paintEmissive: Preset.emissive > 0, emissive: Preset.emissive, emissiveColor: Preset.emissiveColor,
      });
      const Active = Document.activeLayer;
      if (Active && Active.kind === "fill" && !Active.locked) {
        Document.setLayerProps(Active, {
          params: Object.assign({}, Active.params, {
            color: Preset.color, metallic: Preset.metallic, roughness: Preset.roughness,
            emissive: Preset.emissive, emissiveColor: Preset.emissiveColor,
          }),
        }, `Apply ${Preset.name}`);
      } else {
        Document.addLayer("fill", {
          name: Preset.name,
          params: { pattern: "solid", color: Preset.color, color2: "#222222", scale: 8, angle: 0, seed: 7, metallic: Preset.metallic, roughness: Preset.roughness, emissive: Preset.emissive, emissiveColor: Preset.emissiveColor, height: Preset.height },
        });
      }
      this.renderAll();
      this.notify(`${Preset.name} staged — brush carries its PBR values.`);
      return;
    }
    if (Item.library === "svg") {
      const Layer = Document.addLayer("svg", {
        name: Item.ref.name,
        params: Object.assign({}, Document.activeLayer ? undefined : {}, {
          svg: Item.ref.svg, x: 0.5, y: 0.5, scale: 0.55, rotation: 0, flipX: false, flipY: false,
          tint: "", metallic: -1, roughness: -1, emissive: 0, emissiveColor: "#ffffff", height: -1,
        }),
      });
      void Layer;
      this.setTool("move");
      this.renderAll();
      this.notify(`${Item.ref.name} placed — drag to move, corners to scale.`);
      return;
    }
    if (Item.library === "text") {
      const Preset = Item.ref;
      const FontKey = FONT_STACKS.find(([, Label]) => FontFamily(Label) === Preset.font)?.[0] || "grotesk";
      void FontKey;
      const Guess = Preset.font.includes("Impact") ? "industrial" : Preset.font.includes("Mono") ? "mono" : Preset.font.includes("Georgia") ? "serif" : Preset.font.includes("DM Sans") ? "grotesk" : "system";
      Document.addLayer("text", {
        name: Preset.name,
        params: {
          text: Preset.text, font: Guess, size: Preset.size, weight: Preset.weight, italic: Preset.italic,
          align: Preset.align, color: Preset.color, stroke: Preset.stroke, strokeColor: Preset.strokeColor,
          spacing: Preset.spacing, lineHeight: 1.15, x: 0.5, y: 0.5, scale: 1, rotation: 0,
          flipX: false, flipY: false, metallic: -1, roughness: -1, emissive: 0, emissiveColor: "#ffffff", height: -1,
        },
      });
      this.setTool("move");
      this.renderAll();
      this.notify(`${Preset.name} placed — edit copy in the Layer tab.`);
    }
  }

  /* ================= Inspector ================= */

  sliderRow({ id, label, min, max, step, value, unit = "", format = null }) {
    const Display = format ? format(value) : `${value}${unit ? ` ${unit}` : ""}`;
    return `
      <div class="property-row slider-row">
        <span class="property-label">${Escape(label)}<output id="${id}-output">${Escape(Display)}</output></span>
        <div class="slider-control">
          <div class="value-pill"><input type="number" id="${id}-number" min="${min}" max="${max}" step="${step}" value="${value}" aria-label="${Escape(label)} value" /><span class="unit-cell">${Escape(unit || "—")}</span></div>
          <input type="range" id="${id}-range" min="${min}" max="${max}" step="${step}" value="${value}" aria-label="${Escape(label)}" />
        </div>
      </div>`;
  }

  bindSlider(Id, { min, max, step, get, set, unit = "", format = null, onCommit = null, onStart = null }) {
    const NumberInput = Select(`#${Id}-number`);
    const Range = Select(`#${Id}-range`);
    const Output = Select(`#${Id}-output`);
    if (!NumberInput || !Range) return;
    const Paint = () => {
      const Value = get();
      const Fraction = (Value - min) / Math.max(0.0001, max - min);
      Range.style.setProperty("--fraction", Clamp(Fraction, 0, 1).toFixed(4));
      Range.style.setProperty("--range", `${Clamp(Fraction, 0, 1) * 100}%`);
      if (Output) Output.textContent = format ? format(Value) : `${Value}${unit ? ` ${unit}` : ""}`;
      if (document.activeElement !== NumberInput) NumberInput.value = Value;
      if (document.activeElement !== Range) Range.value = Value;
    };
    Range.addEventListener("input", () => {
      set(parseFloat(Range.value));
      Paint();
      if (onCommit) onCommit(false);
    });
    Range.addEventListener("change", () => {
      if (onCommit) onCommit(true);
      Paint();
    });
    NumberInput.addEventListener("change", () => {
      let Value = parseFloat(NumberInput.value);
      if (Number.isNaN(Value)) Value = get();
      set(Clamp(Value, min, max));
      Paint();
      if (onCommit) onCommit(true);
    });
    // Fluid-style scrubbing: drag the numeric value horizontally.
    NumberInput.addEventListener("pointerdown", (Event) => {
      if (Event.button !== 0) return;
      const StartX = Event.clientX;
      const StartValue = get();
      let Moved = false;
      NumberInput.setPointerCapture(Event.pointerId);
      const Move = (MoveEvent) => {
        const Delta = MoveEvent.clientX - StartX;
        if (!Moved && Math.abs(Delta) < 3) return;
        Moved = true;
        NumberInput.classList.add("scrubbing");
        const Rate = step * (MoveEvent.shiftKey ? 0.2 : MoveEvent.altKey ? 8 : 1);
        set(Clamp(StartValue + Delta * Rate, min, max));
        Paint();
      };
      const Up = () => {
        NumberInput.classList.remove("scrubbing");
        NumberInput.removeEventListener("pointermove", Move);
        NumberInput.removeEventListener("pointerup", Up);
        if (Moved && onCommit) onCommit(true);
        if (Moved) Paint();
      };
      NumberInput.addEventListener("pointermove", Move);
      NumberInput.addEventListener("pointerup", Up);
    });
    Paint();
  }

  toggleRow({ id, label }) {
    return `
      <div class="property-row toggle-row">
        <span class="property-label">${Escape(label)}</span>
        <label class="switch"><input type="checkbox" id="${id}" /><span></span></label>
      </div>`;
  }

  selectRow({ id, label, options, value }) {
    return `
      <div class="property-row select-row">
        <span class="property-label">${Escape(label)}</span>
        <select id="${id}" class="inline-select">${options.map(([OptionValue, OptionLabel]) => `<option value="${Escape(OptionValue)}"${OptionValue === value ? " selected" : ""}>${Escape(OptionLabel)}</option>`).join("")}</select>
      </div>`;
  }

  colorRow({ id, label, value }) {
    return `
      <div class="property-row" style="grid-template-columns:minmax(0,1fr) auto;display:flex;align-items:center;gap:8px;">
        <span class="property-label" style="flex:1">${Escape(label)}</span>
        <span class="color-well"><input type="color" id="${id}-color" value="${Escape(value)}" aria-label="${Escape(label)}" /></span>
        <input class="hex-field" id="${id}-hex" value="${Escape(value)}" maxlength="7" spellcheck="false" style="max-width:96px" aria-label="${Escape(label)} hex" />
      </div>`;
  }

  bindColor(Id, { get, set }) {
    const Picker = Select(`#${Id}-color`);
    const Hex = Select(`#${Id}-hex`);
    if (!Picker || !Hex) return;
    Picker.value = get();
    Hex.value = get();
    Picker.addEventListener("input", () => {
      set(Picker.value);
      Hex.value = Picker.value;
    });
    Hex.addEventListener("change", () => {
      let Value = Hex.value.trim();
      if (/^[0-9a-fA-F]{6}$/.test(Value)) Value = `#${Value}`;
      if (/^[0-9a-fA-F]{3}$/.test(Value)) Value = `#${Value}`;
      if (/^#[0-9a-fA-F]{6}$/.test(Value) || /^#[0-9a-fA-F]{3}$/.test(Value)) {
        set(Value);
        Picker.value = Value.length === 4 ? `#${Value.slice(1).split("").map((C) => C + C).join("")}` : Value;
        Hex.value = Value;
      } else {
        Hex.value = get();
      }
    });
  }

  group(Title, Badge, Content, Open = true) {
    return `
      <details class="property-group"${Open ? " open" : ""}>
        <summary>${Escape(Title)}${Badge ? `<span class="section-badge">${Escape(Badge)}</span>` : ""}</summary>
        <div class="group-content">${Content}</div>
      </details>`;
  }

  renderInspector() {
    const Body = Select("#inspector-body");
    SelectAll("#inspector-tabs button").forEach((Button) => {
      Button.classList.toggle("active", Button.dataset.tab === this.inspectorTab);
    });
    const Document = this.doc;
    const Layer = Document ? Document.activeLayer : null;
    if (!Document) {
      Body.innerHTML = `<div class="inspector-note">No document open.</div>`;
      return;
    }
    if (this.inspectorTab === "tool") this.renderToolTab(Body);
    else if (this.inspectorTab === "layer") this.renderLayerTab(Body, Document, Layer);
    else this.renderMaterialTab(Body, Document);
    FillIcons(Body);
  }

  renderToolTab(Body) {
    const Brush = this.engine.brush;
    const Tool = this.engine.tool;
    const TipSegment = `
      <div class="segmented" id="tip-segment">
        ${Object.entries(TIP_LABELS).map(([Value, Label]) => `<button data-tip="${Value}" class="${Brush.tip === Value ? "active" : ""}">${Label}</button>`).join("")}
      </div>`;
    let Extra = "";
    if (Tool === "smudge") {
      Extra += this.sliderRow({ id: "brush-smudge", label: "Smudge strength", min: 0.05, max: 1, step: 0.01, value: Brush.smudge, format: (V) => `${Math.round(V * 100)}%` });
    }
    if (Tool === "fill") {
      Extra += this.sliderRow({ id: "fill-tolerance", label: "Tolerance", min: 0, max: 100, step: 1, value: this.engine.fillTolerance, unit: "%" });
      Extra += this.toggleRow({ id: "fill-contiguous", label: "Contiguous region" });
    }
    if (Tool === "shape") {
      Extra += this.selectRow({ id: "shape-select", label: "Shape", value: this.engine.shapeKind, options: [["line", "Line"], ["rect", "Rectangle"], ["ellipse", "Ellipse"]] });
      Extra += this.toggleRow({ id: "shape-filled", label: "Filled (rect / ellipse)" });
    }
    const Material = this.engine.material;
    Body.innerHTML = `
      ${this.group("Brush", TOOL_LABELS[Tool].toUpperCase(), `
        <div class="brush-preview-wrap"><canvas id="brush-preview" width="128" height="128"></canvas><div class="brush-preview-copy"><strong>${Escape(TOOL_LABELS[Tool])}</strong><small id="brush-preview-text"></small></div></div>
        ${TipSegment}
        ${this.sliderRow({ id: "brush-size", label: "Size", min: 1, max: 512, step: 1, value: Brush.size, unit: "px" })}
        ${this.sliderRow({ id: "brush-hardness", label: "Hardness", min: 0, max: 1, step: 0.01, value: Brush.hardness, format: (V) => `${Math.round(V * 100)}%` })}
        ${this.sliderRow({ id: "brush-opacity", label: "Opacity", min: 0.01, max: 1, step: 0.01, value: Brush.opacity, format: (V) => `${Math.round(V * 100)}%` })}
        ${this.sliderRow({ id: "brush-flow", label: "Flow", min: 0.01, max: 1, step: 0.01, value: Brush.flow, format: (V) => `${Math.round(V * 100)}%` })}
        ${this.sliderRow({ id: "brush-spacing", label: "Spacing", min: 0.02, max: 1, step: 0.01, value: Brush.spacing, format: (V) => `${Math.round(V * 100)}%` })}
        ${this.sliderRow({ id: "brush-roundness", label: "Roundness", min: 0.05, max: 1, step: 0.01, value: Brush.roundness, format: (V) => `${Math.round(V * 100)}%` })}
        ${this.sliderRow({ id: "brush-angle", label: "Angle", min: 0, max: 180, step: 1, value: Brush.angle, unit: "°" })}
        ${this.sliderRow({ id: "brush-scatter", label: "Scatter", min: 0, max: 1, step: 0.01, value: Brush.scatter, format: (V) => `${Math.round(V * 100)}%` })}
        ${this.sliderRow({ id: "brush-smoothing", label: "Stabilizer", min: 0, max: 0.95, step: 0.01, value: Brush.smoothing, format: (V) => `${Math.round(V * 100)}%` })}
        ${Extra}
        ${this.toggleRow({ id: "pressure-size", label: "Pressure controls size" })}
        ${this.toggleRow({ id: "pressure-opacity", label: "Pressure controls flow" })}
        <p class="property-hint">Drag a numeric value sideways to scrub it — Shift slows, Alt speeds up. Symmetry lives in the transport bar.</p>
      `)}
      ${this.group("Color", "FG / BG", `
        ${this.colorRow({ id: "fg", label: "Foreground", value: this.engine.fgColor })}
        ${this.colorRow({ id: "bg", label: "Background", value: this.engine.bgColor })}
        <div class="button-row"><button class="button" id="swap-colors">Swap (X)</button><button class="button" id="sample-mid">Sample composite</button></div>
        <div class="swatch-grid" id="swatch-grid">${SWATCHES.map((Swatch) => `<button data-swatch="${Swatch}" style="background:${Swatch}" aria-label="Swatch ${Swatch}" title="${Swatch}"></button>`).join("")}</div>
      `)}
      ${this.group("Material paint", "PBR", `
        <p class="property-hint">One stroke can write albedo plus scalar channels. Erase and fill honor the same routing.</p>
        ${this.toggleRow({ id: "paint-albedo", label: "Paint albedo" })}
        ${this.toggleRow({ id: "paint-metal", label: "Paint metallic" })}
        ${this.sliderRow({ id: "material-metal", label: "Metallic value", min: 0, max: 1, step: 0.01, value: Material.metal, format: (V) => V.toFixed(2) })}
        ${this.toggleRow({ id: "paint-rough", label: "Paint roughness" })}
        ${this.sliderRow({ id: "material-rough", label: "Roughness value", min: 0, max: 1, step: 0.01, value: Material.rough, format: (V) => V.toFixed(2) })}
        ${this.toggleRow({ id: "paint-height", label: "Paint height" })}
        ${this.sliderRow({ id: "material-height", label: "Height value", min: 0, max: 1, step: 0.01, value: Material.height, format: (V) => V.toFixed(2) })}
        ${this.toggleRow({ id: "paint-emissive", label: "Paint emissive" })}
        ${this.sliderRow({ id: "material-emissive", label: "Emissive strength", min: 0, max: 2.5, step: 0.05, value: Material.emissive, format: (V) => `${V.toFixed(2)}×` })}
        ${this.colorRow({ id: "emissive-color", label: "Emissive color", value: Material.emissiveColor })}
      `)}
    `;
    // Wire brush sliders.
    const Redraw = () => {
      this.drawBrushPreview();
      this.updateChrome();
    };
    const BrushSet = (Key) => (Value) => {
      Brush[Key] = Value;
      this.engine.stampCache.key = "";
      Redraw();
    };
    this.bindSlider("brush-size", { min: 1, max: 512, step: 1, get: () => Brush.size, set: BrushSet("size"), unit: "px" });
    this.bindSlider("brush-hardness", { min: 0, max: 1, step: 0.01, get: () => Brush.hardness, set: BrushSet("hardness"), format: (V) => `${Math.round(V * 100)}%` });
    this.bindSlider("brush-opacity", { min: 0.01, max: 1, step: 0.01, get: () => Brush.opacity, set: BrushSet("opacity"), format: (V) => `${Math.round(V * 100)}%` });
    this.bindSlider("brush-flow", { min: 0.01, max: 1, step: 0.01, get: () => Brush.flow, set: BrushSet("flow"), format: (V) => `${Math.round(V * 100)}%` });
    this.bindSlider("brush-spacing", { min: 0.02, max: 1, step: 0.01, get: () => Brush.spacing, set: BrushSet("spacing"), format: (V) => `${Math.round(V * 100)}%` });
    this.bindSlider("brush-roundness", { min: 0.05, max: 1, step: 0.01, get: () => Brush.roundness, set: BrushSet("roundness"), format: (V) => `${Math.round(V * 100)}%` });
    this.bindSlider("brush-angle", { min: 0, max: 180, step: 1, get: () => Brush.angle, set: BrushSet("angle"), unit: "°" });
    this.bindSlider("brush-scatter", { min: 0, max: 1, step: 0.01, get: () => Brush.scatter, set: BrushSet("scatter"), format: (V) => `${Math.round(V * 100)}%` });
    this.bindSlider("brush-smoothing", { min: 0, max: 0.95, step: 0.01, get: () => Brush.smoothing, set: BrushSet("smoothing"), format: (V) => `${Math.round(V * 100)}%` });
    Body.querySelectorAll("#tip-segment button").forEach((Button) => {
      Button.addEventListener("click", () => {
        Brush.tip = Button.dataset.tip;
        this.engine.stampCache.key = "";
        this.renderInspector();
      });
    });
    const Smudge = Select("#brush-smudge-range");
    if (Smudge) this.bindSlider("brush-smudge", { min: 0.05, max: 1, step: 0.01, get: () => Brush.smudge, set: BrushSet("smudge"), format: (V) => `${Math.round(V * 100)}%` });
    const Tolerance = Select("#fill-tolerance-range");
    if (Tolerance) {
      this.bindSlider("fill-tolerance", { min: 0, max: 100, step: 1, get: () => this.engine.fillTolerance, set: (Value) => { this.engine.fillTolerance = Value; }, unit: "%" });
      Select("#fill-contiguous").checked = this.engine.fillContiguous;
      Select("#fill-contiguous").addEventListener("change", (Event) => {
        this.engine.fillContiguous = Event.target.checked;
      });
    }
    const ShapeSelect = Select("#shape-select");
    if (ShapeSelect) {
      ShapeSelect.addEventListener("change", (Event) => {
        this.engine.shapeKind = Event.target.value;
        this.updateChrome();
      });
      Select("#shape-filled").checked = this.engine.shapeFilled;
      Select("#shape-filled").addEventListener("change", (Event) => {
        this.engine.shapeFilled = Event.target.checked;
      });
    }
    Select("#pressure-size").checked = this.engine.pressureSize;
    Select("#pressure-opacity").checked = this.engine.pressureOpacity;
    Select("#pressure-size").addEventListener("change", (Event) => { this.engine.pressureSize = Event.target.checked; });
    Select("#pressure-opacity").addEventListener("change", (Event) => { this.engine.pressureOpacity = Event.target.checked; });
    // Colors.
    this.bindColor("fg", { get: () => this.engine.fgColor, set: (Value) => { this.engine.fgColor = Value; this.drawBrushPreview(); } });
    this.bindColor("bg", { get: () => this.engine.bgColor, set: (Value) => { this.engine.bgColor = Value; } });
    Select("#swap-colors").addEventListener("click", () => this.swapColors());
    Select("#sample-mid").addEventListener("click", () => {
      const Document = this.doc;
      if (!Document) return;
      const Sample = this.engine.sample(Document, Document.width / 2, Document.height / 2);
      if (Sample) {
        this.engine.fgColor = Sample.color;
        this.renderInspector();
        this.notify(`Sampled ${Sample.color} at texture center.`);
      }
    });
    Body.querySelectorAll("#swatch-grid button").forEach((Button) => {
      if (Button.dataset.swatch.toLowerCase() === this.engine.fgColor.toLowerCase()) Button.classList.add("active");
      Button.addEventListener("click", () => {
        this.engine.fgColor = Button.dataset.swatch;
        this.renderInspector();
      });
    });
    // Material routing.
    const MaterialSet = (Key) => (Value) => {
      Material[Key] = Value;
    };
    const Route = (Id, Key) => {
      Select(`#${Id}`).checked = Material[Key];
      Select(`#${Id}`).addEventListener("change", (Event) => {
        Material[Key] = Event.target.checked;
      });
    };
    Route("paint-albedo", "paintAlbedo");
    Route("paint-metal", "paintMetal");
    Route("paint-rough", "paintRough");
    Route("paint-height", "paintHeight");
    Route("paint-emissive", "paintEmissive");
    this.bindSlider("material-metal", { min: 0, max: 1, step: 0.01, get: () => Material.metal, set: MaterialSet("metal"), format: (V) => V.toFixed(2) });
    this.bindSlider("material-rough", { min: 0, max: 1, step: 0.01, get: () => Material.rough, set: MaterialSet("rough"), format: (V) => V.toFixed(2) });
    this.bindSlider("material-height", { min: 0, max: 1, step: 0.01, get: () => Material.height, set: MaterialSet("height"), format: (V) => V.toFixed(2) });
    this.bindSlider("material-emissive", { min: 0, max: 2.5, step: 0.05, get: () => Material.emissive, set: MaterialSet("emissive"), format: (V) => `${V.toFixed(2)}×` });
    this.bindColor("emissive-color", { get: () => Material.emissiveColor, set: (Value) => { Material.emissiveColor = Value; } });
    this.drawBrushPreview();
  }

  drawBrushPreview() {
    const Canvas = Select("#brush-preview");
    if (!Canvas) return;
    const Context = Canvas.getContext("2d");
    const Brush = this.engine.brush;
    Context.clearRect(0, 0, Canvas.width, Canvas.height);
    const Radius = Clamp(Brush.size, 4, 120) / 120 * 52 + 6;
    Context.save();
    Context.translate(Canvas.width / 2, Canvas.height / 2);
    Context.rotate((Brush.angle * Math.PI) / 180);
    Context.scale(1, Brush.roundness);
    const Gradient = Context.createRadialGradient(0, 0, 0, 0, 0, Radius);
    const Color = this.engine.tool === "eraser" ? "#ff8a80" : this.engine.fgColor;
    Gradient.addColorStop(0, Color);
    Gradient.addColorStop(Clamp(Brush.hardness, 0.03, 0.97), Color);
    Gradient.addColorStop(1, "rgba(0,0,0,0)");
    Context.globalAlpha = Brush.opacity;
    Context.fillStyle = Gradient;
    Context.beginPath();
    Context.arc(0, 0, Radius, 0, Math.PI * 2);
    Context.fill();
    Context.restore();
    const Text = Select("#brush-preview-text");
    if (Text) {
      Text.textContent = `Ø ${Math.round(Brush.size)} · ${Math.round(Brush.hardness * 100)}% hard · ${TIP_LABELS[Brush.tip]}${this.engine.symmetry !== "none" ? ` · ${this.engine.symmetry} symmetry` : ""}`;
    }
  }

  renderLayerTab(Body, Document, Layer) {
    if (!Layer) {
      Body.innerHTML = `<div class="inspector-note">Select a layer to edit its stack, transform and decal properties.</div>`;
      return;
    }
    const Meta = LAYER_KINDS[Layer.kind];
    const Index = Document.layers.findIndex((Entry) => Entry.id === Layer.id);
    let KindHtml = "";
    if (Layer.kind === "paint") {
      const Allocated = ["metal", "rough", "emissive", "height"].filter((Key) => Layer.channels[Key]);
      KindHtml = this.group("Pixels", "RASTER", `
        <div class="kv-grid">
          <span>Resolution</span><b>${Document.width} × ${Document.height}</b>
          <span>Extra channels</span><b>${Allocated.length ? Allocated.join(" · ") : "albedo only"}</b>
          <span>Mask</span><b>${Layer.channels.mask ? (Layer.maskEnabled ? "enabled" : "bypassed") : "none"}</b>
        </div>
        <div class="button-row"><button class="button" id="layer-fill-fg">Fill with FG</button><button class="button" id="layer-clear">Clear</button></div>
        <p class="property-hint">Fill with FG floods the whole layer through the current material routing at full tolerance.</p>
      `);
    } else if (Layer.kind === "fill") {
      const Params = Layer.params;
      KindHtml = this.group("Fill", "PROCEDURAL", `
        ${this.selectRow({ id: "fill-pattern", label: "Pattern", value: Params.pattern, options: [["solid", "Solid"], ["gradient", "Gradient"], ["radial", "Radial"], ["checker", "Checker"], ["stripes", "Stripes"], ["noise", "Noise"]] })}
        ${this.colorRow({ id: "fill-color", label: "Color A", value: Params.color })}
        ${this.colorRow({ id: "fill-color2", label: "Color B", value: Params.color2 })}
        ${this.sliderRow({ id: "fill-scale", label: "Scale / cells", min: 2, max: 256, step: 1, value: Params.scale, format: (V) => `${V}` })}
        ${this.sliderRow({ id: "fill-angle", label: "Angle", min: 0, max: 360, step: 1, value: Params.angle, unit: "°" })}
        ${this.sliderRow({ id: "fill-seed", label: "Noise seed", min: 1, max: 99, step: 1, value: Params.seed, format: (V) => `${V}` })}
        <div class="button-row"><button class="button" id="fill-randomize">Randomize seed</button></div>
        ${this.sliderRow({ id: "fill-metal", label: "Metallic", min: 0, max: 1, step: 0.01, value: Params.metallic, format: (V) => V.toFixed(2) })}
        ${this.sliderRow({ id: "fill-rough", label: "Roughness", min: 0, max: 1, step: 0.01, value: Params.roughness, format: (V) => V.toFixed(2) })}
        ${this.sliderRow({ id: "fill-height", label: "Height", min: 0, max: 1, step: 0.01, value: Params.height, format: (V) => V.toFixed(2) })}
        ${this.sliderRow({ id: "fill-emissive", label: "Emissive", min: 0, max: 2.5, step: 0.05, value: Params.emissive, format: (V) => `${V.toFixed(2)}×` })}
        ${this.colorRow({ id: "fill-emissive-color", label: "Emissive color", value: Params.emissiveColor })}
      `);
    } else if (Layer.kind === "adjust") {
      const Params = Layer.params;
      KindHtml = this.group("Grade", "FILTER", `
        <p class="property-hint">Grades the albedo accumulated below this layer. Opacity fades the grade; blend modes remix it.</p>
        ${this.sliderRow({ id: "adjust-brightness", label: "Brightness", min: -0.8, max: 0.8, step: 0.01, value: Params.brightness, format: (V) => `${Math.round(V * 100)}%` })}
        ${this.sliderRow({ id: "adjust-contrast", label: "Contrast", min: -0.8, max: 0.8, step: 0.01, value: Params.contrast, format: (V) => `${Math.round(V * 100)}%` })}
        ${this.sliderRow({ id: "adjust-saturation", label: "Saturation", min: -1, max: 1, step: 0.01, value: Params.saturation, format: (V) => `${Math.round(V * 100)}%` })}
        ${this.sliderRow({ id: "adjust-hue", label: "Hue shift", min: -180, max: 180, step: 1, value: Params.hue, unit: "°" })}
        ${this.sliderRow({ id: "adjust-blur", label: "Soften", min: 0, max: 12, step: 0.1, value: Params.blur, unit: "px" })}
        ${this.sliderRow({ id: "adjust-invert", label: "Invert", min: 0, max: 1, step: 0.01, value: Params.invert, format: (V) => `${Math.round(V * 100)}%` })}
      `);
    } else if (Layer.kind === "svg") {
      const Params = Layer.params;
      KindHtml = this.group("Vector decal", "SVG", `
        <p class="property-hint">Non-destructive vector source. Edits re-rasterize instantly; transforms stay live.</p>
        <textarea class="code" id="svg-source" spellcheck="false" aria-label="SVG source"></textarea>
        <div class="button-row"><button class="button" id="svg-apply">Apply source</button><button class="button" id="svg-upload">Import .svg</button></div>
        ${this.colorRow({ id: "svg-tint", label: "Tint glaze", value: Params.tint || "#ffffff" })}
        <div class="button-row"><button class="button" id="svg-clear-tint">Clear tint</button></div>
      `) + this.decalMaterialGroup(Layer) + this.transformGroup(Layer);
    } else if (Layer.kind === "text") {
      const Params = Layer.params;
      KindHtml = this.group("Type decal", "TEXT", `
        <div class="property-row select-row"><span class="property-label">Copy</span><textarea class="code" id="text-copy" style="min-height:64px" spellcheck="false"></textarea></div>
        ${this.selectRow({ id: "text-font", label: "Typeface", value: Params.font, options: FONT_STACKS })}
        ${this.sliderRow({ id: "text-size", label: "Size", min: 0.02, max: 0.9, step: 0.005, value: Params.size, format: (V) => `${Math.round(V * 100)}%` })}
        ${this.selectRow({ id: "text-weight", label: "Weight", value: String(Params.weight), options: [["300", "Light"], ["400", "Regular"], ["500", "Medium"], ["700", "Bold"], ["900", "Black"]] })}
        ${this.toggleRow({ id: "text-italic", label: "Italic" })}
        <div class="segmented" id="text-align">${["left", "center", "right"].map((Align) => `<button data-align="${Align}" class="${Params.align === Align ? "active" : ""}">${Align[0].toUpperCase() + Align.slice(1)}</button>`).join("")}</div>
        ${this.colorRow({ id: "text-color", label: "Fill", value: Params.color })}
        ${this.sliderRow({ id: "text-stroke", label: "Outline", min: 0, max: 0.08, step: 0.002, value: Params.stroke, format: (V) => `${Math.round(V * 1000) / 10}%` })}
        ${this.colorRow({ id: "text-stroke-color", label: "Outline color", value: Params.strokeColor })}
        ${this.sliderRow({ id: "text-spacing", label: "Tracking", min: 0, max: 0.6, step: 0.005, value: Params.spacing, format: (V) => `${Math.round(V * 100)}%` })}
        ${this.sliderRow({ id: "text-leading", label: "Leading", min: 0.8, max: 2, step: 0.01, value: Params.lineHeight, format: (V) => `${Math.round(V * 100)}%` })}
      `) + this.decalMaterialGroup(Layer) + this.transformGroup(Layer);
    } else if (Layer.kind === "image") {
      KindHtml = this.group("Image decal", "RASTER", `
        <div class="kv-grid"><span>Source</span><b>${Layer.params.dataURL ? `${Math.round(Layer.params.dataURL.length / 1024)} KB embedded` : "none"}</b></div>
        <div class="button-row"><button class="button" id="image-replace">Replace image…</button></div>
        <p class="property-hint">Images embed into the .texpaint project so decals travel with the stack.</p>
      `) + this.decalMaterialGroup(Layer) + this.transformGroup(Layer);
    }

    Body.innerHTML = `
      ${this.group("Stack", Meta.badge, `
        ${this.sliderRow({ id: "layer-opacity", label: "Opacity", min: 0, max: 1, step: 0.01, value: Layer.opacity, format: (V) => `${Math.round(V * 100)}%` })}
        ${this.selectRow({ id: "layer-blend", label: "Blend mode", value: Layer.blend, options: BLEND_MODES })}
        ${this.toggleRow({ id: "layer-clip", label: "Clip to layers below" })}
        ${this.toggleRow({ id: "layer-lock", label: "Lock pixels" })}
        ${Layer.channels.mask ? this.toggleRow({ id: "layer-mask-edit", label: "Paint on mask (◐)" }) : ""}
        ${Layer.channels.mask ? this.toggleRow({ id: "layer-mask-overlay", label: "Rubylith overlay" }) : ""}
        <div class="button-row">
          ${Layer.channels.mask
            ? `<button class="button" id="layer-mask-toggle">${Layer.maskEnabled ? "Bypass mask" : "Enable mask"}</button><button class="button danger" id="layer-mask-remove">Delete</button>`
            : (Layer.kind === "adjust" ? "" : `<button class="button" id="layer-mask-add">Add mask</button>`)}
        </div>
        ${Layer.channels.mask ? `<div class="button-row"><button class="button" id="layer-mask-invert">Invert</button><button class="button" id="layer-mask-reveal">Reveal all</button><button class="button" id="layer-mask-conceal">Conceal</button></div>` : ""}
        ${Layer.channels.mask ? this.sliderRow({ id: "layer-mask-feather", label: "Feather", min: 0, max: 24, step: 0.5, value: 0, unit: "px" }) : ""}
        ${Layer.channels.mask ? this.sliderRow({ id: "layer-mask-density", label: "Density", min: 0, max: 1, step: 0.01, value: Layer.maskDensity === undefined ? 1 : Layer.maskDensity, format: (V) => `${Math.round(V * 100)}%` }) : ""}
        <div class="button-row"><button class="button" id="layer-quickmask">Quick mask (Q)</button></div>
        ${Layer.maskSelected ? `<p class="property-hint">◐ Painting reveals, erasing conceals. The channel strip previews the mask.</p>` : ""}
        ${Document.quickMaskActive ? `<p class="property-hint">◑ Quick mask live — paint, then Q to commit or Esc to discard.</p>` : ""}
      `)}
      ${KindHtml}
      ${this.group("Arrange", `${Index + 1} / ${Document.layers.length}`, `
        <div class="button-row"><button class="button" id="layer-raise">Raise</button><button class="button" id="layer-lower">Lower</button></div>
        <div class="button-row"><button class="button" id="layer-duplicate">Duplicate</button><button class="button" id="layer-merge">Merge down</button></div>
        <div class="button-row">${Layer.kind === "paint" ? `<button class="button" id="layer-raster" disabled>Raster</button>` : `<button class="button" id="layer-raster">Rasterize</button>`}<button class="button danger" id="layer-delete">Delete</button></div>
      `, false)}
    `;

    // Stack wiring.
    this.bindSlider("layer-opacity", {
      min: 0, max: 1, step: 0.01,
      get: () => Layer.opacity,
      set: (Value) => {
        Layer.opacity = Value;
        Layer.thumbDirty = true;
        Document.markDirty();
      },
      format: (V) => `${Math.round(V * 100)}%`,
      onCommit: () => this.commitLayerProps(Layer, { opacity: Layer.opacity }, "Layer opacity"),
      onStart: () => { this.sliderStartValue = Layer.opacity; },
    });
    Select("#layer-blend").addEventListener("change", (Event) => {
      Document.setLayerProps(Layer, { blend: Event.target.value }, "Blend mode");
      this.renderLayerList();
    });
    Select("#layer-clip").checked = Layer.clip;
    Select("#layer-clip").disabled = Index === 0;
    Select("#layer-clip").addEventListener("change", (Event) => {
      Document.setLayerProps(Layer, { clip: Event.target.checked }, "Clip to below");
      this.renderLayerList();
    });
    Select("#layer-lock").checked = Layer.locked;
    Select("#layer-lock").addEventListener("change", (Event) => {
      Document.setLayerProps(Layer, { locked: Event.target.checked }, "Lock layer");
      this.renderLayerList();
    });
    const MaskEdit = Select("#layer-mask-edit");
    if (MaskEdit) {
      MaskEdit.checked = Layer.maskSelected;
      MaskEdit.addEventListener("change", (Event) => {
        Document.setLayerProps(Layer, { maskSelected: Event.target.checked }, "Edit mask");
        this.renderAll();
      });
    }
    const MaskAdd = Select("#layer-mask-add");
    if (MaskAdd) MaskAdd.addEventListener("click", () => {
      Document.toggleMask(Layer);
      this.renderAll();
    });
    const MaskToggle = Select("#layer-mask-toggle");
    if (MaskToggle) MaskToggle.addEventListener("click", () => {
      Document.toggleMask(Layer);
      this.renderAll();
    });
    const MaskRemove = Select("#layer-mask-remove");
    if (MaskRemove) MaskRemove.addEventListener("click", () => {
      Document.removeMask(Layer);
      this.renderAll();
    });
    const MaskOverlay = Select("#layer-mask-overlay");
    if (MaskOverlay) {
      MaskOverlay.checked = this.view.showMaskOverlay || Layer.maskSelected;
      MaskOverlay.addEventListener("change", (Event) => {
        this.view.showMaskOverlay = Event.target.checked;
      });
    }
    const MaskInvert = Select("#layer-mask-invert");
    if (MaskInvert) MaskInvert.addEventListener("click", () => {
      Document.invertMask(Layer);
      this.updateChrome();
    });
    const MaskReveal = Select("#layer-mask-reveal");
    if (MaskReveal) MaskReveal.addEventListener("click", () => {
      Document.fillMask(Layer, true);
      this.updateChrome();
    });
    const MaskConceal = Select("#layer-mask-conceal");
    if (MaskConceal) MaskConceal.addEventListener("click", () => {
      Document.fillMask(Layer, false);
      this.updateChrome();
    });
    const MaskFeather = Select("#layer-mask-feather-range");
    if (MaskFeather) {
      let Snapshot = null;
      let Live = 0;
      this.bindSlider("layer-mask-feather", {
        min: 0, max: 24, step: 0.5,
        get: () => Live,
        set: (Value) => {
          Live = Value;
          if (Snapshot) Document.featherPreview(Layer, Snapshot, Value);
        },
        unit: "px",
        onStart: () => {
          Snapshot = makeCanvas(Document.width, Document.height);
          Snapshot.getContext("2d").drawImage(Layer.channels.mask, 0, 0);
        },
        onCommit: (Done) => {
          if (!Done || !Snapshot) return;
          Document.commitMaskFeather(Layer, Snapshot);
          Snapshot = null;
          Live = 0;
          // One-shot control: reset to zero without losing inspector state.
          const Range = Select("#layer-mask-feather-range");
          const NumberInput = Select("#layer-mask-feather-number");
          const Output = Select("#layer-mask-feather-output");
          if (Range) {
            Range.value = 0;
            Range.style.setProperty("--fraction", "0");
            Range.style.setProperty("--range", "0%");
          }
          if (NumberInput) NumberInput.value = 0;
          if (Output) Output.textContent = "0 px";
        },
      });
    }
    const MaskDensity = Select("#layer-mask-density-range");
    if (MaskDensity) {
      let StartValue = Layer.maskDensity === undefined ? 1 : Layer.maskDensity;
      this.bindSlider("layer-mask-density", {
        min: 0, max: 1, step: 0.01,
        get: () => (Layer.maskDensity === undefined ? 1 : Layer.maskDensity),
        set: (Value) => {
          Layer.maskDensity = Value;
          Layer.thumbDirty = true;
          Document.markDirty();
        },
        format: (V) => `${Math.round(V * 100)}%`,
        onStart: () => {
          StartValue = Layer.maskDensity === undefined ? 1 : Layer.maskDensity;
        },
        onCommit: (Done) => {
          if (Done) this.pushPropsHistory(Layer, { maskDensity: StartValue }, { maskDensity: Layer.maskDensity }, "Mask density");
        },
      });
    }
    const QuickMaskButton = Select("#layer-quickmask");
    if (QuickMaskButton) QuickMaskButton.addEventListener("click", () => this.toggleQuickMask());

    // Kind wiring.
    if (Layer.kind === "paint") this.wirePaintTab(Document, Layer);
    if (Layer.kind === "fill") this.wireFillTab(Document, Layer);
    if (Layer.kind === "adjust") this.wireAdjustTab(Document, Layer);
    if (Layer.kind === "svg") this.wireSvgTab(Document, Layer);
    if (Layer.kind === "text") this.wireTextTab(Document, Layer);
    if (Layer.kind === "image") this.wireImageTab(Document, Layer);
    if (["svg", "text", "image"].includes(Layer.kind)) {
      this.wireTransformTab(Document, Layer);
      this.wireDecalMaterialTab(Document, Layer);
    }

    // Arrange wiring.
    Select("#layer-raise").disabled = Index >= Document.layers.length - 1;
    Select("#layer-lower").disabled = Index <= 0;
    Select("#layer-raise").addEventListener("click", () => {
      Document.moveActiveLayer(1);
      this.renderLayerList();
      this.renderInspector();
      this.updateChrome();
    });
    Select("#layer-lower").addEventListener("click", () => {
      Document.moveActiveLayer(-1);
      this.renderLayerList();
      this.renderInspector();
      this.updateChrome();
    });
    Select("#layer-duplicate").addEventListener("click", () => {
      Document.duplicateActiveLayer();
      this.renderAll();
    });
    Select("#layer-merge").addEventListener("click", () => {
      const Result = Document.mergeActiveDown();
      if (Result === "rasterize") {
        this.notify("Only paint layers merge — rasterize the decal first.");
        return;
      }
      if (!Result) {
        this.notify("Nothing below to merge into.");
        return;
      }
      this.renderAll();
    });
    const RasterButton = Select("#layer-raster");
    if (RasterButton && !RasterButton.disabled) {
      RasterButton.addEventListener("click", () => {
        Document.rasterizeActiveLayer();
        this.renderAll();
      });
    }
    Select("#layer-delete").addEventListener("click", () => this.deleteActiveLayer());
  }

  /** Push a layer-props history step, coalescing rapid repeats. */
  pushPropsHistory(Layer, Before, After, Label) {
    const Document = this.doc;
    if (!Document) return;
    const Keys = Object.keys(After);
    const Same = Keys.every((Key) => JSON.stringify(Before[Key]) === JSON.stringify(After[Key]));
    if (Same) return;
    const Last = Document.history[Document.historyIndex];
    if (Last && Last.type === "props" && Last.layerId === Layer.id && Keys.every((Key) => Last.keys.includes(Key)) && performance.now() - Last.time < 2500) {
      Last.after = Object.assign({}, Last.after, JSON.parse(JSON.stringify(After)));
      Last.time = performance.now();
      Last.label = Label;
      return;
    }
    const ApplyProps = (Patch) => {
      const Target = Document.layers.find((Entry) => Entry.id === Layer.id);
      if (!Target) return;
      Object.assign(Target, JSON.parse(JSON.stringify(Patch)));
      Target.rasterDirty = true;
      Target.fillCache = null;
      Target.thumbDirty = true;
      Document.markDirty();
      this.renderAll();
    };
    // Undo/redo read through the entry so coalesced repeats redo correctly.
    const Entry = {
      label: Label,
      type: "props",
      time: performance.now(),
      keys: Keys,
      layerId: Layer.id,
      before: JSON.parse(JSON.stringify(Before)),
      after: JSON.parse(JSON.stringify(After)),
      undo: () => ApplyProps(Entry.before),
      redo: () => ApplyProps(Entry.after),
    };
    Document.pushHistory(Entry);
  }

  /** Live param mutation helper for decal/fill sliders (no history until release). */
  liveParam(Layer, Key, Label) {
    const Document = this.doc;
    const Helper = {
      snapshot: null,
      get: () => Layer.params[Key],
      set: (Value) => {
        Layer.params[Key] = Value;
        Layer.rasterDirty = true;
        Layer.fillCache = null;
        Layer.thumbDirty = true;
        Document.markDirty();
      },
      start: () => {
        Helper.snapshot = { ...Layer.params };
      },
      commit: () => {
        if (!Helper.snapshot) return;
        this.pushPropsHistory(Layer, { params: Helper.snapshot }, { params: { ...Layer.params } }, Label);
        Helper.snapshot = null;
      },
    };
    return Helper;
  }

  /** History commit for slider-driven top-level props (called on release). */
  commitLayerProps(Layer, Props, Label) {
    const Before = {};
    for (const Key of Object.keys(Props)) {
      Before[Key] = this.sliderStartValue !== undefined && Object.keys(Props).length === 1 ? this.sliderStartValue : Props[Key];
    }
    this.sliderStartValue = undefined;
    this.pushPropsHistory(Layer, Before, { ...Props }, Label);
  }

  wirePaintTab(Document, Layer) {
    Select("#layer-clear").addEventListener("click", () => {
      if (Document.clearActiveLayer()) this.renderAll();
    });
    Select("#layer-fill-fg").addEventListener("click", () => {
      if (Layer.locked) return;
      const Capture = {};
      const Targets = ["albedo", "metal", "rough", "height", "emissive"].filter((Key) => {
        if (Key === "albedo") return this.engine.material.paintAlbedo;
        if (Key === "metal") return this.engine.material.paintMetal;
        if (Key === "rough") return this.engine.material.paintRough;
        if (Key === "height") return this.engine.material.paintHeight;
        if (Key === "emissive") return this.engine.material.paintEmissive;
        return false;
      });
      if (!Targets.length) {
        this.notify("Enable a material channel in the Tool tab first.");
        return;
      }
      for (const Key of Targets) {
        if (!Layer.channels[Key]) Layer.channels[Key] = makeCanvas(Document.width, Document.height);
        const Clone = makeCanvas(Document.width, Document.height);
        Clone.getContext("2d").drawImage(Layer.channels[Key], 0, 0);
        Capture[Key] = Clone;
      }
      const Paint = (Key, Fill) => {
        const Context = Layer.channels[Key].getContext("2d");
        Context.save();
        Context.globalCompositeOperation = "source-over";
        Context.fillStyle = Fill;
        Context.fillRect(0, 0, Document.width, Document.height);
        Context.restore();
      };
      for (const Key of Targets) {
        if (Key === "albedo") Paint(Key, this.engine.fgColor);
        if (Key === "metal") Paint(Key, grayCss(this.engine.material.metal));
        if (Key === "rough") Paint(Key, grayCss(this.engine.material.rough));
        if (Key === "height") Paint(Key, grayCss(this.engine.material.height));
        if (Key === "emissive") Paint(Key, this.engine.material.emissiveColor);
      }
      Layer.thumbDirty = true;
      Document.markDirty();
      Document.pushStrokeHistory("Fill with FG", Layer, Capture, { x: 0, y: 0, w: Document.width, h: Document.height });
      this.renderAll();
    });
  }

  wireFillTab(Document, Layer) {
    Select("#fill-pattern").addEventListener("change", (Event) => {
      Document.setLayerProps(Layer, { params: { ...Layer.params, pattern: Event.target.value } }, "Fill pattern");
      this.renderInspector();
    });
    this.bindColor("fill-color", {
      get: () => Layer.params.color,
      set: (Value) => {
        Layer.params.color = Value;
        Layer.fillCache = null;
        Layer.thumbDirty = true;
        Document.markDirty();
        Document.dirty = true;
      },
    });
    this.bindColor("fill-color2", {
      get: () => Layer.params.color2,
      set: (Value) => {
        Layer.params.color2 = Value;
        Layer.fillCache = null;
        Layer.thumbDirty = true;
        Document.markDirty();
        Document.dirty = true;
      },
    });
    this.bindColor("fill-emissive-color", {
      get: () => Layer.params.emissiveColor,
      set: (Value) => {
        Layer.params.emissiveColor = Value;
        Layer.fillCache = null;
        Layer.thumbDirty = true;
        Document.markDirty();
        Document.dirty = true;
      },
    });
    const Numeric = (Id, Key, Min, Max, Step, Label, Format) => {
      const Live = this.liveParam(Layer, Key, Label);
      this.bindSlider(Id, { min: Min, max: Max, step: Step, get: Live.get, set: Live.set, format: Format, onCommit: (Done) => { if (Done) Live.commit(); }, onStart: () => Live.start() });
    };
    Numeric("fill-scale", "scale", 2, 256, 1, "Fill scale", (V) => `${V}`);
    Numeric("fill-angle", "angle", 0, 360, 1, "Fill angle");
    Numeric("fill-seed", "seed", 1, 99, 1, "Noise seed", (V) => `${V}`);
    Numeric("fill-metal", "metallic", 0, 1, 0.01, "Fill metallic", (V) => V.toFixed(2));
    Numeric("fill-rough", "roughness", 0, 1, 0.01, "Fill roughness", (V) => V.toFixed(2));
    Numeric("fill-height", "height", 0, 1, 0.01, "Fill height", (V) => V.toFixed(2));
    Numeric("fill-emissive", "emissive", 0, 2.5, 0.05, "Fill emissive", (V) => `${V.toFixed(2)}×`);
    Select("#fill-randomize").addEventListener("click", () => {
      Document.setLayerProps(Layer, { params: { ...Layer.params, seed: 1 + Math.floor(Math.random() * 98) } }, "Randomize seed");
      this.renderInspector();
    });
  }

  wireAdjustTab(Document, Layer) {
    const Numeric = (Id, Key, Min, Max, Step, Label, Format) => {
      const Live = this.liveParam(Layer, Key, Label);
      this.bindSlider(Id, { min: Min, max: Max, step: Step, get: Live.get, set: Live.set, format: Format, unit: Id === "adjust-hue" ? "°" : "", onCommit: (Done) => { if (Done) Live.commit(); }, onStart: () => Live.start() });
    };
    const Percent = (V) => `${Math.round(V * 100)}%`;
    Numeric("adjust-brightness", "brightness", -0.8, 0.8, 0.01, "Brightness", Percent);
    Numeric("adjust-contrast", "contrast", -0.8, 0.8, 0.01, "Contrast", Percent);
    Numeric("adjust-saturation", "saturation", -1, 1, 0.01, "Saturation", Percent);
    Numeric("adjust-hue", "hue", -180, 180, 1, "Hue shift");
    Numeric("adjust-blur", "blur", 0, 12, 0.1, "Soften");
    Numeric("adjust-invert", "invert", 0, 1, 0.01, "Invert", Percent);
  }

  wireSvgTab(Document, Layer) {
    const Source = Select("#svg-source");
    Source.value = Layer.params.svg || "";
    Select("#svg-apply").addEventListener("click", () => {
      Document.setLayerProps(Layer, { params: { ...Layer.params, svg: Source.value } }, "Edit SVG source");
      Layer.artCache = null;
      this.renderAll();
    });
    Select("#svg-upload").addEventListener("click", () => Select("#decal-file").click());
    this.bindColor("svg-tint", {
      get: () => Layer.params.tint || "#ffffff",
      set: (Value) => {
        Layer.params.tint = Value;
        Layer.rasterDirty = true;
        Layer.thumbDirty = true;
        Document.markDirty();
        Document.dirty = true;
      },
    });
    Select("#svg-clear-tint").addEventListener("click", () => {
      Document.setLayerProps(Layer, { params: { ...Layer.params, tint: "" } }, "Clear tint");
      this.renderInspector();
    });
  }

  wireTextTab(Document, Layer) {
    const Copy = Select("#text-copy");
    Copy.value = Layer.params.text || "";
    Copy.addEventListener("change", () => {
      Document.setLayerProps(Layer, { params: { ...Layer.params, text: Copy.value } }, "Edit text");
      this.renderLayerList();
    });
    Select("#text-font").addEventListener("change", (Event) => {
      Document.setLayerProps(Layer, { params: { ...Layer.params, font: Event.target.value } }, "Typeface");
    });
    Select("#text-weight").addEventListener("change", (Event) => {
      Document.setLayerProps(Layer, { params: { ...Layer.params, weight: parseInt(Event.target.value, 10) } }, "Type weight");
    });
    Select("#text-italic").checked = !!Layer.params.italic;
    Select("#text-italic").addEventListener("change", (Event) => {
      Document.setLayerProps(Layer, { params: { ...Layer.params, italic: Event.target.checked } }, "Italic");
    });
    BodyAlignFix: {
      const Segment = Select("#text-align");
      if (Segment) {
        Segment.querySelectorAll("button").forEach((Button) => {
          Button.addEventListener("click", () => {
            Document.setLayerProps(Layer, { params: { ...Layer.params, align: Button.dataset.align } }, "Text align");
            this.renderInspector();
          });
        });
      }
    }
    const Numeric = (Id, Key, Min, Max, Step, Label, Format) => {
      const Live = this.liveParam(Layer, Key, Label);
      this.bindSlider(Id, { min: Min, max: Max, step: Step, get: Live.get, set: Live.set, format: Format, onCommit: (Done) => { if (Done) Live.commit(); }, onStart: () => Live.start() });
    };
    Numeric("text-size", "size", 0.02, 0.9, 0.005, "Type size", (V) => `${Math.round(V * 100)}%`);
    Numeric("text-stroke", "stroke", 0, 0.08, 0.002, "Type outline", (V) => `${Math.round(V * 1000) / 10}%`);
    Numeric("text-spacing", "spacing", 0, 0.6, 0.005, "Tracking", (V) => `${Math.round(V * 100)}%`);
    Numeric("text-leading", "lineHeight", 0.8, 2, 0.01, "Leading", (V) => `${Math.round(V * 100)}%`);
    this.bindColor("text-color", {
      get: () => Layer.params.color,
      set: (Value) => {
        Layer.params.color = Value;
        Layer.rasterDirty = true;
        Layer.thumbDirty = true;
        Document.markDirty();
        Document.dirty = true;
      },
    });
    this.bindColor("text-stroke-color", {
      get: () => Layer.params.strokeColor,
      set: (Value) => {
        Layer.params.strokeColor = Value;
        Layer.rasterDirty = true;
        Layer.thumbDirty = true;
        Document.markDirty();
        Document.dirty = true;
      },
    });
  }

  wireImageTab(Document, Layer) {
    void Document;
    void Layer;
    Select("#image-replace").addEventListener("click", () => Select("#layer-image-file").click());
  }

  transformGroup(Layer) {
    void Layer;
    return this.group("Transform", "DECAL", `
      ${this.sliderRow({ id: "decal-x", label: "Position X", min: -0.5, max: 1.5, step: 0.001, value: 0.5, format: (V) => `${Math.round(V * 100)}%` })}
      ${this.sliderRow({ id: "decal-y", label: "Position Y", min: -0.5, max: 1.5, step: 0.001, value: 0.5, format: (V) => `${Math.round(V * 100)}%` })}
      ${this.sliderRow({ id: "decal-scale", label: "Scale", min: 0.01, max: 3, step: 0.005, value: 0.5, format: (V) => `${Math.round(V * 100)}%` })}
      ${this.sliderRow({ id: "decal-rotation", label: "Rotation", min: -180, max: 180, step: 0.5, value: 0, unit: "°" })}
      ${this.toggleRow({ id: "decal-flipx", label: "Flip horizontal" })}
      ${this.toggleRow({ id: "decal-flipy", label: "Flip vertical" })}
      <div class="button-row"><button class="button" id="decal-center">Center</button><button class="button" id="decal-fit">Fit to canvas</button></div>
      <p class="property-hint">Move tool (V) drags the decal directly; corners scale, the satellite handle rotates.</p>
    `);
  }

  wireTransformTab(Document, Layer) {
    const Numeric = (Id, Key, Min, Max, Step, Label, Format, Unit = "") => {
      const Live = this.liveParam(Layer, Key, Label);
      this.bindSlider(Id, { min: Min, max: Max, step: Step, get: Live.get, set: Live.set, format: Format, unit: Unit, onCommit: (Done) => { if (Done) Live.commit(); }, onStart: () => Live.start() });
    };
    const Percent = (V) => `${Math.round(V * 100)}%`;
    Numeric("decal-x", "x", -0.5, 1.5, 0.001, "Decal X", Percent);
    Numeric("decal-y", "y", -0.5, 1.5, 0.001, "Decal Y", Percent);
    Numeric("decal-scale", "scale", 0.01, 3, 0.005, "Decal scale", Percent);
    Numeric("decal-rotation", "rotation", -180, 180, 0.5, "Decal rotation");
    Select("#decal-flipx").checked = !!Layer.params.flipX;
    Select("#decal-flipy").checked = !!Layer.params.flipY;
    Select("#decal-flipx").addEventListener("change", (Event) => {
      Document.setLayerProps(Layer, { params: { ...Layer.params, flipX: Event.target.checked } }, "Flip decal");
    });
    Select("#decal-flipy").addEventListener("change", (Event) => {
      Document.setLayerProps(Layer, { params: { ...Layer.params, flipY: Event.target.checked } }, "Flip decal");
    });
    Select("#decal-center").addEventListener("click", () => {
      Document.setLayerProps(Layer, { params: { ...Layer.params, x: 0.5, y: 0.5 } }, "Center decal");
      this.renderInspector();
    });
    Select("#decal-fit").addEventListener("click", () => {
      Document.setLayerProps(Layer, { params: { ...Layer.params, x: 0.5, y: 0.5, scale: 1, rotation: 0 } }, "Fit decal");
      this.renderInspector();
    });
  }

  decalMaterialGroup(Layer) {
    const Params = Layer.params;
    const Enabled = (Value) => Value !== undefined && Value !== null && Value >= 0;
    return this.group("Decal material", "PBR", `
      <p class="property-hint">Disabled channels leave the surface below untouched; enabled ones stamp flat PBR through the decal alpha.</p>
      ${this.toggleRow({ id: "decal-metal-on", label: "Write metallic" })}
      ${this.sliderRow({ id: "decal-metal", label: "Metallic", min: 0, max: 1, step: 0.01, value: Enabled(Params.metallic) ? Params.metallic : 0, format: (V) => V.toFixed(2) })}
      ${this.toggleRow({ id: "decal-rough-on", label: "Write roughness" })}
      ${this.sliderRow({ id: "decal-rough", label: "Roughness", min: 0, max: 1, step: 0.01, value: Enabled(Params.roughness) ? Params.roughness : 0.5, format: (V) => V.toFixed(2) })}
      ${this.toggleRow({ id: "decal-height-on", label: "Write height" })}
      ${this.sliderRow({ id: "decal-height", label: "Height", min: 0, max: 1, step: 0.01, value: Enabled(Params.height) ? Params.height : 0.5, format: (V) => V.toFixed(2) })}
      ${this.sliderRow({ id: "decal-emissive", label: "Emissive", min: 0, max: 2.5, step: 0.05, value: Params.emissive || 0, format: (V) => `${V.toFixed(2)}×` })}
      ${this.colorRow({ id: "decal-emissive-color", label: "Emissive color", value: Params.emissiveColor || "#ffffff" })}
    `, false);
  }

  wireDecalMaterialTab(Document, Layer) {
    const Flag = (ToggleId, Key, DefaultValue) => {
      const Toggle = Select(`#${ToggleId}`);
      Toggle.checked = Layer.params[Key] >= 0;
      Toggle.addEventListener("change", (Event) => {
        Document.setLayerProps(Layer, { params: { ...Layer.params, [Key]: Event.target.checked ? DefaultValue : -1 } }, "Decal material");
        this.renderInspector();
      });
    };
    Flag("decal-metal-on", "metallic", 0);
    Flag("decal-rough-on", "roughness", 0.5);
    Flag("decal-height-on", "height", 0.5);
    const Numeric = (Id, Key, Label) => {
      const Live = this.liveParam(Layer, Key, Label);
      this.bindSlider(Id, {
        min: 0, max: 1, step: 0.01,
        get: () => (Layer.params[Key] >= 0 ? Layer.params[Key] : (Key === "metallic" ? 0 : 0.5)),
        set: (Value) => {
          if (Layer.params[Key] < 0) Layer.params[Key] = Value;
          else Live.set(Value);
          Layer.rasterDirty = true;
          Layer.fillCache = null;
          Layer.thumbDirty = true;
          Document.markDirty();
        },
        format: (V) => V.toFixed(2),
        onCommit: (Done) => { if (Done) Live.commit(); }, onStart: () => Live.start(),
      });
    };
    Numeric("decal-metal", "metallic", "Decal metallic");
    Numeric("decal-rough", "roughness", "Decal roughness");
    Numeric("decal-height", "height", "Decal height");
    const Emissive = this.liveParam(Layer, "emissive", "Decal emissive");
    this.bindSlider("decal-emissive", { min: 0, max: 2.5, step: 0.05, get: Emissive.get, set: Emissive.set, format: (V) => `${V.toFixed(2)}×`, onCommit: (Done) => { if (Done) Emissive.commit(); }, onStart: () => Emissive.start() });
    this.bindColor("decal-emissive-color", {
      get: () => Layer.params.emissiveColor || "#ffffff",
      set: (Value) => {
        Layer.params.emissiveColor = Value;
        Layer.rasterDirty = true;
        Layer.thumbDirty = true;
        Document.markDirty();
        Document.dirty = true;
      },
    });
  }

  renderMaterialTab(Body, Document) {
    const Material = Document.material;
    Body.innerHTML = `
      ${this.group("Base material", Document.baseMode.toUpperCase(), `
        <div class="segmented" id="base-segment">
          <button data-base="material" class="${Document.baseMode === "material" ? "active" : ""}">Material base</button>
          <button data-base="transparent" class="${Document.baseMode === "transparent" ? "active" : ""}">Transparent</button>
        </div>
        <div class="material-preview-wrap"><canvas id="material-preview" width="640" height="240"></canvas></div>
        ${this.colorRow({ id: "base-albedo", label: "Albedo", value: Material.albedo })}
        ${this.sliderRow({ id: "base-metal", label: "Metallic", min: 0, max: 1, step: 0.01, value: Material.metallic, format: (V) => V.toFixed(2) })}
        ${this.sliderRow({ id: "base-rough", label: "Roughness", min: 0.04, max: 1, step: 0.01, value: Material.roughness, format: (V) => V.toFixed(2) })}
        ${this.sliderRow({ id: "base-height", label: "Height base", min: 0, max: 1, step: 0.01, value: Material.height, format: (V) => V.toFixed(2) })}
        ${this.sliderRow({ id: "base-normal", label: "Normal strength", min: 0.05, max: 3, step: 0.05, value: Material.normalStrength, format: (V) => `${V.toFixed(2)}×` })}
        ${this.sliderRow({ id: "base-emissive", label: "Emissive", min: 0, max: 2.5, step: 0.05, value: Material.emissiveStrength, format: (V) => `${V.toFixed(2)}×` })}
        ${this.colorRow({ id: "base-emissive-color", label: "Emissive color", value: Material.emissive })}
        <p class="property-hint">The base coats every channel before layers composite. Transparent base keeps albedo alpha for decals and trims.</p>
      `)}
      ${this.group("Composite", "LIVE", `
        <div class="kv-grid">
          <span>Layers</span><b>${Document.layers.length}</b>
          <span>Build time</span><b>${Document.lastCompositeMs.toFixed(1)} ms</b>
          <span>History</span><b>${Document.historyIndex + 1} / ${Document.history.length}</b>
          <span>Normal map</span><b>${Document.normalDirty ? "stale" : "fresh"}</b>
        </div>
        <div class="button-row"><button class="button" id="material-rebuild">Rebuild normals</button></div>
      `)}
      ${this.group("Export", "PNG", `
        <div class="button-row"><button class="button primary" id="material-export">Export textures…</button></div>
        <div class="button-row"><button class="button" id="material-save">Save .texpaint</button></div>
        <p class="property-hint">Per-channel PNGs, a packed ORM and the project file. Naming and resolution live in the export sheet.</p>
      `)}
    `;
    Body.querySelectorAll("#base-segment button").forEach((Button) => {
      Button.addEventListener("click", () => {
        this.pushMaterialHistory({ baseMode: Document.baseMode }, { baseMode: Button.dataset.base }, "Base mode");
        Document.baseMode = Button.dataset.base;
        Document.markDirty();
        this.renderInspector();
      });
    });
    this.bindColor("base-albedo", {
      get: () => Document.material.albedo,
      set: (Value) => {
        Document.material.albedo = Value;
        Document.markDirty();
        this.drawMaterialPreview();
      },
    });
    this.bindColor("base-emissive-color", {
      get: () => Document.material.emissive,
      set: (Value) => {
        Document.material.emissive = Value;
        Document.markDirty();
        this.drawMaterialPreview();
      },
    });
    const Numeric = (Id, Key, Min, Max, Step, Label, Format) => {
      let StartValue = Document.material[Key];
      this.bindSlider(Id, {
        min: Min, max: Max, step: Step,
        get: () => Document.material[Key],
        set: (Value) => {
          Document.material[Key] = Value;
          Document.markDirty();
          this.drawMaterialPreview();
        },
        format: Format,
        onStart: () => {
          StartValue = Document.material[Key];
        },
        onCommit: (Done) => {
          if (Done) this.pushMaterialHistory({ [Key]: StartValue }, { [Key]: Document.material[Key] }, Label);
        },
      });
    };
    const Fixed = (V) => V.toFixed(2);
    Numeric("base-metal", "metallic", 0, 1, 0.01, "Base metallic", Fixed);
    Numeric("base-rough", "roughness", 0.04, 1, 0.01, "Base roughness", Fixed);
    Numeric("base-height", "height", 0, 1, 0.01, "Height base", Fixed);
    Numeric("base-normal", "normalStrength", 0.05, 3, 0.05, "Normal strength", (V) => `${V.toFixed(2)}×`);
    Numeric("base-emissive", "emissiveStrength", 0, 2.5, 0.05, "Base emissive", (V) => `${V.toFixed(2)}×`);
    Select("#material-rebuild").addEventListener("click", () => {
      Document.renderComposite(true);
      Document.renderNormal(true);
      this.compositeVersion++;
      this.renderInspector();
      this.notify("Normal map rebuilt from height.");
    });
    Select("#material-export").addEventListener("click", () => this.openExport());
    Select("#material-save").addEventListener("click", () => this.saveProject());
    this.drawMaterialPreview();
  }

  /** Material/base history step. Caller has already applied `After`. */
  pushMaterialHistory(Before, After, Label) {
    const Document = this.doc;
    if (!Document) return;
    const Keys = Object.keys(After);
    if (Keys.every((Key) => JSON.stringify(Before[Key]) === JSON.stringify(After[Key]))) return;
    const Last = Document.history[Document.historyIndex];
    if (Last && Last.type === "material" && Last.keys && Keys.every((Key) => Last.keys.includes(Key)) && performance.now() - Last.time < 1500) {
      Object.assign(Last.after, JSON.parse(JSON.stringify(After)));
      Last.time = performance.now();
      Last.label = Label;
      return;
    }
    const BeforeCopy = JSON.parse(JSON.stringify(Before));
    const AfterCopy = JSON.parse(JSON.stringify(After));
    const Apply = (Patch) => {
      if (Patch.baseMode) Document.baseMode = Patch.baseMode;
      for (const [Key, Value] of Object.entries(Patch)) {
        if (Key !== "baseMode") Document.material[Key] = Value;
      }
      Document.markDirty();
    };
    // Undo/redo read through the entry so coalesced repeats redo correctly.
    const Entry = {
      label: Label,
      type: "material",
      time: performance.now(),
      keys: Keys,
      before: BeforeCopy,
      after: AfterCopy,
      undo: () => {
        Apply(Entry.before);
        this.renderAll();
      },
      redo: () => {
        Apply(Entry.after);
        this.renderAll();
      },
    };
    Document.pushHistory(Entry);
  }

  drawMaterialPreview() {
    const Canvas = Select("#material-preview");
    const Document = this.doc;
    if (!Canvas || !Document) return;
    const Context = Canvas.getContext("2d");
    const Material = Document.material;
    const W = Canvas.width;
    const H = Canvas.height;
    Context.clearRect(0, 0, W, H);
    const Background = Context.createLinearGradient(0, 0, 0, H);
    Background.addColorStop(0, "#1e1e1e");
    Background.addColorStop(1, "#0d0d0d");
    Context.fillStyle = Background;
    Context.fillRect(0, 0, W, H);
    // Floor line + backdrop sweep.
    Context.fillStyle = "#151515";
    Context.fillRect(0, H * 0.72, W, H * 0.28);
    const Cx = W * 0.5;
    const Cy = H * 0.46;
    const Radius = H * 0.34;
    // Shadow.
    Context.save();
    Context.globalAlpha = 0.5;
    Context.fillStyle = "#000";
    Context.beginPath();
    Context.ellipse(Cx, H * 0.74, Radius * 1.1, Radius * 0.22, 0, 0, Math.PI * 2);
    Context.fill();
    Context.restore();
    // Ball base.
    const Ball = Context.createRadialGradient(Cx - Radius * 0.45, Cy - Radius * 0.5, Radius * 0.1, Cx, Cy, Radius * 1.25);
    const Base = Material.albedo;
    Ball.addColorStop(0, lighten(Base, 0.55));
    Ball.addColorStop(0.45, Base);
    Ball.addColorStop(1, lighten(Base, -0.72));
    Context.fillStyle = Ball;
    Context.beginPath();
    Context.arc(Cx, Cy, Radius, 0, Math.PI * 2);
    Context.fill();
    // Specular streak driven by roughness; tinted by metalness.
    const Sharp = 1 - Material.roughness;
    const SpecColor = Material.metallic > 0.5 ? lighten(Base, 0.75) : "#ffffff";
    Context.save();
    Context.globalAlpha = 0.25 + Sharp * 0.65;
    const Spec = Context.createRadialGradient(Cx - Radius * 0.42, Cy - Radius * 0.48, 0, Cx - Radius * 0.42, Cy - Radius * 0.48, Radius * (0.18 + (1 - Sharp) * 0.75));
    Spec.addColorStop(0, SpecColor);
    Spec.addColorStop(1, "rgba(255,255,255,0)");
    Context.fillStyle = Spec;
    Context.beginPath();
    Context.arc(Cx, Cy, Radius, 0, Math.PI * 2);
    Context.fill();
    Context.restore();
    // Rim + emissive aura.
    Context.save();
    Context.strokeStyle = Material.metallic > 0.5 ? lighten(Base, 0.3) : "#ffffff22";
    Context.lineWidth = 2;
    Context.beginPath();
    Context.arc(Cx, Cy, Radius - 1, Math.PI * 1.05, Math.PI * 1.95);
    Context.stroke();
    Context.restore();
    if ((Material.emissiveStrength || 0) > 0) {
      Context.save();
      Context.globalAlpha = Math.min(1, Material.emissiveStrength * 0.5);
      Context.strokeStyle = Material.emissive;
      Context.lineWidth = 5;
      Context.shadowColor = Material.emissive;
      Context.shadowBlur = 26;
      Context.beginPath();
      Context.arc(Cx, Cy, Radius + 3, 0, Math.PI * 2);
      Context.stroke();
      Context.restore();
    }
    Context.fillStyle = "#ffffff10";
    Context.font = "20px 'JetBrains Mono', monospace";
    Context.textAlign = "left";
    Context.fillText(`M ${Material.metallic.toFixed(2)}  R ${Material.roughness.toFixed(2)}`, 14, H - 14);
  }

  /* ================= Tools ================= */

  setTool(Tool) {
    this.engine.cancelStroke();
    this.engine.tool = Tool;
    if (Tool === "shape") this.engine.tool = "shape";
    this.updateChrome();
    if (this.inspectorTab === "tool") this.renderInspector();
    this.paintCanvas.classList.toggle("paint-cursor", ["paint", "eraser", "smudge", "shape", "fill", "eyedropper"].includes(Tool));
    this.paintCanvas.classList.toggle("pan-cursor", Tool === "pan");
    this.paintCanvas.classList.toggle("move-cursor", Tool === "move");
  }

  swapColors() {
    const Engine = this.engine;
    const Temp = Engine.fgColor;
    Engine.fgColor = Engine.bgColor;
    Engine.bgColor = Temp;
    if (this.inspectorTab === "tool") this.renderInspector();
    this.drawBrushPreview();
  }

  deleteActiveLayer() {
    const Document = this.doc;
    if (!Document || !Document.activeLayer) return;
    Document.deleteActiveLayer();
    this.renderAll();
  }

  /* ================= View transform ================= */

  layoutCanvases() {
    const Viewport = Select("#viewport");
    const Ratio = Math.min(window.devicePixelRatio || 1, 2);
    const SizeCanvas = (Canvas) => {
      const Rect = Canvas.getBoundingClientRect();
      const Width = Math.max(2, Math.floor(Rect.width * Ratio));
      const Height = Math.max(2, Math.floor(Rect.height * Ratio));
      if (Canvas.width !== Width || Canvas.height !== Height) {
        Canvas.width = Width;
        Canvas.height = Height;
      }
    };
    if (this.view.mode !== "3d") SizeCanvas(this.paintCanvas);
    if (this.view.mode !== "2d") {
      const Preview = Select("#preview-canvas");
      if (!this.preview.ready) {
        this.preview.initialize();
        if (!this.preview.ready) this.showPreviewError(this.preview.error);
        else Select("#gpu-error").hidden = true;
      }
      this.preview.resize();
      void Preview;
    }
    this.computeFit();
  }

  computeFit() {
    const Document = this.doc;
    if (!Document) return;
    const Rect = this.paintCanvas.getBoundingClientRect();
    if (Rect.width < 2 || Rect.height < 2) return;
    this.view.fitScale = Math.min(Rect.width / Document.width, Rect.height / Document.height) * 0.94;
  }

  viewScale() {
    return this.view.fitScale * this.view.zoom;
  }

  viewOrigin() {
    const Rect = this.paintCanvas.getBoundingClientRect();
    const Document = this.doc;
    const Scale = this.viewScale();
    const Ox = Rect.width / 2 - (Document.width * Scale) / 2 + this.view.panX;
    const Oy = Rect.height / 2 - (Document.height * Scale) / 2 + this.view.panY;
    return { x: Ox, y: Oy, width: Rect.width, height: Rect.height };
  }

  screenToTexture(ClientX, ClientY) {
    const Rect = this.paintCanvas.getBoundingClientRect();
    const Origin = this.viewOrigin();
    const Scale = this.viewScale();
    return {
      x: (ClientX - Rect.left - Origin.x) / Scale,
      y: (ClientY - Rect.top - Origin.y) / Scale,
    };
  }

  textureToScreen(TexX, TexY) {
    const Origin = this.viewOrigin();
    const Scale = this.viewScale();
    return { x: Origin.x + TexX * Scale, y: Origin.y + TexY * Scale };
  }

  fitView() {
    this.view.zoom = 1;
    this.view.panX = 0;
    this.view.panY = 0;
    this.computeFit();
    this.updateChrome();
  }

  zoomAt(ClientX, ClientY, Factor) {
    const Document = this.doc;
    if (!Document) return;
    const Before = this.screenToTexture(ClientX, ClientY);
    this.view.zoom = Clamp(this.view.zoom * Factor, 0.05, 60);
    // Recompute pan so the texture point under the cursor stays fixed.
    const Scale = this.viewScale();
    const Rect = this.paintCanvas.getBoundingClientRect();
    const Ox = Rect.width / 2 - (Document.width * Scale) / 2 + this.view.panX;
    const Oy = Rect.height / 2 - (Document.height * Scale) / 2 + this.view.panY;
    const CurrentX = (ClientX - Rect.left - Ox) / Scale;
    const CurrentY = (ClientY - Rect.top - Oy) / Scale;
    this.view.panX -= (CurrentX - Before.x) * Scale;
    this.view.panY -= (CurrentY - Before.y) * Scale;
    this.updateChrome();
  }

  /* ================= 2D rendering ================= */

  channelSource() {
    const Document = this.doc;
    if (!Document) return null;
    const Channel = this.view.channel;
    if (Channel === "composite") return this.previewCanvas;
    if (Channel === "albedo") return Document.composite.albedo;
    if (Channel === "metallic") return Document.composite.metallic;
    if (Channel === "roughness") return Document.composite.roughness;
    if (Channel === "emissive") return Document.composite.emissive;
    if (Channel === "height") return Document.composite.height;
    if (Channel === "normal") {
      Document.renderNormal();
      return Document.composite.normal;
    }
    if (Channel === "mask") {
      const Active = Document.activeLayer;
      return (Active && Active.channels.mask) || null;
    }
    return Document.composite.albedo;
  }

  updatePreviewComposite() {
    const Document = this.doc;
    if (!Document) return;
    if (this.previewStamp === this.compositeVersion) return;
    this.previewStamp = this.compositeVersion;
    const Context = this.previewCanvas.getContext("2d");
    Context.save();
    Context.globalCompositeOperation = "source-over";
    Context.clearRect(0, 0, this.previewCanvas.width, this.previewCanvas.height);
    Context.drawImage(Document.composite.albedo, 0, 0);
    Context.globalCompositeOperation = "lighter";
    Context.drawImage(Document.composite.emissive, 0, 0);
    Context.restore();
  }

  drawPaintView() {
    const Document = this.doc;
    const Context = this.paintContext;
    const Canvas = this.paintCanvas;
    const Ratio = Math.min(window.devicePixelRatio || 1, 2);
    const Rect = Canvas.getBoundingClientRect();
    Context.setTransform(Ratio, 0, 0, Ratio, 0, 0);
    Context.clearRect(0, 0, Rect.width, Rect.height);
    // Backdrop.
    Context.fillStyle = "#101010";
    Context.fillRect(0, 0, Rect.width, Rect.height);
    if (!Document) return;
    const Scale = this.viewScale();
    const Origin = this.viewOrigin();
    const Dw = Document.width * Scale;
    const Dh = Document.height * Scale;
    const Tiles = this.view.tiling ? 2 : 1;
    // Shadow plate.
    Context.save();
    Context.shadowColor = "#000c";
    Context.shadowBlur = 32;
    Context.fillStyle = "#000";
    Context.fillRect(Origin.x, Origin.y, Dw * Tiles, Dh * Tiles);
    Context.restore();
    for (let Ty = 0; Ty < Tiles; Ty++) {
      for (let Tx = 0; Tx < Tiles; Tx++) {
        const X = Origin.x + Tx * Dw;
        const Y = Origin.y + Ty * Dh;
        if (this.view.checker && (this.view.channel === "composite" || this.view.channel === "albedo")) {
          this.paintChecker(Context, X, Y, Dw, Dh, Scale);
        } else {
          Context.fillStyle = "#000";
          Context.fillRect(X, Y, Dw, Dh);
        }
        const Source = this.channelSource();
        if (Source) {
          Context.save();
          Context.imageSmoothingEnabled = Scale < 7;
          Context.imageSmoothingQuality = "high";
          if (this.view.channel === "mask") {
            Context.fillStyle = "#000";
            Context.fillRect(X, Y, Dw, Dh);
          }
          Context.drawImage(Source, X, Y, Dw, Dh);
          Context.restore();
        }
        if (this.view.tiling) {
          Context.save();
          Context.strokeStyle = "#34c75988";
          Context.lineWidth = 1;
          Context.setLineDash([6, 5]);
          Context.strokeRect(X + 0.5, Y + 0.5, Dw - 1, Dh - 1);
          Context.restore();
        }
      }
    }
    this.drawMaskOverlay(Context, Origin, Scale, Dw, Dh, Tiles);
    // Pixel grid at high zoom.
    if (Scale >= 9 && Tiles === 1) {
      const StartX = Math.max(0, Math.floor(-Origin.x / Scale));
      const EndX = Math.min(Document.width, Math.ceil((Rect.width - Origin.x) / Scale));
      const StartY = Math.max(0, Math.floor(-Origin.y / Scale));
      const EndY = Math.min(Document.height, Math.ceil((Rect.height - Origin.y) / Scale));
      if (EndX - StartX < 220 && EndY - StartY < 220) {
        Context.save();
        Context.strokeStyle = "#ffffff14";
        Context.lineWidth = 1;
        Context.beginPath();
        for (let X = StartX; X <= EndX; X++) {
          const Sx = Math.round(Origin.x + X * Scale) + 0.5;
          Context.moveTo(Sx, Origin.y + StartY * Scale);
          Context.lineTo(Sx, Origin.y + EndY * Scale);
        }
        for (let Y = StartY; Y <= EndY; Y++) {
          const Sy = Math.round(Origin.y + Y * Scale) + 0.5;
          Context.moveTo(Origin.x + StartX * Scale, Sy);
          Context.lineTo(Origin.x + EndX * Scale, Sy);
        }
        Context.stroke();
        Context.restore();
      }
    }
    // Symmetry guides.
    if (this.engine.symmetry !== "none" && ["paint", "eraser", "smudge"].includes(this.engine.tool)) {
      Context.save();
      Context.strokeStyle = "#5aa9ff66";
      Context.lineWidth = 1;
      Context.setLineDash([8, 6]);
      if (this.engine.symmetry === "x" || this.engine.symmetry === "quad") {
        const Sx = Origin.x + (Document.width / 2) * Scale;
        Context.beginPath();
        Context.moveTo(Sx, Origin.y);
        Context.lineTo(Sx, Origin.y + Dh);
        Context.stroke();
      }
      if (this.engine.symmetry === "y" || this.engine.symmetry === "quad") {
        const Sy = Origin.y + (Document.height / 2) * Scale;
        Context.beginPath();
        Context.moveTo(Origin.x, Sy);
        Context.lineTo(Origin.x + Dw, Sy);
        Context.stroke();
      }
      Context.restore();
    }
    // Shape preview.
    const Shape = this.engine.shapePreview;
    if (Shape) {
      const A = this.textureToScreen(Shape.x0, Shape.y0);
      const B = this.textureToScreen(Shape.x1, Shape.y1);
      Context.save();
      Context.strokeStyle = "#e0e0e0";
      Context.fillStyle = `${this.engine.fgColor}55`;
      Context.lineWidth = 1.5;
      Context.setLineDash([5, 4]);
      if (Shape.kind === "line") {
        Context.beginPath();
        Context.moveTo(A.x, A.y);
        Context.lineTo(B.x, B.y);
        Context.stroke();
      } else if (Shape.kind === "ellipse") {
        Context.beginPath();
        Context.ellipse((A.x + B.x) / 2, (A.y + B.y) / 2, Math.abs(B.x - A.x) / 2, Math.abs(B.y - A.y) / 2, 0, 0, Math.PI * 2);
        Context.fill();
        Context.stroke();
      } else {
        Context.fillRect(Math.min(A.x, B.x), Math.min(A.y, B.y), Math.abs(B.x - A.x), Math.abs(B.y - A.y));
        Context.strokeRect(Math.min(A.x, B.x), Math.min(A.y, B.y), Math.abs(B.x - A.x), Math.abs(B.y - A.y));
      }
      Context.restore();
    }
    // Brush cursor.
    if (this.hover && ["paint", "eraser", "smudge", "shape"].includes(this.engine.tool) && !this.panning) {
      const Radius = (this.engine.brush.size * Scale) / 2;
      if (Radius < 4000) {
        Context.save();
        Context.strokeStyle = this.engine.tool === "eraser" ? "#ff8a80" : "#f0f0f0";
        Context.lineWidth = 1.2;
        Context.shadowColor = "#000";
        Context.shadowBlur = 3;
        Context.beginPath();
        Context.arc(this.hover.x, this.hover.y, Math.max(2, Radius), 0, Math.PI * 2);
        Context.stroke();
        Context.shadowBlur = 0;
        Context.fillStyle = Context.strokeStyle;
        Context.fillRect(this.hover.x - 0.5, this.hover.y - 4, 1, 8);
        Context.fillRect(this.hover.x - 4, this.hover.y - 0.5, 8, 1);
        Context.restore();
      }
    }
  }

  /** Rubylith overlay: red where the mask (or quick mask) conceals. */
  drawMaskOverlay(Context, Origin, Scale, Dw, Dh, Tiles) {
    const Document = this.doc;
    if (!Document) return;
    let Source = null;
    if (Document.quickMaskActive && Document.quickMask) {
      Source = Document.quickMask;
    } else {
      const Active = Document.activeLayer;
      if (Active && Active.channels.mask && Active.maskEnabled && (this.view.showMaskOverlay || Active.maskSelected)) {
        Source = Active.channels.mask;
      }
    }
    if (!Source) return;
    if (!this.overlayCanvas || this.overlayCanvas.width !== Document.width || this.overlayCanvas.height !== Document.height) {
      this.overlayCanvas = makeCanvas(Document.width, Document.height);
    }
    const Overlay = this.overlayCanvas.getContext("2d");
    Overlay.save();
    Overlay.globalCompositeOperation = "source-over";
    Overlay.clearRect(0, 0, Document.width, Document.height);
    Overlay.fillStyle = "rgba(255,64,64,1)";
    Overlay.fillRect(0, 0, Document.width, Document.height);
    Overlay.globalCompositeOperation = "destination-out";
    Overlay.drawImage(Source, 0, 0);
    Overlay.restore();
    Context.save();
    Context.globalAlpha = 0.45;
    Context.imageSmoothingEnabled = Scale < 7;
    for (let Ty = 0; Ty < Tiles; Ty++) {
      for (let Tx = 0; Tx < Tiles; Tx++) {
        Context.drawImage(this.overlayCanvas, Origin.x + Tx * Dw, Origin.y + Ty * Dh, Dw, Dh);
      }
    }
    Context.restore();
  }

  paintChecker(Context, X, Y, W, H, Scale) {
    const Cell = Clamp(16 * Scale, 8, 64);
    Context.save();
    Context.beginPath();
    Context.rect(X, Y, W, H);
    Context.clip();
    Context.fillStyle = "#1c1c1c";
    Context.fillRect(X, Y, W, H);
    Context.fillStyle = "#262626";
    const Cols = Math.ceil(W / Cell);
    const Rows = Math.ceil(H / Cell);
    for (let Row = 0; Row < Rows; Row++) {
      for (let Col = 0; Col < Cols; Col++) {
        if ((Row + Col) % 2 === 0) continue;
        Context.fillRect(X + Col * Cell, Y + Row * Cell, Cell + 0.5, Cell + 0.5);
      }
    }
    Context.restore();
  }

  /* ================= Decal gizmo ================= */

  decalArtSize(Layer) {
    if (!Layer || !["svg", "text", "image"].includes(Layer.kind)) return null;
    if (Layer.kind === "text") {
      // Measure cheaply from params (matches Decals.renderTextArtwork metrics).
      const Scale = Math.max(64, this.doc.width);
      const PixelSize = Math.max(4, Layer.params.size * Scale * (Layer.params.scale || 1));
      const Lines = String(Layer.params.text ?? "Text").split("\n");
      const Context = this.paintContext;
      Context.save();
      Context.font = `${Layer.params.italic ? "italic" : "normal"} ${Layer.params.weight || 400} ${PixelSize}px ${FontFamily(Layer.params.font)}`;
      const Spacing = (Layer.params.spacing || 0) * PixelSize;
      let Widest = 1;
      for (const Line of Lines) {
        Widest = Math.max(Widest, Context.measureText(Line).width + Spacing * Math.max(0, Line.length - 1));
      }
      Context.restore();
      const LineHeight = PixelSize * (Layer.params.lineHeight || 1.15);
      const Pad = PixelSize * 0.25 + (Layer.params.stroke || 0) * PixelSize * 2 + 4;
      return { w: Widest + Pad * 2, h: LineHeight * Lines.length + Pad * 2 };
    }
    const Cache = Layer.artCache;
    if (Cache && Cache.image) return { w: Cache.w, h: Cache.h };
    return null;
  }

  updateGizmo() {
    const Gizmo = Select("#decal-gizmo");
    const Document = this.doc;
    const Layer = Document ? Document.activeLayer : null;
    const Show = !!(
      Layer && ["svg", "text", "image"].includes(Layer.kind) &&
      this.engine.tool === "move" && this.view.mode !== "3d" && !Layer.locked
    );
    if (!Show) {
      Gizmo.hidden = true;
      return;
    }
    const Art = this.decalArtSize(Layer);
    if (!Art) {
      Gizmo.hidden = true;
      return;
    }
    const Bounds = decalBounds(Layer.params, Art.w, Art.h, Document.width, Document.height);
    // Gizmo overlay lives in viewport space; the paint canvas may be offset (split view).
    const ViewRect = Select("#viewport").getBoundingClientRect();
    const PaintRect = this.paintCanvas.getBoundingClientRect();
    const OffsetX = PaintRect.left - ViewRect.left;
    const OffsetY = PaintRect.top - ViewRect.top;
    const ToGizmo = (TexX, TexY) => {
      const Point = this.textureToScreen(TexX, TexY);
      return { x: Point.x + OffsetX, y: Point.y + OffsetY };
    };
    const Screen = Bounds.corners.map(([X, Y]) => ToGizmo(X, Y));
    // Position the gizmo box via transform: center + size + rotation.
    const Center = ToGizmo(Bounds.centerX, Bounds.centerY);
    const Scale = this.viewScale();
    const Box = Gizmo.querySelector(".gizmo-box");
    const FlipX = Layer.params.flipX ? -1 : 1;
    const FlipY = Layer.params.flipY ? -1 : 1;
    Box.style.left = `${Center.x - (Bounds.width * Scale) / 2}px`;
    Box.style.top = `${Center.y - (Bounds.height * Scale) / 2}px`;
    Box.style.width = `${Bounds.width * Scale}px`;
    Box.style.height = `${Bounds.height * Scale}px`;
    Box.style.transform = `rotate(${(Layer.params.rotation || 0) * FlipX * FlipY}deg)`;
    const Handles = Gizmo.querySelectorAll(".gizmo-handle");
    const Order = ["nw", "ne", "se", "sw"];
    // Corners from decalBounds are [nw, ne, se, sw] pre-rotation mapped.
    const CornerIndex = { nw: 0, ne: 1, se: 2, sw: 3 };
    Handles.forEach((Handle) => {
      const Point = Screen[CornerIndex[Handle.dataset.handle]];
      Handle.style.left = `${Point.x}px`;
      Handle.style.top = `${Point.y}px`;
    });
    // Rotate handle floats above the top edge midpoint.
    const TopMid = { x: (Screen[0].x + Screen[1].x) / 2, y: (Screen[0].y + Screen[1].y) / 2 };
    const Angle = Bounds.angle;
    const Offset = 34;
    const RotateHandle = Gizmo.querySelector(".gizmo-rotate");
    RotateHandle.style.left = `${TopMid.x - Math.sin(Angle) * Offset}px`;
    RotateHandle.style.top = `${TopMid.y - Math.cos(Angle) * Offset}px`;
    void Order;
    Gizmo.hidden = false;
  }

  pointInDecal(Layer, TexX, TexY) {
    const Document = this.doc;
    const Art = this.decalArtSize(Layer);
    if (!Art) return false;
    const Bounds = decalBounds(Layer.params, Art.w, Art.h, Document.width, Document.height);
    // Inverse-transform the point into decal space.
    const Dx = TexX - Bounds.centerX;
    const Dy = TexY - Bounds.centerY;
    const Cos = Math.cos(-Bounds.angle);
    const Sin = Math.sin(-Bounds.angle);
    const Lx = Dx * Cos - Dy * Sin;
    const Ly = Dx * Sin + Dy * Cos;
    return Math.abs(Lx) <= Bounds.width / 2 && Math.abs(Ly) <= Bounds.height / 2;
  }

  /* ================= Channel strip ================= */

  buildChannelStrip() {
    const Strip = Select("#channel-strip");
    Strip.innerHTML = "";
    for (const Channel of CHANNELS) {
      const Button = document.createElement("button");
      Button.className = `channel-thumb${Channel.id === this.view.channel ? " active" : ""}`;
      Button.dataset.channel = Channel.id;
      Button.innerHTML = `<canvas width="68" height="68"></canvas><span class="channel-copy"><strong>${Channel.label}</strong><small>${Channel.hint}</small></span>`;
      Button.addEventListener("click", () => {
        this.view.channel = Channel.id;
        Select("#render-channel").value = Channel.id;
        SelectAll(".channel-thumb").forEach((Thumb) => Thumb.classList.toggle("active", Thumb.dataset.channel === Channel.id));
        this.updateChrome();
      });
      Strip.appendChild(Button);
    }
  }

  refreshChannelThumbs(Force = false) {
    const Document = this.doc;
    if (!Document) return;
    SelectAll(".channel-thumb").forEach((Thumb) => {
      const Id = Thumb.dataset.channel;
      const Stamp = `${this.compositeVersion}:${Id}:${Document.activeLayerId}`;
      if (!Force && this.channelStamps.get(Id) === Stamp) return;
      this.channelStamps.set(Id, Stamp);
      const Canvas = Thumb.querySelector("canvas");
      const Context = Canvas.getContext("2d");
      Context.save();
      Context.clearRect(0, 0, Canvas.width, Canvas.height);
      let Source = null;
      if (Id === "composite") Source = this.previewCanvas;
      else if (Id === "albedo") Source = Document.composite.albedo;
      else if (Id === "metallic") Source = Document.composite.metallic;
      else if (Id === "roughness") Source = Document.composite.roughness;
      else if (Id === "emissive") Source = Document.composite.emissive;
      else if (Id === "height") Source = Document.composite.height;
      else if (Id === "normal") Source = Document.composite.normal;
      else if (Id === "mask") {
        const Active = Document.activeLayer;
        Source = (Active && Active.channels.mask) || null;
        Context.fillStyle = "#000";
        Context.fillRect(0, 0, Canvas.width, Canvas.height);
      }
      if (Source) Context.drawImage(Source, 0, 0, Canvas.width, Canvas.height);
      Context.restore();
    });
  }

  drawNavigator() {
    const Canvas = Select("#navigator-canvas");
    const Document = this.doc;
    if (!Canvas || !Document || this.view.mode === "3d") return;
    const Context = Canvas.getContext("2d");
    const W = Canvas.width;
    const H = Canvas.height;
    Context.clearRect(0, 0, W, H);
    Context.drawImage(this.previewCanvas, 0, 0, W, H);
    // Viewport rectangle.
    const Rect = this.paintCanvas.getBoundingClientRect();
    const Origin = this.viewOrigin();
    const Scale = this.viewScale();
    const Vx = (-Origin.x / Scale / Document.width) * W;
    const Vy = (-Origin.y / Scale / Document.height) * H;
    const Vw = (Rect.width / Scale / Document.width) * W;
    const Vh = (Rect.height / Scale / Document.height) * H;
    Context.save();
    Context.strokeStyle = "#f0f0f0dd";
    Context.lineWidth = 1.5;
    Context.strokeRect(Vx, Vy, Vw, Vh);
    Context.fillStyle = "#00000055";
    Context.fillRect(0, 0, W, Vy);
    Context.fillRect(0, Vy + Vh, W, H - Vy - Vh);
    Context.fillRect(0, Vy, Vx, Vh);
    Context.fillRect(Vx + Vw, Vy, W - Vx - Vw, Vh);
    Context.restore();
  }

  /* ================= Frame loop ================= */

  frame(Time) {
    const Delta = Math.min(0.1, (Time - this.lastFrame) / 1000);
    this.lastFrame = Time;
    const Document = this.doc;
    if (Document) {
      if (Document.renderComposite()) {
        this.compositeVersion++;
        Document.updateThumbs();
        this.updateLayerThumbsOnly();
      }
      this.updatePreviewComposite();
      const Needs3D = this.view.mode !== "2d";
      const NeedsNormal = this.view.channel === "normal" || Needs3D;
      if (NeedsNormal) {
        if (Document.renderNormal()) this.compositeVersion++;
      }
      if (this.view.mode !== "3d") {
        this.drawPaintView();
        this.updateGizmo();
      } else {
        Select("#decal-gizmo").hidden = true;
      }
      if (Needs3D && this.preview.ready) {
        this.preview.uploadTextures(Document.composite, this.compositeVersion);
        this.preview.normalStrength = Document.material.normalStrength;
        this.preview.render(Time / 1000, Delta);
      }
      if (!this.thumbTimer || Time - this.thumbTimer > 350) {
        this.thumbTimer = Time;
        this.refreshChannelThumbs();
        this.drawNavigator();
        this.updateDiagnostics();
      }
      if (!this.chromeTimer || Time - this.chromeTimer > 600) {
        this.chromeTimer = Time;
        this.updateChrome();
      }
    }
    requestAnimationFrame((Next) => this.frame(Next));
  }

  updateLayerThumbsOnly() {
    // Thumbnails are live canvas elements already in the DOM — nothing to do
    // except refresh the list when layers were added/removed elsewhere.
  }

  updateDiagnostics() {
    const Panel = Select("#diagnostics");
    if (Panel.hidden || !this.doc) return;
    const Document = this.doc;
    Select("#diagnostic-values").innerHTML = `
      <dt>Composite</dt><dd>${Document.lastCompositeMs.toFixed(2)} ms</dd>
      <dt>Layers</dt><dd>${Document.layers.length}</dd>
      <dt>History</dt><dd>${Document.historyIndex + 1} / ${Document.history.length}</dd>
      <dt>Version</dt><dd>v${this.compositeVersion}</dd>
      <dt>Normal map</dt><dd>${Document.normalDirty ? "stale" : "fresh"}</dd>
      <dt>Preview</dt><dd>${this.preview.ready ? this.preview.mesh : "off"}</dd>`;
  }

  showPreviewError(Message) {
    Select("#gpu-error-message").textContent = Message || "WebGL2 preview failed.";
    Select("#gpu-error").hidden = false;
  }

  /* ================= Events ================= */

  connectInterface() {
    // Header / documents.
    Select("#new-document").addEventListener("click", () => this.openNewDialog());
    Select("#workspace-button").addEventListener("click", () => {
      document.body.classList.remove("mobile-library");
      Select("#workspace-button").classList.add("active");
      Select("#library-button").classList.remove("active");
    });
    Select("#library-button").addEventListener("click", () => {
      document.body.classList.add("mobile-library");
      Select("#library-button").classList.add("active");
      Select("#workspace-button").classList.remove("active");
      Select("#preset-search").focus();
    });
    Select("#help-button").addEventListener("click", () => Select("#help-dialog").showModal());
    Select("#close-help").addEventListener("click", () => Select("#help-dialog").close());
    Select("#export-button").addEventListener("click", () => this.saveProject());
    Select("#document-name").addEventListener("change", (Event) => {
      const Document = this.doc;
      const Value = Event.target.value.trim();
      if (Document && Value) {
        Document.name = Value;
        Document.dirty = true;
        this.renderDocumentTabs();
      } else if (Document) {
        Event.target.value = Document.name;
      }
    });
    Select("#import-button").addEventListener("click", () => Select("#import-file").click());
    Select("#import-file").addEventListener("change", (Event) => this.openProject(Event.target.files[0]));
    Select("#layer-image-file").addEventListener("change", (Event) => this.importImageFile(Event.target.files[0]));
    Select("#decal-file").addEventListener("change", (Event) => this.importSvgFile(Event.target.files[0]));

    // Layers panel.
    Select("#compact-outliner").addEventListener("click", (Event) => {
      const Panel = Select(".left-panel");
      const Compact = Panel.classList.toggle("compact-outliner");
      Event.currentTarget.setAttribute("aria-pressed", Compact ? "true" : "false");
    });
    Select("#scene-search").addEventListener("input", (Event) => {
      this.layerQuery = Event.target.value.trim().toLowerCase();
      this.renderLayerList();
    });
    SelectAll("#scene-filters button").forEach((Button) => {
      Button.addEventListener("click", () => {
        SelectAll("#scene-filters button").forEach((Entry) => {
          Entry.classList.remove("active");
          Entry.setAttribute("aria-pressed", "false");
        });
        Button.classList.add("active");
        Button.setAttribute("aria-pressed", "true");
        this.layerFilter = Button.dataset.layerFilter;
        this.renderLayerList();
      });
    });
    Select("#collection-toggle").addEventListener("click", (Event) => {
      const Button = Event.currentTarget;
      const Expanded = Button.getAttribute("aria-expanded") === "true";
      Button.setAttribute("aria-expanded", Expanded ? "false" : "true");
      Select("#scene-tree").hidden = Expanded;
    });
    Select("#add-button").addEventListener("click", (Event) => {
      Event.stopPropagation();
      this.toggleAddMenu();
    });
    SelectAll("#add-menu [data-add]").forEach((Button) => {
      Button.addEventListener("click", () => {
        this.toggleAddMenu(false);
        this.addLayerOfKind(Button.dataset.add);
      });
    });
    document.addEventListener("click", (Event) => {
      if (!Event.target.closest("#add-menu") && !Event.target.closest("#add-button")) {
        this.toggleAddMenu(false);
      }
    });

    // Library.
    SelectAll("#preset-filters button").forEach((Button) => {
      Button.addEventListener("click", () => {
        SelectAll("#preset-filters button").forEach((Entry) => Entry.classList.remove("active"));
        Button.classList.add("active");
        this.libraryTab = Button.dataset.library;
        this.renderLibrary();
      });
    });
    Select("#preset-search").addEventListener("input", (Event) => {
      this.libraryQuery = Event.target.value.trim().toLowerCase();
      this.renderLibrary();
    });
    Select("#reset-search").addEventListener("click", () => {
      Select("#preset-search").value = "";
      this.libraryQuery = "";
      this.renderLibrary();
    });

    // Viewport bar.
    Select("#view-mode").addEventListener("change", (Event) => this.setViewMode(Event.target.value));
    Select("#mesh-select").addEventListener("change", (Event) => {
      this.preview.mesh = Event.target.value;
    });
    Select("#render-channel").addEventListener("change", (Event) => {
      this.view.channel = Event.target.value;
      SelectAll(".channel-thumb").forEach((Thumb) => Thumb.classList.toggle("active", Thumb.dataset.channel === this.view.channel));
      this.updateChrome();
    });
    Select("#quickmask-button").addEventListener("click", () => this.toggleQuickMask());
    Select("#tiling-button").addEventListener("click", () => {
      this.view.tiling = !this.view.tiling;
      this.updateChrome();
    });
    Select("#overlays-button").addEventListener("click", () => {
      this.view.checker = !this.view.checker;
      this.updateChrome();
    });
    Select("#focus-button").addEventListener("click", () => this.fitView());
    Select("#maximize-button").addEventListener("click", () => {
      document.body.classList.toggle("maximized");
      setTimeout(() => this.layoutCanvases(), 30);
    });
    Select("#retry-gpu").addEventListener("click", () => {
      Select("#gpu-error").hidden = true;
      this.preview.destroy();
      this.preview.ready = false;
      this.layoutCanvases();
    });
    Select("#close-diagnostics").addEventListener("click", () => {
      Select("#diagnostics").hidden = true;
    });

    // Viewport tools.
    SelectAll(".viewport-tools [data-tool]").forEach((Button) => {
      Button.addEventListener("click", () => this.setTool(Button.dataset.tool));
    });

    // Transport.
    Select("#undo-button").addEventListener("click", () => this.undo());
    Select("#redo-button").addEventListener("click", () => this.redo());
    Select("#symmetry-select").addEventListener("change", (Event) => {
      this.engine.symmetry = Event.target.value;
      this.drawBrushPreview();
      this.updateChrome();
    });
    Select("#shape-kind").addEventListener("change", (Event) => {
      this.engine.shapeKind = Event.target.value;
      if (this.inspectorTab === "tool" && this.engine.tool === "shape") this.renderInspector();
    });
    Select("#clear-button").addEventListener("click", () => {
      if (this.doc && this.doc.clearActiveLayer()) {
        this.renderAll();
        this.notify("Layer cleared.");
      }
    });

    // Inspector.
    SelectAll("#inspector-tabs button").forEach((Button) => {
      Button.addEventListener("click", () => {
        this.inspectorTab = Button.dataset.tab;
        this.renderInspector();
      });
    });
    Select("#object-name").addEventListener("change", (Event) => {
      const Document = this.doc;
      const Layer = Document ? Document.activeLayer : null;
      if (Layer && Event.target.value.trim()) {
        Document.setLayerProps(Layer, { name: Event.target.value.trim() }, "Rename layer");
        this.renderLayerList();
      } else if (Layer) {
        Event.target.value = Layer.name;
      }
    });
    Select("#object-enabled").addEventListener("change", (Event) => {
      const Document = this.doc;
      const Layer = Document ? Document.activeLayer : null;
      if (Layer) {
        Document.setLayerProps(Layer, { visible: Event.target.checked }, "Toggle visibility");
        this.renderLayerList();
      }
    });
    Select("#reset-properties").addEventListener("click", () => this.resetInspectorTab());
    Select("#delete-layer").addEventListener("click", () => this.deleteActiveLayer());
    Select("#renderer-select").addEventListener("change", (Event) => {
      this.preview.turntable = Event.target.value !== "low" ? this.preview.turntable : true;
      this.layoutCanvases();
    });

    // Dialogs.
    Select("#close-new").addEventListener("click", () => Select("#new-dialog").close());
    Select("#new-create").addEventListener("click", () => {
      const Name = Select("#new-name").value.trim() || "Untitled texture";
      const Size = parseInt(Select("#new-size").value, 10);
      const Base = Select("#new-base").value;
      const Template = Select("#new-template").value;
      Select("#new-dialog").close();
      this.createDocument(Name, Size, Base, Template);
    });
    Select("#close-export").addEventListener("click", () => Select("#export-dialog").close());
    Select("#export-start").addEventListener("click", () => this.runExport());

    // Canvas pointer.
    this.paintCanvas.addEventListener("pointerdown", (Event) => this.onPaintDown(Event));
    this.paintCanvas.addEventListener("pointermove", (Event) => this.onPaintMove(Event));
    this.paintCanvas.addEventListener("pointerup", (Event) => this.onPaintUp(Event));
    this.paintCanvas.addEventListener("pointercancel", () => this.onPaintCancel());
    this.paintCanvas.addEventListener("pointerleave", () => {
      this.hover = null;
    });
    this.paintCanvas.addEventListener("wheel", (Event) => {
      Event.preventDefault();
      this.zoomAt(Event.clientX, Event.clientY, Event.deltaY < 0 ? 1.12 : 1 / 1.12);
    }, { passive: false });
    this.paintCanvas.addEventListener("dblclick", (Event) => {
      // Toggle 1:1 vs fit.
      const Scale = this.viewScale();
      if (Math.abs(Scale - 1) < 0.05) this.fitView();
      else {
        const Rect = this.paintCanvas.getBoundingClientRect();
        this.view.zoom = 1 / this.view.fitScale;
        this.view.panX = 0;
        this.view.panY = 0;
        void Rect;
        this.updateChrome();
      }
      void Event;
    });

    // 3D orbit.
    const PreviewCanvas = Select("#preview-canvas");
    PreviewCanvas.addEventListener("pointerdown", (Event) => {
      this.orbiting = { x: Event.clientX, y: Event.clientY, yaw: this.preview.yaw, pitch: this.preview.pitch };
      this.preview.dragging = true;
      PreviewCanvas.setPointerCapture(Event.pointerId);
    });
    PreviewCanvas.addEventListener("pointermove", (Event) => {
      if (!this.orbiting) return;
      this.preview.yaw = this.orbiting.yaw + (Event.clientX - this.orbiting.x) * 0.008;
      this.preview.pitch = Clamp(this.orbiting.pitch + (Event.clientY - this.orbiting.y) * 0.006, -1.2, 1.2);
    });
    PreviewCanvas.addEventListener("pointerup", () => {
      this.orbiting = null;
      this.preview.dragging = false;
    });
    PreviewCanvas.addEventListener("wheel", (Event) => {
      Event.preventDefault();
      this.preview.distance = Clamp(this.preview.distance * (Event.deltaY < 0 ? 0.92 : 1.08), 1.4, 8);
    }, { passive: false });
    PreviewCanvas.addEventListener("dblclick", () => {
      this.preview.yaw = 0.6;
      this.preview.pitch = 0.32;
      this.preview.distance = 3.1;
    });

    // Gizmo handles.
    SelectAll("#decal-gizmo [data-handle]").forEach((Handle) => {
      Handle.addEventListener("pointerdown", (Event) => {
        Event.stopPropagation();
        Event.preventDefault();
        this.beginDecalDrag(Event.clientX, Event.clientY, Handle.dataset.handle);
        Handle.setPointerCapture(Event.pointerId);
      });
      Handle.addEventListener("pointermove", (Event) => {
        if (this.decalDrag) this.moveDecalDrag(Event.clientX, Event.clientY);
      });
      Handle.addEventListener("pointerup", () => this.endDecalDrag());
      Handle.addEventListener("pointercancel", () => this.endDecalDrag());
    });

    // Navigator.
    const Navigator = Select("#navigator");
    Navigator.addEventListener("pointerdown", (Event) => {
      this.navigatorDrag = true;
      Navigator.setPointerCapture(Event.pointerId);
      this.panFromNavigator(Event.clientX, Event.clientY);
    });
    Navigator.addEventListener("pointermove", (Event) => {
      if (this.navigatorDrag) this.panFromNavigator(Event.clientX, Event.clientY);
    });
    Navigator.addEventListener("pointerup", () => {
      this.navigatorDrag = false;
    });

    // Keyboard.
    document.addEventListener("keydown", (Event) => this.onKeyDown(Event));
    document.addEventListener("keyup", (Event) => {
      if (Event.key === " ") this.spaceDown = false;
    });
    window.addEventListener("resize", () => this.layoutCanvases());
  }

  panFromNavigator(ClientX, ClientY) {
    const Canvas = Select("#navigator-canvas");
    const Document = this.doc;
    if (!Document) return;
    const Rect = Canvas.getBoundingClientRect();
    const U = Clamp((ClientX - Rect.left) / Rect.width, 0, 1);
    const V = Clamp((ClientY - Rect.top) / Rect.height, 0, 1);
    const Scale = this.viewScale();
    const ViewRect = this.paintCanvas.getBoundingClientRect();
    this.view.panX = ViewRect.width / 2 - U * Document.width * Scale - (ViewRect.width / 2 - (Document.width * Scale) / 2);
    this.view.panY = ViewRect.height / 2 - V * Document.height * Scale - (ViewRect.height / 2 - (Document.height * Scale) / 2);
  }

  onKeyDown(Event) {
    const Target = Event.target;
    const Typing = Target && (Target.tagName === "INPUT" || Target.tagName === "TEXTAREA" || Target.tagName === "SELECT" || Target.isContentEditable);
    if (Event.key === " " && !Typing) {
      this.spaceDown = true;
      Event.preventDefault();
      return;
    }
    if (Event.key === "Escape") {
      if (this.engine.painting) {
        this.engine.cancelStroke();
        return;
      }
      if (this.doc && this.doc.quickMaskActive) {
        this.doc.discardQuickMask();
        this.updateChrome();
        this.notify("Quick mask discarded.");
        return;
      }
      this.toggleAddMenu(false);
      for (const Selector of ["#help-dialog", "#new-dialog", "#export-dialog"]) {
        const Dialog = Select(Selector);
        if (Dialog.open) Dialog.close();
      }
      return;
    }
    if (Typing) return;
    if (Event.key === "Enter" && this.doc && this.doc.quickMaskActive) {
      this.toggleQuickMask();
      return;
    }
    const Key = Event.key.toLowerCase();
    const Mod = Event.ctrlKey || Event.metaKey;
    if (Mod && Key === "z" && !Event.shiftKey) {
      Event.preventDefault();
      this.undo();
      return;
    }
    if ((Mod && Key === "y") || (Mod && Event.shiftKey && Key === "z")) {
      Event.preventDefault();
      this.redo();
      return;
    }
    if (Mod && Key === "s") {
      Event.preventDefault();
      this.saveProject();
      return;
    }
    if (Mod && Event.shiftKey && Key === "f") {
      Event.preventDefault();
      Select("#scene-search").focus();
      return;
    }
    if (Mod || Event.altKey) return;
    switch (Key) {
      case "b": this.setTool("paint"); break;
      case "e": this.setTool("eraser"); break;
      case "u": this.setTool("smudge"); break;
      case "g": this.setTool("fill"); break;
      case "i": this.setTool("eyedropper"); break;
      case "r": this.setTool("shape"); break;
      case "v": this.setTool("move"); break;
      case "h": this.setTool("pan"); break;
      case "x": this.swapColors(); break;
      case "f": this.fitView(); break;
      case "c":
        this.view.checker = !this.view.checker;
        this.updateChrome();
        break;
      case "t":
        this.view.tiling = !this.view.tiling;
        this.updateChrome();
        break;
      case "d":
        Select("#diagnostics").hidden = !Select("#diagnostics").hidden;
        this.updateDiagnostics();
        break;
      case "q":
        this.toggleQuickMask();
        break;
      case "/":
        Event.preventDefault();
        Select("#preset-search").focus();
        break;
      case "[":
        this.engine.brush.size = Clamp(Math.round(this.engine.brush.size / 1.2), 1, 512);
        if (this.inspectorTab === "tool") this.renderInspector();
        this.updateChrome();
        break;
      case "]":
        this.engine.brush.size = Clamp(Math.round(this.engine.brush.size * 1.2 + 1), 1, 512);
        if (this.inspectorTab === "tool") this.renderInspector();
        this.updateChrome();
        break;
      case "delete":
      case "backspace":
        if (this.doc && this.doc.clearActiveLayer()) {
          this.renderAll();
          this.notify("Layer cleared.");
        }
        break;
      case "arrowleft":
      case "arrowright":
      case "arrowup":
      case "arrowdown":
        this.nudgeDecal(Key, Event.shiftKey ? 10 : 1);
        Event.preventDefault();
        break;
      default: {
        if (/^[0-9]$/.test(Key)) {
          const Value = Key === "0" ? 1 : parseInt(Key, 10) / 10;
          this.engine.brush.opacity = Value;
          if (this.inspectorTab === "tool") this.renderInspector();
          this.updateChrome();
        }
      }
    }
  }

  nudgeDecal(Key, Amount) {
    const Document = this.doc;
    const Layer = Document ? Document.activeLayer : null;
    if (!Layer || !["svg", "text", "image"].includes(Layer.kind) || Layer.locked) return;
    const Step = Amount / Document.width;
    const Params = { ...Layer.params };
    if (Key === "arrowleft") Params.x -= Step;
    if (Key === "arrowright") Params.x += Step;
    if (Key === "arrowup") Params.y -= Step;
    if (Key === "arrowdown") Params.y += Step;
    Document.setLayerProps(Layer, { params: Params }, "Nudge decal");
    if (this.inspectorTab === "layer") this.renderInspector();
  }

  /* ================= Paint pointer ================= */

  pointerPressure(Event) {
    if (Event.pointerType === "pen" || Event.pointerType === "touch") {
      return Clamp(Event.pressure || 0.5, 0.05, 1);
    }
    return 1;
  }

  onPaintDown(Event) {
    const Document = this.doc;
    if (!Document) return;
    this.paintCanvas.focus && this.paintCanvas.focus();
    if (Event.button === 1 || this.spaceDown || this.engine.tool === "pan") {
      this.panning = { x: Event.clientX, y: Event.clientY, panX: this.view.panX, panY: this.view.panY };
      this.paintCanvas.setPointerCapture(Event.pointerId);
      Event.preventDefault();
      return;
    }
    if (Event.button !== 0) return;
    const Texture = this.screenToTexture(Event.clientX, Event.clientY);
    const Layer = Document.activeLayer;
    if (this.engine.tool === "eyedropper") {
      const Sample = this.engine.sample(Document, Texture.x, Texture.y);
      if (Sample) {
        this.engine.fgColor = Sample.color;
        if (this.inspectorTab === "tool") this.renderInspector();
        else this.drawBrushPreview();
        this.notify(`Sampled ${Sample.color} · M ${Sample.metal.toFixed(2)} · R ${Sample.rough.toFixed(2)}`);
      }
      this.paintCanvas.setPointerCapture(Event.pointerId);
      this.eyedropperActive = true;
      return;
    }
    if (!Layer) {
      this.notify("Add a layer to paint on.");
      return;
    }
    const QuickMask = Document.quickMaskActive;
    if (Layer.locked && !QuickMask) {
      this.notify(`${Layer.name} is locked.`);
      return;
    }
    if (this.engine.tool === "move") {
      if (["svg", "text", "image"].includes(Layer.kind) && this.pointInDecal(Layer, Texture.x, Texture.y)) {
        this.beginDecalDrag(Event.clientX, Event.clientY, "move");
        this.paintCanvas.setPointerCapture(Event.pointerId);
      } else {
        // Select the topmost decal under the cursor, if any.
        const Hit = [...Document.layers].reverse().find((Entry) => Entry.visible && ["svg", "text", "image"].includes(Entry.kind) && this.pointInDecal(Entry, Texture.x, Texture.y));
        if (Hit) {
          this.selectLayer(Hit.id);
          this.beginDecalDrag(Event.clientX, Event.clientY, "move");
          this.paintCanvas.setPointerCapture(Event.pointerId);
        } else {
          this.notify("Move tool grabs the active decal — select one first.");
        }
      }
      return;
    }
    if (this.engine.tool === "fill") {
      if (Layer.kind !== "paint" && !QuickMask) {
        this.notify("Fill paints raster layers — rasterize or pick a paint layer.");
        return;
      }
      const Filled = this.engine.floodFill(Document, Layer, Texture.x, Texture.y);
      this.renderLayerList();
      this.updateChrome();
      if (!Filled) this.notify("Nothing within tolerance at that point.");
      return;
    }
    if (Layer.kind !== "paint" && !QuickMask) {
      this.notify(`${LAYER_KINDS[Layer.kind].label} is procedural — rasterize it or pick a paint layer.`);
      return;
    }
    if (this.engine.beginStroke(Document, Layer, Texture.x, Texture.y, this.pointerPressure(Event))) {
      this.paintCanvas.setPointerCapture(Event.pointerId);
      Select("#live-pill span").textContent = "PAINTING";
    }
  }

  onPaintMove(Event) {
    const Rect = this.paintCanvas.getBoundingClientRect();
    this.hover = { x: Event.clientX - Rect.left, y: Event.clientY - Rect.top };
    if (this.panning) {
      this.view.panX = this.panning.panX + (Event.clientX - this.panning.x);
      this.view.panY = this.panning.panY + (Event.clientY - this.panning.y);
      return;
    }
    if (this.decalDrag && this.decalDrag.viaCanvas) {
      this.moveDecalDrag(Event.clientX, Event.clientY);
      return;
    }
    if (this.eyedropperActive && Event.buttons) {
      const Document = this.doc;
      const Texture = this.screenToTexture(Event.clientX, Event.clientY);
      const Sample = this.engine.sample(Document, Texture.x, Texture.y);
      if (Sample) {
        this.engine.fgColor = Sample.color;
        if (this.inspectorTab === "tool") this.renderInspector();
      }
      return;
    }
    if (!this.engine.painting) return;
    const Texture = this.screenToTexture(Event.clientX, Event.clientY);
    this.engine.strokeTo(Texture.x, Texture.y, this.pointerPressure(Event));
  }

  onPaintUp(Event) {
    void Event;
    if (this.panning) {
      this.panning = null;
      return;
    }
    if (this.decalDrag && this.decalDrag.viaCanvas) {
      this.endDecalDrag();
      return;
    }
    if (this.eyedropperActive) {
      this.eyedropperActive = false;
      return;
    }
    if (this.engine.painting) {
      this.engine.endStroke();
      Select("#live-pill span").textContent = "LIVE";
      this.updateChrome();
    }
  }

  onPaintCancel() {
    this.panning = null;
    this.eyedropperActive = false;
    if (this.decalDrag) this.endDecalDrag(true);
    if (this.engine.painting) {
      this.engine.cancelStroke();
      Select("#live-pill span").textContent = "LIVE";
    }
  }

  /* ================= Decal dragging ================= */

  beginDecalDrag(ClientX, ClientY, Handle) {
    const Document = this.doc;
    const Layer = Document ? Document.activeLayer : null;
    if (!Layer || Layer.locked) return;
    const Art = this.decalArtSize(Layer);
    if (!Art) return;
    const Bounds = decalBounds(Layer.params, Art.w, Art.h, Document.width, Document.height);
    this.decalDrag = {
      layerId: Layer.id,
      handle: Handle,
      viaCanvas: Handle === "move",
      startX: ClientX,
      startY: ClientY,
      before: { ...Layer.params },
      centerX: Bounds.centerX,
      centerY: Bounds.centerY,
      scale: Layer.params.scale,
      rotation: Layer.params.rotation || 0,
      radius: Math.hypot(Bounds.width, Bounds.height) / 2,
    };
  }

  moveDecalDrag(ClientX, ClientY) {
    const Drag = this.decalDrag;
    const Document = this.doc;
    if (!Drag || !Document) return;
    const Layer = Document.layers.find((Entry) => Entry.id === Drag.layerId);
    if (!Layer) return;
    const Scale = this.viewScale();
    if (Drag.handle === "move") {
      const Dx = (ClientX - Drag.startX) / Scale / Document.width;
      const Dy = (ClientY - Drag.startY) / Scale / Document.height;
      Layer.params.x = Drag.before.x + Dx;
      Layer.params.y = Drag.before.y + Dy;
    } else if (Drag.handle === "rotate") {
      const Center = this.textureToScreen(Drag.centerX, Drag.centerY);
      const Before = Math.atan2(Drag.startY - Center.y, Drag.startX - Center.x);
      const Now = Math.atan2(ClientY - Center.y, ClientX - Center.x);
      let Delta = ((Now - Before) * 180) / Math.PI;
      Layer.params.rotation = Drag.rotation + Delta;
    } else {
      // Corner scale: radial distance from the decal center.
      const Center = this.textureToScreen(Drag.centerX, Drag.centerY);
      const StartRadius = Math.hypot(Drag.startX - Center.x, Drag.startY - Center.y);
      const NowRadius = Math.hypot(ClientX - Center.x, ClientY - Center.y);
      const Factor = StartRadius > 4 ? NowRadius / StartRadius : 1;
      Layer.params.scale = Clamp(Drag.scale * Factor, 0.01, 4);
    }
    Layer.rasterDirty = true;
    Layer.thumbDirty = true;
    Document.markDirty();
    if (this.inspectorTab === "layer") this.renderInspector();
  }

  endDecalDrag(Cancelled = false) {
    const Drag = this.decalDrag;
    this.decalDrag = null;
    const Document = this.doc;
    if (!Drag || !Document) return;
    const Layer = Document.layers.find((Entry) => Entry.id === Drag.layerId);
    if (!Layer) return;
    if (Cancelled) {
      Layer.params = { ...Drag.before };
      Layer.rasterDirty = true;
      Document.markDirty();
      return;
    }
    const After = { ...Layer.params };
    Document.pushHistory({
      label: "Transform decal",
      type: "props",
      time: performance.now(),
      keys: ["params"],
      layerId: Layer.id,
      before: { params: Drag.before },
      after: { params: After },
      undo: () => {
        const Target = Document.layers.find((Entry) => Entry.id === Layer.id);
        if (!Target) return;
        Target.params = { ...Drag.before };
        Target.rasterDirty = true;
        Target.thumbDirty = true;
        Document.markDirty();
        this.renderAll();
      },
      redo: () => {
        const Target = Document.layers.find((Entry) => Entry.id === Layer.id);
        if (!Target) return;
        Target.params = { ...After };
        Target.rasterDirty = true;
        Target.thumbDirty = true;
        Document.markDirty();
        this.renderAll();
      },
    });
    if (this.inspectorTab === "layer") this.renderInspector();
    this.updateChrome();
  }

  /* ================= Quick Mask ================= */

  toggleQuickMask() {
    const Document = this.doc;
    if (!Document) return;
    if (Document.quickMaskActive) {
      if (Document.commitQuickMask()) {
        this.compositeVersion++;
        this.renderAll();
        this.notify("Quick mask committed to the layer mask.");
      }
      return;
    }
    if (this.engine.painting) return;
    if (!Document.enterQuickMask()) {
      this.notify("Quick mask needs an editable layer — not an adjustment.");
      return;
    }
    if (this.view.mode === "3d") this.setViewMode("split", true);
    this.updateChrome();
    this.notify("Quick mask: paint to reveal, erase to conceal. Q commits · Esc discards · Enter commits.");
  }

  /* ================= View modes / history ================= */

  setViewMode(Mode, Silent = false) {
    this.view.mode = Mode;
    this.syncViewModeUI();
    this.layoutCanvases();
    if (!Silent) {
      this.notify(Mode === "2d" ? "2D paint view." : Mode === "3d" ? "3D material preview — drag to orbit." : "Split view — paint left, shade right.");
    }
  }

  syncViewModeUI() {
    const Mode = this.view.mode;
    Select("#viewport").classList.toggle("split", Mode === "split");
    Select("#paint-canvas").hidden = Mode === "3d";
    Select("#preview-canvas").hidden = Mode === "2d";
    Select("#mesh-select").hidden = Mode === "2d";
    Select("#view-mode").value = Mode;
    Select("#navigator").style.display = Mode === "3d" ? "none" : "";
  }

  undo() {
    const Document = this.doc;
    if (!Document) return;
    const Label = Document.undo();
    if (Label) {
      this.compositeVersion++;
      this.renderAll();
    }
  }

  redo() {
    const Document = this.doc;
    if (!Document) return;
    const Label = Document.redo();
    if (Label) {
      this.compositeVersion++;
      this.renderAll();
    }
  }

  resetInspectorTab() {
    if (this.inspectorTab === "tool") {
      const Default = BRUSH_PRESETS[0];
      Object.assign(this.engine.brush, {
        size: Default.size, hardness: Default.hardness, opacity: Default.opacity,
        flow: Default.flow, spacing: Default.spacing, roundness: Default.roundness,
        angle: Default.angle, scatter: Default.scatter, smoothing: Default.smoothing, tip: Default.tip,
      });
      this.engine.symmetry = "none";
      this.renderInspector();
      this.updateChrome();
      this.notify("Brush reset to Soft round.");
    } else if (this.inspectorTab === "material" && this.doc) {
      this.doc.material = defaultMaterial();
      this.doc.markDirty();
      this.renderInspector();
      this.notify("Base material reset.");
    } else if (this.doc && this.doc.activeLayer) {
      const Layer = this.doc.activeLayer;
      this.doc.setLayerProps(Layer, { opacity: 1, blend: "normal", clip: false }, "Reset layer");
      this.renderAll();
    }
  }

  /* ================= Add menu / imports ================= */

  toggleAddMenu(Force) {
    const Menu = Select("#add-menu");
    const Show = Force !== undefined ? Force : Menu.hidden;
    Menu.hidden = !Show;
    Select("#add-button").setAttribute("aria-expanded", Show ? "true" : "false");
    if (Show) {
      const Rect = Select("#add-button").getBoundingClientRect();
      Menu.style.left = `${Math.min(window.innerWidth - 290, Rect.left - 180)}px`;
      Menu.style.top = `${Rect.bottom + 8}px`;
    }
  }

  addLayerOfKind(Kind) {
    const Document = this.doc;
    if (!Document) return;
    if (Kind === "image") {
      Select("#layer-image-file").click();
      return;
    }
    if (Kind === "svg") {
      this.setTool("move");
      Document.addLayer("svg", {
        name: "SVG decal",
        params: {
          svg: SVG_PRESETS[1].svg, x: 0.5, y: 0.5, scale: 0.5, rotation: 0,
          flipX: false, flipY: false, tint: "", metallic: -1, roughness: -1,
          emissive: 0, emissiveColor: "#ffffff", height: -1,
        },
      });
    } else if (Kind === "text") {
      this.setTool("move");
      Document.addLayer("text", { name: "Text decal" });
    } else {
      Document.addLayer(Kind, {});
    }
    this.renderAll();
  }

  importImageFile(File) {
    const Document = this.doc;
    if (!File || !Document) return;
    const Reader = new FileReader();
    Reader.onload = () => {
      const DataURL = Reader.result;
      const Image = new window.Image();
      Image.onload = () => {
        const Active = Document.activeLayer;
        if (Active && Active.kind === "image") {
          Document.setLayerProps(Active, { params: { ...Active.params, dataURL: DataURL } }, "Replace image");
          Active.artCache = null;
        } else {
          const Fit = Math.min(1, Document.width / Image.naturalWidth, Document.height / Image.naturalHeight);
          Document.addLayer("image", {
            name: File.name.replace(/\.[^.]+$/, "").slice(0, 40) || "Image",
            params: {
              dataURL: DataURL, x: 0.5, y: 0.5, scale: Math.max(0.05, Fit), rotation: 0,
              flipX: false, flipY: false, metallic: -1, roughness: -1,
              emissive: 0, emissiveColor: "#ffffff", height: -1,
            },
          });
          this.setTool("move");
        }
        Select("#layer-image-file").value = "";
        this.renderAll();
      };
      Image.onerror = () => this.notify("That file could not be decoded as an image.");
      Image.src = DataURL;
    };
    Reader.readAsDataURL(File);
  }

  importSvgFile(File) {
    const Document = this.doc;
    const Layer = Document ? Document.activeLayer : null;
    if (!File || !Document) return;
    const Reader = new FileReader();
    Reader.onload = () => {
      const Text = String(Reader.result || "");
      if (!Text.includes("<svg")) {
        this.notify("That file does not look like SVG.");
        return;
      }
      if (Layer && Layer.kind === "svg") {
        Document.setLayerProps(Layer, { params: { ...Layer.params, svg: Text } }, "Import SVG");
        Layer.artCache = null;
      } else {
        Document.addLayer("svg", {
          name: File.name.replace(/\.[^.]+$/, "").slice(0, 40) || "SVG decal",
          params: {
            svg: Text, x: 0.5, y: 0.5, scale: 0.55, rotation: 0,
            flipX: false, flipY: false, tint: "", metallic: -1, roughness: -1,
            emissive: 0, emissiveColor: "#ffffff", height: -1,
          },
        });
        this.setTool("move");
      }
      Select("#decal-file").value = "";
      this.renderAll();
    };
    Reader.readAsText(File);
  }

  /* ================= Save / open / export ================= */

  openNewDialog() {
    Select("#new-name").value = `Texture ${this.documents.length + 1}`;
    Select("#new-dialog").showModal();
  }

  saveProject() {
    const Document = this.doc;
    if (!Document) return;
    Document.renderComposite(true);
    const Data = JSON.stringify(Document.serialize());
    const Blob = new window.Blob([Data], { type: "application/json" });
    const Link = document.createElement("a");
    Link.download = `${sanitizeName(Document.name)}.texpaint`;
    Link.href = URL.createObjectURL(Blob);
    document.body.appendChild(Link);
    Link.click();
    Link.remove();
    setTimeout(() => URL.revokeObjectURL(Link.href), 5000);
    Document.dirty = false;
    this.renderDocumentTabs();
    this.updateChrome();
    this.notify(`Saved ${Document.name}.texpaint — layers, decals and masks included.`);
  }

  async openProject(File) {
    if (!File) return;
    try {
      const Text = await File.text();
      const Data = JSON.parse(Text);
      if (Data.app !== "frontier-texture-paint" && !Data.layers) {
        throw new Error("Not a texture project");
      }
      const Document = await TextureDocument.deserialize(Data);
      Document.onChange = () => this.refreshAfterAsync();
      if (this.documents.length >= 6) {
        this.notify("Tab limit reached — close a texture first.");
        return;
      }
      this.documents.push(Document);
      this.docIndex = this.documents.length - 1;
      Select("#import-file").value = "";
      this.afterDocumentSwitch();
      this.notify(`Opened ${Document.name} — ${Document.layers.length} layers.`);
    } catch {
      this.notify("Could not open that project file.");
    }
  }

  openExport() {
    const Document = this.doc;
    if (!Document) return;
    Select("#export-scene-name").textContent = `${Document.name} · ${Document.width}² · ${Document.layers.length} layers`;
    Select("#export-dialog").showModal();
  }

  runExport() {
    const Document = this.doc;
    if (!Document) return;
    Document.renderComposite(true);
    Document.renderNormal(true);
    this.compositeVersion++;
    const SizeOption = Select("#export-size").value;
    const Size = SizeOption === "doc" ? Document.width : parseInt(SizeOption, 10);
    const Naming = Select("#export-naming").value;
    const Base = sanitizeName(Document.name);
    const Name = (Channel) => (Naming === "prefix" ? `${Channel}_${Base}.png` : `${Base}_${Channel}.png`);
    const Jobs = [];
    if (Select("#export-albedo").checked) Jobs.push(["albedo", getResized(Document.composite.albedo, Size)]);
    if (Select("#export-metallic").checked) Jobs.push(["metallic", getResized(Document.composite.metallic, Size)]);
    if (Select("#export-roughness").checked) Jobs.push(["roughness", getResized(Document.composite.roughness, Size)]);
    if (Select("#export-normal").checked) Jobs.push(["normal", getResized(Document.composite.normal, Size)]);
    if (Select("#export-emissive").checked) Jobs.push(["emissive", getResized(Document.composite.emissive, Size)]);
    if (Select("#export-height").checked) Jobs.push(["height", getResized(Document.composite.height, Size)]);
    if (Select("#export-orm").checked) Jobs.push(["orm", buildORM(Document, Size)]);
    if (!Jobs.length && !Select("#export-project").checked) {
      Select("#export-progress-label").textContent = "Tick at least one map or the project file.";
      return;
    }
    Select("#export-progress-label").textContent = `Exporting ${Jobs.length} map${Jobs.length === 1 ? "" : "s"} at ${Size}²…`;
    Jobs.forEach(([Channel, Canvas], Index) => {
      setTimeout(() => downloadCanvas(Canvas, Name(Channel)), Index * 220);
    });
    if (Select("#export-project").checked) {
      setTimeout(() => this.saveProject(), Jobs.length * 220 + 120);
    }
    setTimeout(() => {
      Select("#export-progress-label").textContent = "Each map downloads as a separate file, prefixed with the texture name.";
      Select("#export-dialog").close();
    }, Jobs.length * 220 + 500);
  }
}

function grayCss(Value) {
  const Gray = Math.round(Clamp(Value, 0, 1) * 255);
  return `rgb(${Gray},${Gray},${Gray})`;
}

function lighten(Hex, Amount) {
  const Clean = String(Hex || "#808080").replace("#", "");
  const Full = Clean.length === 3 ? Clean.split("").map((C) => C + C).join("") : Clean;
  const Int = parseInt(Full.slice(0, 6), 16);
  if (Number.isNaN(Int)) return Hex;
  const Channels = [(Int >> 16) & 255, (Int >> 8) & 255, Int & 255].map((Channel) => {
    if (Amount >= 0) return Math.round(Channel + (255 - Channel) * Amount);
    return Math.round(Channel * (1 + Amount));
  });
  return `#${Channels.map((Channel) => Clamp(Channel, 0, 255).toString(16).padStart(2, "0")).join("")}`;
}
