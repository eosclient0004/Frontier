# 💡 `UnifiedLightingArchitecturePlan` — Unified Photometric, Automotive, Softbox, Self-Lit & Environment Lighting (`2026-11-25`)

> **TL;DR**: Do **not** use view-frustum Clustered Lighting or standalone temporal reservoir sampling (`ReSTIR DI`) as Frontier's primary direct-lighting scheduler. Extend Frontier's existing **Conty Estevez & Kulla (2018) `PhotometricLightTree` (`AdaptiveLampCut`)** into a **Unified GPU Luminaire Register** (`LuminaireNetwork` + `PriorityCut` `Split4/Split8`) covering IES `.ies` lamps, automotive headlamps/DRLs/taillights, softboxes/tubes, point/spot luminaires, clustered self-lit surfaces, and environment portals — keeping **all direct lighting 100% analytical** even in **SDF (`GtxLowEnd`)** mode while swapping only visibility & indirect transport per render engine (`MatCap`, `SDF`, `OpenPbrDirect`, `RealTimeGiHighEnd`, `PathTracer`). Live WebGL2 GPU demonstration staged in **[`Experimental/LightingArchitectureLab/`](../../Experimental/LightingArchitectureLab/index.html)** before building the 3 `AutoSpace` lobbies.

---

## 🔍 1. Architectural Decision Comparisons

> **Legend (`SKILL-Emoji.md` §5):** **Accuracy & Cross-Engine Parity**: `⭐⭐⭐` = highest; **GPU Cost & Variance**: `✔️` = lowest/fastest, `🚩` = moderate, `🔴` = heaviest/noisiest; **Selection**: `🥇` = primary backbone, `🥈` = secondary booster, `🔴` = rejected as primary.

### 1.1 Many-Luminaire Scheduling (`Clustered` vs `ReSTIR DI` vs `PhotometricLightTree` Dual Partition)

| Rank | Scheduling Architecture | View-Independent (Reflections, GI, PT & Glass) | Fast-Moving Cars & Camera Cuts | Specular Clearcoat Stability | SDF (`GtxLowEnd`) & `MatCap` Compatibility | Scaling at `200..2000+` Luminaires | Verdict |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| 🥇 | **Spatial Luminaire Partition (`PriorityCut` Real-Time + `StochasticCut` PT)** | `⭐⭐⭐` **Yes** (world-space 3D bounds + directional cone) | `⭐⭐⭐` **Zero lag** (pure deterministic spatial cut; 0 temporal history) | `⭐⭐⭐` **Zero-noise crisp** (`PriorityCut` extracts exact top-K local luminaires on clearcoat; `StochasticCut` unbiased in PT) | `⭐⭐⭐` **Full** (drives `MatCap` top-4, `SDF` cones, `BVH4`, `PT` MIS) | `✔️` **`O(log N)`** (`<= 12` expanded partition entries/px) | 🟢 **SELECTED** (Extend `PhotometricLightTree.h` & wire into `WorldViewportRadiometricCompute.slang`) |
| 🥈 | **16×16 Screen-Tile Luminaire Pre-Filter** *(Primary Ray Booster Only)* | `🔴` Primary pixels only | `⭐⭐⭐` **Zero lag** | `⭐⭐⭐` Crisp | `⭐⭐` Primary `OpenPbrDirect` / `SDF` | `✔️` `O(1)` tile header fetch | 🟢 **OPTIONAL PRIMARY BOOSTER** when luminaire count exceeds `512` |
| 🔴 | **View-Frustum Clustered Forward/Deferred (`16×9×24` Cells)** | `🔴` **Breaks** on reflections, GI bounces, refraction & volumetrics | `⭐⭐` Requires cell rebuild per camera cut | `🚩` Evaluates fixed cluster slots without power/cone pruning | `🔴` Useless for off-screen GI & world-space probes | `🚩` Overflows dense automotive/pit clusters | 🔴 **REJECTED** — Frustum-locked and lacks directional cone/power importance |
| 🔴 | **Standalone ReSTIR DI (Temporal/Spatial Importance Reuse)** | `🔴` **G-Buffer only** (blind to secondary rays, glass, fog & `MatCap`) | `🔴` **Ghosting & shadow lag** on moving headlamps, brake lights & cuts | `🔴` **1-ray RIS sparkle noise** on wet floor & car clearcoat | `🔴` Breaks SDF continuity & deterministic `(0,2)` Sobol' convergence | `✔️` `1–2` shadow rays/px | 🔴 **REJECTED as primary** — Temporal ghosting and screen-space lock break automotive/PT parity |

---

### 1.2 Answering the SDF Question: Analytical Luminaires vs Voxelized Light Volumes

> *"should i use SDF lighting for SDF (just make them analytical?)"* — **Keep ALL Direct Lighting 100% Analytical; Use SDF Strictly for Soft Visibility & Multi-Bounce Indirect Radiosity.**

| Rank | SDF Direct-Light Strategy | Clearcoat Highlight Sharpness | Asymmetric `ECE Low Beam` Cutoff | Dynamic Moving Headlamps | `GtxLowEnd` Bandwidth | Verdict |
| :---: | :--- | :---: | :---: | :---: | :---: | :--- |
| 🥇 | **Analytical Direct Evaluation + `GDF` Soft Cone Visibility (`UseFastGdfVis = true`) + Stokes/Radiosity GI** | `⭐⭐⭐` Exact GGX/LTC & Capsule form factors | `⭐⭐⭐` Exact `128×32` Type-C `.ies` bilinear lookup | `⭐⭐⭐` Instant `0`-bake transform update | `✔️` `4–8` SDF cone steps replace `N` BVH4 rays | 🟢 **SELECTED** (`WorldViewportRadiometricCompute.slang` currently assigns `false` to `UseFastGdfVis`; switch to `true` in `Engine 2`) |
| 🔴 | **Bake Direct Lights into 3D Irradiance / Radiosity Voxels** | `🔴` Destroys sharp clearcoat specular highlights | `🔴` Smears sharp road-beam cutoff across `15–25 cm` voxels | `🔴` Re-bake stall every tick car moves/turns | `🚩` High 3D RW volume bandwidth | 🔴 **REJECTED** — Reserve 3D Radiosity Pyramid strictly for **indirect multi-bounce** & far background self-lit bleed |

---

### 1.3 Cross-Engine Lighting Matrix (All 5 Frontier Render Modes)

Every luminaire uses the **identical analytical equation and register layout**; only the **Visibility Operator** and **Indirect Bounce Integrator** scale across render engines:

| Render Engine (`WorldRenderEngine`) | Direct Light Evaluation (`Quads / Capsules / Point / IES / Automotive`) | Visibility / Shadow Operator | Many Self-Lit Surfaces (`100+` Strips / Pylons) | Environment Lighting |
| :--- | :--- | :--- | :--- | :--- |
| **0 · `MatCap`** | Analytical top-4 `AdaptiveLampCut` + unshadowed luminaire lobe modulated onto MatCap shading sphere (`< 0.2 ms`) | `1` bilinear `2×2` solar cascade sample (`0` BVH rays, `0` SDF steps) | Self-luminous surface exitance + L2 SH sum | **L2 Spherical Harmonics (`9 × float3`)** baked from active Sky / Cove Dome |
| **2 · `RealTimeGiGtxLowEnd` (`SDF`)** | Analytical **LTC Quad**, **Linear Capsule**, **Finite Sphere**, **128×32 `.ies`** via `PriorityCut` (`Split4`) | **`GDF` Soft SDF Cone** (`0.28 m` BVH4 contact + `6–8` 3D SDF cone steps, `UseFastGdfVis = true`) | Hero strips promoted to Capsules/Quads + **Stokes Contour**; background lamps baked into **3D Radiosity Pyramid (`Mip0..3`)** | **3-LUT Sky** (`Outdoor`) or **Analytical Cove Dome** + **Sky Portal Quads** (`Indoor`) |
| **1 · `OpenPbrDirect`** | Identical analytical suite (`Split8`) | Exact **`BVH4` / `RayQuery`** (`1` ray per cut luminaire + `ContactPcss` / Vogel solar) | Hero strips promoted to Capsules/Quads; HDR exitance | **3-LUT Sky + `16×16` Octahedral Specular IBL + L2 SH** |
| **3 · `RealTimeGiHighEnd`** | Identical analytical suite (`Split8`) | **`BVH4` / `RayQuery` (`0 MB SDF`)** + `PlaneAwareShadowFilter` | **Full Stokes Contour** spherical-polygon integral + **Dual-Cascade C1-Hermite Octahedral Surfel Lattice** (`2`-bounce bleed) | Coupled Sky LUT + Sky Portal Quads + Multi-bounce Surfel Lattice |
| **4/5 · `PathTracer` (`GameRealtime` / `SceneFinalRender`)** | Unbiased **`StochasticCut`** descent (`1/Pdf`) + **Power-Heuristic MIS** across Area, Capsule, `.ies`, Self-Lit Triangles & Sun | Exact **`BVH4`** ray per drawn cut entry (`<= 8`), steered by deterministic `(0,2)` Sobol' / Blue-Noise sequence | All self-lit triangles indexed as partition leaves in the same binary network | Exact sky / cove dome **NEE + BSDF MIS** |

---

## 🧩 2. Unified Luminaire Taxonomy & GPU Register Layout

Currently `WorldViewportRadiometricCompute.slang` caps `GpuAreaQuadLight AreaLights[4]` inside `WorldUniforms` and runs an `O(N)` loop over `PhotometricLampEntries` without traversing `PhotometricLightTree`. Replace this split with one unified `StructuredBuffer<GpuLuminaireEntry64>` paired with `PhotometricLightTree`:

```text
StructuredBuffer<GpuLuminaireEntry64>  (64 B / luminaire — unlimited count across Quads, Capsules, Point, Spot, IES, SelfLitClusters)
   ├── LuminaireCategory = 0 · PointSphere    : Center(xyz), Radius(w>0 prevents GGX singularity), Range, ColorIntensity
   ├── LuminaireCategory = 1 · PhotometricIes : Pos, Axis, ZeroAzimuth, Range, Radius, ProfileOrdinal (128x32 Type-C LUT)
   ├── LuminaireCategory = 2 · AreaQuadLtc    : Center, Right*HalfW, Up*HalfH, TwoSided, LouverFocus, SkyPortalOption
   ├── LuminaireCategory = 3 · TubeCapsule    : SegmentA, SegmentB, TubeRadius, Range, ColorIntensity (DRL / Linear LED)
   └── LuminaireCategory = 4 · SelfLitCluster : RepresentativeProxyQuad, ClusterRadius, StokesPatchIndex

StructuredBuffer<GpuLightCluster64>   (Conty Estevez & Kulla 2018 binary BVH over ALL non-zero luminaires)
   ├── BoundsMin(xyz) + TotalFluxPower(w)
   ├── BoundsMax(xyz) + FirstNestedOrLeafIndex(w)  // >= 0: LeftIndex (Right = +1); < 0: -(LuminaireIndex + 1)
   └── BoundingConeAxis(xyz) + CosHalfSpread + SinHalfSpread + CosCutoff
```

### 2.1 Luminaire Category Mathematics

| Luminaire Category | Mathematical Formulation | Automotive & Lobby Presets Supported |
| :--- | :--- | :--- |
| **Measured & Parametric `.ies`** | `128×32` (`θ ∈ [0..π]`, `φ ∈ [0..2π)`) bilinear Type-C candela LUT (`EvaluatePhotometricProfile128x32`) + smooth quartic range window | `ECE R112 Low Beam`, `SAE Low Beam`, `High Beam`, `Fog Lamp`, `Barn-Door Fresnel`, `Car-Park Downlight`, `Wall Wash`, `Batwing`, `Stadium Flood` |
| **Automotive Lighting Suite** | Dual `ECE Low Beam` + `High Beam` `.ies` mounts + **Linear Capsule DRL** (`5800 K`) + **Ruby Tail/Brake Capsule** (`625 nm`) + **Sequential Amber Indicator** (`590 nm`) + wheel-well accent spheres | Driven by vehicle world transform + runtime toggles (`LowBeamActive`, `HighBeamActive`, `BrakeEngaged`, `TurnSignalActive`, `HazardActive`) |
| **Softboxes & Linear Tubes** | Clipped spherical-polygon **Heitz LTC Quad** (diffuse + GGX representative point) + **Barn-Door Louver Collimation** (`pow(saturate(dot(-L, EmitNormal)), Focus)`) + **Linear Capsule Tube** closest-point segment form factor | Overhead Silk Softbox, Parabolic Silver Umbrella (`Disc`), Rim Strip LED, Garage Linear Ceiling Banks |
| **Point & Self-Lit Clusters** | Finite-radius sphere area energy normalization (`a' = saturate(a + r/(2d))`) + **Stokes Contour** spherical boundary integral on near clusters + **3D Radiosity / Surfel Lattice** multi-bounce | `100–1,000+` pit-lane light pylons, telemetry panels, underglow bulbs, brake glow & neon strips |
| **Environment & Portals** | **Outdoor/Archway**: 3-LUT Bruneton/Hillaire Sky + `SkyPortal` Area Quads mounted across open doorways (`Lobby 3` pit portal, `Lobby 2` LED forest backdrop). **Indoor**: Analytical Cove Dome encoded to **`16×16` Octahedral Specular IBL + L2 SH (`9 × float3`)** | `Cyclorama Dark Lighting`, `Sunlit Forest Showcase`, `Afternoon Racing Pit & Stadium` |

---

## 📝 3. Live HTML Demonstration (`Experimental/LightingArchitectureLab/`)

An interactive zero-dependency WebGL2 GPU demonstration runs all **5 Render Modes**, **all Luminaire Categories**, **`PriorityCut Split8` vs `Frustum Clustered` vs `ReSTIR DI Ghosting`**, and **all 3 Lobby Environments** live on a 3D automotive scene:

| Feature in `Experimental/LightingArchitectureLab/` | Verification Role |
| :--- | :--- |
| **5 Live WebGL2 Render Engines** | Switch instantaneously between `MatCap + SH2`, `SDF Analytic + GDF Cone`, `OpenPBR Direct`, `RealTime GI`, and `Deterministic PT (Sobol (0,2))` |
| **SDF Lighting Comparison Toggle** | Side-by-side comparison proving why `Analytical Direct + SDF Cone` stays crisp on clearcoat & low-beam cutoffs whereas `Voxelized Light Grid` blurs beams & leaks |
| **Scheduler Stress Test (`8..96` Self-Lit + `.ies` Luminaires)** | Live moving-vehicle & camera-cut toggle showing `PriorityCut Split8` (0 ghosting, `O(log N)`) vs `ReSTIR DI` temporal lag vs `Clustered` cutoff failure |
| **Live IES Goniometer + Cut Diagnostics** | Real-time C0°/C90° polar candela plot, `Split8` evaluation heatmap overlay, Isolux false-colour (`lx`), and indirect GI buffer view |

---

## ✔️ 4. Phased C++20 & `.slang` Execution Sequence (Gating `AutoSpace` 3 Lobbies)

| Phase | Target Modules | Concrete Deliverable | Gate |
| :---: | :--- | :--- | :--- |
| **L1** | `Engine/GeometricRaster/DirectLighting/PhotometricLightTree.{h,cpp}`<br>`Engine/Shaders/GeometricRaster/WorldViewportRadiometricCompute.slang` | Expand `PhotometricLightTree` -> `UnifiedLuminaireRegister` (`Quads`, `Capsules`, `Point`, `.ies`, `SelfLitClusters`), wire `PriorityCut` + `StochasticCut` in `.slang`, flip `UseFastGdfVis = true` in `Engine 2`, add `SH2 + Top-4` to `MatCap`, add `SkyPortal` + Octahedral Environment IBL | `0` brute-force `N`-luminaire loops; Windows + Linux builds pass; `DirectPbrWorldValidation` + `GtxSdfGiShadowValidation` pass |
| **L2** | `Engine/GeometricRaster/DirectLighting/VehiclePhotometricRig.{h,cpp}` | Attach physical automotive headlamp (`.ies`), DRL capsule, tail/brake bar, and turn-signal luminaires to `ControlVehicle` socket rigs with live brake/signal multipliers | Automotive beam + cutoff proof `.png` verified |
| **L3** | `Projects/AutoSpace/` *(3 Lobbies — Zero React/JS UI)* | Construct `Lobby1Cyclorama`, `Lobby2ForestShowcase`, and `Lobby3RacingGarage` natively inside `Projects/AutoSpace/` using the Unified Lighting Architecture (`.WorldSpace` + procedural surface codecs) | `AutoSpace --headless --verify` + offscreen Vulkan `.png` proofs across all 3 lobbies |
