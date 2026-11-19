# Frontier World Mirror — Open Vehicle World (`C++20 <-> HTML`)

Frontier World Mirror is the live HTML companion to the native C++ world/editor. Its active launch scene is deliberately sparse: it mirrors the physical sun/atmosphere stack while giving the `ControlVehicle` a long ground receiver and an editor camera. The historical Cornell Studio data remains exported only as a renderer/reference fixture; it is not seeded into new sessions.

## Active launch world

- **Open Vehicle World** — `Infinite Ground Plane`, `ControlVehicle`, `Editor Camera → Settings`, `Sun`, and `Atmosphere`; no studio walls/lights, clouds, moon, or stars.
- **C++ atmospheric parity** — browser ports of `CelestialEphemerisSolver` and `AtmosphericSkySequence`: Kelvin sun tint, physical solar transmittance, 128×32 transmittance LUT, 256×128 SkyView LUT, 28-step Rayleigh/Mie/ozone integration, Catmull–Rom SkyView reconstruction, cool lower hemisphere, and analytic solar disc/corona.
- **Dockable Component Viewer** — one Components-only editor tool, opened from an Outliner right-click with **Open Component Viewer**. It deliberately has no standalone/obsolete Content tab.
- **Camera quality controls** — `Editor Camera → Settings` owns render pipeline quality, SDF bake/export controls, and deterministic path-tracer profile routing.

## Renderer and SDF routes

- **Calibrated WebGL2 / WebGPU GTX renderers** — OpenPBR shading, physical solar shadows, PCSS/SMRT/PCF/BVH shadow choices, ControlVehicle BVH, sky LUT upload, and C++ solar-disc/corona evaluation. SDF remains limited to cone-GI/AO; it is not a shadow or reflection substitute.
- **Signed-distance volume** — Settings bakes a Float32 `32³`, `48³`, or `64³` field for the local geometry. The 2 km ground receiver is intentionally SDF-excluded to retain vehicle detail density and can be exported as `FSDF`.
- **Interactive editor camera** — orbit, pan, free-look, dolly, fly, frame selected, viewport picking, and synchronized `Position` / `Target` / focal-length editing.

## Rebuild & serve

```bash
node Experimental/FrontierWorldMirror/Build.mjs
python3 Experimental/FrontierWorldMirror/Serve.py 8098
```

Validate the mirror with:

```bash
node Experimental/FrontierWorldMirror/WorldMirrorValidation.mjs
node Experimental/FrontierWorldMirror/SdfVolumeSystem.test.mjs
```
