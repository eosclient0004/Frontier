# Frontier Unified Lighting, Photometric (IES), and High-Performance Acceleration Plan

> **Status**: Architecture Blueprint (Pre-Implementation Plan)  
> **Scope**: All 7 Luminaire Inspectors (`Point`, `Spot`, `IES`, `Area`, `Tube`, `LED`, `LED Strip`) + Solar/Lunar/Sky across all 6 Frontier Render Engines (`MatCap`, `OpenPBR Direct`, `SDF GTX GI`, `BVH4 High-End GI`, `Path Tracer Realtime`, `Path Tracer Final`).  
> **Hard Constraints**: Zero CPU bottleneck on live edits; zero CPU BVH/LUT stalls; preserve `WorldViewportRadiometricCompute.comp.spv < 225 KB` and Float16/Int16-free GTX compatibility.

---

## 1. Current State Audit (What Frontier Already Has vs. Gaps)

### Already Resident on GPU (`WorldViewportRadiometricCompute.slang`)
- **Binding 22 (`PhotometricProfileTables`)**: `StructuredBuffer<float>` storing 128 × 32 ANSI/IESNA LM-63 Type C vertical/azimuthal candela tables, sampled by `EvaluatePhotometricProfile128x32(...)`.
- **Binding 23 (`PhotometricLampEntries`)**: `StructuredBuffer<GpuPhotometricLamp64>` already evaluated across **OpenPBR Direct** (`ShadePrimarySurfaceDemod`), **SDF GTX GI** (`GlobalRadiosityBakeEntry` Phase 0), **BVH4 High-End GI** (`EvaluateHighEndViewport` + secondary bounce), and **Path Tracer** (`EvaluateProgressiveViewportPath`).
- **Binding 25 (`GlobalRadiosityFieldBuffer`)**: 2-bounce RGBA16F clipmap radiosity cascades already capable of injecting local emissive + photometric energy into real-time SDF voxel cone GI.
- **Heitz LTC Tables**: Linearly Transformed Cosines already integrated for polygonal quad area lights.

### Current Bottlenecks & Disconnections
1. **Inspector Downgrading**: `InspectorInstanceRegister::CombineIntoScene` routes `SpotLight`, `IesLight`, and `LedLight` into generic isotropic `Scene.PointLights` (losing cone angle, penumbra, direction, and IES candela profiles), and routes `TubeLight` / `LedStrip` into `Scene.AreaLights`.
2. **Hard 4-Light UBO Cap**: `WorldUniforms.AreaLights[4]` limits scene area lights to 4 slots in uniform memory.
3. **Brute-Force Light Loops**: All engines iterate linearly over active lights per pixel/bounce (`O(Pixels × Lights)`), which collapses past ~16–32 local lights.
4. **No Local Light Shadowing Unified Across Engines**: Local point/spot/IES lights currently evaluate unoccluded or with limited visibility checks, causing light leaks through walls and vehicle bodywork.

---

## 2. Unified GPU Luminaire Representation (`GpuUnifiedLuminaire128`)

Replace fragmented `PointLights` + fixed `AreaLights[4]` + separate `PhotometricLampEntries` with a single bindless GPU `StructuredBuffer<GpuUnifiedLuminaire128>` (128 bytes, cache-line aligned, unlimited capacity up to 65,536 lights):

```slang
// Type tags packed in FlagsAndIds.x
// 0 = Point (Sphere)
// 1 = Spot (Dual-Cone + Soft Penumbra + Optional IES Cookie)
// 2 = IES Photometric (ANSI/IESNA LM-63 Type C 128x32 Candela LUT + 3D Basis)
// 3 = Area Quad / Softbox (Heitz LTC + Barn Doors / Spread Angle)
// 4 = Tube / Capsule (Linear Most-Representative-Point + Capsule Solid Angle)
// 5 = LED COB Package (Compact Disc + Optic Beam Angle + Phosphor CCT)
// 6 = LED Strip / Ribbon (Segmented Polyline / Elongated LTC + Opal Diffuser)

struct GpuUnifiedLuminaire128
{
    float4 PositionAndRadius;       // xyz = World Center [m], w = Source Radius [m] (or Half-Length for Tube)
    float4 DirectionAndReach;       // xyz = Unit Aim/Normal,  w = Smooth Cutoff Reach [m]
    float4 TangentAndHalfWidth;     // xyz = Unit Right Axis,  w = Half Width [m] (Area/Strip/Tube)
    float4 BitangentAndHalfHeight;  // xyz = Unit Up Axis,     w = Half Height [m] (Area/Strip)
    float4 RadianceAndPower;        // xyz = Linear RGB Radiance/Intensity (CCT × Tint), w = Total Luminous Flux [lm] (for importance sampling!)
    float4 ConeAndFalloff;          // x = cos(InnerCone), y = cos(OuterCone), z = DecayExponent (default 2.0), w = BeamSpread / Diffuser
    uint4  FlagsAndIds;             // x = LuminaireType | ShadowMode | TwoSided | VolumetricBit, y = IesProfileSlot (0xFFFF = none), z = ShadowAtlasPage / SDFMask, w = LightGroupId
    float4 ShadowBiasAndExtra;      // x = NormalBias, y = PenumbraSoftness, z = StripDiodeDensity, w = Reserved
};
```

### Physical Unit Unification (Zero Artist Guesswork)
Every luminaire inspector converts its domain-native photometric unit into linear radiometric energy at upload time (CPU cost `< 0.01 ms` for 1,000 lights):
- **Point / Spot**: Luminous intensity `[cd]` → radiant intensity `I = cd / 683` (or normalized engine EV scale) × `BlackbodyRgb(Kelvin) × RadiantTint`.
- **IES**: True LM-63 candela table `C(theta, phi) × PeakMultiplier × Tilt` packed into a deduplicated GPU `PhotometricProfileTables` atlas (`128 × 32` fp16/fp32 per unique `.ies` profile, so 500 identical streetlamps share **1 single 8 KB LUT**!).
- **Area / Softbox**: Luminous flux `[lm]` divided by `π × Area` → surface radiance `[nit]`, evaluated analytically via **Heitz LTC** (diffuse + GGX specular) with smooth distance reach windowing.
- **Tube / LED Strip**: Linear flux `[lm/m]` over capsule/ribbon segment `[P0, P1]`, evaluated via **Karis Most-Representative-Point (MRP)** for specular + analytical segment integral for diffuse (plus optional diode-pitch modulation when `OpalDiffuser == false`).
- **LED COB**: Compact disc radius + primary optic beam angle (`OpticAngleDeg`) evaluated as a tight cosine-lobe / optional IES optic emitter.

---

## 3. Compatibility Across All 6 Render Engines (Including GI & MatCap)

| Render Engine | Direct Shading Evaluation | Local Light Shadowing | Indirect GI Contribution | Notes |
|---|---|---|---|---|
| **1. OpenPBR Direct** | Analytic LTC (Area/Strip) + MRP (Tube) + IES LUT + Spot/Point via **Froxel / World Grid** | **Virtual Shadow Map (VSM) / Adaptive Atlas** for top-priority shadow casters + contact screen-space / SDF micro-shadows | Optional 1-bounce ground/sky ambient approximation | Fastest raster/compute baseline; zero RT hardware required |
| **2. SDF Tracing (GTX Low-End GI)** | Same unified analytic evaluators via **World Grid / ReSTIR-SDF** | **Local + Global SDF Sphere-Traced Soft Shadows** (zero shadow map VRAM, physically soft penumbra scaled by `PositionAndRadius.w`!) | **Direct injection into `GlobalRadiosityFieldBuffer` Mip 0 -> 2-Bounce Voxel Cone GI** | Every Spot, IES headlight, Tube, and LED strip automatically casts colored indirect bounce light! |
| **3. Raytraced BVH4 High-End GI** | **ReSTIR DI** (1–2 BVH4 visibility rays/pixel across 10,000+ lights) + analytic LTC/IES evaluation | Exact **BVH4 RayQuery** area/sphere-jittered soft shadows + denoised via existing Joint A-Trous pass | **ReSTIR GI** + secondary BVH4 bounce sampling from the **GPU Light BVH** | Scales to thousands of dynamic shadowed IES/Area lights in real time |
| **4. & 5. Path Tracer (RT & Final)** | **Unbiased MIS (BRDF + GPU Light BVH)** with solid-angle Area/Tube sampling & 2D marginal/conditional **IES Angular CDF** sampling | Exact stochastic multi-bounce **BVH4 path visibility** | Full N-bounce unbiased global illumination + volumetric scattering | Ground-truth reference matching Engines 1–3 radiometrically |
| **0. MatCap Studio** | **Unlit spherical normal lookup (no scene lights by design)** | Existing fast `SampleMatCapCheapShadow` solar occlusion only | None (by design) | See dedicated MatCap compatibility note below |

### Dedicated Note: How Does the Lighting System Interact with **MatCap**?
- **Pure MatCap Mode (Default)**: MatCap is intentionally a **single-pass view-space material capture shader** (`Clay / Ceramic / Metallic / Normal`) for inspecting mesh topology, curvature, creases, and silhouettes *independent of scene lighting*. Running full PBR/IES/GI lighting inside MatCap would defeat its purpose (obscuring sculpting surfaces in dark night scenes) and waste compute.
- **Recommended Optional Coupling (Zero Overhead)**:
  - Keep **MatCap unlit by default** so artists can inspect dark night/interior assets cleanly.
  - Optionally add a single toggle in MatCap Studio (`"Follow Key Light Direction"`) that rotates MatCap's `KeyDir` vector toward the dominant Sun or brightest active scene luminaire (3 vector ops, zero light loop, still 100% O(1)!).

---

## 4. High-Performance Lighting Acceleration Techniques Beyond Clustered Lighting

Standard **Clustered Forward+ / Deferred** (splitting the camera view frustum into `16×9×24` froxels) is only a starting point — it still degrades linearly when 200 lights overlap one froxel (e.g., a tunnel of LED strips, a stadium, or a traffic jam of cars with dual IES headlights + brake lights), and it **cannot accelerate off-screen GI bounces or path tracing** because froxels only exist inside the camera frustum.

To achieve **film-quality lighting at real-time frame rates with thousands of lights**, Frontier should adopt a **4-layer modern GPU lighting stack**:

### Technique 1: **ReSTIR DI & ReSTIR GI** *(Reservoir-based Spatiotemporal Importance Resampling)*
- **Why it beats Clustered Lighting**: Instead of shading every light touching a pixel (`O(K)` lights & `O(K)` shadows per pixel), ReSTIR uses **Weighted Reservoir Sampling (WRS)** to pick **1–2 high-contribution light samples per pixel** from *all* scene lights (Point, Spot, IES, Area, Tube, LED Strip, and even emissive mesh triangles!) in **O(1) visibility cost per pixel**.
- **How it works in 3 tiny GPU compute passes**:
  1. **Initial Candidate Generation**: Draw `M = 16–32` candidate lights per pixel from the Light BVH / Power CDF (evaluating unshadowed `TargetPdf = BRDF × IES × Falloff`), keeping 1 winning light in a 32-byte `Reservoir`. Test **1 visibility ray** (BVH4 in Engine 3, or SDF cone in Engine 2!) on the winner only.
  2. **Temporal Reuse**: Reproject previous frame pixel via motion vectors; combine previous pixel's reservoir (`clamped history M <= 20`) with current reservoir.
  3. **Spatial Reuse**: Combine with 3–5 neighboring pixels passing depth + normal similarity tests (with pairwise MIS weights to guarantee zero bias / zero fireflies).
- **Result**: 5,000 shadowed IES/Area/Strip lights cost almost the **exact same GPU time as 2 lights**!

### Technique 2: **GPU Light BVH / Light Tree with Emission Cone Bounds** *(Conty Estevez & Kulla 2018)*
- **Why it beats flat Light Lists & Froxels**:
  - Works in **world space** (works equally for primary pixels, off-screen secondary GI bounces, Path Tracer bounces, and `GlobalRadiosityBakeEntry` voxel cascades!).
  - Every internal BVH node stores:
    1. **World AABB** (`BoundsMin`, `BoundsMax`)
    2. **Total Luminous Power** (`SumFlux`)
    3. **Bounding Emission Cone** (`Axis`, `theta_o` orientation spread, `theta_e` emission half-angle — tightly enclosing Spot cones, Area light normals, and IES headlight beams!).
- **Runtime Query**: At any shading point `(P, N)`, traversal computes node importance `I = Flux × OrientationTerm(Cone, P->Node) × DistanceAtten` and descends in **`O(log N)`** (`~10–12` steps for 4,096 lights!). Lights pointing *away* from `P` (like headlights behind a wall or facing away) get importance `0` and are pruned instantly!
- **GPU Build Cost**: Built completely on GPU in `< 0.15 ms` for 4,096 lights using LBVH Morton codes + bottom-up cone/power reduction whenever `LightingSig` changes (zero CPU overhead!).

### Technique 3: **World-Space Sparse Light Grid (Clipmap-Aligned) + Frustum Froxels**
- **Why**: In **OpenPBR Direct** and **GTX SDF** when ReSTIR is disabled (e.g., low-end GPUs or deterministic non-temporal stills), a **3-cascade World-Space Light Grid** aligned to Frontier's existing 3 GDF cascades (`Fine 36m`, `Medium 120m`, `Coarse 360m`) accelerates BOTH primary shading AND `GlobalRadiosityBakeEntry` voxel injection with zero camera-frustum limitation.
- **Bonus**: Lights with small reach (interior dashboard LEDs, brake lights, room bulbs) only touch 1–4 cells of the Fine cascade and never touch Medium/Coarse cascades.

### Technique 4: **Hybrid Shadowing: Cached SDF Local Shadows + Virtual Shadow Map (VSM) Atlas**
- **GTX SDF Engine**: Trace local light soft shadows directly through Frontier's existing **Local + Global SDF** (`TraceGlobalDistanceConeVisibility` parameterized by `SourceRadius / Distance`). Zero shadow map VRAM, zero shadow map render passes, soft contact-hardening penumbras for free!
- **OpenPBR Direct Engine**: **Adaptive Virtual Shadow Atlas** (`4096×4096` R32F/D16 tiled atlas with LRU page cache):
  - Static lights over static geometry render their shadow tiles **once** and stay cached (`0.0 ms/frame`).
  - Only dynamic lights or tiles overlapping a moving object's AABB invalidate and re-rasterize.
  - Distant/dim lights dynamically drop from `512²` → `256²` → `128²` → unshadowed smoothly without popping.
- **BVH4 GI & Path Tracer**: Exact BVH4 raytraced shadows (already accelerated by ReSTIR DI to 1–2 rays/pixel).

### Technique 5: **2D IES Angular Importance Sampling (Alias Table / Marginal-Conditional CDF)**
- For Path Tracing and ReSTIR, narrow automotive IES beams (e.g. ECE R112 Low Beam with a sharp horizontal cutoff) waste 90% of uniform hemisphere rays.
- Precomputing a tiny GPU **Marginal/Conditional CDF** alongside each 128×32 IES table allows exact solid-angle importance sampling matching the IES lobe shape, cutting Path Tracer variance by **5–10×** on automotive headlights and architectural spotlights!

### Technique 6: **Multi-Bounce Local Light GI via Radiosity Cascade Injection (Zero Per-Pixel Extra Cost)**
- Frontier's `GlobalRadiosityBakeEntry` already runs asynchronously on GPU whenever `LightingSig` changes (verified in `GtxGlobalDistanceFieldViewportValidation` with zero BVH/GDF rebuild!).
- Upgrading `GlobalRadiosityBakeEntry` to query the **World-Space Light Grid / Light BVH** injects **all 7 luminaire types** (including IES beams, Spot cones, Area softboxes, and LED strips!) directly into Mip 0 → 2-bounce cone-traced radiosity cascades.
- Result: Turning on an orange LED strip or blue IES spotlight immediately bathes nearby surfaces and ceilings in real-time multi-bounce colored GI in GTX SDF mode!

---

## 5. Shader Size & Performance Architecture (Staying Strictly `< 225 KB`)

Frontier enforces `WorldViewportRadiometricCompute.comp.spv < 225 KB` (currently `223.6 KB`). Packing ReSTIR + Light BVH + Froxel culling into the monolithic shading shader would exceed that ceiling. Therefore, the new lighting system **must use modular dedicated Vulkan compute shaders**:

1. **`WorldViewportLightBuildCompute.comp.spv`** *(New — runs only on `LightingSig` change or camera move)*:
   - Uploads `GpuUnifiedLuminaire128` SSBO + builds GPU Light BVH (Morton sort + cone/power reduction) + populates 3-cascade World-Space Light Grid.
2. **`WorldViewportRestirLightCompute.comp.spv`** *(New — runs per-frame for Engines 2/3)*:
   - Performs ReSTIR DI initial sampling + temporal reuse + spatial reuse, writing a compact `PickedLightBuffer` (1–2 winning shadowed luminaire contributions per pixel) + denoiser guide channels.
3. **`WorldViewportGlobalRadiosityBake.comp.spv`** *(Existing — upgraded)*:
   - Reads `GpuUnifiedLuminaire128` + World-Space Light Grid to inject all 7 luminaire types into 2-bounce GDF radiosity cascades.
4. **`WorldViewportRadiometricCompute.comp.spv`** *(Existing — actually SHRINKS!)*:
   - Replaces duplicate `AreaLights[4]` + `PhotometricLampEntries` + `PointLights` loops with a single compact `EvaluateUnifiedLuminaire(...)` + reads pre-sampled ReSTIR DI buffer (keeping `.comp.spv` well below `215 KB`!).

---

## 6. Phased Implementation Roadmap

1. **Phase 1 — Unified Luminaire SSBO & Inspector Wiring (100% Parity for All 7 Lights)**:
   - Introduce `GpuUnifiedLuminaire128` + `Scene.UnifiedLuminaires` + deduplicated IES profile atlas.
   - Wire `SpotLight`, `IesLight`, `TubeLight`, `LedLight`, `LedStrip`, `AreaLight`, `PointLight` in `InspectorInstanceRegister::CombineIntoScene` with full orientation, cone/penumbra, IES LUT, dimensions, and physical flux.
   - Replace `AreaLights[4]` UBO array with bindless SSBO (removes 4-light limit immediately).
2. **Phase 2 — World-Space 3-Cascade Light Grid + SDF Local Shadows + Radiosity GI Injection**:
   - Add `WorldViewportLightBuildCompute.comp.spv` to bin all luminaires into Fine/Medium/Coarse world grids.
   - Enable SDF cone soft shadows for local lights in GTX SDF engine and inject all 7 light types into `GlobalRadiosityBakeEntry` (full 2-bounce GI for IES/Spot/Area/Tube/LED!).
3. **Phase 3 — GPU Light BVH (SAOH + Cone Bounds) + ReSTIR DI**:
   - Implement GPU Light BVH with emission cone bounds (`theta_o`, `theta_e`).
   - Add `WorldViewportRestirLightCompute.comp.spv` for `O(1)`-shadow-ray many-light rendering in BVH4 High-End GI and GTX SDF GI (scaling cleanly to 4,096+ shadowed lights!).
4. **Phase 4 — Path Tracer MIS + IES Angular CDF + OpenPBR Direct VSM Shadow Atlas**:
   - Integrate Light BVH + 2D IES CDF importance sampling into `ViewportPathTraceEntry`.
   - Add cached Adaptive Virtual Shadow Atlas for `OpenPBR Direct` local light shadows.
5. **Phase 5 — Automated Empirical Proof Suite (`UnifiedLightingValidation`)**:
   - Render multi-engine proof sheets comparing Point, Spot, ECE Low-Beam IES, Heitz Area Softbox, Linear Tube, COB LED, and LED Strip across OpenPBR Direct, GTX SDF GI (with 2-bounce colored wall bleed), BVH4 High-End GI, Path Tracer (reference convergence), and 1,000-light ReSTIR stress benchmark.
