# Frontier

## Browser world-editor mirror

This branch stages the latest compatible **Frontier World Mirror** HTML prototype
from `streamlinkinbox/Frontier` (`arena/7a73db0c-frontier`, `c205cb6`, 18 Nov
2026) for review. It is an interactive browser-side rendering/editor mirror;
it is not the native C++ editor executable.

Run it from the repository root with:

```bash
python3 Experimental/FrontierWorldMirror/Serve.py 8098
```

The page is served at `/` (or `/Experimental/FrontierWorldMirror/index.html`).
`Experimental/ProjectBrowser/` is included for the mirror's Projects link.

## Renderer settings, SDF, and path tracing

Expand **Editor Camera → Settings** in the Outliner to select a global quality
tier or individual shadow/GI/reflection quality. The same component owns the
realtime SDF bake: **Bake Scene SDF** produces a signed Float32 dense volume
for the current live scene; **Export FSDF** saves its metadata plus voxel data.

The deterministic WebGPU path tracer is also included at
`/Experimental/DeterministicPathTracer/pathtrace.html`. Select **Path Trace ·
Preview** or **Path Trace · Final** in Editor Camera Settings and use **Open
pathtrace.html** to launch it with the matching deterministic profile. This is
a standalone ControlVehicle path-tracing workspace, not a claim that the live
World Mirror scene is exported to that tracer yet.
