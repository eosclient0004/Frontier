/* Frontier Texture Paint — document model.
   Layer stack + per-channel PBR composite + dirty-rect history + serialization.
   Layers composite bottom-up (layers[0] is the bottom of the stack). */

import { BlendOp } from "./Presets.js";
import {
  makeCanvas,
  mulberry32,
  resolveDecalArtwork,
  renderTextArtwork,
  drawDecalArtwork,
  fillChannelFromAlpha,
} from "./Decals.js";

export const SERIAL_VERSION = 1;
export const CHANNEL_IDS = ["albedo", "metallic", "roughness", "emissive", "height"];
/** Short channel keys used on layer channel maps. */
export const LAYER_CHANNELS = ["albedo", "metal", "rough", "emissive", "height", "mask"];

let LayerSeq = 1;
export function nextLayerId() {
  return `l${Date.now().toString(36)}${(LayerSeq++).toString(36)}${Math.floor(Math.random() * 1296).toString(36)}`;
}

function clone(Value) {
  return typeof structuredClone === "function" ? structuredClone(Value) : JSON.parse(JSON.stringify(Value));
}

export function defaultMaterial() {
  return {
    albedo: "#808080",
    metallic: 0.0,
    roughness: 0.85,
    emissive: "#000000",
    emissiveStrength: 0,
    height: 0.5,
    normalStrength: 1.0,
  };
}

export function defaultLayerParams(Kind) {
  switch (Kind) {
    case "fill":
      return { pattern: "solid", color: "#808080", color2: "#3a3a3a", scale: 8, angle: 0, seed: 7, metallic: 0, roughness: 0.85, emissive: 0, emissiveColor: "#ffffff", height: 0.5 };
    case "adjust":
      return { brightness: 0, contrast: 0, saturation: 0, hue: 0, blur: 0, invert: 0, opacity: 1 };
    case "svg":
      return { svg: "", x: 0.5, y: 0.5, scale: 0.5, rotation: 0, flipX: false, flipY: false, tint: "", metallic: -1, roughness: -1, emissive: 0, emissiveColor: "#ffffff", height: -1 };
    case "text":
      return { text: "TEXT", font: "grotesk", size: 0.18, weight: 500, italic: false, align: "center", color: "#ece9e2", stroke: 0, strokeColor: "#111111", spacing: 0.04, lineHeight: 1.15, x: 0.5, y: 0.5, scale: 1, rotation: 0, flipX: false, flipY: false, metallic: -1, roughness: -1, emissive: 0, emissiveColor: "#ffffff", height: -1 };
    case "image":
      return { dataURL: "", x: 0.5, y: 0.5, scale: 1, rotation: 0, flipX: false, flipY: false, metallic: -1, roughness: -1, emissive: 0, emissiveColor: "#ffffff", height: -1 };
    default:
      return {};
  }
}

export function defaultLayerName(Kind, Count) {
  const Names = { paint: "Paint", fill: "Fill", svg: "SVG decal", text: "Text", image: "Image", adjust: "Adjustment" };
  return `${Names[Kind] || "Layer"} ${Count}`;
}

export function createLayer(Document, Kind, Options = {}) {
  const Count = Document.layers.filter((Layer) => Layer.kind === Kind).length + 1;
  const Layer = {
    id: nextLayerId(),
    kind: Kind,
    name: Options.name || defaultLayerName(Kind, Count),
    visible: Options.visible !== undefined ? Options.visible : true,
    locked: false,
    opacity: Options.opacity !== undefined ? Options.opacity : 1,
    blend: Options.blend || "normal",
    clip: false,
    maskEnabled: false,
    maskSelected: false,
    params: Object.assign(defaultLayerParams(Kind), Options.params || {}),
    channels: {},
    artCache: null,
    fillCache: null,
    thumb: makeCanvas(88, 88),
    thumbDirty: true,
    rasterDirty: true,
  };
  if (Kind === "paint") {
    Layer.channels.albedo = makeCanvas(Document.width, Document.height);
  }
  if (Kind === "svg" || Kind === "text" || Kind === "image") {
    Layer.channels.albedo = makeCanvas(Document.width, Document.height);
  }
  return Layer;
}

export function ensureLayerChannel(Document, Layer, Key) {
  if (!Layer.channels[Key]) {
    Layer.channels[Key] = makeCanvas(Document.width, Document.height);
  }
  return Layer.channels[Key];
}

export class TextureDocument {
  constructor(Name, Width, Height, Options = {}) {
    this.name = Name || "Untitled texture";
    this.width = Width;
    this.height = Height;
    this.baseMode = Options.baseMode || "material"; // 'material' | 'transparent'
    this.material = Object.assign(defaultMaterial(), Options.material || {});
    this.layers = [];
    this.activeLayerId = null;
    this.composite = {};
    this.scratch = {};
    for (const Id of ["albedo", "metallic", "roughness", "emissive", "height", "normal"]) {
      this.composite[Id] = makeCanvas(Width, Height);
    }
    for (const Id of ["a", "b", "mask"]) {
      this.scratch[Id] = makeCanvas(Width, Height);
    }
    this.compositeDirty = true;
    this.normalDirty = true;
    this.normalThrottle = 0;
    this.history = [];
    this.historyIndex = -1;
    this.historyLimit = Width >= 4096 ? 6 : Width >= 2048 ? 12 : 30;
    this.dirty = false;
    this.lastCompositeMs = 0;
    this.onChange = null; // () => void — panel hook (async raster completion)
  }

  get activeLayer() {
    return this.layers.find((Layer) => Layer.id === this.activeLayerId) || null;
  }

  setActiveLayer(Id) {
    if (this.layers.some((Layer) => Layer.id === Id)) this.activeLayerId = Id;
  }

  markDirty(Structural = true) {
    this.compositeDirty = true;
    this.normalDirty = true;
    if (Structural) this.dirty = true;
  }

  /* ---------------- Layers ---------------- */

  insertLayer(Layer, Index = this.layers.length) {
    const Safe = Math.max(0, Math.min(this.layers.length, Index));
    this.layers.splice(Safe, 0, Layer);
    this.activeLayerId = Layer.id;
    Layer.thumbDirty = true;
    this.markDirty();
  }

  addLayer(Kind, Options = {}) {
    const Layer = createLayer(this, Kind, Options);
    const ActiveIndex = this.layers.findIndex((Entry) => Entry.id === this.activeLayerId);
    const Index = ActiveIndex >= 0 ? ActiveIndex + 1 : this.layers.length;
    this.pushStructure(`Add ${Layer.name}`, () => this.insertLayer(Layer, Index));
    return Layer;
  }

  removeLayerById(Id) {
    const Index = this.layers.findIndex((Layer) => Layer.id === Id);
    if (Index < 0) return;
    const [Layer] = this.layers.splice(Index, 1);
    if (this.activeLayerId === Id) {
      const Fallback = this.layers[Math.min(Index, this.layers.length - 1)];
      this.activeLayerId = Fallback ? Fallback.id : null;
    }
    this.markDirty();
    return { layer: Layer, index: Index };
  }

  deleteActiveLayer() {
    const Layer = this.activeLayer;
    if (!Layer) return false;
    const Snapshot = serializeLayer(Layer);
    const Index = this.layers.findIndex((Entry) => Entry.id === Layer.id);
    this.pushHistory({
      label: `Delete ${Layer.name}`,
      type: "structure",
      undo: () => {
        const Restored = deserializeLayer(this, Snapshot);
        this.layers.splice(Math.min(Index, this.layers.length), 0, Restored);
        this.activeLayerId = Restored.id;
        this.markDirty();
      },
      redo: () => {
        this.removeLayerById(Layer.id);
      },
    });
    this.removeLayerById(Layer.id);
    return true;
  }

  duplicateActiveLayer() {
    const Layer = this.activeLayer;
    if (!Layer) return null;
    const Snapshot = serializeLayer(Layer);
    const Copy = deserializeLayer(this, Snapshot);
    Copy.id = nextLayerId();
    Copy.name = `${Layer.name} copy`;
    const Index = this.layers.findIndex((Entry) => Entry.id === Layer.id) + 1;
    this.pushStructure(`Duplicate ${Layer.name}`, () => this.insertLayer(Copy, Index));
    return Copy;
  }

  moveActiveLayer(Direction) {
    const Index = this.layers.findIndex((Layer) => Layer.id === this.activeLayerId);
    if (Index < 0) return false;
    const Target = Index + Direction;
    if (Target < 0 || Target >= this.layers.length) return false;
    const Before = this.layers.map((Layer) => Layer.id);
    const [Layer] = this.layers.splice(Index, 1);
    this.layers.splice(Target, 0, Layer);
    const After = this.layers.map((Layer) => Layer.id);
    this.pushHistory({
      label: Direction > 0 ? "Raise layer" : "Lower layer",
      type: "order",
      undo: () => this.applyOrder(Before),
      redo: () => this.applyOrder(After),
    });
    this.markDirty();
    return true;
  }

  reorderLayer(DragId, TargetId, PlaceAbove) {
    if (DragId === TargetId) return false;
    const Before = this.layers.map((Layer) => Layer.id);
    const From = this.layers.findIndex((Layer) => Layer.id === DragId);
    const To = this.layers.findIndex((Layer) => Layer.id === TargetId);
    if (From < 0 || To < 0) return false;
    const [Layer] = this.layers.splice(From, 1);
    let Insert = this.layers.findIndex((Entry) => Entry.id === TargetId);
    if (PlaceAbove) Insert += 1;
    this.layers.splice(Insert, 0, Layer);
    const After = this.layers.map((Entry) => Entry.id);
    this.pushHistory({ label: "Reorder layers", type: "order", undo: () => this.applyOrder(Before), redo: () => this.applyOrder(After) });
    this.markDirty();
    return true;
  }

  applyOrder(Ids) {
    const Map = new Map(this.layers.map((Layer) => [Layer.id, Layer]));
    this.layers = Ids.map((Id) => Map.get(Id)).filter(Boolean);
    for (const Layer of Map.values()) {
      if (!Ids.includes(Layer.id)) this.layers.push(Layer);
    }
    this.markDirty();
  }

  mergeActiveDown() {
    const Index = this.layers.findIndex((Layer) => Layer.id === this.activeLayerId);
    if (Index <= 0) return false;
    const Upper = this.layers[Index];
    const Lower = this.layers[Index - 1];
    if (Lower.locked || Upper.locked) return false;
    if (Lower.kind !== "paint" || Upper.kind !== "paint") return "rasterize";
    // Snapshot lower channels + upper layer, then composite upper into lower.
    const Before = {};
    for (const Key of ["albedo", "metal", "rough", "emissive", "height"]) {
      if (Lower.channels[Key] || Upper.channels[Key]) {
        ensureLayerChannel(this, Lower, Key);
        Before[Key] = Lower.channels[Key].getContext("2d").getImageData(0, 0, this.width, this.height);
      }
    }
    const UpperSnapshot = serializeLayer(Upper);
    this.flattenLayerInto(Lower, Upper);
    this.removeLayerById(Upper.id);
    this.activeLayerId = Lower.id;
    Lower.thumbDirty = true;
    this.pushHistory({
      label: `Merge ${Upper.name} down`,
      type: "merge",
      undo: () => {
        for (const [Key, Data] of Object.entries(Before)) {
          ensureLayerChannel(this, Lower, Key).getContext("2d").putImageData(Data, 0, 0);
        }
        const Restored = deserializeLayer(this, UpperSnapshot);
        const LowerIndex = this.layers.findIndex((Entry) => Entry.id === Lower.id);
        this.layers.splice(LowerIndex + 1, 0, Restored);
        this.activeLayerId = Restored.id;
        Lower.thumbDirty = true;
        this.markDirty();
      },
      redo: () => {
        const Target = this.layers.find((Entry) => Entry.id === Lower.id);
        const Source = this.layers.find((Entry) => Entry.id === Upper.id);
        if (Target && Source) {
          this.flattenLayerInto(Target, Source);
          this.removeLayerById(Source.id);
          this.activeLayerId = Target.id;
          Target.thumbDirty = true;
          this.markDirty();
        }
      },
    });
    return true;
  }

  /** Bake `Source` paint pixels into `Target` honoring opacity/blend/mask. */
  flattenLayerInto(Target, Source) {
    const Masked = this.maskedSource(Source, "albedo");
    if (!Masked) return;
    const Context = Target.channels.albedo.getContext("2d");
    Context.save();
    Context.globalAlpha = Source.opacity;
    Context.globalCompositeOperation = BlendOp(Source.blend);
    Context.drawImage(Masked, 0, 0);
    Context.restore();
    for (const Key of ["metal", "rough", "emissive", "height"]) {
      if (!Source.channels[Key]) continue;
      ensureLayerChannel(this, Target, Key);
      const TargetContext = Target.channels[Key].getContext("2d");
      TargetContext.save();
      TargetContext.globalAlpha = Source.opacity;
      TargetContext.globalCompositeOperation = Source.blend === "erase" ? "destination-out" : "source-over";
      TargetContext.drawImage(this.maskedSource(Source, Key) || Source.channels[Key], 0, 0);
      TargetContext.restore();
    }
    if (Source.maskEnabled && Source.channels.mask && !Target.channels.mask) {
      Target.channels.mask = makeCanvas(this.width, this.height);
      Target.channels.mask.getContext("2d").drawImage(Source.channels.mask, 0, 0);
      Target.maskEnabled = true;
    }
  }

  /** Rasterize an svg/text/image/fill/adjust layer into a paint layer. */
  rasterizeActiveLayer() {
    const Index = this.layers.findIndex((Layer) => Layer.id === this.activeLayerId);
    if (Index < 0) return false;
    const Layer = this.layers[Index];
    if (Layer.kind === "paint") return false;
    const Snapshot = serializeLayer(Layer);
    const Paint = createLayer(this, Layer.kind === "adjust" ? "paint" : "paint", { name: `${Layer.name} raster` });
    Paint.opacity = Layer.opacity;
    Paint.blend = Layer.blend;
    Paint.visible = Layer.visible;
    // Render the source layer alone through the composite path.
    const Solo = this.renderLayerSources(Layer);
    if (Solo.albedo) Paint.channels.albedo.getContext("2d").drawImage(Solo.albedo, 0, 0);
    for (const [Key, Canvas] of Object.entries(Solo)) {
      if (Key === "albedo" || !Canvas) continue;
      const Map = { metallic: "metal", roughness: "rough", emissive: "emissive", height: "height" };
      const Target = Map[Key];
      if (!Target) continue;
      ensureLayerChannel(this, Paint, Target).getContext("2d").drawImage(Canvas, 0, 0);
    }
    Paint.thumbDirty = true;
    this.layers.splice(Index, 1, Paint);
    this.activeLayerId = Paint.id;
    this.markDirty();
    this.pushHistory({
      label: `Rasterize ${Layer.name}`,
      type: "structure",
      undo: () => {
        const At = this.layers.findIndex((Entry) => Entry.id === Paint.id);
        const Restored = deserializeLayer(this, Snapshot);
        if (At >= 0) this.layers.splice(At, 1, Restored);
        this.activeLayerId = Restored.id;
        this.markDirty();
      },
      redo: () => {
        const At = this.layers.findIndex((Entry) => Entry.id === Paint.id || Entry.id === Layer.id);
        if (At >= 0) {
          const Again = deserializeLayer(this, serializeLayer(Paint));
          Again.id = Paint.id;
          this.layers.splice(At, 1, Again);
          this.activeLayerId = Again.id;
          this.markDirty();
        }
      },
    });
    return true;
  }

  clearActiveLayer() {
    const Layer = this.activeLayer;
    if (!Layer || Layer.locked) return false;
    if (Layer.kind !== "paint" && Layer.kind !== "svg" && Layer.kind !== "text" && Layer.kind !== "image") return false;
    const Target = Layer.maskSelected && Layer.channels.mask ? "mask" : "albedo";
    const Keys = Target === "mask" ? ["mask"] : ["albedo", "metal", "rough", "emissive", "height"].filter((Key) => Layer.channels[Key]);
    if (!Keys.length) return false;
    const Before = {};
    for (const Key of Keys) {
      Before[Key] = Layer.channels[Key].getContext("2d").getImageData(0, 0, this.width, this.height);
      Layer.channels[Key].getContext("2d").clearRect(0, 0, this.width, this.height);
    }
    Layer.thumbDirty = true;
    this.markDirty();
    this.pushHistory({
      label: `Clear ${Layer.name}`,
      type: "pixels",
      undo: () => {
        for (const [Key, Data] of Object.entries(Before)) {
          ensureLayerChannel(this, Layer, Key).getContext("2d").putImageData(Data, 0, 0);
        }
        Layer.thumbDirty = true;
        this.markDirty();
      },
      redo: () => {
        for (const Key of Object.keys(Before)) {
          if (Layer.channels[Key]) Layer.channels[Key].getContext("2d").clearRect(0, 0, this.width, this.height);
        }
        Layer.thumbDirty = true;
        this.markDirty();
      },
    });
    return true;
  }

  /* ---------------- Masks ---------------- */

  toggleMask(Layer = this.activeLayer) {
    if (!Layer || Layer.kind === "adjust") return false;
    if (Layer.channels.mask) {
      Layer.maskEnabled = !Layer.maskEnabled;
    } else {
      Layer.channels.mask = makeCanvas(this.width, this.height);
      const Context = Layer.channels.mask.getContext("2d");
      Context.fillStyle = "#ffffff";
      Context.fillRect(0, 0, this.width, this.height);
      Layer.maskEnabled = true;
      Layer.maskSelected = true;
    }
    Layer.thumbDirty = true;
    this.markDirty();
    this.dirty = true;
    return true;
  }

  removeMask(Layer = this.activeLayer) {
    if (!Layer || !Layer.channels.mask) return false;
    delete Layer.channels.mask;
    Layer.maskEnabled = false;
    Layer.maskSelected = false;
    Layer.thumbDirty = true;
    this.markDirty();
    this.dirty = true;
    return true;
  }

  /* ---------------- Layer property tweaks (coalesced history) ---------------- */

  setLayerProps(Layer, Props, Label = "Edit layer") {
    if (!Layer) return;
    const Keys = Object.keys(Props);
    const Before = {};
    const After = {};
    for (const Key of Keys) {
      Before[Key] = clone(Layer[Key]);
      After[Key] = clone(Props[Key]);
    }
    Object.assign(Layer, clone(After));
    if ("params" in Props) {
      Layer.rasterDirty = true;
      Layer.fillCache = null;
    }
    Layer.thumbDirty = true;
    this.markDirty();
    // Coalesce rapid slider drags into one step.
    const Last = this.history[this.historyIndex];
    const Now = performance.now();
    if (Last && Last.type === "props" && Last.layerId === Layer.id && Now - Last.time < 900 && shallowEqualKeys(Last.keys, Keys)) {
      Last.after = Object.assign({}, Last.after, After);
      Last.time = Now;
      Last.label = Label;
      return;
    }
    this.pushHistory({
      label: Label,
      type: "props",
      time: Now,
      keys: Keys,
      layerId: Layer.id,
      before: Before,
      after: clone(After),
      undo: () => {
        const Target = this.layers.find((Entry) => Entry.id === Layer.id);
        if (!Target) return;
        Object.assign(Target, clone(Before));
        Target.rasterDirty = true;
        Target.fillCache = null;
        Target.thumbDirty = true;
        this.markDirty();
      },
      redo: () => {
        const Target = this.layers.find((Entry) => Entry.id === Layer.id);
        if (!Target) return;
        const Entry = this.history.find((Step) => Step.type === "props" && Step.layerId === Layer.id && Step.after);
        void Entry;
        Object.assign(Target, clone(After));
        Target.rasterDirty = true;
        Target.fillCache = null;
        Target.thumbDirty = true;
        this.markDirty();
      },
    });
  }

  pushStructure(Label, Apply) {
    const Before = this.layers.map((Layer) => Layer.id);
    Apply();
    const Added = this.layers.find((Layer) => !Before.includes(Layer.id));
    const After = this.layers.map((Layer) => Layer.id);
    const Snapshot = Added ? serializeLayer(Added) : null;
    const AddedIndex = Added ? After.indexOf(Added.id) : -1;
    this.pushHistory({
      label: Label,
      type: "structure",
      undo: () => {
        if (Added) this.removeLayerById(Added.id);
      },
      redo: () => {
        if (Snapshot) {
          const Restored = deserializeLayer(this, Snapshot);
          this.layers.splice(Math.min(AddedIndex, this.layers.length), 0, Restored);
          this.activeLayerId = Restored.id;
          this.markDirty();
        }
      },
    });
  }

  /* ---------------- History ---------------- */

  pushHistory(Entry) {
    this.history = this.history.slice(0, this.historyIndex + 1);
    this.history.push(Object.assign({ time: performance.now() }, Entry));
    if (this.history.length > this.historyLimit) this.history.shift();
    this.historyIndex = this.history.length - 1;
    this.dirty = true;
  }

  /** Pixel-stroke snapshot. `Capture` holds pre-stroke full clones per channel. */
  pushStrokeHistory(Label, Layer, Capture, Rect) {
    if (!Rect || Rect.w <= 0 || Rect.h <= 0) return;
    const X = Math.max(0, Math.floor(Rect.x));
    const Y = Math.max(0, Math.floor(Rect.y));
    const W = Math.min(this.width - X, Math.ceil(Rect.w));
    const H = Math.min(this.height - Y, Math.ceil(Rect.h));
    if (W <= 0 || H <= 0) return;
    const Channels = {};
    for (const [Key, Canvas] of Object.entries(Capture)) {
      const Before = Canvas.getContext("2d").getImageData(X, Y, W, H);
      const Live = Layer.channels[Key];
      const After = Live ? Live.getContext("2d").getImageData(X, Y, W, H) : null;
      Channels[Key] = { before: Before, after: After, created: !Live };
    }
    const LayerId = Layer.id;
    this.pushHistory({
      label: Label,
      type: "pixels",
      undo: () => {
        const Target = this.layers.find((Entry) => Entry.id === LayerId);
        if (!Target) return;
        for (const [Key, Pair] of Object.entries(Channels)) {
          ensureLayerChannel(this, Target, Key).getContext("2d").putImageData(Pair.before, X, Y);
        }
        Target.thumbDirty = true;
        this.markDirty();
      },
      redo: () => {
        const Target = this.layers.find((Entry) => Entry.id === LayerId);
        if (!Target) return;
        for (const [Key, Pair] of Object.entries(Channels)) {
          if (!Pair.after) {
            delete Target.channels[Key];
            continue;
          }
          ensureLayerChannel(this, Target, Key).getContext("2d").putImageData(Pair.after, X, Y);
        }
        Target.thumbDirty = true;
        this.markDirty();
      },
    });
  }

  undo() {
    const Entry = this.history[this.historyIndex];
    if (!Entry) return null;
    Entry.undo();
    this.historyIndex -= 1;
    this.dirty = true;
    return Entry.label;
  }

  redo() {
    const Entry = this.history[this.historyIndex + 1];
    if (!Entry) return null;
    Entry.redo();
    this.historyIndex += 1;
    this.dirty = true;
    return Entry.label;
  }

  get canUndo() { return this.historyIndex >= 0; }
  get canRedo() { return this.historyIndex < this.history.length - 1; }

  /* ---------------- Composite ---------------- */

  /** Ensure decal/fill sources are baked; returns false while async art loads. */
  ensureLayerPixels(Layer) {
    if (Layer.kind === "svg" || Layer.kind === "image") {
      const Art = resolveDecalArtwork(Layer, this.width, this.height, () => {
        Layer.rasterDirty = true;
        this.markDirty();
        if (this.onChange) this.onChange();
      });
      if (!Art || !Art.image) {
        // Keep previous pixels while loading; clear only when no art at all.
        if (Layer.artCache && Layer.artCache.error && Layer.rasterDirty) {
          Layer.channels.albedo.getContext("2d").clearRect(0, 0, this.width, this.height);
          Layer.rasterDirty = false;
        }
        return false;
      }
      if (Layer.rasterDirty) this.bakeDecalLayer(Layer, Art.image, Art.w, Art.h);
      return true;
    }
    if (Layer.kind === "text") {
      if (Layer.rasterDirty) {
        const Art = renderTextArtwork(Layer.params, this.width, this.height);
        this.bakeDecalLayer(Layer, Art, Art.width, Art.height);
      }
      return true;
    }
    return true;
  }

  bakeDecalLayer(Layer, Art, ArtWidth, ArtHeight) {
    const Albedo = ensureLayerChannel(this, Layer, "albedo");
    const Context = Albedo.getContext("2d");
    Context.save();
    Context.clearRect(0, 0, this.width, this.height);
    drawDecalArtwork(Context, Art, ArtWidth, ArtHeight, Layer, this.width, this.height);
    Context.restore();
    const Params = Layer.params;
    const Writes = [
      ["metallic", "metal", Params.metallic],
      ["roughness", "rough", Params.roughness],
      ["height", "height", Params.height],
    ];
    for (const [, Key, Value] of Writes) {
      if (Value === undefined || Value === null || Value < 0) {
        if (Layer.channels[Key]) delete Layer.channels[Key];
        continue;
      }
      fillChannelFromAlpha(ensureLayerChannel(this, Layer, Key), Albedo, Value, null);
    }
    if ((Params.emissive || 0) > 0) {
      fillChannelFromAlpha(ensureLayerChannel(this, Layer, "emissive"), Albedo, Params.emissive, Params.emissiveColor || "#ffffff");
    } else if (Layer.channels.emissive) {
      delete Layer.channels.emissive;
    }
    Layer.rasterDirty = false;
    Layer.thumbDirty = true;
  }

  /** Render one layer's per-channel sources (handles fill/adjust/decal). */
  renderLayerSources(Layer) {
    const Out = { albedo: null, metallic: null, roughness: null, emissive: null, height: null };
    if (Layer.kind === "fill") {
      const Cache = this.fillLayerCanvases(Layer);
      Out.albedo = Cache.albedo;
      Out.metallic = Cache.metal;
      Out.roughness = Cache.rough;
      Out.emissive = Cache.emissive;
      Out.height = Cache.height;
      return Out;
    }
    if (Layer.kind === "adjust") return Out; // applied in-place during composite
    this.ensureLayerPixels(Layer);
    Out.albedo = Layer.channels.albedo || null;
    Out.metallic = Layer.channels.metal || null;
    Out.roughness = Layer.channels.rough || null;
    Out.emissive = Layer.channels.emissive || null;
    Out.height = Layer.channels.height || null;
    return Out;
  }

  maskedSource(Layer, Key) {
    const Source = Layer.channels[Key];
    if (!Source) return null;
    if (!Layer.maskEnabled || !Layer.channels.mask) return Source;
    const Scratch = this.scratch.mask;
    const Context = Scratch.getContext("2d");
    Context.save();
    Context.globalCompositeOperation = "source-over";
    Context.clearRect(0, 0, this.width, this.height);
    Context.drawImage(Source, 0, 0);
    Context.globalCompositeOperation = "destination-in";
    Context.drawImage(Layer.channels.mask, 0, 0);
    Context.restore();
    return Scratch;
  }

  fillLayerCanvases(Layer) {
    const Key = JSON.stringify([this.width, this.height, Layer.params]);
    if (Layer.fillCache && Layer.fillCache.key === Key) return Layer.fillCache.canvases;
    const Params = Layer.params;
    const Albedo = makeCanvas(this.width, this.height);
    renderFillPattern(Albedo.getContext("2d"), this.width, this.height, Params);
    const Canvases = { albedo: Albedo, metal: null, rough: null, emissive: null, height: null };
    const Fill = (Value) => {
      const Canvas = makeCanvas(this.width, this.height);
      const Gray = Math.round(Math.max(0, Math.min(1, Value)) * 255);
      const Context = Canvas.getContext("2d");
      Context.fillStyle = `rgb(${Gray},${Gray},${Gray})`;
      Context.fillRect(0, 0, this.width, this.height);
      return Canvas;
    };
    Canvases.metal = Fill(Params.metallic || 0);
    Canvases.rough = Fill(Params.roughness !== undefined ? Params.roughness : 0.85);
    Canvases.height = Fill(Params.height !== undefined ? Params.height : 0.5);
    if ((Params.emissive || 0) > 0) {
      const Canvas = makeCanvas(this.width, this.height);
      const Context = Canvas.getContext("2d");
      Context.fillStyle = Params.emissiveColor || "#ffffff";
      Context.globalAlpha = Math.min(1, Params.emissive);
      Context.fillRect(0, 0, this.width, this.height);
      Canvases.emissive = Canvas;
    }
    Layer.fillCache = { key: Key, canvases: Canvases };
    return Canvases;
  }

  renderComposite(Force = false) {
    if (!this.compositeDirty && !Force) return false;
    const Started = performance.now();
    const { width: W, height: H } = this;
    const Material = this.material;
    // Base coats.
    const Albedo = this.composite.albedo.getContext("2d");
    Albedo.save();
    Albedo.globalCompositeOperation = "source-over";
    Albedo.globalAlpha = 1;
    Albedo.clearRect(0, 0, W, H);
    if (this.baseMode === "material") {
      Albedo.fillStyle = Material.albedo;
      Albedo.fillRect(0, 0, W, H);
    }
    Albedo.restore();
    const Coat = (Id, Value) => {
      const Context = this.composite[Id].getContext("2d");
      Context.save();
      Context.globalCompositeOperation = "source-over";
      Context.globalAlpha = 1;
      const Gray = Math.round(Math.max(0, Math.min(1, Value)) * 255);
      Context.fillStyle = `rgb(${Gray},${Gray},${Gray})`;
      Context.fillRect(0, 0, W, H);
      Context.restore();
    };
    Coat("metallic", Material.metallic);
    Coat("roughness", Material.roughness);
    Coat("height", Material.height);
    const Emissive = this.composite.emissive.getContext("2d");
    Emissive.save();
    Emissive.globalCompositeOperation = "source-over";
    Emissive.globalAlpha = 1;
    if ((Material.emissiveStrength || 0) > 0) {
      Emissive.fillStyle = Material.emissive || "#000000";
      Emissive.globalAlpha = Math.min(1, Material.emissiveStrength);
      Emissive.fillRect(0, 0, W, H);
    } else {
      Emissive.clearRect(0, 0, W, H);
      Emissive.fillStyle = "#000000";
      Emissive.fillRect(0, 0, W, H);
    }
    Emissive.restore();

    const Targets = {
      albedo: this.composite.albedo.getContext("2d"),
      metallic: this.composite.metallic.getContext("2d"),
      roughness: this.composite.roughness.getContext("2d"),
      emissive: this.composite.emissive.getContext("2d"),
      height: this.composite.height.getContext("2d"),
    };

    for (const Layer of this.layers) {
      if (!Layer.visible) continue;
      if (Layer.kind === "adjust") {
        this.applyAdjustment(Targets.albedo, Layer);
        continue;
      }
      const Sources = this.renderLayerSources(Layer);
      // Mask application per channel (fill layers honor masks too).
      const Masked = {};
      const MaskActive = Layer.maskEnabled && Layer.channels.mask;
      for (const [Id, Canvas] of Object.entries(Sources)) {
        if (!Canvas) continue;
        if (Layer.kind === "fill") {
          Masked[Id] = MaskActive ? this.applyMaskToCanvas(Canvas, Layer.channels.mask) : Canvas;
          continue;
        }
        const LayerKey = Id === "metallic" ? "metal" : Id === "roughness" ? "rough" : Id;
        Masked[Id] = this.maskedSource(Layer, LayerKey) || Canvas;
      }
      // Clip-to-below: intersect with the alpha accumulated so far.
      if (Layer.clip && Masked.albedo) {
        const Clip = this.scratch.a;
        const ClipContext = Clip.getContext("2d");
        ClipContext.save();
        ClipContext.globalCompositeOperation = "source-over";
        ClipContext.clearRect(0, 0, W, H);
        ClipContext.drawImage(this.composite.albedo, 0, 0);
        ClipContext.restore();
        for (const Id of Object.keys(Masked)) {
          const Temp = this.scratch.b;
          const TempContext = Temp.getContext("2d");
          TempContext.save();
          TempContext.globalCompositeOperation = "source-over";
          TempContext.clearRect(0, 0, W, H);
          TempContext.drawImage(Masked[Id], 0, 0);
          TempContext.globalCompositeOperation = "destination-in";
          TempContext.drawImage(Clip, 0, 0);
          TempContext.restore();
          // Copy out of the shared scratch (maskedSource reuses scratch.mask,
          // scratch.b is reused per channel — snapshot into a fresh canvas).
          const Snapshot = makeCanvas(W, H);
          Snapshot.getContext("2d").drawImage(Temp, 0, 0);
          Masked[Id] = Snapshot;
        }
      }
      const Op = BlendOp(Layer.blend);
      const Erase = Layer.blend === "erase";
      if (Masked.albedo) {
        Targets.albedo.save();
        Targets.albedo.globalAlpha = Layer.opacity;
        Targets.albedo.globalCompositeOperation = Op;
        Targets.albedo.drawImage(Masked.albedo, 0, 0);
        Targets.albedo.restore();
      }
      for (const Id of ["metallic", "roughness", "emissive", "height"]) {
        if (!Masked[Id]) continue;
        Targets[Id].save();
        Targets[Id].globalAlpha = Layer.opacity;
        Targets[Id].globalCompositeOperation = Erase ? "destination-out" : Id === "emissive" && !Erase ? Op : "source-over";
        Targets[Id].drawImage(Masked[Id], 0, 0);
        Targets[Id].restore();
      }
      Layer.thumbDirty = true;
    }
    this.compositeDirty = false;
    this.normalDirty = true;
    this.lastCompositeMs = performance.now() - Started;
    return true;
  }

  applyAdjustment(Target, Layer) {
    if (!Layer.visible) return;
    const Params = Layer.params;
    const Filter = adjustmentFilter(Params);
    if (!Filter) return;
    const { width: W, height: H } = this;
    const Snapshot = this.scratch.a;
    Snapshot.getContext("2d").save();
    const SnapContext = Snapshot.getContext("2d");
    SnapContext.globalCompositeOperation = "source-over";
    SnapContext.clearRect(0, 0, W, H);
    SnapContext.drawImage(this.composite.albedo, 0, 0);
    SnapContext.restore();
    const Filtered = this.scratch.b;
    const FilteredContext = Filtered.getContext("2d");
    FilteredContext.save();
    FilteredContext.globalCompositeOperation = "source-over";
    FilteredContext.clearRect(0, 0, W, H);
    try {
      FilteredContext.filter = Filter;
    } catch {
      FilteredContext.filter = "none";
    }
    FilteredContext.drawImage(Snapshot, 0, 0);
    FilteredContext.restore();
    Target.save();
    Target.globalAlpha = Layer.opacity * (Params.opacity !== undefined ? Params.opacity : 1);
    Target.globalCompositeOperation = BlendOp(Layer.blend);
    Target.drawImage(Filtered, 0, 0);
    Target.restore();
  }

  /** Sobel height→normal with wrapped edges (tiling-safe). Throttled. */
  renderNormal(Force = false) {
    const Now = performance.now();
    if (!this.normalDirty && !Force) return false;
    if (!Force && Now - this.normalThrottle < 220) return false;
    this.normalThrottle = Now;
    const W = this.width;
    const H = this.height;
    const HeightData = this.composite.height.getContext("2d").getImageData(0, 0, W, H).data;
    const Out = this.composite.normal.getContext("2d").createImageData(W, H);
    const Strength = Math.max(0.05, (this.material.normalStrength || 1) * 2.2);
    const At = (X, Y) => {
      const XX = (X + W) % W;
      const YY = (Y + H) % H;
      return HeightData[(YY * W + XX) * 4] / 255;
    };
    for (let Y = 0; Y < H; Y++) {
      for (let X = 0; X < W; X++) {
        const Tl = At(X - 1, Y - 1);
        const L = At(X - 1, Y);
        const Bl = At(X - 1, Y + 1);
        const Tr = At(X + 1, Y - 1);
        const R = At(X + 1, Y);
        const Br = At(X + 1, Y + 1);
        const T = At(X, Y - 1);
        const B = At(X, Y + 1);
        const Dx = (Tr + 2 * R + Br - Tl - 2 * L - Bl) * Strength;
        const Dy = (Bl + 2 * B + Br - Tl - 2 * T - Tr) * Strength;
        const Inv = 1 / Math.sqrt(Dx * Dx + Dy * Dy + 1);
        const Index = (Y * W + X) * 4;
        Out.data[Index] = Math.round((-Dx * Inv * 0.5 + 0.5) * 255);
        Out.data[Index + 1] = Math.round((Dy * Inv * 0.5 + 0.5) * 255);
        Out.data[Index + 2] = Math.round((Inv * 0.5 + 0.5) * 255);
        Out.data[Index + 3] = 255;
      }
    }
    this.composite.normal.getContext("2d").putImageData(Out, 0, 0);
    this.normalDirty = false;
    return true;
  }

  updateThumbs() {
    for (const Layer of this.layers) {
      if (!Layer.thumbDirty) continue;
      Layer.thumbDirty = false;
      const Context = Layer.thumb.getContext("2d");
      Context.save();
      Context.globalCompositeOperation = "source-over";
      Context.clearRect(0, 0, 88, 88);
      if (Layer.kind === "fill") {
        const Cache = this.fillLayerCanvases(Layer);
        Context.drawImage(Cache.albedo, 0, 0, 88, 88);
      } else if (Layer.kind === "adjust") {
        Context.fillStyle = "#232323";
        Context.fillRect(0, 0, 88, 88);
        Context.fillStyle = "#b49aff";
        Context.font = "28px sans-serif";
        Context.textAlign = "center";
        Context.textBaseline = "middle";
        Context.fillText("◐", 44, 46);
      } else if (Layer.channels.albedo) {
        const Source = Layer.maskSelected && Layer.channels.mask ? Layer.channels.mask : Layer.channels.albedo;
        Context.drawImage(Source, 0, 0, 88, 88);
        if (Layer.maskEnabled && Layer.channels.mask && !Layer.maskSelected) {
          Context.drawImage(Layer.channels.mask, 56, 56, 28, 28);
          Context.strokeStyle = "#b49aff";
          Context.lineWidth = 2;
          Context.strokeRect(56, 56, 28, 28);
        }
      }
      if (!Layer.visible) {
        Context.fillStyle = "#0b0b0baa";
        Context.fillRect(0, 0, 88, 88);
      }
      Context.restore();
    }
  }

  /* ---------------- Serialization ---------------- */

  serialize() {
    return {
      app: "frontier-texture-paint",
      version: SERIAL_VERSION,
      name: this.name,
      width: this.width,
      height: this.height,
      baseMode: this.baseMode,
      material: clone(this.material),
      activeLayerId: this.activeLayerId,
      layers: this.layers.map((Layer) => serializeLayer(Layer)),
    };
  }

  static async deserialize(Data) {
    const Document = new TextureDocument(Data.name, Data.width, Data.height, {
      baseMode: Data.baseMode,
      material: Data.material,
    });
    for (const Saved of Data.layers || []) {
      Document.layers.push(await deserializeLayerAsync(Document, Saved));
    }
    Document.activeLayerId = Data.activeLayerId || (Document.layers[Document.layers.length - 1] || {}).id || null;
    Document.history = [];
    Document.historyIndex = -1;
    Document.markDirty();
    Document.dirty = false;
    return Document;
  }
}

function shallowEqualKeys(A, B) {
  if (A.length !== B.length) return false;
  return A.every((Key) => B.includes(Key));
}

function serializeLayer(Layer) {
  const Channels = {};
  for (const [Key, Canvas] of Object.entries(Layer.channels)) {
    if (!Canvas) continue;
    try {
      Channels[Key] = Canvas.toDataURL("image/png");
    } catch {
      Channels[Key] = null;
    }
  }
  return {
    id: Layer.id,
    kind: Layer.kind,
    name: Layer.name,
    visible: Layer.visible,
    locked: Layer.locked,
    opacity: Layer.opacity,
    blend: Layer.blend,
    clip: Layer.clip,
    maskEnabled: Layer.maskEnabled,
    maskSelected: Layer.maskSelected,
    params: clone(Layer.params),
    channels: Channels,
  };
}

function baseLayerFromData(Document, Data) {
  const Layer = {
    id: Data.id || nextLayerId(),
    kind: Data.kind,
    name: Data.name,
    visible: Data.visible !== false,
    locked: !!Data.locked,
    opacity: Data.opacity !== undefined ? Data.opacity : 1,
    blend: Data.blend || "normal",
    clip: !!Data.clip,
    maskEnabled: !!Data.maskEnabled,
    maskSelected: !!Data.maskSelected,
    params: Object.assign(defaultLayerParams(Data.kind), clone(Data.params || {})),
    channels: {},
    artCache: null,
    fillCache: null,
    thumb: makeCanvas(88, 88),
    thumbDirty: true,
    rasterDirty: Data.kind === "svg" || Data.kind === "text" || Data.kind === "image",
  };
  void Document;
  return Layer;
}

function deserializeLayer(Document, Data) {
  const Layer = baseLayerFromData(Document, Data);
  for (const [Key, Url] of Object.entries(Data.channels || {})) {
    if (!Url) continue;
    const Image = new window.Image();
    const Canvas = makeCanvas(Document.width, Document.height);
    Layer.channels[Key] = Canvas;
    Image.onload = () => {
      Canvas.getContext("2d").drawImage(Image, 0, 0, Document.width, Document.height);
      Layer.thumbDirty = true;
      Document.markDirty();
      if (Document.onChange) Document.onChange();
    };
    Image.src = Url;
  }
  return Layer;
}

async function deserializeLayerAsync(Document, Data) {
  const Layer = baseLayerFromData(Document, Data);
  const Jobs = Object.entries(Data.channels || {}).filter(([, Url]) => !!Url).map(
    ([Key, Url]) =>
      new Promise((Resolve) => {
        const Image = new window.Image();
        Image.onload = () => {
          const Canvas = makeCanvas(Document.width, Document.height);
          Canvas.getContext("2d").drawImage(Image, 0, 0, Document.width, Document.height);
          Layer.channels[Key] = Canvas;
          Resolve();
        };
        Image.onerror = () => Resolve();
        Image.src = Url;
      }),
  );
  await Promise.all(Jobs);
  return Layer;
}

export function adjustmentFilter(Params) {
  const Parts = [];
  if (Params.brightness) Parts.push(`brightness(${1 + Params.brightness})`);
  if (Params.contrast) Parts.push(`contrast(${1 + Params.contrast})`);
  if (Params.saturation) Parts.push(`saturate(${1 + Params.saturation})`);
  if (Params.hue) Parts.push(`hue-rotate(${Params.hue}deg)`);
  if (Params.blur) Parts.push(`blur(${Params.blur}px)`);
  if (Params.invert) Parts.push(`invert(${Params.invert})`);
  return Parts.join(" ");
}

/* ---------------- Fill patterns ---------------- */

export function renderFillPattern(Context, Width, Height, Params) {
  const Pattern = Params.pattern || "solid";
  Context.save();
  Context.globalCompositeOperation = "source-over";
  Context.clearRect(0, 0, Width, Height);
  if (Pattern === "solid") {
    Context.fillStyle = Params.color || "#808080";
    Context.fillRect(0, 0, Width, Height);
  } else if (Pattern === "gradient") {
    const Angle = ((Params.angle || 0) * Math.PI) / 180;
    const Dx = Math.cos(Angle);
    const Dy = Math.sin(Angle);
    const Radius = (Math.abs(Dx) * Width + Math.abs(Dy) * Height) / 2;
    const Cx = Width / 2;
    const Cy = Height / 2;
    const Gradient = Context.createLinearGradient(Cx - Dx * Radius, Cy - Dy * Radius, Cx + Dx * Radius, Cy + Dy * Radius);
    Gradient.addColorStop(0, Params.color || "#808080");
    Gradient.addColorStop(1, Params.color2 || "#3a3a3a");
    Context.fillStyle = Gradient;
    Context.fillRect(0, 0, Width, Height);
  } else if (Pattern === "checker") {
    const Cells = Math.max(2, Math.round(Params.scale || 8));
    const CellW = Width / Cells;
    const CellH = Height / Cells;
    Context.fillStyle = Params.color || "#c8c8c8";
    Context.fillRect(0, 0, Width, Height);
    Context.fillStyle = Params.color2 || "#3a3a3a";
    for (let Y = 0; Y < Cells; Y++) {
      for (let X = 0; X < Cells; X++) {
        if ((X + Y) % 2 === 0) continue;
        Context.fillRect(X * CellW, Y * CellH, CellW + 0.5, CellH + 0.5);
      }
    }
  } else if (Pattern === "stripes") {
    const Count = Math.max(2, Math.round(Params.scale || 8));
    Context.fillStyle = Params.color2 || "#3a3a3a";
    Context.fillRect(0, 0, Width, Height);
    Context.translate(Width / 2, Height / 2);
    Context.rotate(((Params.angle || 45) * Math.PI) / 180);
    Context.fillStyle = Params.color || "#808080";
    const StripeW = Math.max(Width, Height) / Count;
    for (let Index = -Count; Index < Count * 2; Index += 2) {
      Context.fillRect(Index * StripeW, -Math.max(Width, Height), StripeW, Math.max(Width, Height) * 2);
    }
  } else if (Pattern === "noise") {
    const Random = mulberry32(Params.seed || 7);
    const Detail = Math.max(24, Math.min(256, Math.round(Params.scale || 96)));
    const Small = makeCanvas(Detail, Detail);
    const SmallContext = Small.getContext("2d");
    const Image = SmallContext.createImageData(Detail, Detail);
    const C0 = parseColor(Params.color || "#808080");
    const C1 = parseColor(Params.color2 || "#3a3a3a");
    for (let Index = 0; Index < Detail * Detail; Index++) {
      const Mix = Random();
      // Two-octave feel: bias toward smooth blotches.
      const Value = Mix * 0.65 + Random() * 0.35;
      Image.data[Index * 4] = C0[0] + (C1[0] - C0[0]) * Value;
      Image.data[Index * 4 + 1] = C0[1] + (C1[1] - C0[1]) * Value;
      Image.data[Index * 4 + 2] = C0[2] + (C1[2] - C0[2]) * Value;
      Image.data[Index * 4 + 3] = 255;
    }
    SmallContext.putImageData(Image, 0, 0);
    Context.imageSmoothingEnabled = true;
    Context.drawImage(Small, 0, 0, Width, Height);
  } else if (Pattern === "radial") {
    const Gradient = Context.createRadialGradient(Width / 2, Height / 2, 0, Width / 2, Height / 2, Math.max(Width, Height) / 2);
    Gradient.addColorStop(0, Params.color || "#808080");
    Gradient.addColorStop(1, Params.color2 || "#3a3a3a");
    Context.fillStyle = Gradient;
    Context.fillRect(0, 0, Width, Height);
  }
  Context.restore();
}

function parseColor(Hex) {
  const Clean = String(Hex).replace("#", "");
  const Full = Clean.length === 3 ? Clean.split("").map((C) => C + C).join("") : Clean;
  const Int = parseInt(Full.slice(0, 6), 16);
  if (Number.isNaN(Int)) return [128, 128, 128];
  return [(Int >> 16) & 255, (Int >> 8) & 255, Int & 255];
}

/* ---------------- Templates ---------------- */

export function applyTemplate(Document, TemplateId) {
  const W = Document.width;
  const H = Document.height;
  if (TemplateId === "checker") {
    Document.baseMode = "material";
    Document.material = Object.assign(defaultMaterial(), { albedo: "#8a8a8a", roughness: 0.9 });
    const Base = createLayer(Document, "fill", { name: "UV checker", params: { pattern: "checker", color: "#c9c9c9", color2: "#2e2e2e", scale: 8, metallic: 0, roughness: 0.9, height: 0.5 } });
    Document.insertLayer(Base, 0);
    const Paint = createLayer(Document, "paint", { name: "Paint" });
    Document.insertLayer(Paint, 1);
  } else if (TemplateId === "testgrid") {
    Document.baseMode = "material";
    Document.material = Object.assign(defaultMaterial(), { albedo: "#7a7a7a", roughness: 0.8 });
    const Base = createLayer(Document, "fill", { name: "Base", params: { pattern: "solid", color: "#7a7a7a", metallic: 0, roughness: 0.8, height: 0.5 } });
    Document.insertLayer(Base, 0);
    const Grid = createLayer(Document, "paint", { name: "PBR grid" });
    paintTestGrid(Document, Grid);
    Document.insertLayer(Grid, 1);
  } else if (TemplateId === "label") {
    Document.baseMode = "material";
    Document.material = Object.assign(defaultMaterial(), { albedo: "#2a2c2e", roughness: 0.55, metallic: 0.1 });
    const Base = createLayer(Document, "fill", { name: "Housing", params: { pattern: "noise", color: "#33363a", color2: "#222426", scale: 120, seed: 21, metallic: 0.1, roughness: 0.55, height: 0.5 } });
    Document.insertLayer(Base, 0);
    const Hazard = createLayer(Document, "svg", {
      name: "Hazard band",
      params: Object.assign(defaultLayerParams("svg"), {
        svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 128"><rect width="512" height="128" fill="#151310"/><g fill="#e8b53a"><path d="M-40 128 88 0h64L24 128Z"/><path d="M120 128 248 0h64L184 128Z"/><path d="M280 128 408 0h64L344 128Z"/><path d="M440 128 568 0h64L504 128Z"/></g></svg>`,
        x: 0.5, y: 0.78, scale: 0.8, roughness: 0.7,
      }),
    });
    Document.insertLayer(Hazard, 1);
    const Label = createLayer(Document, "text", {
      name: "Serial",
      params: Object.assign(defaultLayerParams("text"), {
        text: "FRN-0042", font: "mono", size: 0.11, weight: 400, color: "#d7d7d7", spacing: 0.12, x: 0.5, y: 0.3, roughness: 0.5,
      }),
    });
    Document.insertLayer(Label, 2);
    const Paint = createLayer(Document, "paint", { name: "Paint" });
    Document.insertLayer(Paint, 3);
  } else {
    if (Document.baseMode === "material") {
      const Base = createLayer(Document, "fill", {
        name: "Base",
        params: { pattern: "solid", color: Document.material.albedo, metallic: Document.material.metallic, roughness: Document.material.roughness, height: Document.material.height },
      });
      Document.insertLayer(Base, 0);
    }
    const Paint = createLayer(Document, "paint", { name: "Paint" });
    Document.insertLayer(Paint, Document.layers.length);
  }
  const Top = Document.layers[Document.layers.length - 1];
  Document.activeLayerId = Top ? Top.id : null;
  Document.history = [];
  Document.historyIndex = -1;
  Document.markDirty();
  Document.dirty = false;
  void W;
  void H;
}

function paintTestGrid(Document, Layer) {
  const W = Document.width;
  const H = Document.height;
  const Albedo = Layer.channels.albedo.getContext("2d");
  // Metallic ramp left→right, roughness ramp top→bottom as albedo guides.
  const Cell = W / 8;
  Albedo.save();
  for (let Y = 0; Y < 8; Y++) {
    for (let X = 0; X < 8; X++) {
      const Metal = X / 7;
      const Rough = Y / 7;
      const Light = Math.round(40 + Metal * 120);
      Albedo.fillStyle = `rgb(${Light},${Math.round(Light * (1 - Rough * 0.25))},${Math.round(Light * (1 - Rough * 0.4))})`;
      Albedo.fillRect(X * Cell, Y * Cell, Cell + 0.5, Cell + 0.5);
    }
  }
  Albedo.strokeStyle = "#0b0b0b";
  Albedo.lineWidth = Math.max(1, W / 512);
  for (let Index = 0; Index <= 8; Index++) {
    Albedo.beginPath();
    Albedo.moveTo(Index * Cell, 0);
    Albedo.lineTo(Index * Cell, H);
    Albedo.stroke();
    Albedo.beginPath();
    Albedo.moveTo(0, Index * Cell);
    Albedo.lineTo(W, Index * Cell);
    Albedo.stroke();
  }
  Albedo.fillStyle = "#f0f0f0";
  Albedo.font = `${Math.round(W * 0.045)}px sans-serif`;
  Albedo.textAlign = "center";
  Albedo.fillText("METALLIC →", W / 2, H - W * 0.03);
  Albedo.save();
  Albedo.translate(W * 0.03, H / 2);
  Albedo.rotate(-Math.PI / 2);
  Albedo.fillText("ROUGHNESS →", 0, 0);
  Albedo.restore();
  Albedo.restore();
  // Matching scalar channels.
  const Metal = ensureLayerChannel(Document, Layer, "metal").getContext("2d");
  const RoughC = ensureLayerChannel(Document, Layer, "rough").getContext("2d");
  for (let Y = 0; Y < 8; Y++) {
    for (let X = 0; X < 8; X++) {
      const Gray = Math.round((X / 7) * 255);
      Metal.fillStyle = `rgb(${Gray},${Gray},${Gray})`;
      Metal.fillRect(X * Cell, Y * Cell, Cell + 0.5, Cell + 0.5);
      const GrayR = Math.round((Y / 7) * 255);
      RoughC.fillStyle = `rgb(${GrayR},${GrayR},${GrayR})`;
      RoughC.fillRect(X * Cell, Y * Cell, Cell + 0.5, Cell + 0.5);
    }
  }
}

/* ---------------- Export helpers ---------------- */

/** Packed ORM canvas: R = occlusion (white), G = roughness, B = metallic. */
export function buildORM(Document, Size) {
  const Canvas = makeCanvas(Size, Size);
  const Context = Canvas.getContext("2d");
  const Rough = getResized(Document.composite.roughness, Size);
  const Metal = getResized(Document.composite.metallic, Size);
  const RoughData = Rough.getContext("2d").getImageData(0, 0, Size, Size);
  const MetalData = Metal.getContext("2d").getImageData(0, 0, Size, Size);
  const Out = Context.createImageData(Size, Size);
  for (let Index = 0; Index < Size * Size; Index++) {
    Out.data[Index * 4] = 255;
    Out.data[Index * 4 + 1] = RoughData.data[Index * 4];
    Out.data[Index * 4 + 2] = MetalData.data[Index * 4];
    Out.data[Index * 4 + 3] = 255;
  }
  Context.putImageData(Out, 0, 0);
  return Canvas;
}

export function getResized(Source, Size) {
  if (Source.width === Size && Source.height === Size) return Source;
  const Canvas = makeCanvas(Size, Size);
  const Context = Canvas.getContext("2d");
  Context.imageSmoothingEnabled = true;
  Context.imageSmoothingQuality = "high";
  Context.drawImage(Source, 0, 0, Size, Size);
  return Canvas;
}

export function downloadCanvas(Canvas, Filename) {
  const Link = document.createElement("a");
  Link.download = Filename;
  Link.href = Canvas.toDataURL("image/png");
  document.body.appendChild(Link);
  Link.click();
  Link.remove();
}
