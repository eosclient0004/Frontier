# Strata — Frontier Terrain Authoring

A browser-first, layer-stack terrain authoring prototype for Frontier. It uses **WebGPU** for real-time displaced terrain, procedural material masks, physically styled lighting, aerial depth, and 4× MSAA. The UI is intentionally a direct-manipulation layer workflow—there is no node editor.

## What is included

- GPU terrain preview built with WebGPU / WGSL
  - multi-octave continental form and alpine ridges
  - procedural drainage carving and talus breakup
  - analytic terrain normals, directional lighting, fog, and material-aware shading
  - real-time height and material-mask diagnostics
- Terrain **Build** layer stack with visibility, ordering, add-layer, reset, undo/redo, and deterministic seed generation.
- Separate **Surface** layer stack for bedrock, meadow, scree, snow, and runoff wetness.
- Contextual inspector controls: sliders, segmented settings, toggles, summaries, and editable values.
- Orbit camera controls, zoom, framing, grid, projection toggle, render quality state, snapshots, presets, and keyboard shortcuts.
- A presentation fallback renderer for browsers that do not expose WebGPU. The primary path is genuine WebGPU; use a current Chromium/Edge build with WebGPU enabled for the full renderer.

The graphite workspace, narrow typography, restrained accent color, and right-hand outliner/inspector rhythm take design direction from the supplied Frontier Experimental editor while the terrain workflow and implementation are purpose-built for this app.

## Run locally

```bash
npm install
npm run dev
```

Open the printed local URL. The Vite development server binds to `0.0.0.0` and accepts Arena live-preview hosts.

## Build

```bash
npm run build
```

## Interaction model

| Action | Control |
| --- | --- |
| Generate a new deterministic terrain | **Generate** or `G` |
| Orbit | Drag the viewport |
| Dolly | Mouse wheel / trackpad scroll |
| Frame terrain | viewport frame button or `F` |
| Switch to surface stack | Surface mode or `2` |
| Undo / redo | toolbar or `Cmd/Ctrl+Z`, `Cmd/Ctrl+Shift+Z` |

Changes are kept in local browser storage for the current workspace. Generated terrain is seed-based, so a stack plus seed is reproducible.
