// Parametric PBR material model shared by fill layers, decals and the brush.
// A material is two endpoint surfaces (A/B) blended by a procedural pattern,
// plus optional bitmap albedo, micro grain and emission.

export const PATTERNS = [
  { id: "none", label: "Solid" },
  { id: "noise", label: "Fractal noise" },
  { id: "grunge", label: "Grunge patches" },
  { id: "cells", label: "Voronoi cells" },
  { id: "cracks", label: "Cracks" },
  { id: "brushed", label: "Brushed streaks" },
  { id: "scratches", label: "Scratches" },
  { id: "stripes", label: "Stripes" },
  { id: "checker", label: "Checker" },
  { id: "bricks", label: "Bricks" },
  { id: "hex", label: "Hex tiles" },
  { id: "wood", label: "Wood rings" },
  { id: "weave", label: "Carbon weave" },
  { id: "fabric", label: "Fabric" },
  { id: "dots", label: "Dots" },
];
export const PatternIndex = (Id) =>
  Math.max(0, PATTERNS.findIndex((Pattern) => Pattern.id === Id));

export const MASK_GENERATORS = [
  { id: "none", label: "None" },
  { id: "noise", label: "Noise" },
  { id: "gradient", label: "Height gradient" },
  { id: "facing", label: "Facing up (dust)" },
  { id: "convex", label: "Convex edges (wear)" },
  { id: "concave", label: "Cavities (dirt)" },
];
export const MaskGeneratorIndex = (Id) =>
  Math.max(0, MASK_GENERATORS.findIndex((Generator) => Generator.id === Id));

export const BLEND_MODES = [
  { id: "normal", label: "Normal" },
  { id: "multiply", label: "Multiply" },
  { id: "screen", label: "Screen" },
  { id: "overlay", label: "Overlay" },
  { id: "softlight", label: "Soft light" },
  { id: "add", label: "Linear dodge" },
  { id: "subtract", label: "Subtract" },
  { id: "darken", label: "Darken" },
  { id: "lighten", label: "Lighten" },
  { id: "color", label: "Tint (hue)" },
];
export const HEIGHT_BLENDS = [
  { id: "normal", label: "Replace" },
  { id: "add", label: "Add" },
  { id: "subtract", label: "Subtract" },
  { id: "max", label: "Max" },
  { id: "min", label: "Min" },
];
export const BlendIndex = (List, Id) =>
  Math.max(0, List.findIndex((Item) => Item.id === Id));

export const CHANNELS = [
  { id: "color", label: "Base color", short: "COL" },
  { id: "roughness", label: "Roughness", short: "RGH" },
  { id: "metallic", label: "Metallic", short: "MTL" },
  { id: "height", label: "Height", short: "HGT" },
  { id: "emissive", label: "Emissive", short: "EMI" },
];
export const AllChannels = (Value = true) =>
  Object.fromEntries(CHANNELS.map((Channel) => [Channel.id, Value]));

export const DEFAULT_MATERIAL = Object.freeze({
  Color: "#b8b8b8",
  Color2: "#8c8c8c",
  Roughness: 0.5,
  Roughness2: 0.6,
  Metallic: 0,
  Metallic2: 0,
  Height: 0,
  HeightAmount: 0,
  Emissive: "#000000",
  EmissiveStrength: 0,
  EmissiveFromPattern: false,
  Pattern: "none",
  Scale: 4,
  Rotation: 0,
  Contrast: 1,
  Balance: 0,
  Warp: 0,
  Seed: 1,
  Grain: 0,
  Mapping: "triplanar",
  Bitmap: null,
  BitmapScale: 1,
  BitmapHeight: 0,
});

export const DEFAULT_MASK = Object.freeze({
  Enabled: true,
  Painted: false,
  Generator: "none",
  Scale: 4,
  Contrast: 2,
  Offset: 0,
  Invert: false,
  Seed: 3,
  Breakup: 0.4,
});

const Ranges = {
  Roughness: [0, 1], Roughness2: [0, 1], Metallic: [0, 1], Metallic2: [0, 1],
  Height: [-1, 1], HeightAmount: [-1, 1], EmissiveStrength: [0, 20],
  Scale: [0.05, 128], Rotation: [-180, 180], Contrast: [0, 16], Balance: [-1, 1],
  Warp: [0, 4], Seed: [0, 999], Grain: [0, 1], BitmapScale: [0.05, 64], BitmapHeight: [-1, 1],
};
const IsHex = (Value) => typeof Value === "string" && /^#[0-9a-f]{6}$/i.test(Value);

export function SanitizeMaterial(Input = {}) {
  const Output = { ...DEFAULT_MATERIAL };
  for (const Key of Object.keys(DEFAULT_MATERIAL)) {
    const Value = Input[Key];
    if (Value === undefined) continue;
    if (Key in Ranges) {
      const Number_ = Number(Value);
      if (Number.isFinite(Number_))
        Output[Key] = Math.min(Ranges[Key][1], Math.max(Ranges[Key][0], Number_));
    } else if (["Color", "Color2", "Emissive"].includes(Key)) {
      if (IsHex(Value)) Output[Key] = Value.toLowerCase();
    } else if (Key === "Pattern") {
      if (PATTERNS.some((Pattern) => Pattern.id === Value)) Output.Pattern = Value;
    } else if (Key === "Mapping") {
      Output.Mapping = Value === "uv" ? "uv" : "triplanar";
    } else if (Key === "EmissiveFromPattern") Output[Key] = Boolean(Value);
    else if (Key === "Bitmap")
      Output.Bitmap = typeof Value === "string" && Value.startsWith("data:image/") ? Value : null;
  }
  return Output;
}

export function SanitizeMask(Input = {}) {
  const Output = { ...DEFAULT_MASK };
  if (Input.Enabled !== undefined) Output.Enabled = Boolean(Input.Enabled);
  if (Input.Painted !== undefined) Output.Painted = Boolean(Input.Painted);
  if (MASK_GENERATORS.some((G) => G.id === Input.Generator)) Output.Generator = Input.Generator;
  for (const [Key, Low, High] of [["Scale", 0.1, 64], ["Contrast", 0.1, 32], ["Offset", -1, 1], ["Seed", 0, 999], ["Breakup", 0, 1.5]])
    if (Number.isFinite(Number(Input[Key]))) Output[Key] = Math.min(High, Math.max(Low, Number(Input[Key])));
  if (Input.Invert !== undefined) Output.Invert = Boolean(Input.Invert);
  return Output;
}

const M = (Overrides) => SanitizeMaterial(Overrides);

// Category: metal · surface · effect · smart. Smart materials expand into a
// small stack of masked fill layers.
export const MATERIAL_PRESETS = [
  { id: "steel", name: "Brushed steel", category: "metal", description: "Directional streaks · anisotropic feel", glyph: "metal",
    material: M({ Color: "#c5c8cc", Color2: "#9fa3a8", Metallic: 1, Metallic2: 1, Roughness: 0.28, Roughness2: 0.42, Pattern: "brushed", Scale: 3, Contrast: 1.4, Grain: 0.12 }) },
  { id: "chrome", name: "Polished chrome", category: "metal", description: "Mirror metal · near-zero roughness", glyph: "metal",
    material: M({ Color: "#e9ebee", Color2: "#e2e4e8", Metallic: 1, Metallic2: 1, Roughness: 0.04, Roughness2: 0.08, Pattern: "noise", Scale: 6, Contrast: 0.6 }) },
  { id: "gold", name: "Gold", category: "metal", description: "Warm polished precious metal", glyph: "metal",
    material: M({ Color: "#ffcf6e", Color2: "#f1b74f", Metallic: 1, Metallic2: 1, Roughness: 0.18, Roughness2: 0.3, Pattern: "noise", Scale: 5, Grain: 0.06 }) },
  { id: "copper", name: "Aged copper", category: "metal", description: "Copper with oxidised verdigris", glyph: "metal",
    material: M({ Color: "#d98a5f", Color2: "#5fa38d", Metallic: 1, Metallic2: 0.1, Roughness: 0.3, Roughness2: 0.8, Pattern: "grunge", Scale: 2.5, Contrast: 2.4, Balance: -0.25, HeightAmount: 0.05 }) },
  { id: "aluminium", name: "Anodised aluminium", category: "metal", description: "Satin coloured metal finish", glyph: "metal",
    material: M({ Color: "#4f7bb8", Color2: "#456da6", Metallic: 1, Metallic2: 1, Roughness: 0.36, Roughness2: 0.44, Pattern: "noise", Scale: 12, Grain: 0.18 }) },
  { id: "rust", name: "Heavy rust", category: "metal", description: "Corroded iron · flaking height", glyph: "metal",
    material: M({ Color: "#7a3d1f", Color2: "#3b2416", Metallic: 0.15, Metallic2: 0, Roughness: 0.82, Roughness2: 0.95, Pattern: "grunge", Scale: 3, Contrast: 1.8, HeightAmount: 0.18, Warp: 0.6 }) },
  { id: "plastic", name: "Matte plastic", category: "surface", description: "Soft dielectric · fine grain", glyph: "sphere",
    material: M({ Color: "#e4e4e0", Color2: "#d8d8d4", Roughness: 0.62, Roughness2: 0.7, Pattern: "noise", Scale: 30, Grain: 0.2 }) },
  { id: "gloss", name: "Gloss lacquer", category: "surface", description: "Deep clear coat red", glyph: "sphere",
    material: M({ Color: "#b3121a", Color2: "#a50f17", Roughness: 0.12, Roughness2: 0.16, Pattern: "noise", Scale: 8 }) },
  { id: "rubber", name: "Rubber", category: "surface", description: "Dark, diffuse, slightly dusty", glyph: "sphere",
    material: M({ Color: "#222222", Color2: "#2c2c2c", Roughness: 0.86, Roughness2: 0.94, Pattern: "noise", Scale: 18, Grain: 0.3 }) },
  { id: "wood", name: "Oak wood", category: "surface", description: "Growth rings with grain height", glyph: "wood",
    material: M({ Color: "#a8743f", Color2: "#6a4321", Roughness: 0.55, Roughness2: 0.72, Pattern: "wood", Scale: 2, Contrast: 1.4, Warp: 0.8, HeightAmount: 0.06 }) },
  { id: "concrete", name: "Concrete", category: "surface", description: "Porous grey with pits", glyph: "box",
    material: M({ Color: "#9a9893", Color2: "#6f6d69", Roughness: 0.85, Roughness2: 0.95, Pattern: "grunge", Scale: 6, Contrast: 1.2, HeightAmount: 0.08, Grain: 0.35 }) },
  { id: "leather", name: "Leather", category: "surface", description: "Pebbled hide · soft sheen", glyph: "cells",
    material: M({ Color: "#5a3424", Color2: "#3f2219", Roughness: 0.48, Roughness2: 0.7, Pattern: "cells", Scale: 40, Contrast: 1.6, HeightAmount: 0.06 }) },
  { id: "carbon", name: "Carbon fibre", category: "surface", description: "Twill weave under clear coat", glyph: "grid",
    material: M({ Color: "#2a2b2e", Color2: "#0f1012", Metallic: 0.2, Metallic2: 0.1, Roughness: 0.22, Roughness2: 0.32, Pattern: "weave", Scale: 24, Mapping: "uv", HeightAmount: 0.03 }) },
  { id: "tiles", name: "Ceramic tiles", category: "surface", description: "Glazed bricks with grout", glyph: "wall",
    material: M({ Color: "#e8e3d8", Color2: "#5e5a55", Roughness: 0.15, Roughness2: 0.9, Pattern: "bricks", Scale: 6, Contrast: 3, HeightAmount: -0.12 }) },
  { id: "fabric", name: "Canvas fabric", category: "surface", description: "Woven threads · fuzzy", glyph: "grid",
    material: M({ Color: "#c9b48d", Color2: "#a48e68", Roughness: 0.9, Roughness2: 0.98, Pattern: "fabric", Scale: 60, Mapping: "uv", HeightAmount: 0.04 }) },
  { id: "hexgrid", name: "Sci-fi hex panel", category: "effect", description: "Emissive hex seams", glyph: "hex",
    material: M({ Color: "#2a2f36", Color2: "#0d1014", Metallic: 0.8, Metallic2: 0.6, Roughness: 0.35, Roughness2: 0.6, Pattern: "hex", Scale: 10, Contrast: 3, HeightAmount: -0.08, Emissive: "#2fd2ff", EmissiveStrength: 3, EmissiveFromPattern: true, Balance: -0.3 }) },
  { id: "neon", name: "Neon glow", category: "effect", description: "Flat emissive paint", glyph: "sun",
    material: M({ Color: "#ff3c8e", Color2: "#ff3c8e", Roughness: 0.4, Roughness2: 0.4, Emissive: "#ff3c8e", EmissiveStrength: 4 }) },
  { id: "dust", name: "Settled dust", category: "effect", description: "Pale grime on up-facing areas", glyph: "smoke",
    material: M({ Color: "#b5a68c", Color2: "#9a8b73", Roughness: 0.95, Roughness2: 1, Pattern: "noise", Scale: 14, Grain: 0.2 }),
    mask: { Generator: "facing", Contrast: 3, Offset: -0.1 }, channels: { color: true, roughness: true, metallic: true, height: false, emissive: false } },
  { id: "dirt", name: "Cavity dirt", category: "effect", description: "Grime collected in crevices", glyph: "smoke",
    material: M({ Color: "#3a2c1f", Color2: "#2a2017", Roughness: 0.9, Roughness2: 0.95, Pattern: "noise", Scale: 10 }),
    mask: { Generator: "concave", Contrast: 4, Offset: 0 }, channels: { color: true, roughness: true, metallic: true, height: false, emissive: false } },
  { id: "wornpaint", name: "Worn painted steel", category: "smart", description: "Steel base · chipped paint edges", glyph: "layers",
    layers: [
      { Name: "Steel base", Material: M({ Color: "#a9adb2", Color2: "#8e9196", Metallic: 1, Metallic2: 1, Roughness: 0.32, Roughness2: 0.5, Pattern: "scratches", Scale: 5, Contrast: 1.2, Grain: 0.15 }) },
      { Name: "Paint coat", Material: M({ Color: "#d9822b", Color2: "#c87322", Roughness: 0.45, Roughness2: 0.55, Pattern: "noise", Scale: 20, Height: 0.04 }),
        Mask: { Generator: "convex", Contrast: 5, Offset: 0.05, Invert: true }, HeightBlend: "add" },
      { Name: "Grime", Material: M({ Color: "#2f2620", Color2: "#211b16", Roughness: 0.9, Roughness2: 0.95 }),
        Mask: { Generator: "concave", Contrast: 3 }, Opacity: 0.7, Channels: { color: true, roughness: true, metallic: true, height: false, emissive: false } },
    ] },
  { id: "rustedpanel", name: "Rusted iron panel", category: "smart", description: "Iron with rust blooming from below", glyph: "layers",
    layers: [
      { Name: "Iron", Material: M({ Color: "#6d6e70", Color2: "#58595b", Metallic: 1, Metallic2: 0.9, Roughness: 0.45, Roughness2: 0.6, Pattern: "noise", Scale: 8, Grain: 0.2 }) },
      { Name: "Rust bloom", Material: M({ Color: "#8a4320", Color2: "#4a2614", Metallic: 0.1, Metallic2: 0, Roughness: 0.85, Roughness2: 0.95, Pattern: "grunge", Scale: 4, Contrast: 1.6, HeightAmount: 0.12, Warp: 0.5 }),
        Mask: { Generator: "gradient", Contrast: 3, Offset: 0.25, Invert: true }, HeightBlend: "add" },
      { Name: "Edge wear", Material: M({ Color: "#c4c6c8", Color2: "#b0b2b5", Metallic: 1, Metallic2: 1, Roughness: 0.22, Roughness2: 0.3 }),
        Mask: { Generator: "convex", Contrast: 6, Offset: -0.1 }, Channels: { color: true, roughness: true, metallic: true, height: false, emissive: false } },
    ] },
  { id: "dustyplastic", name: "Dusty toy plastic", category: "smart", description: "Glossy plastic with dust caps", glyph: "layers",
    layers: [
      { Name: "Toy plastic", Material: M({ Color: "#2d7be0", Color2: "#2a73d3", Roughness: 0.25, Roughness2: 0.35, Pattern: "noise", Scale: 24, Grain: 0.08 }) },
      { Name: "Dust", Material: M({ Color: "#c8bca4", Color2: "#b1a68f", Roughness: 0.95, Roughness2: 1, Pattern: "noise", Scale: 18 }),
        Mask: { Generator: "facing", Contrast: 2.5, Offset: -0.2 }, Opacity: 0.85, Channels: { color: true, roughness: true, metallic: true, height: false, emissive: false } },
    ] },
];

export const MATERIAL_FILTERS = [
  { id: "all", label: "All" },
  { id: "metal", label: "Metal" },
  { id: "surface", label: "Surface" },
  { id: "effect", label: "Effect" },
  { id: "smart", label: "Smart" },
];
