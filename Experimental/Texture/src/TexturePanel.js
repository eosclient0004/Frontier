import { TextureEngine, ENVIRONMENTS, VIEW_CHANNELS } from "./TextureEngine.js";
import { OrbitCamera } from "./OrbitCamera.js";
import { Mat4, Clamp, Lerp, DecalFrame, HexToRgb, RgbToHex, Vec3 } from "./MathLibrary.js";
import { CreateMesh, ParseObj, MESH_OPTIONS } from "./MeshLibrary.js";
import {
  MATERIAL_PRESETS,
  MATERIAL_FILTERS,
  PATTERNS,
  MASK_GENERATORS,
  BLEND_MODES,
  HEIGHT_BLENDS,
  CHANNELS,
  SanitizeMaterial,
  SanitizeMask,
  AllChannels,
  DEFAULT_MATERIAL,
  DEFAULT_MASK,
} from "./MaterialSpecification.js";
import {
  CreateLayer,
  CloneLayer,
  SerializeLayer,
  ValidateLayer,
  CreateDocumentRecord,
  History,
  LAYER_TYPES,
  DEFAULT_BASE,
} from "./DocumentModel.js";
import {
  SAMPLE_SVGS,
  FONT_OPTIONS,
  DEFAULT_DECAL,
  DecalSignature,
  RasterizeDecal,
  SvgDimensions,
} from "./DecalSources.js";
import { EncodePng, DecodePng, CreateZip, ReadZip, DownloadBlob } from "./FileFormats.js";
import { Icon, FillIcons } from "./Icons.js";
import {
  Escape,
  Slider,
  Percent,
  ColorField,
  Toggle,
  Choice,
  Segmented,
  Buttons,
  Hint,
  Channels,
  TextArea,
  RenderGroup,
  FormatValue,
  Decimals,
} from "./InspectorForms.js";

const Select = (Selector) => document.querySelector(Selector);
const SelectAll = (Selector) => [...document.querySelectorAll(Selector)];
const Wait = () => new Promise((Resolve) => setTimeout(Resolve, 0));
const MAX_DOCUMENTS = 4;
const USER_MATERIALS_KEY = "frontier-texture-materials";

const DEFAULT_BRUSH = {
  Tip: "round",
  Size: 26,
  Hardness: 0.55,
  Roundness: 1,
  Angle: 0,
  Spacing: 0.1,
  Flow: 0.85,
  Opacity: 1,
  Smoothing: 0.2,
  PressureSize: true,
  PressureFlow: false,
  Jitter: 0,
  SizeJitter: 0,
  AngleJitter: 0,
  FollowStroke: false,
  Backface: true,
  Channels: AllChannels(true),
  Material: SanitizeMaterial({ Color: "#d0532f", Color2: "#d0532f", Roughness: 0.42, Roughness2: 0.42, Metallic: 0, Metallic2: 0, Height: 0.06 }),
  Target: "layer",
  MaskValue: 0,
};

const TOOL_LABELS = { paint: "PAINT", erase: "ERASE", decal: "DECAL", picker: "SAMPLE", orbit: "ORBIT" };
const TYPE_LABELS = { paint: "PAINT LAYER", fill: "FILL LAYER", decal: "DECAL" };
const RESOLUTIONS = [512, 1024, 2048, 4096];
const THUMB_CHANNELS = [
  { id: "color", label: "Base color" },
  { id: "roughness", label: "Roughness" },
  { id: "metallic", label: "Metallic" },
  { id: "height", label: "Height" },
  { id: "normal", label: "Normal" },
  { id: "emissive", label: "Emissive" },
  { id: "mask", label: "Mask" },
];

class TexturePanel {
  constructor() {
    this.Documents = [];
    this.Doc = null;
    this.Brush = structuredClone(DEFAULT_BRUSH);
    this.Tool = "paint";
    this.ViewMode = "3d";
    this.Channel = "lit";
    this.Wireframe = false;
    this.Camera = new OrbitCamera();
    this.Uv = { Zoom: 1, PanX: 0, PanY: 0 };
    this.LayerFilter = "all";
    this.MaterialFilter = "all";
    this.Tabs = { texture: "mesh", paint: "layer", fill: "layer", decal: "layer" };
    this.GroupState = {};
    this.Thumbnails = new Map();
    this.CustomFonts = [];
    this.UserMaterials = this.LoadUserMaterials();
    this.Stroke = null;
    this.Drag = null;
    this.NeedsRender = true;
    this.ThumbsDirty = true;
    this.LastThumbs = 0;
    this.Frames = 0;
    this.FpsTime = performance.now();
    this.LastTime = performance.now();
    this.Dpr = Math.min(2, window.devicePixelRatio || 1);
    this.Random = Math.random;
    FillIcons();
    this.BuildStaticInterface();
    this.ConnectInterface();
    try {
      this.Engine = new TextureEngine(Select("#paint-canvas"));
    } catch (ErrorValue) {
      this.ShowGpuError(ErrorValue.message);
      return;
    }
    this.Engine.OnAssetReady = (Signature, ErrorMessage) => {
      for (const Doc of this.Documents) if (Doc.Gpu) { Doc.Gpu.CacheSignature = null; Doc.Gpu.Dirty = true; }
      if (ErrorMessage) this.Notify(ErrorMessage);
      this.ThumbsDirty = true;
    };
    this.Engine.OnContextLost = () => this.ShowGpuError("The GPU context was lost. Save is unavailable; reload the editor to continue.");
    Select("#gpu-status").textContent = `WebGL2 · RGBA16F · max ${this.Engine.MaxTexture}`;
    this.CreateDocument({ Name: "Frontier crate" }, true);
    new ResizeObserver(() => this.Resize()).observe(Select("#viewport"));
    this.Resize();
    this.SetStatus("Ready");
    requestAnimationFrame((Time) => this.Frame(Time));
    setTimeout(() => this.GenerateThumbnails(), 60);
  }

  // ------------------------------------------------------------ utilities
  get Layer() {
    return this.Doc?.Layers.find((L) => L.Id === this.Doc.ActiveId) || null;
  }
  get SelectionKind() {
    if (!this.Doc || this.Doc.Selection === "texture" || !this.Layer) return "texture";
    return this.Layer.Type;
  }
  Notify(Message) {
    clearTimeout(this.ToastTimeout);
    const Toast = Select("#toast");
    Toast.textContent = Message;
    Toast.hidden = false;
    this.ToastTimeout = setTimeout(() => (Toast.hidden = true), 4200);
  }
  SetStatus(Text) {
    Select("#status-ready").innerHTML = `<i></i>${Escape(Text)}`;
  }
  Busy(Label) {
    Select("#busy").hidden = !Label;
    if (Label) Select("#busy-label").textContent = Label;
  }
  ShowGpuError(Message) {
    Select("#gpu-error").hidden = false;
    Select("#gpu-error-message").textContent = Message;
    this.SetStatus("GPU unavailable");
  }
  MarkDirty() {
    if (!this.Doc) return;
    this.Doc.Dirty = true;
    Select("#dirty-indicator").classList.remove("clean");
    this.RenderDocumentTabs();
  }
  Invalidate(Composite = true) {
    if (Composite && this.Doc?.Gpu) this.Doc.Gpu.Dirty = true;
    this.NeedsRender = true;
    this.ThumbsDirty = true;
  }
  Bump(Layer) {
    Layer.Revision++;
    this.Invalidate();
  }
  LoadUserMaterials() {
    try {
      const Saved = JSON.parse(localStorage.getItem(USER_MATERIALS_KEY) || "[]");
      return Array.isArray(Saved)
        ? Saved.filter((E) => E && E.id && E.material).map((E) => ({ ...E, category: "custom", glyph: "sparkle", material: SanitizeMaterial(E.material) }))
        : [];
    } catch {
      return [];
    }
  }
  SaveUserMaterials() {
    try {
      localStorage.setItem(USER_MATERIALS_KEY, JSON.stringify(this.UserMaterials.map(({ id, name, description, material }) => ({ id, name, description, material }))));
    } catch {
      this.Notify("Browser storage is full; the material was kept for this session only.");
    }
  }

  // ------------------------------------------------------------- documents
  CreateDocument(Options = {}, Demo = false) {
    if (this.Documents.length >= MAX_DOCUMENTS) {
      this.Notify(`Up to ${MAX_DOCUMENTS} texture sets stay resident on the GPU. Close one first.`);
      return null;
    }
    const Doc = CreateDocumentRecord({ Name: Options.Name || `Texture set ${this.Documents.length + 1}`, ...Options });
    this.Engine.CreateDocument(Doc, CreateMesh(Doc.MeshKey));
    const Base = CreateLayer("fill", {
      Name: Demo ? "Painted steel" : "Base material",
      Material: Demo ? MATERIAL_PRESETS.find((P) => P.id === "aluminium").material : MATERIAL_PRESETS.find((P) => P.id === "plastic").material,
    });
    Doc.Layers.push(Base);
    if (Demo) {
      const Wear = CreateLayer("fill", {
        Name: "Edge wear",
        Material: SanitizeMaterial({ Color: "#d5d7da", Color2: "#c3c5c8", Metallic: 1, Metallic2: 1, Roughness: 0.25, Roughness2: 0.35, Pattern: "scratches", Scale: 6 }),
        Mask: { Generator: "convex", Contrast: 6, Offset: -0.05, Breakup: 0.5 },
        Channels: { color: true, roughness: true, metallic: true, height: false, emissive: false },
      });
      const Dirt = CreateLayer("fill", {
        Name: "Settled dust",
        Material: MATERIAL_PRESETS.find((P) => P.id === "dust").material,
        Mask: { Generator: "facing", Contrast: 3, Offset: -0.22, Breakup: 0.5 },
        Opacity: 0.65,
        Channels: { color: true, roughness: true, metallic: true, height: false, emissive: false },
      });
      const Label = CreateLayer("decal", { Name: "Frontier label" });
      Label.Decal.Position = [0, 0.05, Doc.Gpu.Mesh.Bounds.Max[2]];
      Label.Decal.Size = 1.15;
      Label.Material.Roughness = Label.Material.Roughness2 = 0.3;
      const Badge = CreateLayer("decal", { Name: "Badge", Decal: { Source: "svg", Svg: SAMPLE_SVGS[0].svg, SvgName: SAMPLE_SVGS[0].name, Position: [Doc.Gpu.Mesh.Bounds.Max[0], 0, 0], Normal: [1, 0, 0], Size: 0.9 } });
      Doc.Layers.push(Wear, Dirt, Label, Badge);
    }
    const Paint = CreateLayer("paint", { Name: "Paint layer" });
    Paint.Gpu.Textures = this.Engine.CreatePaintTextures(Doc.Resolution);
    Doc.Layers.push(Paint);
    Doc.ActiveId = Paint.Id;
    Doc.Selection = Paint.Id;
    Doc.Layers.forEach((L) => this.EnsureDecal(L));
    this.Documents.push(Doc);
    this.ActivateDocument(Doc);
    return Doc;
  }
  ActivateDocument(Doc) {
    if (this.Stroke) return;
    if (this.Doc) this.Doc.Camera = this.Camera.Serialize();
    this.Doc = Doc;
    if (Doc.Camera) this.Camera.Restore(Doc.Camera, true);
    else {
      this.Camera.Reset();
      this.Camera.Snap();
    }
    this.Engine.PickDirty = true;
    Select("#document-name").value = Doc.Name;
    Select("#dirty-indicator").classList.toggle("clean", !Doc.Dirty);
    this.Invalidate();
    this.RefreshAll();
  }
  CloseDocument(Doc) {
    if (this.Documents.length <= 1) return;
    if (Doc.Dirty && !confirm(`Close “${Doc.Name}” without saving?`)) return;
    const Index = this.Documents.indexOf(Doc);
    this.Documents.splice(Index, 1);
    Doc.History.Clear();
    this.Engine.DisposeDocument(Doc);
    Doc.Layers = [];
    if (this.Doc === Doc) {
      this.Doc = null;
      this.ActivateDocument(this.Documents[Math.max(0, Index - 1)]);
    }
    this.CollectGarbage();
    this.RenderDocumentTabs();
  }
  RenderDocumentTabs() {
    Select("#document-tabs").innerHTML = this.Documents.map(
      (Doc) =>
        `<div class="document-tab ${Doc === this.Doc ? "active" : ""}" role="tab" tabindex="0" aria-selected="${Doc === this.Doc}" data-document="${Doc.Id}">${Icon("brush")}<span class="document-label">${Escape(Doc.Name)}.ftex</span>${Doc.Dirty ? '<span class="tab-dirty"></span>' : ""}<button class="tab-close" data-close-document="${Doc.Id}" aria-label="Close ${Escape(Doc.Name)}" ${this.Documents.length <= 1 ? "disabled" : ""}>${Icon("close")}</button></div>`,
    ).join("");
    Select("#new-document").disabled = this.Documents.length >= MAX_DOCUMENTS;
  }
  CollectGarbage() {
    if (!this.Engine) return;
    const Live = new Set();
    for (const Doc of this.Documents) {
      for (const Layer of Doc.Layers) {
        Layer.Gpu.Textures?.forEach((T) => Live.add(T));
        if (Layer.Gpu.Mask) Live.add(Layer.Gpu.Mask);
      }
      Doc.History.Textures(Live);
    }
    this.Engine.CollectGarbage(Live);
  }

  // ---------------------------------------------------------------- layers
  EnsureDecal(Layer) {
    if (Layer.Type !== "decal") return;
    const Signature = DecalSignature(Layer.Decal);
    if (Layer.DecalEntry && Layer.DecalSignature === Signature) return;
    const Source = structuredClone(Layer.Decal);
    Layer.DecalSignature = Signature;
    Layer.DecalEntry = this.Engine.DecalTexture(Signature, () => RasterizeDecal(Source));
  }
  Checkpoint(Label, Key = null) {
    if (!this.Doc) return;
    this.Doc.History.Checkpoint(this.Doc, Label, Key);
    this.RefreshHistory();
  }
  AddLayer(Type, Options = {}, Quiet = false) {
    const Doc = this.Doc;
    if (!Quiet) this.Checkpoint(`Add ${LAYER_TYPES[Type].label.toLowerCase()}`);
    const Layer = CreateLayer(Type, Options);
    if (Type === "paint") Layer.Gpu.Textures = this.Engine.CreatePaintTextures(Doc.Resolution);
    if (Type === "decal" && !Options.KeepPlacement) this.PlaceDecalAtView(Layer, false);
    const Index = Doc.Layers.findIndex((L) => L.Id === Doc.ActiveId);
    Doc.Layers.splice(Index < 0 ? Doc.Layers.length : Index + 1, 0, Layer);
    Doc.ActiveId = Layer.Id;
    Doc.Selection = Layer.Id;
    this.EnsureDecal(Layer);
    this.Invalidate();
    this.MarkDirty();
    if (!Quiet) this.RefreshAll();
    return Layer;
  }
  AddMaterialLayers(Preset) {
    this.Checkpoint(`Add ${Preset.name}`);
    if (Preset.layers) {
      for (const Spec of Preset.layers)
        this.AddLayer("fill", { Name: Spec.Name, Material: Spec.Material, Mask: Spec.Mask, Opacity: Spec.Opacity, Channels: Spec.Channels, HeightBlend: Spec.HeightBlend }, true);
    } else this.AddLayer("fill", { Name: Preset.name, Material: Preset.material, Mask: Preset.mask, Channels: Preset.channels }, true);
    this.Notify(Preset.layers ? `Added smart material “${Preset.name}” · ${Preset.layers.length} layers` : `Added fill layer “${Preset.name}”`);
    this.RefreshAll();
  }
  SelectLayer(Id) {
    this.Doc.ActiveId = Id;
    this.Doc.Selection = Id;
    this.RefreshAll();
    if (this.Channel === "mask") this.Invalidate();
  }
  SelectTextureSet() {
    this.Doc.Selection = "texture";
    this.RefreshAll();
  }
  DuplicateLayer() {
    const Layer = this.Layer;
    if (!Layer) return;
    this.Checkpoint("Duplicate layer");
    const Copy = CloneLayer(Layer, false);
    Copy.Id = CreateLayer(Layer.Type).Id;
    Copy.Name = `${Layer.Name} copy`.slice(0, 64);
    Copy.Revision = 0;
    if (Layer.Gpu.Textures) Copy.Gpu.Textures = Layer.Gpu.Textures.map((T) => this.Engine.CopyTexture(T));
    if (Layer.Gpu.Mask) Copy.Gpu.Mask = this.Engine.CopyTexture(Layer.Gpu.Mask);
    const Index = this.Doc.Layers.indexOf(Layer);
    this.Doc.Layers.splice(Index + 1, 0, Copy);
    this.Doc.ActiveId = this.Doc.Selection = Copy.Id;
    this.EnsureDecal(Copy);
    this.Invalidate();
    this.MarkDirty();
    this.RefreshAll();
  }
  DeleteLayer() {
    const Layer = this.Layer;
    if (!Layer || this.Doc.Selection === "texture") return;
    this.Checkpoint("Delete layer");
    const Index = this.Doc.Layers.indexOf(Layer);
    this.Doc.Layers.splice(Index, 1);
    const Next = this.Doc.Layers[Math.min(Index, this.Doc.Layers.length - 1)];
    this.Doc.ActiveId = Next?.Id || null;
    this.Doc.Selection = Next?.Id || "texture";
    this.Invalidate();
    this.MarkDirty();
    this.RefreshAll();
    this.Notify(`Deleted “${Layer.Name}” · Ctrl Z restores it`);
  }
  MoveLayer(Delta) {
    const Layer = this.Layer;
    if (!Layer) return;
    const Index = this.Doc.Layers.indexOf(Layer);
    const Target = Index + Delta;
    if (Target < 0 || Target >= this.Doc.Layers.length) return;
    this.Checkpoint("Reorder layers");
    this.Doc.Layers.splice(Index, 1);
    this.Doc.Layers.splice(Target, 0, Layer);
    this.Invalidate();
    this.MarkDirty();
    this.RefreshAll();
  }
  ReorderLayer(Id, TargetId, After) {
    const Layers = this.Doc.Layers;
    const Layer = Layers.find((L) => L.Id === Id);
    if (!Layer || Id === TargetId) return;
    this.Checkpoint("Reorder layers");
    Layers.splice(Layers.indexOf(Layer), 1);
    let Index = Layers.findIndex((L) => L.Id === TargetId);
    // The list is drawn top-down, which is the reverse of stack order.
    Layers.splice(After ? Index : Index + 1, 0, Layer);
    this.Invalidate();
    this.MarkDirty();
    this.RefreshAll();
  }
  ToggleLayer(Id) {
    const Layer = this.Doc.Layers.find((L) => L.Id === Id);
    if (!Layer) return;
    this.Checkpoint(Layer.Visible ? "Hide layer" : "Show layer");
    Layer.Visible = !Layer.Visible;
    this.Bump(Layer);
    this.MarkDirty();
    this.RefreshAll();
  }
  ReplaceLayerTextures(Label, Fill) {
    const Layer = this.Layer;
    if (!Layer?.Gpu.Textures) return;
    const Old = Layer.Gpu.Textures;
    if (Fill) {
      const G = this.Doc.Gpu;
      this.Engine.Clear([G.Stroke], [[1, 0, 0, 1]]);
      const Replaced = this.Engine.CommitStroke(this.Doc, Layer, this.StrokeParameters(Layer, "layer", false));
      this.Doc.History.PushTexture(Layer.Id, Replaced, Label);
    } else {
      Layer.Gpu.Textures = this.Engine.CreatePaintTextures(this.Doc.Resolution);
      this.Doc.History.PushTexture(Layer.Id, { Textures: { 0: Old[0], 1: Old[1], 2: Old[2], 3: Old[3] } }, Label);
    }
    this.Bump(Layer);
    this.MarkDirty();
    this.CollectGarbage();
    this.RefreshHistory();
  }
  FillMask(Value) {
    const Layer = this.Layer;
    if (!Layer) return;
    if (!Layer.Mask.Painted) {
      this.Checkpoint("Enable painted mask");
      Layer.Mask.Painted = true;
    }
    const Old = Layer.Gpu.Mask;
    Layer.Gpu.Mask = this.Engine.CreateMaskTexture(this.Doc.Resolution, Value);
    if (Old) this.Doc.History.PushTexture(Layer.Id, { Mask: Old }, "Fill mask");
    this.Bump(Layer);
    this.MarkDirty();
    this.CollectGarbage();
    this.RefreshAll();
  }
  EnsureMaskTexture(Layer) {
    if (!Layer.Gpu.Mask) Layer.Gpu.Mask = this.Engine.CreateMaskTexture(this.Doc.Resolution, 1);
  }

  Undo() {
    if (this.Stroke || !this.Doc) return;
    const Entry = this.Doc.History.StepBack(this.Doc);
    if (!Entry) return this.Notify("Nothing to undo");
    this.AfterHistory(`Undid ${Entry.Label}`);
  }
  Redo() {
    if (this.Stroke || !this.Doc) return;
    const Entry = this.Doc.History.StepForward(this.Doc);
    if (!Entry) return this.Notify("Nothing to redo");
    this.AfterHistory(`Redid ${Entry.Label}`);
  }
  AfterHistory(Message) {
    this.Doc.Layers.forEach((L) => this.EnsureDecal(L));
    this.Doc.Gpu.CacheSignature = null;
    this.Invalidate();
    this.MarkDirty();
    this.RefreshAll();
    this.SetStatus(Message);
  }

  // --------------------------------------------------------- mesh & res
  ChangeMesh(Key) {
    const Doc = this.Doc;
    if (Key === "custom" && !Doc.CustomMesh) return;
    Doc.MeshKey = Key;
    this.Engine.SetMesh(Doc, Key === "custom" ? Doc.CustomMesh : CreateMesh(Key));
    this.Invalidate();
    this.MarkDirty();
    this.FocusCamera();
    this.RefreshAll();
  }
  ImportObj(Text, Name) {
    try {
      const Mesh = ParseObj(Text, Name.replace(/\.obj$/i, ""));
      this.Doc.CustomMesh = Mesh;
      this.Doc.CustomObj = Text;
      this.ChangeMesh("custom");
      this.Notify(`Imported ${Mesh.Name} · ${Mesh.TriangleCount.toLocaleString()} triangles`);
    } catch (ErrorValue) {
      this.Notify(ErrorValue.message);
    }
  }
  ChangeResolution(Resolution) {
    const Doc = this.Doc;
    if (Resolution === Doc.Resolution) return;
    if (Resolution > this.Engine.MaxTexture) return this.Notify("This GPU cannot allocate that texture size.");
    this.Busy("Resampling layers…");
    for (const Layer of Doc.Layers) {
      if (Layer.Gpu.Textures) Layer.Gpu.Textures = Layer.Gpu.Textures.map((T) => this.Engine.Resample(T, Resolution));
      if (Layer.Gpu.Mask) Layer.Gpu.Mask = this.Engine.Resample(Layer.Gpu.Mask, Resolution);
      Layer.Revision++;
    }
    Doc.Resolution = Resolution;
    Doc.History.Clear();
    this.Engine.ResizeDocument(Doc, Doc.MeshKey === "custom" ? Doc.CustomMesh : CreateMesh(Doc.MeshKey));
    this.CollectGarbage();
    this.Busy(null);
    this.Invalidate();
    this.MarkDirty();
    this.RefreshAll();
    this.Notify(`Resampled to ${Resolution} × ${Resolution}. Undo history was cleared.`);
  }

  // ------------------------------------------------------------ viewport
  Resize() {
    const Rect = Select("#viewport").getBoundingClientRect();
    this.ViewportWidth = Math.max(1, Rect.width);
    this.ViewportHeight = Math.max(1, Rect.height);
    this.Dpr = Math.min(2, window.devicePixelRatio || 1);
    this.Engine?.Resize(Math.round(this.ViewportWidth * this.Dpr), Math.round(this.ViewportHeight * this.Dpr));
    if (this.Engine) this.Engine.PickDirty = true;
    this.NeedsRender = true;
  }
  Panes() {
    const W = this.ViewportWidth;
    const H = this.ViewportHeight;
    if (this.ViewMode === "uv") return [{ Kind: "uv", X: 0, Y: 0, W, H }];
    if (this.ViewMode === "split") {
      const Half = Math.round(W / 2);
      return [{ Kind: "3d", X: 0, Y: 0, W: Half, H }, { Kind: "uv", X: Half, Y: 0, W: W - Half, H }];
    }
    return [{ Kind: "3d", X: 0, Y: 0, W, H }];
  }
  PaneAt(X, Y) {
    return this.Panes().find((P) => X >= P.X && X < P.X + P.W && Y >= P.Y && Y < P.Y + P.H) || null;
  }
  Pane(Kind) {
    return this.Panes().find((P) => P.Kind === Kind) || null;
  }
  GlRect(Pane) {
    const D = this.Dpr;
    const X = Math.round(Pane.X * D);
    const Y = Math.round((this.ViewportHeight - Pane.Y - Pane.H) * D);
    return { X, Y, Width: Math.max(1, Math.round((Pane.X + Pane.W) * D) - X), Height: Math.max(1, Math.round(Pane.H * D)) };
  }
  ViewFor(Pane) {
    const Rect = this.GlRect(Pane);
    const View = this.Camera.View();
    const ViewProjection = Mat4.Multiply(this.Camera.Projection(Rect.Width / Rect.Height), View);
    return { View, ViewProjection, Camera: this.Camera.Eye(), Width: Rect.Width, Height: Rect.Height, Mode: 0 };
  }
  UvSquare(Pane) {
    const Side = Math.min(Pane.W, Pane.H) * 0.9 * this.Uv.Zoom;
    return { Left: Pane.X + (Pane.W - Side) / 2 + this.Uv.PanX, Top: Pane.Y + (Pane.H - Side) / 2 + this.Uv.PanY, Side };
  }
  UvRectNdc(Pane) {
    const S = this.UvSquare(Pane);
    return [((S.Left - Pane.X) / Pane.W) * 2 - 1, 1 - ((S.Top - Pane.Y + S.Side) / Pane.H) * 2, (S.Side / Pane.W) * 2, (S.Side / Pane.H) * 2];
  }
  ScreenToUv(Pane, X, Y) {
    const S = this.UvSquare(Pane);
    return [(X - S.Left) / S.Side, 1 - (Y - S.Top) / S.Side];
  }
  UvToScreen(Pane, U, V) {
    const S = this.UvSquare(Pane);
    return [S.Left + U * S.Side, S.Top + (1 - V) * S.Side];
  }
  ProjectToScreen(Pane, P) {
    const View = this.ViewFor(Pane);
    const C = Mat4.TransformPoint(View.ViewProjection, P);
    if (C[3] <= 0.01) return null;
    return [Pane.X + (C[0] / C[3] * 0.5 + 0.5) * Pane.W, Pane.Y + (1 - (C[1] / C[3] * 0.5 + 0.5)) * Pane.H];
  }
  EnsurePick(Pane) {
    const View = this.ViewFor(Pane);
    const Key = `${View.Width}x${View.Height}|${View.ViewProjection.map((V) => V.toFixed(5)).join(",")}|${this.Doc.Id}|${this.Doc.Gpu.Mesh.Key}|${this.Doc.Gpu.Mesh.TriangleCount}`;
    if (this.Engine.PickDirty || this.PickKey !== Key) {
      this.Engine.RenderPick(this.Doc, View);
      this.PickKey = Key;
    }
    return View;
  }
  PickAt(Pane, X, Y) {
    this.EnsurePick(Pane);
    return this.Engine.ReadPick((X - Pane.X) * this.Dpr, (Pane.Y + Pane.H - Y) * this.Dpr);
  }
  FocusCamera() {
    const B = this.Doc.Gpu.Mesh.Bounds;
    const Center = B.Min.map((M, A) => (M + B.Max[A]) / 2);
    // Fit the largest half-extent with a margin; the bounding-sphere fit leaves
    // compact meshes tiny in frame.
    const Radius = Math.max(...B.Max.map((M, A) => (M - B.Min[A]) / 2)) * 1.3;
    this.Camera.TargetCenter = Center;
    this.Camera.TargetDistance = Clamp(Radius / Math.tan(this.Camera.FieldOfView / 2), 0.8, 14);
    this.Uv = { Zoom: 1, PanX: 0, PanY: 0 };
    this.NeedsRender = true;
  }

  Frame(Time) {
    if (this.Engine.Lost) return;
    const DeltaTime = Math.min(0.1, (Time - this.LastTime) / 1000);
    this.LastTime = Time;
    if (!this.Stroke && this.Camera.Update(DeltaTime)) {
      this.NeedsRender = true;
      this.Engine.PickDirty = true;
    }
    const Doc = this.Doc;
    if (Doc?.Gpu) {
      if (this.Stroke?.Pending.length) {
        this.Engine.StampDabs(Doc, this.Stroke.Pending, this.Brush, this.Stroke.View);
        this.Stroke.Pending = [];
        Doc.Gpu.Dirty = true;
      }
      if (Doc.Gpu.Dirty) {
        const Stroke = this.Stroke ? this.StrokeParameters(this.Doc.Layers.find((L) => L.Id === this.Stroke.LayerId), this.Stroke.Target, this.Stroke.Erase) : null;
        this.Engine.Composite(Doc, Stroke, this.Doc.ActiveId);
        if (this.Channel === "mask" || this.ThumbsDirty) this.Engine.RenderMask(Doc, this.Layer, Stroke);
        this.NeedsRender = true;
      }
      if (this.NeedsRender) this.Render();
      if (this.ThumbsDirty && !this.Stroke && Time - this.LastThumbs > 350) {
        this.UpdateChannelTiles();
        this.ThumbsDirty = false;
        this.LastThumbs = Time;
      }
    }
    this.Frames++;
    if (Time - this.FpsTime > 500) {
      Select("#fps").textContent = Math.round((this.Frames * 1000) / (Time - this.FpsTime));
      this.Frames = 0;
      this.FpsTime = Time;
    }
    requestAnimationFrame((T) => this.Frame(T));
  }
  Render() {
    this.NeedsRender = false;
    const Doc = this.Doc;
    const ChannelIndex = VIEW_CHANNELS.find((C) => C.id === this.Channel)?.index ?? 0;
    for (const Pane of this.Panes()) {
      const Rect = this.GlRect(Pane);
      if (Pane.Kind === "3d") this.Engine.Render3D(Doc, { ...this.ViewFor(Pane), Channel: ChannelIndex, Wireframe: this.Wireframe }, Rect);
      else this.Engine.RenderUv(Doc, { UvRect: this.UvRectNdc(Pane), Channel: ChannelIndex, Wireframe: this.Wireframe || this.ViewMode !== "3d" }, Rect);
    }
    Select("#split-divider").hidden = this.ViewMode !== "split";
    this.UpdateGizmo();
    this.UpdateAxis();
  }
  UpdateAxis() {
    const { Right, Up } = this.Camera.Basis();
    const Axes = [
      { Name: "X", Class: "axis-x", V: [1, 0, 0] },
      { Name: "Y", Class: "axis-y", V: [0, 1, 0] },
      { Name: "Z", Class: "axis-z", V: [0, 0, 1] },
    ].map((A) => ({ ...A, X: 38 + Vec3.Dot(A.V, Right) * 24, Y: 39 - Vec3.Dot(A.V, Up) * 24 }));
    Select("#axis-svg").innerHTML = `${Axes.map((A) => `<path d="M38 39L${A.X.toFixed(1)} ${A.Y.toFixed(1)}"/>`).join("")}<circle cx="38" cy="39" r="3"/>${Axes.map((A) => `<text x="${(A.X - 3 + (A.X - 38) * 0.18).toFixed(1)}" y="${(A.Y + 4 + (A.Y - 39) * 0.18).toFixed(1)}" class="${A.Class}">${A.Name}</text>`).join("")}`;
  }

  // ---------------------------------------------------------------- decals
  DecalCorners(Layer, Pane) {
    const D = Layer.Decal;
    const Aspect = Layer.DecalEntry?.Aspect || 1;
    if (D.Mapping === "projected" && Pane.Kind === "3d") {
      const Frame = DecalFrame(D.Normal, D.Up, (D.Rotation * Math.PI) / 180);
      const W = D.Size / 2;
      const H = ((D.Size / Aspect) * D.Stretch) / 2;
      const Point = (X, Y) => this.ProjectToScreen(Pane, Vec3.Add(D.Position, Vec3.Add(Vec3.Scale(Frame.Right, X * W), Vec3.Scale(Frame.Up, Y * H))));
      const Corners = [Point(-1, -1), Point(1, -1), Point(1, 1), Point(-1, 1)];
      const Center = this.ProjectToScreen(Pane, D.Position);
      const Top = Point(0, 1);
      if (Corners.some((C) => !C) || !Center || !Top) return null;
      return { Corners, Center, Top };
    }
    if (D.Mapping === "uv" && Pane.Kind === "uv") {
      const Angle = (D.Rotation * Math.PI) / 180;
      const W = D.UvSize / 2;
      const H = ((D.UvSize / Aspect) * D.Stretch) / 2;
      const Point = (X, Y) => {
        const U = D.UvCenter[0] + Math.cos(Angle) * X * W - Math.sin(Angle) * Y * H;
        const V = D.UvCenter[1] + Math.sin(Angle) * X * W + Math.cos(Angle) * Y * H;
        return this.UvToScreen(Pane, U, V);
      };
      return { Corners: [Point(-1, -1), Point(1, -1), Point(1, 1), Point(-1, 1)], Center: this.UvToScreen(Pane, ...D.UvCenter), Top: Point(0, 1) };
    }
    return null;
  }
  UpdateGizmo() {
    const Svg = Select("#decal-gizmo");
    const Layer = this.Layer;
    if (!Layer || Layer.Type !== "decal" || !Layer.Visible || this.SelectionKind === "texture") {
      Svg.style.display = "none";
      return;
    }
    let Shape = null;
    let Pane = null;
    for (const P of this.Panes()) {
      Shape = this.DecalCorners(Layer, P);
      if (Shape) { Pane = P; break; }
    }
    if (!Shape) {
      Svg.style.display = "none";
      return;
    }
    Svg.style.display = "block";
    Svg.setAttribute("viewBox", `0 0 ${this.ViewportWidth} ${this.ViewportHeight}`);
    const Active = this.Tool === "decal";
    const [Cx, Cy] = Shape.Center;
    const Dx = Shape.Top[0] - Cx, Dy = Shape.Top[1] - Cy;
    const Length = Math.hypot(Dx, Dy) || 1;
    const Handle = [Shape.Top[0] + (Dx / Length) * 26, Shape.Top[1] + (Dy / Length) * 26];
    this.GizmoShape = { ...Shape, Pane };
    Svg.classList.toggle("passive", !Active);
    Svg.innerHTML = `<polygon class="gizmo-outline" points="${Shape.Corners.map((C) => C.map((V) => V.toFixed(1)).join(",")).join(" ")}"/>${Active ? `<line class="gizmo-stem" x1="${Shape.Top[0].toFixed(1)}" y1="${Shape.Top[1].toFixed(1)}" x2="${Handle[0].toFixed(1)}" y2="${Handle[1].toFixed(1)}"/><circle class="gizmo-handle gizmo-rotate" data-handle="rotate" cx="${Handle[0].toFixed(1)}" cy="${Handle[1].toFixed(1)}" r="6"><title>Drag to rotate</title></circle>${Shape.Corners.map((C) => `<rect class="gizmo-handle gizmo-corner" data-handle="scale" x="${(C[0] - 5).toFixed(1)}" y="${(C[1] - 5).toFixed(1)}" width="10" height="10" rx="2"><title>Drag to scale</title></rect>`).join("")}<circle class="gizmo-handle gizmo-center" data-handle="move" cx="${Cx.toFixed(1)}" cy="${Cy.toFixed(1)}" r="7"><title>Drag across the surface</title></circle>` : `<circle class="gizmo-dot" cx="${Cx.toFixed(1)}" cy="${Cy.toFixed(1)}" r="3"/>`}`;
  }
  SetDecalFromHit(Layer, Hit) {
    const D = Layer.Decal;
    D.Position = Hit.Position.map((V) => Number(V.toFixed(5)));
    D.Normal = Hit.Normal.map((V) => Number(V.toFixed(5)));
    D.Up = this.Camera.Basis().Up.map((V) => Number(V.toFixed(5)));
    this.Bump(Layer);
  }
  PlaceDecalAtView(Layer, Notify = true) {
    const Pane = this.Pane("3d");
    const D = Layer.Decal;
    if (Pane && this.Engine) {
      const Hit = this.PickAt(Pane, Pane.X + Pane.W / 2, Pane.Y + Pane.H / 2);
      if (Hit) {
        D.Mapping = "projected";
        this.SetDecalFromHit(Layer, Hit);
        const Distance = Vec3.Length(Vec3.Sub(this.Camera.Eye(), Hit.Position));
        D.Size = Number(Clamp(Distance * Math.tan(this.Camera.FieldOfView / 2) * 0.55, 0.1, 3).toFixed(3));
        return true;
      }
    }
    if (!this.Doc) return false;
    const B = this.Doc.Gpu.Mesh.Bounds;
    D.Position = [0, (B.Min[1] + B.Max[1]) / 2, B.Max[2]];
    D.Normal = [0, 0, 1];
    D.Up = [0, 1, 0];
    if (Notify) this.Notify("Aim the view at the model to place the decal.");
    return false;
  }

  // --------------------------------------------------------------- strokes
  StrokeParameters(Layer, Target, Erase) {
    const Source = this.Stroke || {
      Channels: this.Brush.Channels,
      Material: this.Brush.Material,
      Opacity: this.Brush.Opacity,
      MaskValue: this.Brush.MaskValue,
    };
    return {
      LayerId: Layer?.Id,
      Target,
      Texture: this.Doc.Gpu.Stroke,
      Channels: Source.Channels,
      Material: Source.Material,
      Opacity: Source.Opacity,
      Erase: Target === "layer" && Erase,
      MaskValue: Source.MaskValue,
    };
  }
  BeginStroke(Event, Pane, X, Y) {
    const Doc = this.Doc;
    const Layer = this.Layer;
    if (!Layer || this.SelectionKind === "texture") return this.Notify("Select a layer to paint on.");
    const Target = this.Brush.Target;
    if (Target === "layer" && Layer.Type !== "paint")
      return this.Notify(`“${Layer.Name}” is a ${Layer.Type} layer. Paint its mask (M) or add a paint layer.`);
    if (!Layer.Visible) return this.Notify(`“${Layer.Name}” is hidden.`);
    if (Target === "mask" && (!Layer.Mask.Painted || !Layer.Mask.Enabled)) {
      this.Checkpoint("Enable painted mask");
      Layer.Mask.Painted = true;
      Layer.Mask.Enabled = true;
      this.EnsureMaskTexture(Layer);
      this.Bump(Layer);
      this.RefreshInspector();
      this.RefreshLayers();
    }
    let View;
    if (Pane.Kind === "3d") {
      this.Camera.Snap();
      View = this.EnsurePick(Pane);
    } else View = { Mode: 1 };
    this.Engine.BeginStroke(Doc);
    const Erase = this.Tool === "erase";
    this.Stroke = {
      Pane,
      View,
      PointerId: Event.pointerId,
      LayerId: Layer.Id,
      Target,
      Erase,
      Channels: { ...this.Brush.Channels },
      Material: structuredClone(this.Brush.Material),
      Opacity: this.Brush.Opacity,
      MaskValue: Target === "mask" && Erase ? 1 - this.Brush.MaskValue : this.Brush.MaskValue,
      Pending: [],
      Last: null,
      Smoothed: null,
      Residual: 0,
      Direction: 0,
      Dabs: 0,
    };
    if (Event.shiftKey && this.LastStrokePoint && this.LastStrokePoint.Kind === Pane.Kind) {
      this.AddStrokePoint(this.LastStrokePoint.X, this.LastStrokePoint.Y, 1, true);
      this.AddStrokePoint(X, Y, this.Pressure(Event), true);
    } else this.AddStrokePoint(X, Y, this.Pressure(Event));
    Doc.Gpu.Dirty = true;
  }
  Pressure(Event) {
    return Event.pointerType === "pen" ? Clamp(Event.pressure || 0.5, 0.05, 1) : 1;
  }
  AddStrokePoint(X, Y, Pressure, Raw = false) {
    const S = this.Stroke;
    if (!S.Smoothed || Raw) S.Smoothed = [X, Y];
    else {
      const Follow = 1 - this.Brush.Smoothing * 0.9;
      S.Smoothed = [S.Smoothed[0] + (X - S.Smoothed[0]) * Follow, S.Smoothed[1] + (Y - S.Smoothed[1]) * Follow];
    }
    const P = S.Smoothed;
    if (!S.Last) {
      this.EmitDab(P, Pressure);
      S.Last = P;
      return;
    }
    const Dx = P[0] - S.Last[0];
    const Dy = P[1] - S.Last[1];
    const Distance = Math.hypot(Dx, Dy);
    if (Distance < 0.01) return;
    S.Direction = Math.atan2(Dy, Dx);
    const Spacing = Math.max(0.6, this.Brush.Size * 2 * this.Brush.Spacing * (this.Brush.PressureSize ? Lerp(0.2, 1, Pressure) : 1));
    let Along = Spacing - S.Residual;
    while (Along <= Distance) {
      this.EmitDab([S.Last[0] + (Dx * Along) / Distance, S.Last[1] + (Dy * Along) / Distance], Pressure);
      Along += Spacing;
    }
    S.Residual = Distance - (Along - Spacing);
    S.Last = P;
  }
  EmitDab(P, Pressure) {
    const S = this.Stroke;
    const B = this.Brush;
    const R = this.Random;
    let Size = B.Size * (B.PressureSize ? Lerp(0.2, 1, Pressure) : 1) * (1 - R() * B.SizeJitter);
    const JitterAngle = R() * Math.PI * 2;
    const JitterRadius = R() * B.Jitter * B.Size;
    const X = P[0] + Math.cos(JitterAngle) * JitterRadius;
    const Y = P[1] + Math.sin(JitterAngle) * JitterRadius;
    const Flow = B.Flow * (B.PressureFlow ? Pressure : 1);
    const Angle = (B.Angle * Math.PI) / 180 + (B.FollowStroke ? S.Direction : 0) + (R() - 0.5) * 2 * B.AngleJitter * Math.PI;
    const Pane = S.Pane;
    if (Pane.Kind === "3d") {
      const D = this.Dpr;
      S.Pending.push([(X - Pane.X) * D, (Pane.Y + Pane.H - Y) * D, Math.max(0.5, Size * D), Flow, -Angle, R()]);
    } else {
      const Uv = this.ScreenToUv(Pane, X, Y);
      S.Pending.push([Uv[0], Uv[1], Size / this.UvSquare(Pane).Side, Flow, -Angle, R()]);
    }
    S.Dabs++;
  }
  EndStroke() {
    const S = this.Stroke;
    if (!S) return;
    const Doc = this.Doc;
    if (S.Pending.length) {
      this.Engine.StampDabs(Doc, S.Pending, this.Brush, S.View);
      S.Pending = [];
    }
    const Layer = Doc.Layers.find((L) => L.Id === S.LayerId);
    if (Layer) {
      const Replaced = this.Engine.CommitStroke(Doc, Layer, this.StrokeParameters(Layer, S.Target, S.Erase));
      const Label = S.Target === "mask" ? "mask stroke" : S.Erase ? "erase stroke" : "paint stroke";
      Doc.History.PushTexture(Layer.Id, Replaced, Label);
      Layer.Revision++;
    }
    this.LastStrokePoint = { Kind: S.Pane.Kind, X: S.Last[0], Y: S.Last[1] };
    this.Stroke = null;
    this.Invalidate();
    this.MarkDirty();
    this.CollectGarbage();
    this.RefreshHistory();
  }
  SampleMaterial(Pane, X, Y) {
    let Uv;
    if (Pane.Kind === "3d") {
      const Hit = this.PickAt(Pane, X, Y);
      if (!Hit) return this.Notify("Nothing under the cursor to sample.");
      Uv = Hit.Uv;
    } else {
      Uv = this.ScreenToUv(Pane, X, Y);
      if (Uv.some((V) => V < 0 || V > 1)) return;
    }
    const Texel = this.Engine.ReadFinalTexel(this.Doc, Uv);
    const M = this.Brush.Material;
    M.Color = M.Color2 = RgbToHex(Texel.Color);
    M.Roughness = M.Roughness2 = Number(Clamp(Texel.Roughness, 0, 1).toFixed(3));
    M.Metallic = M.Metallic2 = Number(Clamp(Texel.Metallic, 0, 1).toFixed(3));
    M.Pattern = "none";
    this.RefreshBrushBar();
    if (this.SelectionKind === "paint" && this.CurrentTab() === "material") this.RefreshInspector();
    this.Notify(`Sampled ${M.Color.toUpperCase()} · roughness ${M.Roughness.toFixed(2)} · metallic ${M.Metallic.toFixed(2)}`);
  }

  // ----------------------------------------------------------------- input
  LocalPoint(Event) {
    const Rect = Select("#viewport").getBoundingClientRect();
    return [Event.clientX - Rect.left, Event.clientY - Rect.top];
  }
  ConnectViewport() {
    const Viewport = Select("#viewport");
    const Canvas = Select("#paint-canvas");
    const Gizmo = Select("#decal-gizmo");
    Viewport.addEventListener("contextmenu", (Event) => Event.preventDefault());
    const Down = (Event) => {
      if (!this.Doc || this.Drag || this.Stroke) return;
      const [X, Y] = this.LocalPoint(Event);
      const Pane = this.PaneAt(X, Y);
      if (!Pane) return;
      const Handle = Event.target.closest?.("[data-handle]")?.dataset.handle;
      Canvas.setPointerCapture(Event.pointerId);
      Event.preventDefault();
      const Base = { PointerId: Event.pointerId, Pane, X, Y, StartX: X, StartY: Y };
      if (Handle && this.GizmoShape && Event.button === 0) {
        const Layer = this.Layer;
        const D = Layer.Decal;
        const [Cx, Cy] = this.GizmoShape.Center;
        this.Checkpoint(Handle === "move" ? "Move decal" : Handle === "rotate" ? "Rotate decal" : "Scale decal", `decal-${Handle}-${Layer.Id}`);
        this.Drag = {
          ...Base,
          Pane: this.GizmoShape.Pane,
          Kind: `decal-${Handle}`,
          Center: [Cx, Cy],
          StartAngle: Math.atan2(Y - Cy, X - Cx),
          StartRotation: D.Rotation,
          StartDistance: Math.max(4, Math.hypot(X - Cx, Y - Cy)),
          StartSize: D.Mapping === "uv" ? D.UvSize : D.Size,
        };
        return;
      }
      if (Event.button === 1 || Event.button === 2) {
        this.Drag = { ...Base, Kind: Pane.Kind === "3d" ? "pan" : "uv-pan" };
        return;
      }
      if (Event.button !== 0) return;
      if (Event.altKey || this.Tool === "orbit") {
        this.Drag = { ...Base, Kind: Pane.Kind === "3d" ? (Event.ctrlKey ? "pan" : "orbit") : "uv-pan" };
        return;
      }
      if (this.Tool === "paint" || this.Tool === "erase") {
        this.BeginStroke(Event, Pane, X, Y);
        if (this.Stroke) this.Drag = { ...Base, Kind: "stroke" };
        return;
      }
      if (this.Tool === "picker") {
        this.SampleMaterial(Pane, X, Y);
        return;
      }
      if (this.Tool === "decal") {
        const Layer = this.Layer;
        if (!Layer || Layer.Type !== "decal") return this.Notify("Select a decal layer, or add one from the + menu.");
        const D = Layer.Decal;
        if (D.Mapping === "projected" && Pane.Kind === "3d") {
          const Hit = this.PickAt(Pane, X, Y);
          if (!Hit) return;
          this.Checkpoint("Move decal", `decal-move-${Layer.Id}`);
          this.SetDecalFromHit(Layer, Hit);
          this.Drag = { ...Base, Kind: "decal-move" };
        } else if (D.Mapping === "uv" && Pane.Kind === "uv") {
          this.Checkpoint("Move decal", `decal-move-${Layer.Id}`);
          D.UvCenter = this.ScreenToUv(Pane, X, Y).map((V) => Number(V.toFixed(4)));
          this.Bump(Layer);
          this.Drag = { ...Base, Kind: "decal-move" };
        } else this.Notify(D.Mapping === "uv" ? "This decal is UV-mapped — place it in the UV view." : "Projected decals are placed in the 3D view.");
        this.MarkDirty();
      }
    };
    Canvas.addEventListener("pointerdown", Down);
    Gizmo.addEventListener("pointerdown", Down);
    Canvas.addEventListener("pointermove", (Event) => {
      const [X, Y] = this.LocalPoint(Event);
      this.UpdateCursor(X, Y);
      const Drag = this.Drag;
      if (!Drag || Drag.PointerId !== Event.pointerId) return;
      const DeltaX = X - Drag.X;
      const DeltaY = Y - Drag.Y;
      Drag.X = X;
      Drag.Y = Y;
      if (Drag.Kind === "stroke" && this.Stroke) {
        const Events = Event.getCoalescedEvents?.() || [Event];
        for (const E of Events.length ? Events : [Event]) {
          const [EX, EY] = this.LocalPoint(E);
          this.AddStrokePoint(EX, EY, this.Pressure(E));
        }
      } else if (Drag.Kind === "orbit") {
        this.Camera.Orbit(DeltaX, DeltaY);
      } else if (Drag.Kind === "pan") {
        this.Camera.Pan(DeltaX, DeltaY, Drag.Pane.H);
      } else if (Drag.Kind === "uv-pan") {
        this.Uv.PanX += DeltaX;
        this.Uv.PanY += DeltaY;
        this.NeedsRender = true;
      } else if (Drag.Kind.startsWith("decal-")) this.DragDecal(Drag, X, Y);
    });
    const Up = (Event) => {
      const Drag = this.Drag;
      if (!Drag || Drag.PointerId !== Event.pointerId) return;
      if (Drag.Kind === "stroke") this.EndStroke();
      if (Drag.Kind.startsWith("decal-")) this.RefreshInspector();
      this.Drag = null;
    };
    Canvas.addEventListener("pointerup", Up);
    Canvas.addEventListener("pointercancel", Up);
    Canvas.addEventListener("lostpointercapture", Up);
    Canvas.addEventListener("pointerleave", () => (Select("#brush-cursor").hidden = true));
    Viewport.addEventListener(
      "wheel",
      (Event) => {
        if (!this.Doc) return;
        Event.preventDefault();
        const [X, Y] = this.LocalPoint(Event);
        const Pane = this.PaneAt(X, Y);
        if (!Pane) return;
        const Layer = this.Layer;
        const Delta = Event.deltaMode === 1 ? Event.deltaY * 33 : Event.deltaY;
        if (this.Tool === "decal" && Layer?.Type === "decal" && (Event.shiftKey || Event.ctrlKey)) {
          this.Checkpoint(Event.shiftKey ? "Scale decal" : "Rotate decal", `decal-wheel-${Layer.Id}`);
          const D = Layer.Decal;
          if (Event.shiftKey) {
            if (D.Mapping === "uv") D.UvSize = Clamp(D.UvSize * Math.exp(-Delta * 0.0012), 0.01, 2);
            else D.Size = Clamp(D.Size * Math.exp(-Delta * 0.0012), 0.02, 4);
          } else D.Rotation = ((D.Rotation + Delta * 0.08 + 540) % 360) - 180;
          this.Bump(Layer);
          this.MarkDirty();
          clearTimeout(this.WheelRefresh);
          this.WheelRefresh = setTimeout(() => this.RefreshInspector(), 200);
          return;
        }
        if (Pane.Kind === "3d") this.Camera.Zoom(Delta);
        else {
          const Before = this.ScreenToUv(Pane, X, Y);
          this.Uv.Zoom = Clamp(this.Uv.Zoom * Math.exp(-Delta * 0.0015), 0.25, 24);
          const After = this.UvToScreen(Pane, ...Before);
          this.Uv.PanX += X - After[0];
          this.Uv.PanY += Y - After[1];
          this.NeedsRender = true;
        }
      },
      { passive: false },
    );
  }
  DragDecal(Drag, X, Y) {
    const Layer = this.Layer;
    if (!Layer || Layer.Type !== "decal") return;
    const D = Layer.Decal;
    if (Drag.Kind === "decal-move") {
      if (Drag.Pane.Kind === "3d" && D.Mapping === "projected") {
        const Hit = this.PickAt(Drag.Pane, X, Y);
        if (Hit) this.SetDecalFromHit(Layer, Hit);
      } else if (Drag.Pane.Kind === "uv" && D.Mapping === "uv") {
        D.UvCenter = this.ScreenToUv(Drag.Pane, X, Y).map((V) => Number(V.toFixed(4)));
        this.Bump(Layer);
      }
    } else if (Drag.Kind === "decal-rotate") {
      const Angle = Math.atan2(Y - Drag.Center[1], X - Drag.Center[0]);
      let Rotation = Drag.StartRotation - ((Angle - Drag.StartAngle) * 180) / Math.PI;
      if (Drag.Pane.Kind === "uv") Rotation = Drag.StartRotation - ((Angle - Drag.StartAngle) * 180) / Math.PI;
      Rotation = ((Rotation + 540) % 360) - 180;
      D.Rotation = Number((this.KeyShift ? Math.round(Rotation / 15) * 15 : Rotation).toFixed(2));
      this.Bump(Layer);
    } else if (Drag.Kind === "decal-scale") {
      const Ratio = Math.max(4, Math.hypot(X - Drag.Center[0], Y - Drag.Center[1])) / Drag.StartDistance;
      if (D.Mapping === "uv") D.UvSize = Number(Clamp(Drag.StartSize * Ratio, 0.01, 2).toFixed(4));
      else D.Size = Number(Clamp(Drag.StartSize * Ratio, 0.02, 4).toFixed(4));
      this.Bump(Layer);
    }
    this.MarkDirty();
  }
  UpdateCursor(X, Y) {
    const Cursor = Select("#brush-cursor");
    const Pane = this.PaneAt(X, Y);
    const Painting = (this.Tool === "paint" || this.Tool === "erase") && Pane && !(this.Drag && this.Drag.Kind !== "stroke");
    Cursor.hidden = !Painting;
    Select("#paint-canvas").dataset.tool = this.Tool;
    if (!Painting) return;
    const Diameter = this.Brush.Size * 2;
    Cursor.style.transform = `translate(${X - Diameter / 2}px, ${Y - Diameter / 2}px)`;
    Cursor.style.width = Cursor.style.height = `${Diameter}px`;
    Cursor.style.setProperty("--hardness", `${Math.round(this.Brush.Hardness * 100)}%`);
    Cursor.style.setProperty("--roundness", this.Brush.Roundness);
    Cursor.style.setProperty("--angle", `${this.Brush.Angle}deg`);
    Cursor.classList.toggle("erase", this.Tool === "erase");
    Cursor.classList.toggle("mask", this.Brush.Target === "mask");
    Cursor.classList.toggle("square", this.Brush.Tip === "square");
  }
  SetTool(Tool) {
    this.Tool = Tool;
    SelectAll("[data-tool]").forEach((Button) => Button.classList.toggle("active", Button.dataset.tool === Tool));
    this.RefreshCaption();
    this.NeedsRender = true;
  }
  SetTarget(Target) {
    this.Brush.Target = Target;
    SelectAll("#paint-target [data-target]").forEach((B) => {
      B.classList.toggle("active", B.dataset.target === Target);
      B.setAttribute("aria-checked", String(B.dataset.target === Target));
    });
    Select("#mask-value").hidden = Target !== "mask";
    this.RefreshMaskValue();
    this.RefreshCaption();
    this.RefreshLayers();
    if (Target === "mask" && this.Tool !== "paint" && this.Tool !== "erase") this.SetTool("paint");
  }
  RefreshMaskValue() {
    const Button = Select("#mask-value");
    Button.classList.toggle("reveal", this.Brush.MaskValue > 0.5);
    Button.querySelector("span").textContent = this.Brush.MaskValue > 0.5 ? "Reveal" : "Hide";
  }
  SetViewMode(Mode) {
    this.ViewMode = Mode;
    Select("#view-mode").value = Mode;
    this.Engine && (this.Engine.PickDirty = true);
    Select("#viewport-help").innerHTML =
      Mode === "uv"
        ? "<span>Paint in texture space</span><i>·</i><span>Right-drag to pan</span><i>·</i><span>Scroll to zoom</span>"
        : "<span>Alt-drag to orbit</span><i>·</i><span>Right-drag to pan</span><i>·</i><span>Scroll to zoom</span><i>·</i><span>[ ] brush size</span>";
    this.NeedsRender = true;
  }
  SetChannel(Channel) {
    this.Channel = Channel;
    Select("#view-channel").value = Channel;
    SelectAll("[data-channel-tile]").forEach((T) => T.classList.toggle("active", T.dataset.channelTile === Channel));
    this.Invalidate(Channel === "mask");
  }

  // ------------------------------------------------------------ interface
  BuildStaticInterface() {
    Select("#view-channel").innerHTML = VIEW_CHANNELS.map((C) => `<option value="${C.id}">${C.label}</option>`).join("");
    Select("#material-filters").innerHTML = [...MATERIAL_FILTERS, { id: "custom", label: "Mine" }]
      .map((F) => `<button data-material-filter="${F.id}" class="${F.id === "all" ? "active" : ""}">${F.label}</button>`)
      .join("");
    Select("#channel-tiles").innerHTML = THUMB_CHANNELS.map(
      (C) => `<button class="channel-tile" data-channel-tile="${C.id}" title="View ${C.label}"><canvas width="64" height="64"></canvas><span>${C.label}</span></button>`,
    ).join("");
    const Chip = (Key, Label, Min, Max, Step, Unit, Display = 1) =>
      `<label class="brush-chip"><span>${Label}</span><span class="value-pill compact"><input type="number" data-bind="brush.${Key}" data-kind="number" min="${Min * Display}" max="${Max * Display}" step="${Step * Display}" data-display="${Display}" data-step="${Step}" data-min="${Min}" data-max="${Max}" aria-label="Brush ${Label}"/><span class="unit-cell">${Unit}</span></span></label>`;
    Select("#brush-chips").innerHTML = [
      Chip("Size", "Size", 1, 400, 1, "px"),
      Chip("Hardness", "Hardness", 0, 1, 0.01, "%", 100),
      Chip("Flow", "Flow", 0.01, 1, 0.01, "%", 100),
      Chip("Opacity", "Opacity", 0.01, 1, 0.01, "%", 100),
    ].join("");
  }
  ConnectInterface() {
    this.ConnectViewport();
    Select("#retry-gpu").addEventListener("click", () => location.reload());
    Select("#new-document").addEventListener("click", () => this.CreateDocument());
    Select("#document-tabs").addEventListener("click", (Event) => {
      const Close = Event.target.closest("[data-close-document]");
      if (Close) {
        Event.stopPropagation();
        return this.CloseDocument(this.Documents.find((D) => D.Id === Close.dataset.closeDocument));
      }
      const Tab = Event.target.closest("[data-document]");
      if (Tab) this.ActivateDocument(this.Documents.find((D) => D.Id === Tab.dataset.document));
    });
    Select("#document-tabs").addEventListener("dblclick", () => Select("#document-name").select());
    Select("#document-name").addEventListener("input", (Event) => {
      this.Doc.Name = Event.target.value.trim().slice(0, 64) || "Untitled set";
      this.MarkDirty();
      if (this.SelectionKind === "texture") Select("#object-name").value = this.Doc.Name;
    });
    Select("#undo-button").addEventListener("click", () => this.Undo());
    Select("#redo-button").addEventListener("click", () => this.Redo());
    Select("#save-button").addEventListener("click", () => this.SaveProject());
    Select("#open-button").addEventListener("click", () => Select("#open-file").click());
    Select("#open-file").addEventListener("change", (Event) => this.ReadFile(Event, (File_, Buffer_) => this.OpenProject(File_, Buffer_), "buffer"));
    Select("#svg-file").addEventListener("change", (Event) => this.ReadFile(Event, (File_, Text) => this.ImportSvg(Text, File_.name), "text"));
    Select("#obj-file").addEventListener("change", (Event) => this.ReadFile(Event, (File_, Text) => this.ImportObj(Text, File_.name), "text"));
    Select("#bitmap-file").addEventListener("change", (Event) => this.ReadFile(Event, (File_) => this.ImportBitmap(File_), "none"));
    Select("#font-file").addEventListener("change", (Event) => this.ReadFile(Event, (File_, Buffer_) => this.ImportFont(File_, Buffer_), "buffer"));
    Select("#material-file").addEventListener("change", (Event) => this.ReadFile(Event, (File_, Text) => this.ImportMaterial(Text), "text"));
    Select("#help-button").addEventListener("click", () => Select("#help-dialog").showModal());
    Select("#close-help").addEventListener("click", () => Select("#help-dialog").close());
    Select("#export-button").addEventListener("click", () => this.OpenExport());
    Select("#close-export").addEventListener("click", () => Select("#export-dialog").close());
    Select("#export-start").addEventListener("click", () => this.ExportMaps());
    for (const Dialog of SelectAll("dialog"))
      Dialog.addEventListener("click", (Event) => { if (Event.target === Dialog) Dialog.close(); });
    Select("#view-mode").addEventListener("change", (Event) => this.SetViewMode(Event.target.value));
    Select("#view-channel").addEventListener("change", (Event) => this.SetChannel(Event.target.value));
    Select("#channel-tiles").addEventListener("click", (Event) => {
      const Tile = Event.target.closest("[data-channel-tile]");
      if (Tile) this.SetChannel(this.Channel === Tile.dataset.channelTile ? "lit" : Tile.dataset.channelTile);
    });
    Select("#wire-button").addEventListener("click", () => this.ToggleWireframe());
    Select("#env-button").addEventListener("click", () => {
      this.Doc.Environment.Show = !this.Doc.Environment.Show;
      Select("#env-button").classList.toggle("active", this.Doc.Environment.Show);
      Select("#env-button").setAttribute("aria-pressed", String(this.Doc.Environment.Show));
      this.NeedsRender = true;
      if (this.SelectionKind === "texture") this.RefreshInspector();
    });
    Select("#focus-button").addEventListener("click", () => this.FocusCamera());
    Select("#maximize-button").addEventListener("click", () => {
      Select("#app").classList.toggle("maximized");
      Select("#maximize-button").classList.toggle("active", Select("#app").classList.contains("maximized"));
    });
    Select("#compact-outliner").addEventListener("click", (Event) => {
      const Compact = Select(".left-panel").classList.toggle("compact-outliner");
      Event.currentTarget.setAttribute("aria-pressed", String(Compact));
    });
    Select("#collection-toggle").addEventListener("click", (Event) => {
      const Expanded = Event.currentTarget.getAttribute("aria-expanded") !== "false";
      Event.currentTarget.setAttribute("aria-expanded", String(!Expanded));
      this.RefreshLayers();
    });
    Select("#workspace-button").addEventListener("click", () => this.SetNav("workspace"));
    Select("#library-button").addEventListener("click", () => this.SetNav("library"));
    SelectAll("[data-tool]").forEach((Button) => Button.addEventListener("click", () => this.SetTool(Button.dataset.tool)));
    Select("#paint-target").addEventListener("click", (Event) => {
      const Button = Event.target.closest("[data-target]");
      if (Button) this.SetTarget(Button.dataset.target);
    });
    Select("#mask-value").addEventListener("click", () => this.SwapMaskValue());
    Select("#brush-color").addEventListener("input", (Event) => {
      this.Brush.Material.Color = this.Brush.Material.Color2 = Event.target.value;
      this.RefreshBrushBar();
      if (this.SelectionKind === "paint" && this.CurrentTab() === "material") this.RefreshInspector();
    });

    // Layers
    Select("#layer-search").addEventListener("input", () => this.RefreshLayers());
    Select("#layer-filters").addEventListener("click", (Event) => {
      const Button = Event.target.closest("[data-layer-filter]");
      if (!Button) return;
      this.LayerFilter = Button.dataset.layerFilter;
      this.RefreshLayers();
    });
    const Tree = Select("#layer-tree");
    Tree.addEventListener("click", (Event) => {
      const Toggle_ = Event.target.closest("[data-toggle-layer]");
      if (Toggle_) {
        Event.stopPropagation();
        return this.ToggleLayer(Toggle_.dataset.toggleLayer);
      }
      const Row = Event.target.closest("[data-layer]");
      if (!Row) return;
      if (Row.dataset.layer === "texture") this.SelectTextureSet();
      else this.SelectLayer(Row.dataset.layer);
    });
    Tree.addEventListener("dblclick", (Event) => {
      if (Event.target.closest("[data-layer]") && !Event.target.closest("[data-toggle-layer]")) {
        Select("#object-name").focus();
        Select("#object-name").select();
      }
    });
    Tree.addEventListener("keydown", (Event) => {
      const Row = Event.target.closest("[data-layer]");
      if (!Row) return;
      if (Event.key === "Enter" || Event.key === " ") {
        Event.preventDefault();
        Row.click();
      }
      if (Event.key === "ArrowDown" || Event.key === "ArrowUp") {
        Event.preventDefault();
        const Rows = [...Tree.querySelectorAll("[data-layer]")];
        const Next = Rows[Rows.indexOf(Row) + (Event.key === "ArrowDown" ? 1 : -1)];
        if (Next) { Next.click(); Select(`#layer-tree [data-layer="${Next.dataset.layer}"]`)?.focus(); }
      }
    });
    Tree.addEventListener("dragstart", (Event) => {
      const Row = Event.target.closest("[data-layer]");
      if (!Row || Row.dataset.layer === "texture") return Event.preventDefault();
      this.DragLayer = Row.dataset.layer;
      Event.dataTransfer.effectAllowed = "move";
      Event.dataTransfer.setData("text/plain", Row.dataset.layer);
      Row.classList.add("dragging");
    });
    Tree.addEventListener("dragover", (Event) => {
      const Row = Event.target.closest("[data-layer]");
      if (!this.DragLayer || !Row || Row.dataset.layer === "texture") return;
      Event.preventDefault();
      const Rect = Row.getBoundingClientRect();
      const After = Event.clientY > Rect.top + Rect.height / 2;
      SelectAll(".drop-before, .drop-after").forEach((E) => E.classList.remove("drop-before", "drop-after"));
      Row.classList.add(After ? "drop-after" : "drop-before");
    });
    Tree.addEventListener("drop", (Event) => {
      const Row = Event.target.closest("[data-layer]");
      if (!this.DragLayer || !Row) return;
      Event.preventDefault();
      const After = Row.classList.contains("drop-after");
      this.ReorderLayer(this.DragLayer, Row.dataset.layer, After);
    });
    Tree.addEventListener("dragend", () => {
      this.DragLayer = null;
      SelectAll(".drop-before, .drop-after, .dragging").forEach((E) => E.classList.remove("drop-before", "drop-after", "dragging"));
    });
    Select("#add-button").addEventListener("click", (Event) => {
      Event.stopPropagation();
      const Menu = Select("#add-menu");
      const Open = Menu.hidden;
      Menu.hidden = !Open;
      Select("#add-button").setAttribute("aria-expanded", String(Open));
      if (Open) {
        const Rect = Select("#add-button").getBoundingClientRect();
        Menu.style.left = `${Math.min(window.innerWidth - 270, Rect.left)}px`;
        Menu.style.top = `${Rect.bottom + 8}px`;
      }
    });
    Select("#add-menu").addEventListener("click", (Event) => {
      const Button = Event.target.closest("[data-add]");
      if (!Button) return;
      Select("#add-menu").hidden = true;
      const Kind = Button.dataset.add;
      if (Kind === "paint") this.AddLayer("paint", { Name: `Paint layer ${this.Doc.Layers.filter((L) => L.Type === "paint").length + 1}` });
      if (Kind === "fill") this.AddLayer("fill", { Name: "Fill layer", Material: this.Brush.Material });
      if (Kind === "svg") this.AddLayer("decal", { Name: "SVG decal", Decal: { Source: "svg" } });
      if (Kind === "text") this.AddLayer("decal", { Name: "Text decal", Decal: { Source: "text" } });
      if (Kind === "svg" || Kind === "text") this.SetTool("decal");
    });
    document.addEventListener("pointerdown", (Event) => {
      if (!Event.target.closest("#add-menu, #add-button")) Select("#add-menu").hidden = true;
    });

    // Materials
    Select("#material-search").addEventListener("input", () => this.RefreshMaterials());
    Select("#reset-material-search").addEventListener("click", () => {
      Select("#material-search").value = "";
      this.MaterialFilter = "all";
      this.RefreshMaterials();
    });
    Select("#material-filters").addEventListener("click", (Event) => {
      const Button = Event.target.closest("[data-material-filter]");
      if (!Button) return;
      this.MaterialFilter = Button.dataset.materialFilter;
      this.RefreshMaterials();
    });
    Select("#material-list").addEventListener("click", (Event) => {
      const Remove = Event.target.closest("[data-remove-material]");
      if (Remove) {
        Event.stopPropagation();
        this.UserMaterials = this.UserMaterials.filter((M) => M.id !== Remove.dataset.removeMaterial);
        this.SaveUserMaterials();
        return this.RefreshMaterials();
      }
      const Add = Event.target.closest("[data-add-material]");
      const Card = Event.target.closest("[data-material]");
      const Preset = this.FindPreset((Add || Card)?.dataset.addMaterial || Card?.dataset.material);
      if (!Preset) return;
      if (Add || Preset.layers) this.AddMaterialLayers(Preset);
      else this.ApplyMaterial(Preset);
    });
    Select("#material-list").addEventListener("dblclick", (Event) => {
      const Card = Event.target.closest("[data-material]");
      const Preset = this.FindPreset(Card?.dataset.material);
      if (Preset && !Preset.layers) this.AddMaterialLayers(Preset);
    });
    Select("#material-list").addEventListener("keydown", (Event) => {
      if ((Event.key === "Enter" || Event.key === " ") && Event.target.matches("[data-material]")) {
        Event.preventDefault();
        Event.target.click();
      }
    });

    // Inspector
    Select("#inspector-tabs").addEventListener("click", (Event) => {
      const Button = Event.target.closest("[data-tab]");
      if (!Button) return;
      this.Tabs[this.SelectionKind] = Button.dataset.tab;
      this.RefreshInspector();
    });
    Select("#object-name").addEventListener("input", (Event) => {
      const Name = Event.target.value.trim().slice(0, 64);
      if (!Name) return;
      if (this.SelectionKind === "texture") {
        this.Doc.Name = Name;
        Select("#document-name").value = Name;
        this.RenderDocumentTabs();
      } else {
        this.Checkpoint("Rename layer", `rename-${this.Layer.Id}`);
        this.Layer.Name = Name;
        this.RefreshLayers();
        this.RefreshCaption();
      }
      this.MarkDirty();
    });
    Select("#object-enabled").addEventListener("change", () => {
      if (this.Layer && this.SelectionKind !== "texture") this.ToggleLayer(this.Layer.Id);
    });
    Select("#reset-properties").addEventListener("click", () => this.ResetTab());
    Select("#inspector-body").addEventListener("toggle", (Event) => {
      if (Event.target.matches?.("details[data-group]")) this.GroupState[Event.target.dataset.group] = Event.target.open;
    }, true);
    const Bodies = ["#inspector-body", "#brush-chips"];
    for (const Body of Bodies) {
      const Root = Select(Body);
      Root.addEventListener("input", (Event) => this.OnControlInput(Event, false));
      Root.addEventListener("change", (Event) => this.OnControlInput(Event, true));
      Root.addEventListener("click", (Event) => this.OnControlClick(Event));
      this.ConnectScrubbing(Root);
    }

    // Keyboard
    window.addEventListener("keydown", (Event) => this.OnKey(Event));
    window.addEventListener("keyup", (Event) => { if (Event.key === "Shift") this.KeyShift = false; });
    window.addEventListener("beforeunload", (Event) => {
      if (this.Documents.some((D) => D.Dirty)) { Event.preventDefault(); Event.returnValue = ""; }
    });
  }
  SetNav(Kind) {
    Select("#workspace-button").classList.toggle("active", Kind === "workspace");
    Select("#library-button").classList.toggle("active", Kind === "library");
    Select("#app").classList.toggle("mobile-library", Kind === "library");
    Select("#app").classList.toggle("library-focus", Kind === "library");
    if (Kind === "library") Select("#material-search").focus({ preventScroll: true });
  }
  ToggleWireframe() {
    this.Wireframe = !this.Wireframe;
    Select("#wire-button").classList.toggle("active", this.Wireframe);
    Select("#wire-button").setAttribute("aria-pressed", String(this.Wireframe));
    this.NeedsRender = true;
  }
  SwapMaskValue() {
    this.Brush.MaskValue = this.Brush.MaskValue > 0.5 ? 0 : 1;
    this.RefreshMaskValue();
    this.RefreshCaption();
    if (this.CurrentTab() === "mask") this.RefreshInspector();
  }
  ReadFile(Event, Handler, Mode) {
    const File_ = Event.target.files?.[0];
    Event.target.value = "";
    if (!File_) return;
    if (Mode === "text") File_.text().then((Text) => Handler(File_, Text));
    else if (Mode === "buffer") File_.arrayBuffer().then((Buffer_) => Handler(File_, Buffer_));
    else Handler(File_);
  }
  OnKey(Event) {
    if (Event.key === "Shift") this.KeyShift = true;
    const Typing = Event.target.matches?.("input:not([type=range]):not([type=checkbox]):not([type=color]), textarea, select");
    const Ctrl = Event.ctrlKey || Event.metaKey;
    const Key = Event.key.toLowerCase();
    if (Ctrl && Key === "s") { Event.preventDefault(); return this.SaveProject(); }
    if (Ctrl && Key === "o") { Event.preventDefault(); return Select("#open-file").click(); }
    if (Typing || !this.Doc) {
      if (Event.key === "Escape") Event.target.blur?.();
      return;
    }
    if (Ctrl && Key === "z") { Event.preventDefault(); return Event.shiftKey ? this.Redo() : this.Undo(); }
    if (Ctrl && Key === "y") { Event.preventDefault(); return this.Redo(); }
    if (Ctrl && Key === "d") { Event.preventDefault(); return this.DuplicateLayer(); }
    if (Ctrl && Event.shiftKey && Key === "n") { Event.preventDefault(); return this.AddLayer("paint", { Name: `Paint layer ${this.Doc.Layers.filter((L) => L.Type === "paint").length + 1}` }); }
    if (Ctrl && Event.altKey && Key === "n") { Event.preventDefault(); return this.CreateDocument(); }
    if (Ctrl && Event.shiftKey && Key === "f") { Event.preventDefault(); return Select("#layer-search").focus(); }
    if (Ctrl) return;
    if (Event.key === "?") {
      Event.preventDefault();
      return Select("#help-dialog").showModal();
    }
    const Actions = {
      b: () => this.SetTool("paint"),
      e: () => this.SetTool("erase"),
      d: () => this.SetTool("decal"),
      i: () => this.SetTool("picker"),
      o: () => this.SetTool("orbit"),
      m: () => this.SetTarget(this.Brush.Target === "mask" ? "layer" : "mask"),
      x: () => this.SwapMaskValue(),
      w: () => this.ToggleWireframe(),
      f: () => this.FocusCamera(),
      1: () => this.SetViewMode("3d"),
      2: () => this.SetViewMode("uv"),
      3: () => this.SetViewMode("split"),
      "/": () => Select("#material-search").focus(),
      delete: () => this.DeleteLayer(),
      backspace: () => this.DeleteLayer(),
    };
    if (Event.key === "[" || Event.key === "]" || Event.key === "{" || Event.key === "}") {
      const Up = Event.key === "]" || Event.key === "}";
      if (Event.shiftKey) this.Brush.Hardness = Clamp(this.Brush.Hardness + (Up ? 0.1 : -0.1), 0, 1);
      else this.Brush.Size = Clamp(Math.round(this.Brush.Size * (Up ? 1.15 : 1 / 1.15)), 1, 400);
      this.RefreshBrushBar();
      if (this.CurrentTab() === "brush") this.RefreshInspector();
      return;
    }
    const Action = Actions[Key];
    if (Action) {
      Event.preventDefault();
      Action();
    }
  }

  // ------------------------------------------------------------- bindings
  BindingRoot(Name) {
    const Layer = this.Layer;
    switch (Name) {
      case "layer": return Layer;
      case "material": return this.SelectionKind === "paint" ? this.Brush.Material : Layer?.Material;
      case "mask": return Layer?.Mask;
      case "decal": return Layer?.Decal;
      case "brush": return this.Brush;
      case "base": return this.Doc.Base;
      case "doc": return this.Doc;
      case "env": return this.Doc.Environment;
    }
    return null;
  }
  GetBinding(Bind) {
    const [Root, ...Path] = Bind.split(".");
    if (Bind === "decal.SampleId") {
      const D = this.Layer.Decal;
      return SAMPLE_SVGS.find((S) => S.svg === D.Svg)?.id || "custom";
    }
    let Value = this.BindingRoot(Root);
    for (const Key of Path) Value = Value?.[Key];
    return Value;
  }
  SetBinding(Bind, Value) {
    const [Root, ...Path] = Bind.split(".");
    let Target = this.BindingRoot(Root);
    for (const Key of Path.slice(0, -1)) Target = Target[Key];
    Target[Path[Path.length - 1]] = Value;
  }
  IsDocumentBinding(Root) {
    return ["layer", "mask", "decal", "base", "doc", "env"].includes(Root) || (Root === "material" && this.SelectionKind !== "paint");
  }
  ApplyBinding(Bind, Value, Commit) {
    const [Root, ...Path] = Bind.split(".");
    const Key = Path.join(".");
    if (Bind === "doc.Resolution") return Commit && this.ChangeResolution(Number(Value));
    if (Bind === "doc.MeshKey") return Commit && this.ChangeMesh(Value);
    if (Bind === "decal.SampleId") {
      const Sample = SAMPLE_SVGS.find((S) => S.id === Value);
      if (!Sample) return;
      this.Checkpoint("Change decal artwork");
      Object.assign(this.Layer.Decal, { Svg: Sample.svg, SvgName: Sample.name, Source: "svg" });
      this.EnsureDecal(this.Layer);
      this.Bump(this.Layer);
      this.MarkDirty();
      return this.RefreshInspector();
    }
    if (this.GetBinding(Bind) === Value) return;
    if (this.IsDocumentBinding(Root)) this.Checkpoint(`Edit ${Path[Path.length - 1].replace(/([A-Z])/g, " $1").trim().toLowerCase()}`, `${Bind}@${this.Doc.ActiveId}`);
    this.SetBinding(Bind, Value);
    const Layer = this.Layer;
    if (["layer", "mask", "decal"].includes(Root) || (Root === "material" && this.SelectionKind !== "paint")) {
      if (Root === "mask" && Key === "Painted" && Value) this.EnsureMaskTexture(Layer);
      if (Root === "decal") this.EnsureDecal(Layer);
      this.Bump(Layer);
      if (Root === "layer" || Key === "Painted" || Key === "Enabled" || Key === "Generator") this.RefreshLayers();
      if (Root === "layer") this.RefreshCaption();
    } else if (Root === "base") {
      this.Doc.BaseRevision++;
      this.Invalidate();
    } else if (Root === "doc") this.Invalidate();
    else if (Root === "env") {
      this.NeedsRender = true;
      if (Key === "Show") Select("#env-button").classList.toggle("active", Boolean(Value));
    } else if (Root === "brush" || Root === "material") this.RefreshBrushBar();
    if (this.IsDocumentBinding(Root)) this.MarkDirty();
    const Structural = ["Pattern", "Source", "Mapping", "Painted", "Generator", "Tip", "Enabled", "Bitmap", "Preset", "SourceColor"];
    if (Commit && Structural.includes(Path[Path.length - 1])) this.RefreshInspector();
  }
  ParseControl(Element) {
    const Kind = Element.dataset.kind;
    if (Kind === "number" || Kind === "range") {
      let Value = Number(Element.value);
      if (!Number.isFinite(Value)) return undefined;
      if (Kind === "number") Value /= Number(Element.dataset.display || 1);
      const Min = Number(Element.dataset.min ?? Element.min);
      const Max = Number(Element.dataset.max ?? Element.max);
      return Clamp(Value, Min, Max);
    }
    if (Kind === "toggle") return Element.checked;
    if (Kind === "color") return Element.value.toLowerCase();
    if (Kind === "hex") {
      const Match = Element.value.trim().match(/^#?([0-9a-f]{6}|[0-9a-f]{3})$/i);
      if (!Match) return undefined;
      return RgbToHex(HexToRgb(Match[1]));
    }
    if (Kind === "select") {
      const Current = this.GetBinding(Element.dataset.bind);
      return typeof Current === "number" ? Number(Element.value) : Element.value;
    }
    if (Kind === "text") return Element.value;
    return undefined;
  }
  OnControlInput(Event, Commit) {
    const Element = Event.target;
    const Bind = Element.dataset?.bind;
    if (!Bind) return;
    const Kind = Element.dataset.kind;
    if (Kind === "number" && !Commit) return;
    if (Kind === "hex" && !Commit) return;
    if ((Kind === "select" || Kind === "toggle") && !Commit) return;
    if (Kind === "text" && Commit) return;
    const Value = this.ParseControl(Element);
    if (Value === undefined) return this.SyncControls(Bind);
    this.ApplyBinding(Bind, Value, Commit || Kind === "range" || Kind === "color" || Kind === "text");
    this.SyncControls(Bind, Element);
  }
  OnControlClick(Event) {
    const Element = Event.target.closest("[data-kind=chip], [data-kind=segment], [data-action]");
    if (!Element) return;
    if (Element.dataset.action) return this.RunAction(Element.dataset.action, Element);
    const Bind = Element.dataset.bind;
    if (Element.dataset.kind === "chip") this.ApplyBinding(Bind, !this.GetBinding(Bind), true);
    else this.ApplyBinding(Bind, Element.dataset.value, true);
    this.SyncControls(Bind);
  }
  // Keeps every control bound to the same key (pill, slider, hex) in sync.
  SyncControls(Bind, Except = null) {
    const Value = this.GetBinding(Bind);
    SelectAll(`[data-bind="${CSS.escape(Bind)}"]`).forEach((Element) => {
      if (Element === Except && Element.dataset.kind !== "range") return;
      const Kind = Element.dataset.kind;
      if (Kind === "number") {
        const Display = Number(Element.dataset.display || 1);
        Element.value = (Value * Display).toFixed(Decimals(Number(Element.dataset.step) * Display));
      } else if (Kind === "range") {
        if (Element !== Except) Element.value = Value;
        const Min = Number(Element.min), Max = Number(Element.max);
        Element.style.setProperty("--fraction", Clamp((Value - Min) / (Max - Min || 1), 0, 1).toFixed(4));
      } else if (Kind === "color") {
        Element.value = Value;
        Element.parentElement.style.setProperty("--swatch", Value);
      } else if (Kind === "hex" && Element !== Except) Element.value = String(Value).toUpperCase();
      else if (Kind === "toggle") Element.checked = Boolean(Value);
      else if (Kind === "chip") {
        Element.classList.toggle("active", Boolean(Value));
        Element.setAttribute("aria-pressed", String(Boolean(Value)));
      } else if (Kind === "segment") Element.classList.toggle("active", Element.dataset.value === String(Value));
    });
    if (Bind.startsWith("brush.") || (Bind.startsWith("material.") && this.SelectionKind === "paint")) this.RefreshBrushBar();
  }
  ConnectScrubbing(Root) {
    let Scrub = null;
    Root.addEventListener("pointerdown", (Event) => {
      if (!Event.target.matches(".value-pill input[type=number]") || Event.button !== 0) return;
      Scrub = { Element: Event.target, Pointer: Event.pointerId, X: Event.clientX, Value: Number(Event.target.value), Moved: false };
      Event.target.setPointerCapture(Event.pointerId);
    });
    Root.addEventListener("pointermove", (Event) => {
      if (!Scrub || Scrub.Pointer !== Event.pointerId) return;
      const Delta = Event.clientX - Scrub.X;
      if (!Scrub.Moved && Math.abs(Delta) < 3) return;
      Event.preventDefault();
      Scrub.Moved = true;
      Scrub.Element.classList.add("scrubbing");
      const E = Scrub.Element;
      const Display = Number(E.dataset.display || 1);
      const Step = Number(E.dataset.step) * Display;
      const Scale = Event.shiftKey ? 0.1 : Event.altKey ? 10 : 1;
      const Raw = Math.round((Scrub.Value + Delta * Step * Scale) / (Step * (Event.shiftKey ? 0.1 : 1))) * Step * (Event.shiftKey ? 0.1 : 1);
      const Value = Clamp(Raw / Display, Number(E.dataset.min), Number(E.dataset.max));
      this.ApplyBinding(E.dataset.bind, Number(Value.toFixed(6)), true);
      this.SyncControls(E.dataset.bind);
    });
    for (const Name of ["pointerup", "pointercancel", "lostpointercapture"])
      Root.addEventListener(Name, () => {
        if (Scrub) Scrub.Element.classList.remove("scrubbing");
        Scrub = null;
      });
  }

  // --------------------------------------------------------------- actions
  RunAction(Action) {
    const Layer = this.Layer;
    switch (Action) {
      case "layer-duplicate": return this.DuplicateLayer();
      case "layer-delete": return this.DeleteLayer();
      case "layer-up": return this.MoveLayer(1);
      case "layer-down": return this.MoveLayer(-1);
      case "layer-clear": return this.ReplaceLayerTextures("Clear layer", false);
      case "layer-fill": return this.ReplaceLayerTextures("Fill layer", true);
      case "mask-white": return this.FillMask(1);
      case "mask-black": return this.FillMask(0);
      case "mask-view": return this.SetChannel("mask");
      case "mask-paint": this.SetTarget("mask"); return this.RefreshInspector();
      case "material-save": return this.SaveMaterialToLibrary();
      case "material-export": return this.ExportMaterial();
      case "material-import": return Select("#material-file").click();
      case "material-bitmap": return Select("#bitmap-file").click();
      case "material-bitmap-clear": return this.ApplyBinding("material.Bitmap", null, true);
      case "material-new-fill": return this.AddLayer("fill", { Name: "Fill layer", Material: this.Brush.Material });
      case "decal-svg": return Select("#svg-file").click();
      case "decal-font": return Select("#font-file").click();
      case "decal-place": {
        this.Checkpoint("Place decal");
        if (this.PlaceDecalAtView(Layer)) this.Notify("Decal projected from the view center.");
        this.Bump(Layer);
        this.MarkDirty();
        return this.RefreshInspector();
      }
      case "decal-tool": return this.SetTool("decal");
      case "mesh-obj": return Select("#obj-file").click();
      case "focus": return this.FocusCamera();
    }
  }
  ResetTab() {
    const Kind = this.SelectionKind;
    const Tab = this.CurrentTab();
    const Layer = this.Layer;
    if (Tab === "brush") {
      const { Material, Channels, Target, MaskValue } = this.Brush;
      this.Brush = { ...structuredClone(DEFAULT_BRUSH), Material, Channels, Target, MaskValue };
      this.RefreshBrushBar();
    } else if (Kind === "texture") {
      this.Checkpoint("Reset settings");
      if (Tab === "texture") { this.Doc.Base = { ...DEFAULT_BASE }; this.Doc.NormalStrength = 1; this.Doc.BaseRevision++; }
      if (Tab === "lighting") this.Doc.Environment = { Preset: "studio", Rotation: 0, Exposure: 1, Show: false };
      this.Invalidate();
    } else if (Tab === "material") {
      if (Kind === "paint") this.Brush.Material = SanitizeMaterial(DEFAULT_BRUSH.Material);
      else {
        this.Checkpoint("Reset material");
        Layer.Material = SanitizeMaterial(Kind === "decal" ? CreateLayer("decal").Material : DEFAULT_MATERIAL);
        this.Bump(Layer);
      }
      this.RefreshBrushBar();
    } else if (Tab === "mask") {
      this.Checkpoint("Reset mask");
      Layer.Mask = SanitizeMask({ Painted: Layer.Mask.Painted });
      this.Bump(Layer);
    } else if (Tab === "layer") {
      this.Checkpoint("Reset layer");
      Object.assign(Layer, { Opacity: 1, Blend: "normal", HeightBlend: Layer.Type === "fill" ? "normal" : "add", Channels: AllChannels(true) });
      this.Bump(Layer);
    } else if (Tab === "decal") {
      this.Checkpoint("Reset decal placement");
      const D = Layer.Decal;
      Object.assign(D, { Rotation: 0, Stretch: 1, Depth: DEFAULT_DECAL.Depth, AngleLimit: DEFAULT_DECAL.AngleLimit, FlipX: false, FlipY: false, TwoSided: false });
      this.Bump(Layer);
    }
    this.MarkDirty();
    this.RefreshAll();
  }
  FindPreset(Id) {
    return MATERIAL_PRESETS.find((P) => P.id === Id) || this.UserMaterials.find((P) => P.id === Id);
  }
  ApplyMaterial(Preset) {
    const Kind = this.SelectionKind;
    if (Kind === "paint" || Kind === "texture") {
      this.Brush.Material = SanitizeMaterial(Preset.material);
      this.ActiveMaterial = Preset.id;
      this.RefreshBrushBar();
      this.Notify(`Brush loaded with “${Preset.name}”`);
    } else {
      const Layer = this.Layer;
      this.Checkpoint(`Assign ${Preset.name}`);
      const Keep = Kind === "decal" ? { Height: Layer.Material.Height } : {};
      Layer.Material = SanitizeMaterial({ ...Preset.material, ...Keep });
      if (Kind === "fill" && Preset.mask && Layer.Mask.Generator === "none") Layer.Mask = SanitizeMask({ ...Layer.Mask, ...Preset.mask });
      this.Bump(Layer);
      this.MarkDirty();
      this.ActiveMaterial = Preset.id;
      this.Notify(`Assigned “${Preset.name}” to ${Layer.Name}`);
    }
    this.RefreshMaterials();
    this.RefreshInspector();
  }
  SaveMaterialToLibrary() {
    const Material = SanitizeMaterial(this.BindingRoot("material"));
    const Name = this.SelectionKind === "paint" ? `Brush material ${this.UserMaterials.length + 1}` : this.Layer.Name;
    const Entry = { id: `custom-${Date.now().toString(36)}`, name: Name, category: "custom", glyph: "sparkle", description: `Saved ${new Date().toLocaleDateString()}`, material: Material };
    this.UserMaterials.unshift(Entry);
    this.SaveUserMaterials();
    this.Thumbnails.delete(Entry.id);
    this.MaterialFilter = "custom";
    this.RefreshMaterials();
    this.GenerateThumbnails();
    this.Notify(`Saved “${Name}” to your library`);
  }
  ExportMaterial() {
    const Material = SanitizeMaterial(this.BindingRoot("material"));
    const Name = this.SelectionKind === "paint" ? "brush-material" : this.Layer.Name;
    DownloadBlob(new Blob([JSON.stringify({ Format: "frontier-material", Version: 1, Name, Material }, null, 2)], { type: "application/json" }), `${Name.replace(/[^\w-]+/g, "_")}.material.json`);
  }
  ImportMaterial(Text) {
    try {
      const Data = JSON.parse(Text);
      const Material = SanitizeMaterial(Data.Material || Data);
      this.ApplyMaterial({ id: "import", name: Data.Name || "Imported material", material: Material });
    } catch {
      this.Notify("That file is not a material JSON.");
    }
  }
  ImportBitmap(File_) {
    const Url = URL.createObjectURL(File_);
    const Image_ = new Image();
    Image_.onload = () => {
      const Scale = Math.min(1, 2048 / Math.max(Image_.width, Image_.height));
      const Canvas = document.createElement("canvas");
      Canvas.width = Math.max(1, Math.round(Image_.width * Scale));
      Canvas.height = Math.max(1, Math.round(Image_.height * Scale));
      Canvas.getContext("2d").drawImage(Image_, 0, 0, Canvas.width, Canvas.height);
      URL.revokeObjectURL(Url);
      this.ApplyBinding("material.Bitmap", Canvas.toDataURL("image/jpeg", 0.92), true);
      if (this.BindingRoot("material").Color === "#b8b8b8") this.ApplyBinding("material.Color", "#ffffff", true);
      this.Notify(`Loaded ${File_.name} as the albedo bitmap`);
    };
    Image_.onerror = () => {
      URL.revokeObjectURL(Url);
      this.Notify("That image could not be decoded.");
    };
    Image_.src = Url;
  }
  ImportSvg(Text, Name) {
    const Layer = this.Layer;
    try {
      SvgDimensions(Text);
    } catch (ErrorValue) {
      return this.Notify(ErrorValue.message);
    }
    if (!Layer || Layer.Type !== "decal") {
      this.AddLayer("decal", { Name: Name.replace(/\.svg$/i, "").slice(0, 40) || "SVG decal", Decal: { Source: "svg", Svg: Text, SvgName: Name } });
      this.SetTool("decal");
      return;
    }
    this.Checkpoint("Import SVG");
    Object.assign(Layer.Decal, { Source: "svg", Svg: Text, SvgName: Name.slice(0, 80) });
    this.EnsureDecal(Layer);
    this.Bump(Layer);
    this.MarkDirty();
    this.RefreshInspector();
  }
  async ImportFont(File_, Buffer_) {
    const Family = File_.name.replace(/\.(ttf|otf|woff2?)$/i, "").replace(/[^\w -]+/g, "").slice(0, 40) || "Custom font";
    try {
      const Face = new FontFace(Family, Buffer_);
      await Face.load();
      document.fonts.add(Face);
      if (!this.CustomFonts.some((F) => F.id === Family)) this.CustomFonts.push({ id: Family, label: `${Family} · uploaded` });
      if (this.Layer?.Type === "decal") this.ApplyBinding("decal.Font", Family, true);
      this.RefreshInspector();
      this.Notify(`Font “${Family}” is available for text decals this session`);
    } catch {
      this.Notify("That font file could not be loaded.");
    }
  }

  // --------------------------------------------------------------- refresh
  RefreshAll() {
    if (!this.Doc) return;
    this.RenderDocumentTabs();
    this.RefreshLayers();
    this.RefreshInspector();
    this.RefreshMaterials();
    this.RefreshBrushBar();
    this.RefreshCaption();
    this.RefreshHistory();
    this.RefreshStatus();
    Select("#env-button").classList.toggle("active", this.Doc.Environment.Show);
  }
  RefreshStatus() {
    const Doc = this.Doc;
    const R = Doc.Resolution;
    Select("#resolution-status").textContent = `${R} × ${R} px`;
    Select("#texel-hud").textContent = `${R >= 1024 ? `${R / 1024}K` : R} TEXELS`;
    Select("#mesh-status").textContent = `${Doc.Gpu.Mesh.Name} · ${Doc.Gpu.Mesh.TriangleCount.toLocaleString()} tris`;
    Select("#texture-strip-label").textContent = `${R} × ${R} · ${THUMB_CHANNELS.length} MAPS`;
    Select("#document-note").textContent = `${Doc.Gpu.Mesh.Name} · ${R}²`;
  }
  RefreshHistory() {
    if (!this.Doc) return;
    const H = this.Doc.History;
    Select("#undo-button").disabled = !H.Undo.length;
    Select("#redo-button").disabled = !H.Redo.length;
    Select("#undo-button").title = H.Undo.length ? `Undo ${H.Undo[H.Undo.length - 1].Label} · Ctrl Z` : "Nothing to undo";
    Select("#redo-button").title = H.Redo.length ? `Redo ${H.Redo[H.Redo.length - 1].Label} · Ctrl Shift Z` : "Nothing to redo";
    const Megabytes = (this.Engine?.MemoryUsage() || 0) / 1048576;
    Select("#history-status").textContent = `History ${H.Undo.length} · GPU ${Megabytes.toFixed(0)} MB`;
  }
  RefreshCaption() {
    if (!this.Doc) return;
    const Layer = this.Layer;
    const Mask = this.Brush.Target === "mask";
    Select("#mode-pill").innerHTML = `<i></i><span>${TOOL_LABELS[this.Tool]}${Mask && (this.Tool === "paint" || this.Tool === "erase") ? " · MASK" : ""}</span>`;
    Select("#mode-pill").classList.toggle("paused", this.Tool === "orbit" || this.Tool === "picker");
    Select("#viewport-object").textContent = Layer ? Layer.Name : this.Doc.Name;
    let Subtitle = "Projection painting · metal / rough";
    if (Layer) {
      const Blend = BLEND_MODES.find((B) => B.id === Layer.Blend)?.label;
      if (Mask) Subtitle = `Painting mask · ${this.Brush.MaskValue > 0.5 ? "reveal" : "hide"}`;
      else if (Layer.Type === "paint") Subtitle = `Paint layer · ${Blend} · ${Math.round(Layer.Opacity * 100)}%`;
      else if (Layer.Type === "fill") Subtitle = `Fill · ${PATTERNS.find((P) => P.id === Layer.Material.Pattern)?.label} · ${Blend}`;
      else Subtitle = `Decal · ${Layer.Decal.Source === "svg" ? "SVG" : "text"} · ${Layer.Decal.Mapping === "uv" ? "UV mapped" : "projected"}`;
    }
    Select("#viewport-subtitle").textContent = Subtitle;
  }
  RefreshBrushBar() {
    const M = this.Brush.Material;
    Select("#brush-color").value = M.Color;
    Select("#brush-swatch-fill").style.background = M.Pattern === "none" ? M.Color : `linear-gradient(135deg, ${M.Color} 0 50%, ${M.Color2} 50%)`;
    Select(".brush-swatch").classList.toggle("metal", M.Metallic > 0.5);
    for (const Key of ["Size", "Hardness", "Flow", "Opacity"]) this.SyncControlsOnly(`brush.${Key}`);
  }
  SyncControlsOnly(Bind) {
    const Value = this.GetBinding(Bind);
    SelectAll(`[data-bind="${CSS.escape(Bind)}"]`).forEach((Element) => {
      if (Element.dataset.kind === "number" && document.activeElement !== Element) {
        const Display = Number(Element.dataset.display || 1);
        Element.value = (Value * Display).toFixed(Decimals(Number(Element.dataset.step) * Display));
      } else if (Element.dataset.kind === "range") {
        Element.value = Value;
        Element.style.setProperty("--fraction", Clamp((Value - Number(Element.min)) / (Number(Element.max) - Number(Element.min) || 1), 0, 1).toFixed(4));
      }
    });
  }
  RefreshLayers() {
    const Doc = this.Doc;
    if (!Doc) return;
    const Layers = Doc.Layers;
    Select("#layer-count").textContent = Layers.length;
    Select("#visible-count").textContent = Layers.filter((L) => L.Visible).length;
    Select("#hidden-count").textContent = Layers.filter((L) => !L.Visible).length;
    const Search = Select("#layer-search").value.toLowerCase().trim();
    const Collapsed = Select("#collection-toggle").getAttribute("aria-expanded") === "false";
    const Matches = [...Layers].reverse().filter((L) => (this.LayerFilter === "all" || L.Type === this.LayerFilter) && L.Name.toLowerCase().includes(Search));
    const MaskTarget = this.Brush.Target === "mask";
    const Rows = Matches.map((L) => {
      const Selected = Doc.Selection === L.Id;
      const Glyph = L.Type === "decal" ? (L.Decal.Source === "text" ? "type" : "shape") : LAYER_TYPES[L.Type].glyph;
      const Masked = L.Mask.Enabled && (L.Mask.Painted || L.Mask.Generator !== "none");
      const BlendShort = L.Blend !== "normal" ? BLEND_MODES.find((B) => B.id === L.Blend).label.split(" ")[0].slice(0, 4).toUpperCase() : "";
      const Badge = [BlendShort, L.Opacity < 1 ? `${Math.round(L.Opacity * 100)}%` : ""].filter(Boolean).join(" · ") || LAYER_TYPES[L.Type].badge;
      const Swatch = L.Type === "fill" ? `<span class="layer-swatch" style="background:${L.Material.Pattern === "none" ? L.Material.Color : `linear-gradient(135deg, ${L.Material.Color} 0 50%, ${L.Material.Color2} 50%)`}"></span>` : "";
      return `<div class="scene-row layer-row ${Selected ? "selected" : ""} ${L.Visible ? "" : "muted"}" data-layer="${L.Id}" data-object="${L.Type}" draggable="true" role="treeitem" aria-selected="${Selected}" aria-level="2" tabindex="0"><span class="row-chevron drag-handle" title="Drag to reorder">${Icon("drag")}</span><span class="row-icon">${Icon(Glyph)}</span>${Swatch}<span class="scene-label">${Escape(L.Name)}</span>${Masked ? `<span class="row-mask ${Selected && MaskTarget ? "target" : ""}" title="${Selected && MaskTarget ? "Painting this mask" : "Masked"}">${Icon("mask")}</span>` : ""}<span class="row-badge">${Escape(Badge)}</span><button class="icon-button row-toggle" data-toggle-layer="${L.Id}" title="${L.Visible ? "Hide" : "Show"} ${Escape(L.Name)}" aria-label="${L.Visible ? "Hide" : "Show"} ${Escape(L.Name)}">${Icon(L.Visible ? "eye" : "hidden")}</button></div>`;
    });
    const TextureRow = `<div class="scene-row layer-row texture-row ${Doc.Selection === "texture" ? "selected" : ""}" data-layer="texture" data-object="domain" role="treeitem" aria-selected="${Doc.Selection === "texture"}" aria-level="1" tabindex="0"><span class="row-chevron"></span><span class="row-icon">${Icon(this.MeshGlyph())}</span><span class="scene-label">${Escape(Doc.Gpu.Mesh.Name)}</span><span class="row-badge">${Doc.Resolution >= 1024 ? `${Doc.Resolution / 1024}K` : Doc.Resolution} SET</span></div>`;
    Select("#layer-tree").hidden = Collapsed && !Search;
    Select("#layer-tree").innerHTML = TextureRow + (Rows.join("") || '<div class="outliner-empty" role="status">No matching layers.</div>');
    SelectAll("[data-layer-filter]").forEach((B) => {
      B.classList.toggle("active", B.dataset.layerFilter === this.LayerFilter);
      B.setAttribute("aria-pressed", String(B.dataset.layerFilter === this.LayerFilter));
    });
    const Layer = this.Layer;
    Select("#layer-footnote").textContent = !Layer
      ? "Add a layer to start painting"
      : MaskTarget
        ? `Strokes paint the mask of “${Layer.Name}”`
        : Layer.Type === "paint"
          ? `Strokes land on “${Layer.Name}”`
          : `Select a paint layer, or press M to paint this mask`;
  }
  MeshGlyph() {
    return { cube: "box", sphere: "sphere", can: "cylinder", bottle: "bottle", torus: "torus", plane: "plane", custom: "box" }[this.Doc.MeshKey] || "box";
  }
  RefreshMaterials() {
    const Search = Select("#material-search").value.toLowerCase().trim();
    const All = [...this.UserMaterials, ...MATERIAL_PRESETS];
    const Matches = All.filter((P) => (this.MaterialFilter === "all" || P.category === this.MaterialFilter) && `${P.name} ${P.description} ${P.category}`.toLowerCase().includes(Search));
    Select("#material-count").textContent = All.length;
    Select("#material-list").innerHTML = Matches.map((P) => {
      const Thumb = this.Thumbnails.get(P.id);
      return `<div class="preset-card material-card ${this.ActiveMaterial === P.id ? "active" : ""}" role="button" tabindex="0" data-material="${P.id}" data-category="${P.category}" title="${Escape(P.layers ? "Add this smart material as a layer stack" : "Click to use · ↗ adds a fill layer")}"><span class="preset-swatch material-swatch">${Thumb ? `<img src="${Thumb}" alt="" />` : Icon(P.glyph)}${P.layers ? `<b>${P.layers.length}</b>` : ""}</span><span class="preset-copy"><strong>${Escape(P.name)}</strong><small>${Escape(P.description)}</small></span>${P.category === "custom" ? `<button class="preset-remove" data-remove-material="${P.id}" title="Remove from library" aria-label="Remove ${Escape(P.name)}">${Icon("close")}</button>` : ""}<button class="preset-arrow" data-add-material="${P.id}" title="Add as ${P.layers ? "layer stack" : "fill layer"}" aria-label="Add ${Escape(P.name)} as layer">↗</button></div>`;
    }).join("");
    Select("#empty-materials").hidden = Matches.length > 0;
    SelectAll("[data-material-filter]").forEach((B) => B.classList.toggle("active", B.dataset.materialFilter === this.MaterialFilter));
  }
  async GenerateThumbnails() {
    if (!this.Engine) return;
    const Environment = { Preset: "studio", Rotation: 30, Exposure: 1.05 };
    const Canvas = document.createElement("canvas");
    Canvas.width = Canvas.height = 88;
    const Context = Canvas.getContext("2d");
    for (const Preset of [...this.UserMaterials, ...MATERIAL_PRESETS]) {
      if (this.Thumbnails.has(Preset.id)) continue;
      const Material = Preset.material || Preset.layers[Preset.layers.length > 1 ? 1 : 0].Material;
      if (Material.Bitmap && !this.Engine.BitmapTexture(Material.Bitmap)) continue;
      const Pixels = this.Engine.RenderPreview(Material, 88, Environment);
      const Image_ = Context.createImageData(88, 88);
      for (let Y = 0; Y < 88; Y++) Image_.data.set(Pixels.subarray((87 - Y) * 352, (88 - Y) * 352), Y * 352);
      Context.putImageData(Image_, 0, 0);
      this.Thumbnails.set(Preset.id, Canvas.toDataURL());
      await Wait();
    }
    this.RefreshMaterials();
  }
  UpdateChannelTiles() {
    const Doc = this.Doc;
    if (!Doc?.Gpu) return;
    const Map_ = { color: 1, roughness: 2, metallic: 3, height: 4, normal: 5, emissive: 6, mask: 7 };
    for (const Tile of SelectAll("[data-channel-tile]")) {
      const Canvas = Tile.querySelector("canvas");
      const Pixels = this.Engine.RenderChannelThumbnail(Doc, Map_[Tile.dataset.channelTile], 64);
      const Context = Canvas.getContext("2d");
      const Image_ = Context.createImageData(64, 64);
      for (let Y = 0; Y < 64; Y++) Image_.data.set(Pixels.subarray((63 - Y) * 256, (64 - Y) * 256), Y * 256);
      Context.putImageData(Image_, 0, 0);
      Tile.classList.toggle("active", Tile.dataset.channelTile === this.Channel);
    }
  }

  // -------------------------------------------------------------- inspector
  CurrentTab() {
    const Kind = this.SelectionKind;
    const Tabs = this.TabsFor(Kind).map((T) => T.id);
    if (!Tabs.includes(this.Tabs[Kind])) this.Tabs[Kind] = Tabs[0];
    return this.Tabs[Kind];
  }
  TabsFor(Kind) {
    if (Kind === "texture") return [{ id: "mesh", label: "Mesh" }, { id: "texture", label: "Texture" }, { id: "lighting", label: "Lighting" }];
    if (Kind === "decal") return [{ id: "layer", label: "Layer" }, { id: "decal", label: "Decal" }, { id: "material", label: "Material" }, { id: "mask", label: "Mask" }];
    return [{ id: "layer", label: "Layer" }, { id: "material", label: Kind === "paint" ? "Brush" : "Material" }, { id: "mask", label: "Mask" }, { id: "brush", label: "Tip" }];
  }
  RefreshInspector() {
    if (!this.Doc) return;
    const Kind = this.SelectionKind;
    const Tab = this.CurrentTab();
    const Layer = this.Layer;
    const Texture = Kind === "texture";
    Select("#object-name").value = Texture ? this.Doc.Name : Layer.Name;
    Select("#object-name").setAttribute("aria-label", Texture ? "Texture set name" : "Layer name");
    Select("#object-type").textContent = Texture ? `TEXTURE SET · ${this.Doc.Resolution}² · ${this.Doc.Layers.length} LAYERS` : `${TYPE_LABELS[Kind]} · ${BLEND_MODES.find((B) => B.id === Layer.Blend).label.toUpperCase()}`;
    Select("#object-symbol").innerHTML = Icon(Texture ? this.MeshGlyph() : Kind === "decal" ? (Layer.Decal.Source === "text" ? "type" : "shape") : LAYER_TYPES[Kind].glyph);
    Select("#object-switch").hidden = Texture;
    Select("#object-enabled").checked = Texture || Layer.Visible;
    Select("#inspector-tabs").innerHTML = this.TabsFor(Kind).map((T) => `<button class="${T.id === Tab ? "active" : ""}" data-tab="${T.id}">${T.label}</button>`).join("");
    const Groups = this.InspectorGroups(Kind, Tab, Layer);
    const Body = Select("#inspector-body");
    const Scroll = Body.scrollTop;
    Body.innerHTML = Groups.map((G) => RenderGroup(G, (Bind) => this.GetBinding(Bind), this.GroupState)).join("");
    Body.scrollTop = Scroll;
    FillIcons(Body);
    Select("#inspector-footer-text").textContent =
      Tab === "brush" || (Kind === "paint" && Tab === "material") ? "Brush settings apply to the next stroke" : Texture && Tab === "lighting" ? "Lighting is preview-only, never baked" : "Non-destructive · Ctrl Z to undo";
  }
  MaterialGroups(Context) {
    const M = this.BindingRoot("material");
    const Patterned = M.Pattern !== "none";
    const IsDecal = Context === "decal";
    const Groups = [];
    if (Context === "brush")
      Groups.push({ Id: "brush-channels", Title: "Brush channels", Badge: "WRITE", Hint: "A stroke only writes the enabled channels — paint roughness alone, or height without color.", Controls: [Channels("brush.Channels", "Channels")] });
    Groups.push({
      Id: `${Context}-surface`,
      Title: Patterned ? "Surface A" : "Surface",
      Badge: "PBR",
      Hint: IsDecal && this.Layer.Decal.SourceColor ? "Color comes from the artwork while “Use artwork colors” is on." : "",
      Controls: [
        ColorField("material.Color", "Base color"),
        Slider("material.Roughness", "Roughness", 0, 1, 0.01),
        Slider("material.Metallic", "Metallic", 0, 1, 0.01),
      ],
    });
    Groups.push({
      Id: `${Context}-pattern`,
      Title: "Pattern",
      Badge: "PROCEDURAL",
      Hint: Patterned ? "The pattern blends Surface A into Surface B and can drive height and emission." : "Add a procedural pattern to mix a second surface in.",
      Controls: [
        Choice("material.Pattern", "Pattern", PATTERNS),
        Patterned && ColorField("material.Color2", "Surface B color"),
        Patterned && Slider("material.Roughness2", "Surface B roughness", 0, 1, 0.01),
        Patterned && Slider("material.Metallic2", "Surface B metallic", 0, 1, 0.01),
        Patterned && Choice("material.Mapping", "Projection", [{ id: "triplanar", label: "Triplanar · seamless" }, { id: "uv", label: "UV space" }]),
        Patterned && Slider("material.Scale", "Scale", 0.05, 128, 0.05, "×"),
        Patterned && Slider("material.Rotation", "Rotation", -180, 180, 1, "°"),
        Patterned && Slider("material.Contrast", "Contrast", 0, 16, 0.05, "×"),
        Patterned && Slider("material.Balance", "Balance", -1, 1, 0.01),
        Patterned && Slider("material.Warp", "Warp", 0, 4, 0.01),
        Patterned && Slider("material.Seed", "Seed", 0, 999, 1, "#"),
      ],
    });
    Groups.push({
      Id: `${Context}-height`,
      Title: IsDecal ? "Emboss" : "Height",
      Badge: "NORMAL",
      Hint: IsDecal ? "Positive values raise the artwork, negative values engrave it." : "Height feeds the generated normal map. Paint layers add on top of the stack by default.",
      Controls: [
        Slider("material.Height", IsDecal ? "Emboss depth" : "Height", -1, 1, 0.005),
        Patterned && Slider("material.HeightAmount", "Pattern height", -1, 1, 0.005),
        M.Bitmap && Slider("material.BitmapHeight", "Bitmap height", -1, 1, 0.01),
      ],
    });
    Groups.push({
      Id: `${Context}-emission`,
      Title: "Emission",
      Badge: "GLOW",
      Open: M.EmissiveStrength > 0,
      Controls: [
        ColorField("material.Emissive", "Emissive color"),
        Slider("material.EmissiveStrength", "Strength", 0, 20, 0.05, "×"),
        Patterned && Toggle("material.EmissiveFromPattern", "Only Surface B glows"),
      ],
    });
    Groups.push({
      Id: `${Context}-detail`,
      Title: "Detail & bitmap",
      Badge: "TEXTURE",
      Open: Boolean(M.Bitmap) || M.Grain > 0,
      Hint: M.Bitmap ? "The bitmap multiplies the base color." : "Load a photo or tile to multiply the albedo.",
      Controls: [
        Slider("material.Grain", "Micro grain", 0, 1, 0.01),
        M.Bitmap && Slider("material.BitmapScale", "Bitmap tiling", 0.05, 64, 0.05, "×"),
        M.Bitmap && !Patterned && Choice("material.Mapping", "Projection", [{ id: "triplanar", label: "Triplanar · seamless" }, { id: "uv", label: "UV space" }]),
        Buttons([{ Action: "material-bitmap", Label: M.Bitmap ? "Replace bitmap" : "Load bitmap", Icon: "image" }, M.Bitmap && { Action: "material-bitmap-clear", Label: "Remove", Icon: "trash" }].filter(Boolean)),
      ],
    });
    Groups.push({
      Id: `${Context}-library`,
      Title: "Library",
      Badge: "SHARE",
      Open: false,
      Controls: [
        Buttons([
          { Action: "material-save", Label: "Save to library", Icon: "save" },
          { Action: "material-export", Label: "Export", Icon: "download" },
          { Action: "material-import", Label: "Import", Icon: "upload" },
        ]),
        Context === "brush" && Buttons([{ Action: "material-new-fill", Label: "New fill layer from brush", Icon: "bucket" }]),
      ],
    });
    return Groups;
  }
  InspectorGroups(Kind, Tab, Layer) {
    const Doc = this.Doc;
    if (Kind === "texture") {
      if (Tab === "mesh") {
        const Options = [...MESH_OPTIONS, ...(Doc.CustomMesh ? [{ id: "custom", label: `${Doc.CustomMesh.Name} · OBJ` }] : [])];
        const Mesh = Doc.Gpu.Mesh;
        const Size = Mesh.Bounds.Max.map((V, A) => (V - Mesh.Bounds.Min[A]).toFixed(2)).join(" × ");
        return [
          { Id: "mesh-target", Title: "Paint target", Badge: "MESH", Hint: "Layers live in UV space, so you can swap meshes without losing paint.", Controls: [Choice("doc.MeshKey", "Mesh", Options), Buttons([{ Action: "mesh-obj", Label: "Import OBJ…", Icon: "upload" }, { Action: "focus", Label: "Frame", Icon: "focus" }]), Hint(`${Mesh.TriangleCount.toLocaleString()} triangles · ${(Mesh.Positions.length / 3).toLocaleString()} vertices · ${Size} units. OBJ files need UVs inside the 0–1 tile.`)] },
          { Id: "mesh-generators", Title: "Baked maps", Badge: "AUTO", Hint: "Position, normal and curvature are baked into texture space whenever the mesh or resolution changes. Mask generators read them for edge wear, cavity dirt, dust and gradients.", Controls: [] },
        ];
      }
      if (Tab === "texture") {
        const Options = RESOLUTIONS.filter((R) => R <= this.Engine.MaxTexture).map((R) => ({ id: R, label: `${R} × ${R}${R >= 4096 ? " · heavy" : ""}` }));
        return [
          { Id: "texture-resolution", Title: "Resolution", Badge: "TEXELS", Hint: `Changing resolution resamples every layer and clears undo. GPU memory in use: ${((this.Engine.MemoryUsage() || 0) / 1048576).toFixed(0)} MB.`, Controls: [Choice("doc.Resolution", "Texture size", Options)] },
          { Id: "texture-base", Title: "Base surface", Badge: "UNDER", Hint: "Shown wherever no layer covers a channel.", Controls: [ColorField("base.Color", "Base color"), Slider("base.Roughness", "Roughness", 0, 1, 0.01), Slider("base.Metallic", "Metallic", 0, 1, 0.01), Slider("base.Height", "Height", -1, 1, 0.01)] },
          { Id: "texture-normal", Title: "Normal map", Badge: "TANGENT", Hint: "Generated from the composited height (OpenGL convention; DirectX on export).", Controls: [Slider("doc.NormalStrength", "Strength", 0, 6, 0.05, "×")] },
        ];
      }
      return [
        { Id: "lighting-env", Title: "Environment", Badge: "PREVIEW", Controls: [Choice("env.Preset", "Preset", Object.entries(ENVIRONMENTS).map(([id, E]) => ({ id, label: E.label }))), Slider("env.Rotation", "Rotation", -180, 180, 1, "°"), Slider("env.Exposure", "Exposure", 0.2, 3, 0.01, "EV"), Toggle("env.Show", "Show as background")] },
      ];
    }
    if (Tab === "layer") {
      const Groups = [
        { Id: "layer-blend", Title: "Blending", Badge: "STACK", Controls: [Percent("layer.Opacity", "Opacity"), Choice("layer.Blend", "Color blend", BLEND_MODES), Choice("layer.HeightBlend", "Height blend", HEIGHT_BLENDS)] },
        { Id: "layer-channels", Title: "Channels", Badge: "OUTPUT", Hint: "Disabled channels pass the layers below through unchanged.", Controls: [Channels("layer.Channels", "Writes")] },
      ];
      const Actions = [
        { Action: "layer-up", Label: "Raise", Icon: "up" },
        { Action: "layer-down", Label: "Lower", Icon: "down" },
        { Action: "layer-duplicate", Label: "Duplicate", Icon: "copy" },
        { Action: "layer-delete", Label: "Delete", Icon: "trash", Danger: true },
      ];
      Groups.push({ Id: "layer-actions", Title: "Layer", Badge: Kind.toUpperCase(), Controls: [Buttons(Actions.slice(0, 2)), Buttons(Actions.slice(2)), Kind === "paint" && Buttons([{ Action: "layer-fill", Label: "Fill with brush", Icon: "bucket" }, { Action: "layer-clear", Label: "Clear pixels", Icon: "empty" }])] });
      return Groups;
    }
    if (Tab === "material") return this.MaterialGroups(Kind === "paint" ? "brush" : Kind);
    if (Tab === "mask") {
      const Mask = Layer.Mask;
      const Generated = Mask.Generator !== "none";
      return [
        {
          Id: "mask-main",
          Title: "Mask",
          Badge: Mask.Painted ? "PAINTED" : "OFF",
          Hint: "White shows the layer, black hides it. Press M to paint the mask, X to swap reveal and hide.",
          Controls: [
            Toggle("mask.Enabled", "Mask enabled"),
            Toggle("mask.Painted", "Painted mask"),
            Buttons([{ Action: "mask-paint", Label: this.Brush.Target === "mask" ? "Painting mask" : "Paint mask", Icon: "brush", Primary: this.Brush.Target === "mask" }, { Action: "mask-view", Label: "View mask", Icon: "eye" }]),
            Buttons([{ Action: "mask-white", Label: "Fill white", Icon: "fill" }, { Action: "mask-black", Label: "Fill black", Icon: "empty" }]),
          ],
        },
        {
          Id: "mask-generator",
          Title: "Generator",
          Badge: "PROCEDURAL",
          Hint: Generated ? "Multiplied with the painted mask. Curvature and facing come from the baked mesh maps." : "Drive the mask from mesh curvature, facing, height or noise.",
          Controls: [
            Choice("mask.Generator", "Generator", MASK_GENERATORS),
            Generated && Slider("mask.Scale", Mask.Generator === "noise" ? "Noise scale" : "Sensitivity", 0.1, 64, 0.1, "×"),
            Generated && Slider("mask.Contrast", "Contrast", 0.1, 32, 0.1, "×"),
            Generated && Slider("mask.Offset", "Level", -1, 1, 0.01),
            Generated && Mask.Generator !== "noise" && Slider("mask.Breakup", "Noise breakup", 0, 1.5, 0.01),
            Generated && Slider("mask.Seed", "Seed", 0, 999, 1, "#"),
            Generated && Toggle("mask.Invert", "Invert"),
          ],
        },
      ];
    }
    if (Tab === "brush") {
      return [
        { Id: "brush-tip", Title: "Tip", Badge: "SHAPE", Controls: [Choice("brush.Tip", "Tip", [{ id: "round", label: "Round" }, { id: "square", label: "Square" }, { id: "noise", label: "Grunge" }, { id: "splatter", label: "Splatter" }]), Slider("brush.Size", "Size", 1, 400, 1, "px"), Percent("brush.Hardness", "Hardness"), Slider("brush.Roundness", "Roundness", 0.05, 1, 0.01), Slider("brush.Angle", "Angle", -180, 180, 1, "°"), Percent("brush.Spacing", "Spacing", { Min: 0.02, Max: 2 })] },
        { Id: "brush-stroke", Title: "Stroke", Badge: "FLOW", Hint: "Flow builds up within a stroke; opacity caps the whole stroke.", Controls: [Percent("brush.Flow", "Flow"), Percent("brush.Opacity", "Opacity"), Percent("brush.Smoothing", "Smoothing", { Max: 0.95 }), Toggle("brush.PressureSize", "Pen pressure → size"), Toggle("brush.PressureFlow", "Pen pressure → flow")] },
        { Id: "brush-jitter", Title: "Jitter", Badge: "SCATTER", Open: this.Brush.Jitter > 0 || this.Brush.SizeJitter > 0 || this.Brush.AngleJitter > 0, Controls: [Percent("brush.Jitter", "Position", { Max: 2 }), Percent("brush.SizeJitter", "Size"), Percent("brush.AngleJitter", "Angle"), Toggle("brush.FollowStroke", "Follow stroke direction")] },
        { Id: "brush-projection", Title: "Projection", Badge: "3D", Hint: "In 3D the brush is projected from the camera through the surface into texture space, so strokes cross UV seams. In the UV view it paints texels directly.", Controls: [Toggle("brush.Backface", "Skip back-facing surfaces")] },
      ];
    }
    if (Tab === "decal") {
      const D = Layer.Decal;
      const Svg = D.Source === "svg";
      const Projected = D.Mapping === "projected";
      const Fonts = [...FONT_OPTIONS, ...this.CustomFonts];
      if (!Fonts.some((F) => F.id === D.Font)) Fonts.push({ id: D.Font, label: `${D.Font} · missing` });
      const Error = Layer.DecalEntry?.Error;
      return [
        {
          Id: "decal-source",
          Title: "Artwork",
          Badge: Svg ? "SVG" : "TEXT",
          Hint: Error ? `<span class="hint-error">${Escape(Error)}</span>` : "",
          Controls: [
            Segmented("decal.Source", "", [{ id: "svg", label: "SVG", icon: "shape" }, { id: "text", label: "Text", icon: "type" }]),
            ...(Svg
              ? [
                  Choice("decal.SampleId", "Artwork", [...SAMPLE_SVGS.map((S) => ({ id: S.id, label: S.name })), ...(SAMPLE_SVGS.some((S) => S.svg === D.Svg) ? [] : [{ id: "custom", label: D.SvgName || "Imported SVG" }])]),
                  Buttons([{ Action: "decal-svg", Label: "Import SVG…", Icon: "upload" }]),
                  Toggle("decal.SourceColor", "Use artwork colors"),
                ]
              : [
                  TextArea("decal.Text", "Text", 2),
                  Choice("decal.Font", "Font", Fonts),
                  Buttons([{ Action: "decal-font", Label: "Upload font…", Icon: "upload" }]),
                  Slider("decal.Weight", "Weight", 100, 900, 100, "wt"),
                  Toggle("decal.Italic", "Italic"),
                  Choice("decal.Align", "Alignment", [{ id: "left", label: "Left" }, { id: "center", label: "Center" }, { id: "right", label: "Right" }]),
                  Slider("decal.LetterSpacing", "Letter spacing", -0.2, 1, 0.01, "em"),
                  D.Text.includes("\n") && Slider("decal.LineHeight", "Line height", 0.6, 3, 0.01, "×"),
                  ColorField("decal.TextColor", "Text color"),
                  Slider("decal.Outline", "Outline", 0, 0.3, 0.005, "em"),
                  D.Outline > 0 && ColorField("decal.OutlineColor", "Outline color"),
                  Toggle("decal.SourceColor", "Use text colors"),
                ]),
          ],
        },
        {
          Id: "decal-placement",
          Title: "Placement",
          Badge: Projected ? "PROJECTED" : "UV",
          Hint: Projected
            ? "Decal tool (D): click the model to stick it, drag the round handle to rotate, corners to scale. Shift + scroll scales, Ctrl + scroll rotates."
            : "Placed in texture space. Use the UV or split view with the decal tool to drag it.",
          Controls: [
            Choice("decal.Mapping", "Mapping", [{ id: "projected", label: "Projected from view" }, { id: "uv", label: "UV space" }]),
            Projected && Buttons([{ Action: "decal-tool", Label: this.Tool === "decal" ? "Decal tool active" : "Use decal tool", Icon: "target", Primary: this.Tool === "decal" }, { Action: "decal-place", Label: "Project from view", Icon: "focus" }]),
            Projected ? Slider("decal.Size", "Width", 0.02, 4, 0.005, "u") : Slider("decal.UvSize", "Width", 0.01, 2, 0.005, "uv"),
            !Projected && Slider("decal.UvCenter.0", "U position", -0.5, 1.5, 0.001),
            !Projected && Slider("decal.UvCenter.1", "V position", -0.5, 1.5, 0.001),
            Slider("decal.Rotation", "Rotation", -180, 180, 0.5, "°"),
            Slider("decal.Stretch", "Vertical stretch", 0.1, 10, 0.01, "×"),
            Projected && Slider("decal.Depth", "Projection depth", 0.01, 2, 0.01, "u"),
            Projected && Slider("decal.AngleLimit", "Angle limit", 1, 180, 1, "°"),
            Projected && Toggle("decal.TwoSided", "Project through back faces"),
            Toggle("decal.FlipX", "Mirror horizontally"),
            Toggle("decal.FlipY", "Mirror vertically"),
          ],
        },
      ];
    }
    return [];
  }

  // ----------------------------------------------------------- save / load
  async SaveProject() {
    const Doc = this.Doc;
    if (!Doc || !this.Engine || this.Engine.Lost) return;
    if (this.Stroke) return;
    this.Busy("Saving project…");
    try {
      await Wait();
      const Files = [];
      const Layers = [];
      for (const Layer of Doc.Layers) {
        const Record = SerializeLayer(Layer);
        delete Record.DecalSignature;
        Record.Files = {};
        const R = Doc.Resolution;
        if (Layer.Gpu.Textures) {
          for (let Index = 0; Index < 4; Index++) {
            const Texture = Layer.Gpu.Textures[Index];
            const Pixels = this.Engine.ReadTexture(Texture);
            let Blob_;
            if (Index === 1) {
              const Data = new Uint16Array(R * R * 4);
              for (let K = 0; K < R * R; K++) {
                Data[K * 4] = Math.round(Clamp(Pixels[K * 4], 0, 1) * 65535);
                Data[K * 4 + 1] = Math.round(Clamp(Pixels[K * 4 + 1], 0, 1) * 65535);
                Data[K * 4 + 2] = Math.round(Clamp(Pixels[K * 4 + 2] * 0.5 + 0.5, 0, 1) * 65535);
                Data[K * 4 + 3] = 65535;
              }
              Blob_ = await EncodePng(R, R, 4, 16, Data);
            } else Blob_ = await EncodePng(R, R, 4, 8, Pixels);
            const Name = `layers/${Layer.Id}_t${Index}.png`;
            Files.push({ Name, Data: new Uint8Array(await Blob_.arrayBuffer()) });
            Record.Files[`T${Index}`] = Name;
            await Wait();
          }
        }
        if (Layer.Gpu.Mask) {
          const Pixels = this.Engine.ReadTexture(Layer.Gpu.Mask);
          const Gray = new Uint8Array(R * R);
          for (let K = 0; K < R * R; K++) Gray[K] = Pixels[K * 4];
          const Name = `layers/${Layer.Id}_mask.png`;
          Files.push({ Name, Data: new Uint8Array(await (await EncodePng(R, R, 1, 8, Gray)).arrayBuffer()) });
          Record.Files.Mask = Name;
        }
        Layers.push(Record);
      }
      if (Doc.MeshKey === "custom" && Doc.CustomObj) Files.push({ Name: "mesh.obj", Data: new TextEncoder().encode(Doc.CustomObj) });
      const Project = {
        Format: "frontier-texture",
        Version: 1,
        Name: Doc.Name,
        Resolution: Doc.Resolution,
        MeshKey: Doc.MeshKey,
        MeshName: Doc.CustomMesh?.Name,
        NormalStrength: Doc.NormalStrength,
        Environment: Doc.Environment,
        Base: Doc.Base,
        Camera: this.Camera.Serialize(),
        ActiveId: Doc.ActiveId,
        Layers,
      };
      Files.unshift({ Name: "project.json", Data: new TextEncoder().encode(JSON.stringify(Project, null, 1)) });
      DownloadBlob(CreateZip(Files), `${Doc.Name.replace(/[^\w -]+/g, "_") || "texture"}.ftex`);
      Doc.Dirty = false;
      Select("#dirty-indicator").classList.add("clean");
      this.RenderDocumentTabs();
      this.SetStatus("Project saved");
    } catch (ErrorValue) {
      console.error(ErrorValue);
      this.Notify(`Save failed: ${ErrorValue.message}`);
    } finally {
      this.Busy(null);
    }
  }
  async OpenProject(File_, Buffer_) {
    if (this.Documents.length >= MAX_DOCUMENTS) return this.Notify(`Close a tab first — up to ${MAX_DOCUMENTS} texture sets stay resident.`);
    this.Busy("Opening project…");
    let Doc = null;
    try {
      const Files = await ReadZip(new Uint8Array(Buffer_));
      const Json = Files.get("project.json");
      if (!Json) throw new Error("The archive has no project.json.");
      const Project = JSON.parse(new TextDecoder().decode(Json));
      if (Project.Format !== "frontier-texture") throw new Error("This is not a Frontier texture project.");
      const Resolution = RESOLUTIONS.includes(Project.Resolution) ? Project.Resolution : 1024;
      if (Resolution > this.Engine.MaxTexture) throw new Error("This GPU cannot allocate the project's texture size.");
      Doc = CreateDocumentRecord({
        Name: String(Project.Name || File_.name.replace(/\.ftex$/i, "")).slice(0, 64),
        Resolution,
        MeshKey: Project.MeshKey === "custom" ? "custom" : CreateMesh(Project.MeshKey).Key,
        NormalStrength: Clamp(Number(Project.NormalStrength ?? 1), 0, 6),
        Environment: { Preset: ENVIRONMENTS[Project.Environment?.Preset] ? Project.Environment.Preset : "studio", Rotation: Clamp(Number(Project.Environment?.Rotation || 0), -180, 180), Exposure: Clamp(Number(Project.Environment?.Exposure || 1), 0.2, 3), Show: Boolean(Project.Environment?.Show) },
        Base: { Color: /^#[0-9a-f]{6}$/i.test(Project.Base?.Color) ? Project.Base.Color : DEFAULT_BASE.Color, Roughness: Clamp(Number(Project.Base?.Roughness ?? 0.55), 0, 1), Metallic: Clamp(Number(Project.Base?.Metallic ?? 0), 0, 1), Height: Clamp(Number(Project.Base?.Height ?? 0), -1, 1) },
      });
      let Mesh;
      if (Doc.MeshKey === "custom") {
        const Obj = Files.get("mesh.obj");
        if (!Obj) throw new Error("The project references a custom mesh that is missing.");
        Doc.CustomObj = new TextDecoder().decode(Obj);
        Doc.CustomMesh = Mesh = ParseObj(Doc.CustomObj, String(Project.MeshName || "Imported mesh"));
      } else Mesh = CreateMesh(Doc.MeshKey);
      this.Engine.CreateDocument(Doc, Mesh);
      const R = Resolution;
      const LoadPng = async (Name) => {
        const Bytes = Files.get(Name);
        if (!Bytes) throw new Error(`Missing ${Name} in the archive.`);
        const Image_ = await DecodePng(Bytes);
        if (Image_.Width !== R || Image_.Height !== R) throw new Error(`${Name} does not match the project resolution.`);
        return Image_;
      };
      let ActiveId = null;
      for (const Record of Array.isArray(Project.Layers) ? Project.Layers : []) {
        const Layer = ValidateLayer(Record);
        if (Record.Id === Project.ActiveId) ActiveId = Layer.Id;
        if (Layer.Type === "paint") {
          if (Record.Files?.T0) {
            const Textures = [];
            for (let Index = 0; Index < 4; Index++) {
              const Image_ = await LoadPng(Record.Files[`T${Index}`]);
              if (Index === 1) {
                const Data = new Float32Array(R * R * 4);
                const Scale = Image_.Depth === 16 ? 65535 : 255;
                for (let K = 0; K < R * R; K++) {
                  Data[K * 4] = Image_.Data[K * 4] / Scale;
                  Data[K * 4 + 1] = Image_.Data[K * 4 + 1] / Scale;
                  Data[K * 4 + 2] = (Image_.Data[K * 4 + 2] / Scale) * 2 - 1;
                  Data[K * 4 + 3] = 1;
                }
                Textures.push(this.Engine.UploadLayerTexture(R, "RGBA16F", Data));
              } else Textures.push(this.Engine.UploadLayerTexture(R, "RGBA8", Image_.Data));
              await Wait();
            }
            Layer.Gpu.Textures = Textures;
          } else Layer.Gpu.Textures = this.Engine.CreatePaintTextures(R);
        }
        if (Record.Files?.Mask) {
          const Image_ = await LoadPng(Record.Files.Mask);
          const Rgba = new Uint8Array(R * R * 4);
          for (let K = 0; K < R * R; K++) {
            const V = Image_.Data[K * Image_.Channels];
            Rgba[K * 4] = Rgba[K * 4 + 1] = Rgba[K * 4 + 2] = V;
            Rgba[K * 4 + 3] = 255;
          }
          Layer.Gpu.Mask = this.Engine.UploadLayerTexture(R, "RGBA8", Rgba);
        } else if (Layer.Mask.Painted) Layer.Gpu.Mask = this.Engine.CreateMaskTexture(R, 1);
        this.EnsureDecal(Layer);
        Doc.Layers.push(Layer);
      }
      Doc.ActiveId = ActiveId || Doc.Layers[Doc.Layers.length - 1]?.Id || null;
      Doc.Selection = Doc.ActiveId || "texture";
      if (Project.Camera && ["Yaw", "Pitch", "Distance"].every((K) => Number.isFinite(Project.Camera[K])) && Array.isArray(Project.Camera.Center)) Doc.Camera = Project.Camera;
      this.Documents.push(Doc);
      this.ActivateDocument(Doc);
      this.SetStatus(`Opened ${File_.name}`);
    } catch (ErrorValue) {
      console.error(ErrorValue);
      if (Doc?.Gpu) {
        this.Engine.DisposeDocument(Doc);
        Doc.Layers = [];
        this.CollectGarbage();
      }
      this.Notify(`Could not open the project: ${ErrorValue.message}`);
    } finally {
      this.Busy(null);
    }
  }

  // ---------------------------------------------------------------- export
  OpenExport() {
    const Doc = this.Doc;
    Select("#export-summary").textContent = `${Doc.Name} · ${Doc.Resolution} × ${Doc.Resolution} · ${Doc.Gpu.Mesh.Name} · ${Doc.Layers.length} layers`;
    if (!Select("#export-prefix").value || Select("#export-prefix").dataset.document !== Doc.Id) {
      Select("#export-prefix").value = Doc.Name.replace(/[^\w-]+/g, "_");
      Select("#export-prefix").dataset.document = Doc.Id;
    }
    Select("#export-progress").value = 0;
    Select("#export-dialog").showModal();
  }
  async ExportMaps() {
    const Doc = this.Doc;
    const Wanted = SelectAll("[data-export]").filter((E) => E.checked).map((E) => E.dataset.export);
    if (!Wanted.length) return this.Notify("Choose at least one map to export.");
    const Button = Select("#export-start");
    Button.disabled = true;
    const Label = Select("#export-label");
    const Progress = Select("#export-progress");
    try {
      if (Doc.Gpu.Dirty) this.Engine.Composite(Doc, null, null);
      const R = Doc.Resolution;
      const [Color, Surface, Emissive, Normal] = Doc.Gpu.Final.map((T) => this.Engine.ReadTexture(T));
      const DirectX = Select("#export-normal").value === "dx";
      const Prefix = (Select("#export-prefix").value || "texture").replace(/[^\w-]+/g, "_");
      const Files = [];
      const Byte = (V) => Math.round(Clamp(V, 0, 1) * 255);
      const Build = (Channels, Depth, Pixel) => {
        const Data = Depth === 16 ? new Uint16Array(R * R * Channels) : new Uint8Array(R * R * Channels);
        for (let Y = 0; Y < R; Y++) {
          const Source = (R - 1 - Y) * R;
          for (let X = 0; X < R; X++) Pixel(Data, (Y * R + X) * Channels, (Source + X) * 4);
        }
        return Data;
      };
      const Maps = {
        BaseColor: () => [3, 8, Build(3, 8, (D, O, S) => { D[O] = Byte(Color[S]); D[O + 1] = Byte(Color[S + 1]); D[O + 2] = Byte(Color[S + 2]); })],
        Roughness: () => [1, 8, Build(1, 8, (D, O, S) => { D[O] = Byte(Surface[S]); })],
        Metallic: () => [1, 8, Build(1, 8, (D, O, S) => { D[O] = Byte(Surface[S + 1]); })],
        Height: () => [1, 16, Build(1, 16, (D, O, S) => { D[O] = Math.round(Clamp(Surface[S + 2] * 0.5 + 0.5, 0, 1) * 65535); })],
        Normal: () => [3, 8, Build(3, 8, (D, O, S) => { D[O] = Byte(Normal[S]); D[O + 1] = Byte(DirectX ? 1 - Normal[S + 1] : Normal[S + 1]); D[O + 2] = Byte(Normal[S + 2]); })],
        Emissive: () => [3, 8, Build(3, 8, (D, O, S) => { D[O] = Byte(Emissive[S]); D[O + 1] = Byte(Emissive[S + 1]); D[O + 2] = Byte(Emissive[S + 2]); })],
        ORM: () => [3, 8, Build(3, 8, (D, O, S) => { D[O] = 255; D[O + 1] = Byte(Surface[S]); D[O + 2] = Byte(Surface[S + 1]); })],
        Mask: () => [4, 8, Build(4, 8, (D, O, S) => { D[O] = Byte(Surface[S + 1]); D[O + 1] = 255; D[O + 2] = 0; D[O + 3] = Byte(1 - Surface[S]); })],
      };
      for (let Index = 0; Index < Wanted.length; Index++) {
        const Name = Wanted[Index];
        Label.textContent = `Encoding ${Name}…`;
        Progress.value = Index / Wanted.length;
        await Wait();
        const [Channels, Depth, Data] = Maps[Name]();
        const Blob_ = await EncodePng(R, R, Channels, Depth, Data);
        Files.push({ Name: `${Prefix}_${Name === "Mask" ? "MaskMap" : Name}.png`, Data: new Uint8Array(await Blob_.arrayBuffer()) });
      }
      Progress.value = 1;
      DownloadBlob(CreateZip(Files), `${Prefix}_textures.zip`);
      Label.textContent = `Exported ${Files.length} map${Files.length === 1 ? "" : "s"} at ${R} × ${R}.`;
    } catch (ErrorValue) {
      console.error(ErrorValue);
      Label.textContent = `Export failed: ${ErrorValue.message}`;
    } finally {
      Button.disabled = false;
    }
  }
}

window.TexturePanel = new TexturePanel();
