# Frontier — Texture Paint

A PBR texture-painting studio in the browser, skinned in the **Fluid experimental
theme** (charcoal surfaces, pill sliders, slanted document tabs, outliner /
viewport / inspector triptych).

## What it does

- **Layer stack** — paint, fill (solid / gradient / radial / checker / stripes /
  noise), SVG decal, text decal, image and adjustment layers. Drag-reorder,
  18 blend modes, opacity, clip-to-below, per-layer masks, merge-down,
  rasterize, duplicate.
- **Robust material system** — every stroke can write albedo + metallic +
  roughness + height + emissive in one pass. Fill and decal layers stamp flat
  PBR through their alpha. Tiling-safe Sobel normals, ORM packing, material
  presets, and a live WebGL2 PBR preview (sphere / cube / plane / cylinder)
  with studio lighting and ACES output.
- **SVG + text decals** — non-destructive vector source editing, `.svg` import,
  tint glaze, full typography controls (typeface, weight, tracking, leading,
  outline), and an on-canvas transform gizmo (move / scale / rotate, arrow-key
  nudge).
- **Masking** — per-layer masks with rubylith overlay, invert, reveal/conceal
  fills, feather and density; plus a Photoshop-style **Quick Mask** (`Q`):
  paint the scratch buffer in red, `Q`/`Enter` commits it to the active mask,
  `Esc` discards.
- **Paint tools** — dab-engine brush with hardness, flow, spacing, scatter,
  stabilizer, symmetry and pressure; eraser, smudge, flood fill, eyedropper
  (samples all channels), shapes, 2D / 3D / split views, tiling preview,
  channel inspector, navigator, dirty-rect undo history, `.texpaint` projects.

## Open it instantly (no install)

Single-file build, served straight from GitHub:

- **App:** https://raw.githack.com/eosclient0004/Frontier/arena/01a10787-frontier/texture-paint.html
- Multi-file dev page: https://raw.githack.com/eosclient0004/Frontier/arena/01a10787-frontier/index.html

The app boots into the **3D material view** — drag to orbit, switch to
2D / split from the viewport bar to paint.

**No-WebGL fallback:** the 3D view tries WebGL2, then WebGL1, then a CPU
software renderer that shades the same PBR sphere — so blocked-GPU
browsers still get a live preview instead of an error. The viewport
subtitle names the active backend.

**Painting in 3D:** brush tools (paint, eraser, smudge, fill, eyedropper)
work directly on the sphere — just drag on the model. Alt-drag (or the
pan tool) orbits while a brush is selected; drags that miss the model
orbit too. Cube / plane / cylinder meshes are orbit-only in 3D, so switch
to the sphere or paint in 2D. The software renderer idles at zero CPU
when the scene is static and renders turntable motion at reduced rate.

**Stale page?** If you ever see the old "Retry WebGL2" error wall, your
browser cached copy predates the fallback — hard-refresh with Ctrl+F5
(Cmd+Shift+R on Mac) to load the current build.

## Run it

```bash
npm install
npm run dev               # http://localhost:5173
npm run build             # static dist/
npm run build:standalone  # single-file texture-paint.html (for githack)
npm test                  # headless smoke suite (no browser needed)
```

Node ≥ 18. No runtime dependencies besides Vite (dev).

## Layout

| File | Purpose |
| --- | --- |
| `index.html` | Fluid-chrome shell: header, layer outliner, viewport, inspector |
| `src/TexturePanel.css` / `src/ThemeSpecification.css` | Fluid theme (ported 1:1, fonts via Google Fonts) |
| `src/TextureExtras.css` | Texture-studio additions (gizmo, channel strip, navigator…) |
| `src/TexturePanel.js` | Studio controller: documents, UI, views, export |
| `src/Document.js` | Layer stack, PBR composite, history, serialization, ORM |
| `src/BrushEngine.js` | Dab strokes, smudge, fill, eyedropper, shapes |
| `src/Decals.js` | SVG/text rasterizer + transform math |
| `src/PreviewRenderer.js` | WebGL2 PBR turntable renderer |
| `src/Presets.js` | Brush / material / decal libraries |
| `src/Icons.js` | Stroke icon vocabulary |
| `test/smoke.mjs` | Dependency-free DOM-stub smoke test |

## Shortcuts

`B` `E` `U` `G` `I` `R` `V` `H` tools · `[` `]` size · `1–0` opacity · `X` swap
colors · `Ctrl Z/Y` undo/redo · `Ctrl S` save · `F` fit · `C` checker ·
`T` tiling · `D` diagnostics · `Q` quick mask (`Enter` commit · `Esc` discard) ·
`/` library search · arrows nudge decal.
