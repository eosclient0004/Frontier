// Texture set documents, layer records and undo history.
//
// Layer metadata is plain data; GPU resources live in Layer.Gpu and are shared
// by reference between the document and history snapshots. Texture history
// entries swap whole textures, so an undo never copies pixels.

import {
  SanitizeMaterial,
  SanitizeMask,
  AllChannels,
  BLEND_MODES,
  HEIGHT_BLENDS,
  DEFAULT_MATERIAL,
} from "./MaterialSpecification.js";
import { SanitizeDecal } from "./DecalSources.js";

let NextId = 1;
export const NewId = (Prefix) => `${Prefix}${(NextId++).toString(36)}${Math.random().toString(36).slice(2, 6)}`;

export const LAYER_TYPES = {
  paint: { label: "Paint layer", badge: "PAINT", glyph: "brush" },
  fill: { label: "Fill layer", badge: "FILL", glyph: "bucket" },
  decal: { label: "Decal", badge: "DECAL", glyph: "shape" },
};

export function CreateLayer(Type, Options = {}) {
  const Layer = {
    Id: NewId("L"),
    Name: Options.Name || LAYER_TYPES[Type].label,
    Type,
    Visible: Options.Visible ?? true,
    Opacity: Options.Opacity ?? 1,
    Blend: Options.Blend || "normal",
    HeightBlend: Options.HeightBlend || (Type === "fill" ? "normal" : "add"),
    Channels: { ...AllChannels(true), ...(Options.Channels || {}) },
    Material: SanitizeMaterial(
      Options.Material ||
        (Type === "decal"
          ? { ...DEFAULT_MATERIAL, Color: "#ffffff", Roughness: 0.35, Roughness2: 0.35, Height: 0.04 }
          : DEFAULT_MATERIAL),
    ),
    Mask: SanitizeMask(Options.Mask || {}),
    Decal: Type === "decal" ? SanitizeDecal(Options.Decal || {}) : null,
    Revision: 0,
    Gpu: { Textures: null, Mask: null },
  };
  if (Type === "decal" && !Options.Channels) Layer.Channels.height = true;
  return Layer;
}

// Metadata clone without runtime GPU/decal fields; textures stay shared.
export function CloneLayer(Layer, KeepGpu = true) {
  const { Gpu, DecalEntry, ...Meta } = Layer;
  const Clone = structuredClone(Meta);
  Clone.Gpu = KeepGpu
    ? { Textures: Gpu.Textures ? [...Gpu.Textures] : null, Mask: Gpu.Mask }
    : { Textures: null, Mask: null };
  return Clone;
}

export function SerializeLayer(Layer) {
  const { Gpu, DecalEntry, ...Meta } = Layer;
  return structuredClone(Meta);
}

export function ValidateLayer(Input) {
  if (!Input || !LAYER_TYPES[Input.Type]) throw new Error("Project contains an unknown layer type.");
  const Layer = CreateLayer(Input.Type, {
    Name: typeof Input.Name === "string" ? Input.Name.slice(0, 64) : undefined,
    Visible: Input.Visible !== false,
    Opacity: Math.min(1, Math.max(0, Number(Input.Opacity ?? 1))),
    Blend: BLEND_MODES.some((B) => B.id === Input.Blend) ? Input.Blend : "normal",
    HeightBlend: HEIGHT_BLENDS.some((B) => B.id === Input.HeightBlend) ? Input.HeightBlend : undefined,
    Channels: Object.fromEntries(Object.entries(Input.Channels || {}).map(([K, V]) => [K, Boolean(V)])),
    Material: Input.Material,
    Mask: Input.Mask,
    Decal: Input.Decal,
  });
  return Layer;
}

export const DEFAULT_BASE = Object.freeze({ Color: "#7f7f7f", Roughness: 0.55, Metallic: 0, Height: 0 });

export function CreateDocumentRecord(Options = {}) {
  return {
    Id: NewId("D"),
    Name: Options.Name || "Untitled set",
    Resolution: Options.Resolution || 1024,
    MeshKey: Options.MeshKey || "cube",
    CustomMesh: null,
    CustomObj: null,
    NormalStrength: Options.NormalStrength ?? 1,
    Environment: { Preset: "studio", Rotation: 0, Exposure: 1, Show: false, ...(Options.Environment || {}) },
    Base: { ...DEFAULT_BASE, ...(Options.Base || {}) },
    BaseRevision: 0,
    Layers: [],
    ActiveId: null,
    Selection: "texture",
    Camera: null,
    Dirty: false,
    History: new History(),
    Gpu: null,
  };
}

const TextureBytes = (Texture) => (Texture ? Texture.Bytes || 0 : 0);

export class History {
  constructor() {
    this.Undo = [];
    this.Redo = [];
    this.LastKey = null;
    this.LastTime = 0;
    this.Budget = 640 * 1024 * 1024;
    this.Limit = 80;
  }
  static Snapshot(Doc) {
    return {
      Layers: Doc.Layers.map((Layer) => CloneLayer(Layer)),
      ActiveId: Doc.ActiveId,
      Selection: Doc.Selection,
      Base: { ...Doc.Base },
      NormalStrength: Doc.NormalStrength,
      Environment: { ...Doc.Environment },
    };
  }
  static Restore(Doc, Snapshot) {
    const Current = History.Snapshot(Doc);
    Doc.Layers = Snapshot.Layers.map((Layer) => {
      const Clone = CloneLayer(Layer);
      Clone.Revision = (Clone.Revision || 0) + 1;
      return Clone;
    });
    Doc.ActiveId = Snapshot.ActiveId;
    Doc.Selection = Snapshot.Selection;
    Doc.Base = { ...Snapshot.Base };
    Doc.BaseRevision++;
    Doc.NormalStrength = Snapshot.NormalStrength;
    Doc.Environment = { ...Snapshot.Environment };
    return Current;
  }
  // Records the document state before a structural or property change.
  // Repeated edits to the same key within a short window collapse into one.
  Checkpoint(Doc, Label, Key = null) {
    const Now = performance.now();
    if (Key && Key === this.LastKey && Now - this.LastTime < 1500 && !this.Redo.length && this.Undo.length) {
      this.LastTime = Now;
      return false;
    }
    this.Push({ Kind: "state", Label, Snapshot: History.Snapshot(Doc) });
    this.LastKey = Key;
    this.LastTime = Now;
    return true;
  }
  PushTexture(LayerId, Replaced, Label) {
    this.Push({ Kind: "texture", Label, LayerId, Textures: Replaced.Textures || null, Mask: Replaced.Mask || null });
    this.LastKey = null;
  }
  Push(Entry) {
    this.Undo.push(Entry);
    this.Redo = [];
    this.Trim();
  }
  EntryBytes(Entry) {
    if (Entry.Kind === "texture")
      return Object.values(Entry.Textures || {}).reduce((S, T) => S + TextureBytes(T), 0) + TextureBytes(Entry.Mask);
    return 0;
  }
  Bytes() {
    return [...this.Undo, ...this.Redo].reduce((S, E) => S + this.EntryBytes(E), 0);
  }
  Trim() {
    while (this.Undo.length > this.Limit) this.Undo.shift();
    while (this.Undo.length > 2 && this.Bytes() > this.Budget) this.Undo.shift();
  }
  Apply(Doc, Entry) {
    if (Entry.Kind === "state") {
      Entry.Snapshot = History.Restore(Doc, Entry.Snapshot);
      return true;
    }
    const Layer = Doc.Layers.find((L) => L.Id === Entry.LayerId);
    if (!Layer) return false;
    if (Entry.Textures && Layer.Gpu.Textures) {
      for (const [Index, Texture] of Object.entries(Entry.Textures)) {
        const Current = Layer.Gpu.Textures[Index];
        Layer.Gpu.Textures[Index] = Texture;
        Entry.Textures[Index] = Current;
      }
    }
    if (Entry.Mask) {
      const Current = Layer.Gpu.Mask;
      Layer.Gpu.Mask = Entry.Mask;
      Entry.Mask = Current;
    }
    Layer.Revision++;
    return true;
  }
  StepBack(Doc) {
    const Entry = this.Undo.pop();
    if (!Entry) return null;
    this.Apply(Doc, Entry);
    this.Redo.push(Entry);
    this.LastKey = null;
    return Entry;
  }
  StepForward(Doc) {
    const Entry = this.Redo.pop();
    if (!Entry) return null;
    this.Apply(Doc, Entry);
    this.Undo.push(Entry);
    this.LastKey = null;
    return Entry;
  }
  Clear() {
    this.Undo = [];
    this.Redo = [];
    this.LastKey = null;
  }
  // Every texture referenced by history, for garbage collection.
  Textures(Set_) {
    const Visit = (Entry) => {
      if (Entry.Kind === "texture") {
        Object.values(Entry.Textures || {}).forEach((T) => T && Set_.add(T));
        if (Entry.Mask) Set_.add(Entry.Mask);
      } else
        for (const Layer of Entry.Snapshot.Layers) {
          Layer.Gpu.Textures?.forEach((T) => Set_.add(T));
          if (Layer.Gpu.Mask) Set_.add(Layer.Gpu.Mask);
        }
    };
    this.Undo.forEach(Visit);
    this.Redo.forEach(Visit);
  }
}
