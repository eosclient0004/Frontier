# Frontier / Texture

Experimental WebGL2 texture painter built with the Fluid editor's visual language.

```
npm install
npm run dev      # http://localhost:5173
```

- **Layer stack**: paint, fill and decal layers, each with opacity, ten color blend modes, height blending,
  per-channel output (base color, roughness, metallic, height, emissive), a painted mask and a procedural
  mask generator (noise, height gradient, facing/dust, convex edges/wear, cavities/dirt).
- **Material system**: two-surface PBR materials blended by 14 procedural patterns (triplanar or UV), pattern
  height and emission, micro grain, bitmap albedo, 22 presets including multi-layer smart materials, and a
  user library you can save to, export from and import into (JSON).
- **Decals**: SVG artwork (built-in samples or imported files) and typeset text (fonts, weight, spacing,
  outline, uploaded fonts), projected from the view with an on-canvas gizmo or placed in UV space.
- **Painting**: camera-projected brushes that cross UV seams, UV-space painting, pen pressure, jitter,
  smoothing, Shift-click lines, material sampling and undo/redo with GPU texture snapshots.
- **Files**: `.ftex` projects (ZIP with lossless PNG layer data, 16-bit height), PNG map export
  (OpenGL/DirectX normals, ORM, Unity mask map) and OBJ mesh import.

Press `?` in the header for the full shortcut list.

## GPU safety

- Material, mask and layer shaders are compiled as small **variants** per feature combination (pattern, mapping, warp, grain, bitmap, layer type, stroke mode, mask generator) instead of one uber shader. Direct3D shader compilers (Chrome/Edge on Windows) could spend tens of seconds on the uber shader, which stalls every tab and can trip the GPU watchdog.
- With `KHR_parallel_shader_compile` variants compile on driver threads; the viewport keeps the previous result and shows "Preparing shaders…" instead of blocking the page.
- Every layer pass is flushed as its own GPU submission so no batch approaches the OS timeout (TDR).
- If the WebGL context is lost, the next launch starts the demo set at 512² ("safe mode").
- When WebGL2 is refused, the viewport lists browser-specific fixes and a copyable diagnostics report.
