# Frontier — Texture Paint Studio

An experimental, browser-based **texture painting** tool built on the visual
language of the Slate *Fluid* prototype (Project Zero look: near-black surfaces,
DM Sans, pill controls, large rounded panels). It is purpose-built for painting
rather than simulation and ships with the three systems called for:

- **Layer stack** — paint layers with per-layer opacity, blend mode
  (Normal / Multiply / Screen / Overlay / Lighten / Darken / Add / Difference),
  visibility and full **transform** (position, scale, rotation) so decals can be
  moved, scaled and rotated.
- **Robust material system** — a PBR material composed of six maps
  (Base Color, Roughness, Metallic, Normal, Height, Emissive). Pick a map in the
  left outliner to paint into it; each map has its own layer stack. Toggle maps
  on/off, switch resolution (512 / 1024 / 2048), set tiling, preview a live
  **shaded PBR** result, and export every map as a PNG or bundle the whole
  project to a `.texture.json`.
- **Decals — SVG & text** — import an `.svg` file, drop a built-in vector decal
  from the library, or type a text decal (font / weight / size / color). Use the
  Text or SVG tool in the viewport to place a decal exactly where you click, then
  nudge it from the layer transform controls.

## Run

```bash
npm install
npm run dev      # http://localhost:5173
```

## Controls

| Key | Action | Key | Action |
|-----|--------|-----|--------|
| `B` | Brush | `E` | Eraser |
| `G` | Fill (flood) | `I` | Eyedropper |
| `T` | Text decal | `S` | SVG decal |
| `[` / `]` | Brush size − / + | `Ctrl+Z` | Undo |
| `Ctrl+S` | Save project | `/` | Search maps |

## Layout

- **Left** — Material outliner (the channel/maps stack), plus the Brush and
  Decal libraries.
- **Center** — Texture viewport with Channel / Material (shaded) preview, grid,
  zoom, and the floating tool dock.
- **Right** — Layer stack + selected-layer properties, Brush, Material and Decal
  inspectors.

## Notes

The shaded preview is a lightweight tangent-space Blinn-Phong evaluation done on
the CPU (tiling-aware) so the material reads correctly as you author it; it is a
preview, not a renderer. Resolution changes reset painted layers by design.
