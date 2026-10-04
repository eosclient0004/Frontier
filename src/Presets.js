/* Frontier Texture Paint — brush, material and decal libraries. */

export const LAYER_KINDS = {
  paint: { label: "Paint layer", icon: "brush", badge: "PNT" },
  fill: { label: "Fill layer", icon: "fill-layer", badge: "FIL" },
  svg: { label: "SVG decal", icon: "svg", badge: "SVG" },
  text: { label: "Text decal", icon: "text", badge: "TXT" },
  image: { label: "Image layer", icon: "image", badge: "IMG" },
  adjust: { label: "Adjustment", icon: "adjust", badge: "ADJ" },
};

export const BLEND_MODES = [
  ["normal", "Normal"],
  ["multiply", "Multiply"],
  ["screen", "Screen"],
  ["overlay", "Overlay"],
  ["darken", "Darken"],
  ["lighten", "Lighten"],
  ["color-dodge", "Color dodge"],
  ["color-burn", "Color burn"],
  ["hard-light", "Hard light"],
  ["soft-light", "Soft light"],
  ["difference", "Difference"],
  ["exclusion", "Exclusion"],
  ["hue", "Hue"],
  ["saturation", "Saturation"],
  ["color", "Color"],
  ["luminosity", "Luminosity"],
  ["add", "Linear add"],
  ["erase", "Erase"],
];

/** Canvas composite op per blend id (erase handled specially). */
export function BlendOp(Id) {
  if (Id === "add") return "lighter";
  if (Id === "erase") return "destination-out";
  if (Id === "normal") return "source-over";
  return Id;
}

export const CHANNELS = [
  { id: "composite", label: "Composite", hint: "LIT PREVIEW" },
  { id: "albedo", label: "Albedo", hint: "BASE COLOR" },
  { id: "metallic", label: "Metallic", hint: "0 ··· 1" },
  { id: "roughness", label: "Roughness", hint: "0 ··· 1" },
  { id: "emissive", label: "Emissive", hint: "EMISSION" },
  { id: "height", label: "Height", hint: "DISPLACE" },
  { id: "normal", label: "Normal", hint: "OPENGL" },
  { id: "mask", label: "Mask", hint: "ACTIVE" },
];

export const BRUSH_PRESETS = [
  { id: "soft", name: "Soft round", desc: "Airbrush falloff · general paint", tool: "paint", size: 64, hardness: 0.15, opacity: 1, flow: 0.55, spacing: 0.08, roundness: 1, angle: 0, scatter: 0, smoothing: 0.45, tip: "round" },
  { id: "hard", name: "Hard round", desc: "Crisp edge · decals & masks", tool: "paint", size: 32, hardness: 0.92, opacity: 1, flow: 1, spacing: 0.12, roundness: 1, angle: 0, scatter: 0, smoothing: 0.25, tip: "round" },
  { id: "airbrush", name: "Airbrush", desc: "Low flow buildup · shading", tool: "paint", size: 120, hardness: 0.05, opacity: 0.6, flow: 0.18, spacing: 0.05, roundness: 1, angle: 0, scatter: 0, smoothing: 0.6, tip: "round" },
  { id: "chalk", name: "Chalk grain", desc: "Textured scatter · wear", tool: "paint", size: 72, hardness: 0.55, opacity: 0.9, flow: 0.7, spacing: 0.14, roundness: 0.9, angle: 0, scatter: 0.45, smoothing: 0.2, tip: "chalk" },
  { id: "flat", name: "Flat chip", desc: "Angled flat · edge wear", tool: "paint", size: 48, hardness: 0.8, opacity: 1, flow: 0.9, spacing: 0.16, roundness: 0.35, angle: 35, scatter: 0.1, smoothing: 0.15, tip: "square" },
  { id: "grain-eraser", name: "Grain eraser", desc: "Textured lift · distress", tool: "eraser", size: 72, hardness: 0.5, opacity: 1, flow: 0.8, spacing: 0.14, roundness: 1, angle: 0, scatter: 0.4, smoothing: 0.2, tip: "chalk" },
  { id: "soft-eraser", name: "Soft eraser", desc: "Feathered removal", tool: "eraser", size: 80, hardness: 0.2, opacity: 1, flow: 0.7, spacing: 0.08, roundness: 1, angle: 0, scatter: 0, smoothing: 0.4, tip: "round" },
  { id: "smudge-finger", name: "Finger smudge", desc: "Drag & blend pixels", tool: "smudge", size: 56, hardness: 0.4, opacity: 1, flow: 0.6, spacing: 0.1, roundness: 1, angle: 0, scatter: 0, smoothing: 0.5, tip: "round", smudge: 0.65 },
  { id: "mottler", name: "Mottle", desc: "Blotchy scatter · grunge", tool: "paint", size: 140, hardness: 0.35, opacity: 0.7, flow: 0.5, spacing: 0.22, roundness: 0.85, angle: 0, scatter: 0.7, smoothing: 0.1, tip: "chalk" },
  { id: "liner", name: "Liner", desc: "Fine stabilized line", tool: "paint", size: 8, hardness: 0.85, opacity: 1, flow: 1, spacing: 0.1, roundness: 1, angle: 0, scatter: 0, smoothing: 0.75, tip: "round" },
];

export const MATERIAL_PRESETS = [
  { id: "matte-plastic", name: "Matte plastic", desc: "Dielectric · soft sheen", color: "#3a3d42", metallic: 0, roughness: 0.82, emissive: 0, emissiveColor: "#000000", height: 0.5 },
  { id: "brushed-steel", name: "Brushed steel", desc: "Metal · satin grain", color: "#9aa0a6", metallic: 1, roughness: 0.42, emissive: 0, emissiveColor: "#000000", height: 0.5 },
  { id: "polished-gold", name: "Polished gold", desc: "Metal · mirror", color: "#d8a94e", metallic: 1, roughness: 0.16, emissive: 0, emissiveColor: "#000000", height: 0.5 },
  { id: "aged-copper", name: "Aged copper", desc: "Metal · worn patina", color: "#8a5a3b", metallic: 0.9, roughness: 0.55, emissive: 0, emissiveColor: "#000000", height: 0.52 },
  { id: "rubber", name: "Rubber", desc: "Dielectric · dead flat", color: "#1d1d1f", metallic: 0, roughness: 0.96, emissive: 0, emissiveColor: "#000000", height: 0.5 },
  { id: "ceramic", name: "Glazed ceramic", desc: "Dielectric · gloss", color: "#e8e4dc", metallic: 0, roughness: 0.24, emissive: 0, emissiveColor: "#000000", height: 0.5 },
  { id: "painted-metal", name: "Painted metal", desc: "Coated · eggshell", color: "#33526b", metallic: 0.15, roughness: 0.5, emissive: 0, emissiveColor: "#000000", height: 0.5 },
  { id: "neon", name: "Neon tube", desc: "Emissive · cyan glow", color: "#0b2b30", metallic: 0, roughness: 0.4, emissive: 2.2, emissiveColor: "#46f0ff", height: 0.5 },
  { id: "ember", name: "Ember", desc: "Emissive · hot orange", color: "#2b0f08", metallic: 0, roughness: 0.6, emissive: 1.6, emissiveColor: "#ff6a2a", height: 0.55 },
  { id: "carbon", name: "Carbon weave", desc: "Dark dielectric · semi-gloss", color: "#17181a", metallic: 0.25, roughness: 0.38, emissive: 0, emissiveColor: "#000000", height: 0.5 },
];

export const SVG_PRESETS = [
  {
    id: "hazard-stripes", name: "Hazard stripes", desc: "Diagonal warning band",
    svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 128"><rect width="512" height="128" fill="#151310"/><g fill="#e8b53a"><path d="M-40 128 88 0h64L24 128Z"/><path d="M120 128 248 0h64L184 128Z"/><path d="M280 128 408 0h64L344 128Z"/><path d="M440 128 568 0h64L504 128Z"/></g></svg>`,
  },
  {
    id: "roundel", name: "Roundel badge", desc: "Ring + star insignia",
    svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256"><circle cx="128" cy="128" r="112" fill="none" stroke="#e8e6e0" stroke-width="14"/><circle cx="128" cy="128" r="78" fill="#e8e6e0"/><path d="M128 62l20 44 47 4-35 31 11 46-43-25-43 25 11-46-35-31 47-4Z" fill="#1b1b1b"/></svg>`,
  },
  {
    id: "arrow", name: "Stencil arrow", desc: "Direction chevron",
    svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 128"><path d="M8 44h150V16l90 48-90 48V84H8Z" fill="#e8e6e0"/></svg>`,
  },
  {
    id: "warning-tri", name: "Warning triangle", desc: "High-voltage mark",
    svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 224"><path d="M128 8 248 216H8Z" fill="#e8b53a"/><path d="M128 44 216 192H40Z" fill="#151310"/><path d="M138 84l-34 62h22l-8 34 36-66h-22Z" fill="#e8b53a"/></svg>`,
  },
  {
    id: "grunge-ring", name: "Grunge ring", desc: "Distressed circle",
    svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256"><g fill="none" stroke="#e8e6e0" stroke-width="18" stroke-dasharray="46 18 90 26 30 40" stroke-linecap="butt"><circle cx="128" cy="128" r="100"/></g><g fill="#e8e6e0"><circle cx="52" cy="70" r="5"/><circle cx="204" cy="52" r="3"/><circle cx="218" cy="188" r="6"/><circle cx="60" cy="200" r="4"/></g></svg>`,
  },
  {
    id: "crosshair", name: "Crosshair", desc: "Optic reticle",
    svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" fill="none" stroke="#e8e6e0"><circle cx="128" cy="128" r="86" stroke-width="8"/><circle cx="128" cy="128" r="6" fill="#e8e6e0"/><path d="M128 8v52M128 196v52M8 128h52M196 128h52" stroke-width="8"/></svg>`,
  },
];

export const TEXT_PRESETS = [
  { id: "stencil-07", name: "Stencil 07", desc: "Bold industrial numeral", text: "07", font: "Impact, 'Arial Black', sans-serif", size: 0.42, weight: 900, italic: false, align: "center", color: "#ece9e2", stroke: 0, strokeColor: "#111111", spacing: 0.02 },
  { id: "caution", name: "Caution band", desc: "Spaced cap label", text: "CAUTION", font: "'DM Sans', Arial, sans-serif", size: 0.16, weight: 500, italic: false, align: "center", color: "#e8b53a", stroke: 0, strokeColor: "#111111", spacing: 0.32 },
  { id: "serial", name: "Serial plate", desc: "Monospace identifier", text: "FRN-0042-X", font: "'JetBrains Mono', monospace", size: 0.11, weight: 400, italic: false, align: "center", color: "#d7d7d7", stroke: 0, strokeColor: "#111111", spacing: 0.12 },
  { id: "est", name: "Est. mark", desc: "Small caps footer", text: "EST. 2026", font: "Georgia, serif", size: 0.09, weight: 400, italic: true, align: "center", color: "#b9b4a8", stroke: 0, strokeColor: "#111111", spacing: 0.2 },
];

export const FONT_STACKS = [
  ["system", "System sans"],
  ["grotesk", "Grotesk / DM Sans"],
  ["industrial", "Industrial / Impact"],
  ["mono", "Monospace"],
  ["serif", "Serif"],
];

export function FontFamily(Key) {
  switch (Key) {
    case "grotesk": return "'DM Sans', 'Helvetica Neue', Arial, sans-serif";
    case "industrial": return "Impact, 'Arial Black', 'Helvetica Neue', sans-serif";
    case "mono": return "'JetBrains Mono', ui-monospace, Menlo, monospace";
    case "serif": return "Georgia, 'Times New Roman', serif";
    default: return "-apple-system, 'Segoe UI', Arial, sans-serif";
  }
}

export const SWATCHES = [
  "#ece9e2", "#c9c9c9", "#8a8a8a", "#4a4a4a",
  "#1b1b1b", "#e8b53a", "#ff6a2a", "#c23b2e",
  "#7bc47f", "#34c759", "#46c8f0", "#5aa9ff",
  "#b49aff", "#e08bc0", "#8a5a3b", "#d8a94e",
];

/** Stamp tip identifiers exposed to the brush engine. */
export const TIP_LABELS = {
  round: "Soft round",
  square: "Flat square",
  chalk: "Chalk grain",
};
