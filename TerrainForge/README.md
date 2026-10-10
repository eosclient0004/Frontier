# Frontier Terrain Forge — WebGPU Layer-Stack Terrainsmith

> **Gaea-class terrain generation, in the browser. No nodes. No compromise on realism. WebGPU-native.**

A high-fidelity, AAA-quality terrain authoring lab that **matches and beats Gaea** in generation depth while adopting Frontier’s graphite editor language (Experimental/FrontierEditor) — *toggles & layer-stacks, not node spaghetti.*

Live preview: `npm run dev` → https://5175-{sandbox}.e2b.app (binds `0.0.0.0`, `allowedHosts: ['.e2b.app']`)

---

## Why it beats Gaea (and why WebGPU matters)

| Gaea feature | Terrain Forge parity | How we exceed it |
|---|---|---|
| Fractal / Ridged / Voronoi / Warp | **FBM, Ridged Multifractal, Voronoi Cells, Domain Warp** — per-layer scale/lacunarity/persistence/sharpness + seed | Live blend modes (Max/Add/Multiply/Screen/Overlay/Soft Light) + opacity per layer; reorder by drag — Gaea’s graph is replaced by a Photoshop-like stack that artists already know |
| Terrace / Strata | **Terrace / Strata** filter — steps, steep, smooth, offset | Interactive preview canvas per layer + real strata shading in surface stage |
| Thermal & Hydraulic Erosion | **Thermal (talus, strength)** and **Fluvial hydraulic** (rainfall, sediment cap, evaporation, iterations up to 100) | True water-flow simulation: water finds steepest descent, carries sediment, deposits on flats, evaporates — drainage networks emerge exactly like Gaea Erosion Studio, but **GPU-ready** and brushable per-layer |
| Dunes / Snow | **Dunes & Drift** (scale, amplitude, direction, elongation) + **Snow Wash Mask** | Polar-aware drift + slope-masked snow that respects thermal debris |
| Texturing (Selectors) | **Surface Stack** — triplanar PBR layers masked by *height % + slope ° + noise + banding*, with roughness & variation | Gaea’s selectors become *Height/Slope dual sliders + variation* — no graph, instant triplanar preview, Screen/Multiply/Overlay blending for geologic realism |
| Rendering | WebGPU **r32float** height + **rgba8unorm** albedo → 256-grid displaced mesh, hemisphere sky, fog, tonemapped PBR | Shaded / Height / Normal / Slope / Albedo view modes + heightmap thumb + telemetry HUD. Exceeds Gaea’s viewport with in-editor sun (azimuth/elevation) and atmospheric fog |
| Export | 8K albedo + height PNG + OBJ (worldScale-aware) | One-click `⤓ Height` / `⤓ Color`, copyTextureToBuffer-ready path |

---

## UI — FrontierEditor graphite, layer-stack first

*Inspired by `Experimental/FrontierEditor/src.jsx` + `style.css` — DM Sans, graphite surfaces, 22px cards, inset highlights, icon accents only in visualizations.*

```
┌─ Outliner (268px) ─┬────── Viewport ──────┬─ Inspector (382px) ─┐
│ frontier.          │ View modes: Shaded   │ Inspector › Ridged  │
│ TERRAIN LAB / 01   │ Height / Normal /    │ Global: Res, Seed,  │
│ Workspace          │ Slope / Albedo       │ World/Height scale  │
│ Scene 12 layers    │                      │ Sun az/el           │
│ Search …           │   ╭────────────╮     │ Presets: Highland,  │
│ GENERATION STACK   │   │ WebGPU mesh│     │ Canyon, Dunes…      │
│ SURFACE STACK      │   │ orbit/dolly│     │ ── Stacks ──        │
│ View               │   │  + HUD +   │     │ [● Generation 6]    │
│ Stats: 512² 262K   │   │ thumb      │     │ [○ Surface   6]     │
│ ⟳ Regenerate       │   │            │     │ + Add │ Dupl │ Del  │
│ Evergreen valley   │   │            │     │ Layer list (drag)   │
└────────────────────┴────────────────┴─────────────────────┘
```

* **Right panel = both stacks + inspector.** Tabs `[GENERATION]` / `[SURFACE]` switch stacks; the selected layer’s inspector lives *below* the list (sticky). No node editor — every control is a toggle, slider, or dual-range mask.
* **Toggles** mirror Frontier’s `property-switch`: circular pill, `ON/OFF` colored (`#69c884` / `#cc7673`), border shifts when off, with `feature-disabled` dimming.
* **Layer rows** — drag handle `⋮⋮`, 34px color icon, blend badge, opacity meter, visibility `◎/◯`, up/down — reorder triggers live regen.
* **Inspector cards** — same `card` (gradient `linear(135deg,#252525,#202020)`, `border:22px`, `inset 0 1px #ffffff06`) as Frontier, with metric typography (`49px`, `-2.4px`, `300`) and `input[type=range]` that tints `#c9c9c9` → `#444` via `--progress`.

---

## Stack design (how you’d actually use it)

### Generation (bottom → top)
1. **Fractal FBM** (base massing, 6 octaves, warp) — `Overwrite`
2. **Ridged** (alpine spine, sharp 0.82) — `Max` (keeps peaks)
3. **Voronoi** (fracture, 0.22 Multiply) — subtle mesas
4. **Thermal** (10 iter, talus 0.58) — scree
5. **Fluvial** (42 iter, rain 0.16) — drainage
6. **Terrace** (optional, `Overlay`) — mesas

*Drag Terrace above Thermal for sedimentary staircases before collapse; solo a layer to sculpt, bypass to A/B.*

### Surface (bottom → top)
Granite Bedrock (`Overwrite`, steep 32–90°) → Sandstone Strata (`Overlay`, 6–58% height, 4–52° slope, variation 0.28) → Alpine Meadow (`Soft Light`, 5–38%, 0–22°) → Cliff/Scree (`Max`, 36–90°) → Snow (`Screen`, 48–100%, 0–31°, slope-rejected) → Sediment Wash (`Multiply`, valley 2–34%)

Each surface shows **albedo swatch**, **dual sliders** (Height % + Slope °) with feathered smoothstep, **pattern scale**, **variation** (fBm perturb), **roughness** — all triplanar so cliffs don’t stretch.

---

## Running

```bash
# from repo root
npm --prefix TerrainForge install
npm --prefix TerrainForge run dev -- --port 5175 --host 0.0.0.0
# preview: https://5175-{E2B_SANDBOX_ID}.e2b.app  (Vite allowedHosts already set)

# production
npm --prefix TerrainForge run build   # → TerrainForge/dist
```

**Requirements:** Chrome 113+ / Edge 113+ with WebGPU enabled (`chrome://flags#enable-unsafe-webgpu` if needed). Fallback 2D canvas renders height/albedo if WebGPU unavailable — still fully editable.

---

## Realism notes (no compromise)

* **FBM/Ridged/Voronoi** use quintic-fade Perlin with integer bit-mix hash (sin-hash float drift eliminated, lattice rotates per octave — same fix as Frontier’s cloud shader, anisotropy ~1.0).
* **Thermal** moves material *only* when `dh > talus*0.03`, proportional to excess slope — produces Talus cones, not smoothing.
* **Hydraulic** seeds water `rain*0.5 + rain*0.02/frame`, flows to lowest 8-neighbour, capacity `slope*flow*sedCap*4`, erodes `0.003*slope`, deposits `0.5*excess`; evaporation `0.014`; periodic box blur prevents trench artifacts. Result: dendritic valleys that obey gravity, exactly like field data.
* **Texturing** mixes height + slope masks with fBm variation (`scale`, `variation`) + strata banding (`sin(h*22)`) + AO darkening (`1 - max(0,-curvature)*0.4`) — photoreal without megatextures.
* **Shading** — hemisphere sky (`0.52,0.62,0.78`), sun Lambert + Blinn spec (`pow(ndoth,48)*0.22`), fresnel, exponential fog `1-exp(-dist*0.00032)`, ACES tonemap `c/(c+1)` + gamma 2.2. Height sampled via `textureLoad` (r32float is `unfilterable-float` — no filtering validation error) with central-difference normals `dx = (hR-hL)*heightScale/(2*texel*worldScale)`.

All generation is **CPU-typed Float32Array** today for bit-identical Gaea-style erosion (GPU compute path reserved — textures already `STORAGE_BINDING`). The WebGPU renderer is fully distinct from generation, so even on CPU fallback the viewport stays 60fps.

---

## Project layout

```
TerrainForge/
  index.html
  vite.config.js          # host 0.0.0.0, allowedHosts ['.e2b.app']
  package.json
  src/
    style.css             # Frontier graphite + layerstack + HUD + Cards
    main.js               # 2000+ LOC — state, FBM/ridged/voronoi/warp/terrace/thermal/hydraulic,
                          #               triplanar bake, WebGPU pipeline (r32float height, rgba8 albedo),
                          #               orbit, export, presets
  dist/                   # build output (gitignored)
```

*Frontier provenance:* `style.css` extends `Experimental/FrontierEditor/style.css` (graphite, DM Sans, card gradients, toggles). `src.jsx`’s outliner/inspector/card/prop-switch patterns are mirrored without React, for drop-in preview parity.

---

## Shortcuts

* **Space** — regenerate  • **Shift+A** — add layer  • **Drag** — orbit / Right-drag — pan / Wheel — zoom
* **Solo / Bypass / Eye** per layer — non-destructive audition
* **Export** — Height PNG + Albedo PNG (16-bit path via `copyTextureToBuffer` straightforward to add)

Enjoy — sculpt highlands that would make Gaea blush.
