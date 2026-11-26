# 🧩 Botanical Procedural Geometry & 3-Tier Forest Plan (`Engine/GeometrySpace/Procedural/`)

> **TL;DR**: Upgrade the existing starter `ProceduralTreeGenerator` (`"procedural.tree"`) inside **`Engine/GeometrySpace/Procedural/`** into a Weber & Penn botanical generator (ported from `unassignedinbox/Slate`, converted to Frontier Right-Hand `+Z` Up) scoped to **3 representative species (`English Oak`, `Scots Pine`, `Quaking Aspen`)** across **3 distance tiers (`Tier 0 · Tier 1 · Tier 2`)**. Drive **all** sway, leaf rustle, fracture wind-torque, and analytical SDF proxies from **1 shared WindField (`ForceFieldRegister`)**, using **1 canonical `+X` Wind-Direction Sway VAT LUT** (azimuth-rotated at runtime) + **1 seamless Rustle VAT** (multi-scale, multi-phase, 2-octave self-blended) occupying **`~4.2 MB` total VRAM** across `6 / 16` slots of the existing `VertexAtlasFleetTable`.

**Interactive HTML Reference Workbench (Project Zero Editor UI):** [`Experimental/BotanicalWorkbench/index.html`](../../Experimental/BotanicalWorkbench/index.html) (`:8081`) · **Markdown Studio:** (`:8080`)

---

## 🏷️ 1. Exact Repository Placement & Existing Engine Reuse

Frontier **already** owns `Engine/GeometrySpace/Procedural/ProceduralGeometry.{h,cpp}` with `ProceduralGeometryDomain::Botanical = 2u`, `IProceduralGeometryGenerator`, `ProceduralGeometryRegistry::Global()`, `ProceduralGeometryAsset::EnsureUpToDate(BuildClusterPyramid = true)`, an existing `EvaluateAnalyticSdf(...)` virtual hook, and starter slot `5` (`"procedural.tree"`).

| File Path | Action | Responsibility |
| :--- | :---: | :--- |
| `Engine/GeometrySpace/Procedural/ProceduralGeometry.{h,cpp}` | **Extend in place** | Replace starter `ProceduralTreeGenerator` body (`"procedural.tree"`) with the Weber & Penn botanical generator; keep `IProceduralGeometryGenerator` & `EvaluateAnalyticSdf` contract intact. |
| `Engine/GeometrySpace/Procedural/BotanicalParameters.h` | **New** | ISO C++20 `BotanicalParameters`, `CrownEnvelope` (`8` shapes), `BotanicalSdfProxyList` (capsules + canopy lobes), and `BotanicalVertexWindChannel` (`aWind`, `aPivot`). |
| `Engine/GeometrySpace/Procedural/BotanicalSpeciesPresets.{h,cpp}` | **New** | Phase-1 **3 canonical presets** (`English Oak`, `Scots Pine`, `Quaking Aspen`) with exact Slate Weber & Penn numbers; table-driven so the remaining `34` Slate presets plug in with zero code changes. |
| `Engine/GeometrySpace/Procedural/WeberPennBotanicalBuilder.{h,cpp}` | **New** | Deterministic C++20 port of Slate `skeleton.ts` + `roots.ts` + `mesher.ts` (converted from Slate `+Y` Up to Frontier Right-Hand **`+Z` Up**), emitting `PolygonNetwork` (`UDIM 1001` cylindrical bark UV + `UDIM 1002` leaf sheet UV), `FacePartitionRegister`, `ClusterPyramid`, and analytical SDF proxies. |
| `Engine/GeometrySpace/Procedural/BotanicalWindVatBaker.{h,cpp}` | **New** | Offline/setup-time baker built on `VertexMotionAtlasGpuBaker` producing **2 VAT clips per species**: (1) `DirectionalSwayLut` (`+X` canonical wind), (2) `LeafRustleVat` (seamless loop). |
| `Engine/Shaders/Botanical/BotanicalWindDeform.slang` | **New** | Shared `.slang` unit reading the **1 `ForceFieldGpuUploader` WindField** + rotated `DirectionalSwayLut` + scaled `LeafRustleVat`, imported by VisBuffer rasterization, shadow depth, and GDF proxy compute. |
| `Projects/Untitled/Content/Worlds/MainWorld.WorldSpace` | **Wire** | Instantiates `GlobalWindField` + the 3 species across `Tier 0 / Tier 1 / Tier 2` using the existing `WorldInstanceRegister`, `OutlinerPanel`, and `InspectorPanel`. |

---

## 🧩 2. Phase-1 Scope — 3 Canonical Species × 3 Distance Tiers

### 2.1 The 3 Starter Species (Full Morphological Coverage)

| Slot | Species (Slate Exact Preset) | Crown & Bark Role | Foliage Morphology | Validation Stress Test |
| :---: | :--- | :--- | :--- | :--- |
| **0** | **`English Oak`** | Hemispherical gnarled `4`-level limbs, thick buttress roots (`16 m`) | Dense broadleaf sprays | Limb pivot bend, cylindrical collar UV, Voronoi trunk fracture |
| **1** | **`Scots Pine`** | Tapered conical trunk, reddish upper bark plates (`18 m`) | Conifer needle sprays | Area-preserving cluster decimation, needle translucency |
| **2** | **`Quaking Aspen`** | `TendFlame` tall slender trunk, white lenticel bark (`13 m`) | High-count trembling leaves (`30 / twig`) | Multi-scale Rustle VAT & seamless `Tier 0 → Tier 1 → Tier 2` transition |

### 2.2 The 3 Distance Tiers (`Tier 0` · `Tier 1` · `Tier 2`) Driven by 1 WindField

| Tier | Range & Budget | Geometry & Cluster Path | Trunk / Limb Sway | Leaf Rustle / Shuffle | Analytical SDF → GDF |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Tier 0 · Hero** | `0 – 25 m`<br>(`1 – 16` trees) | Full `4`-level Weber & Penn `PolygonNetwork` + roots + `ClusterPyramid` LOD0 + Fracture seams | **1 Rotated `+X` Sway VAT LUT** + optional Jolt/XPBD vehicle/character push | **1 Scaled Rustle VAT** + optional Hero XPBD gust whip (`XpbdConstraintSolver`) | `12` wood capsules + `16` canopy lobes → `GdfFineImage3D` (`64³`) + exact `BVH4` shadows |
| **Tier 1 · Midground** | `25 – 120 m`<br>(`16 – 1,500` trees) | `ClusterPyramid` continuous screen-space LOD (`ClusterCullEncoder` + Hi-Z `DepthPyramidSequence`) | **1 Rotated `+X` Sway VAT LUT** sampled at instance WindField speed & gust | **1 Scaled Rustle VAT** (phase-offset + 2-octave self-blend via `VertexAtlasFleetTable`) | `6` wood capsules + `8` canopy lobes → `GdfCoarseImage3D` (`64³`) |
| **Tier 2 · Forest** | `120 m+`<br>(`1,500 – 10,000+` trees) | Area-preserving coarse `128`-tri clusters (`LOD2/3`) + octahedral impostors (`> 250 m`) via 1 indirect draw | **1 Rotated `+X` Sway VAT LUT** (or 1-instruction cantilever fallback on impostors) driven by traveling spatial wave | **Scaled Rustle VAT** (distance-attenuated amplitude -> `0` at horizon) | `1 – 3` merged canopy ellipsoid lobes → `GdfCoarseImage3D` (`64³`) |

---

## 🏆 3. Refined Two-Channel VAT Architecture (`1 Direction LUT` + `1 Scaled Rustle VAT`)

### 3.1 Channel A — Wind-Direction Sway LUT (Decision Table)

Legend: **VRAM / Bake Cost:** `✔️` = Lowest (best) · **Asymmetric Limb Accuracy / Direction Freedom:** `🔴` = Highest (best).

| Option | Clips / Species | Total VRAM (`3` Species) | Any 360° Wind Direction | Asymmetric Limb Stiffness | Overall |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **A. 8-Direction Baked Ring** (every `45°`) | `8` clips | 🔴 `~19 MB` | 🚩 Interpolated sectors | 🔴 Exact | `⭐⭐⭐` |
| **🥇 B. 1 Canonical `+X` Direction LUT + Runtime Azimuth Rotation** *(Recommended Phase 1)* | **`1` clip** | ✔️ **`~2.4 MB`** | 🔴 **Continuous `360°` + Vortex** | 🚩 **95% natural** (with limb-alignment factor) | `⭐⭐⭐⭐⭐` 🏆 |
| **C. 2 Orthogonal Basis LUTs (`+X` & `+Y`, blended `cos θ, sin θ`)** *(Optional Phase 2)* | `2` clips | 🚩 `~4.8 MB` | 🔴 Continuous `360°` + Vortex | 🔴 Exact anisotropic boughs | `⭐⭐⭐⭐` |

#### 💡 Exact GPU Math for 1 Canonical Wind-Direction LUT (`BotanicalWindDeform.slang`)

1. **Bake once along `+X`** (`32` rows = wind speed `[0 .. 24 m/s]` with nonlinear gust cantilever + limb drag; columns = vertices; `RGBA16F`: `xyz = ΔP_local(+X)`, `w = normalized strain`).
2. **At runtime for every vertex of every instance**:
   - Evaluate the **1 shared WindField** at instance origin: `W = SampleUnifiedWind(InstanceOrigin, Time)` → speed `s = length(W.xy)` (in Frontier `+Z` Up), azimuth `θ = atan2(W.y, W.x)`.
   - Compute LUT row `v = saturate(s / 24.0)` (linearly filtered in hardware `VK_FILTER_LINEAR`!).
   - Fetch `ΔP_canon = SampleDirectionalSwayLut(VertexIdx, v).xyz`.
   - **Rotate `ΔP_canon` horizontally by `R_z(θ)`** into the live wind direction:
     ```
     ΔP_sway.x =  ΔP_canon.x * cos(θ) - ΔP_canon.y * sin(θ)
     ΔP_sway.y =  ΔP_canon.x * sin(θ) + ΔP_canon.y * cos(θ)
     ΔP_sway.z =  ΔP_canon.z   // arc-length vertical droop stays invariant under azimuth rotation!
     ```
   - ⚠️ **Why this works so well**: Trunk cantilever bending and arc-length vertical droop (`ΔP_canon.z`) are rotationally invariant around `+Z`, and scaling lateral limb lag by `abs(dot(normalize(LocalPos.xy), WindDirXY))` keeps cross-wind limbs stiffer than downwind limbs using **only 1 LUT**!

### 3.2 Channel B — Leaf Rustle VAT (`1` Seamless VAT vs. Multiple Rustle VATs)

Legend: **VRAM / Authoring Cost:** `✔️` = Lowest (best) · **Anti-Repetition & Gust Realism:** `🔴` = Highest (best).

| Option | Clips / Species | Total VRAM (`3` Species) | Anti-Repetition (`10k` Trees) | Calm-to-Gale Dynamic Range | Overall |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **🥇 A. 1 Seamless Rustle VAT + Dynamic Scale, Rate & 2-Octave Self-Blend** *(Recommended)* | **`1` clip** (`64` loop slices) | ✔️ **`~1.8 MB`** | 🔴 **Zero visible sync** | 🔴 **Continuous `0..24 m/s`** | `⭐⭐⭐⭐⭐` 🏆 |
| **B. 3 Separate Rustle VATs** (`Calm` · `Breeze` · `Gale`) | `3` clips | 🚩 `~5.4 MB` | 🔴 Zero sync | 🔴 Explicit flutter modes | `⭐⭐⭐` |

#### 💡 Why 1 Rustle VAT per Species Is Sufficient

- **Dynamic Amplitude Scaling**: `Amp = RustleScale * smoothstep(0.4, 16.0, WindSpeed) * LeafFlex` turns the exact same 1 VAT from gentle idle shimmer at `1.5 m/s` into energetic thrashing at `18 m/s`.
- **Speed-Modulated Phase + Per-Instance & Per-Leaf Desync**:
  `u0 = fract(Time * (0.55 + 0.14 * WindSpeed) + InstancePhase + LeafPhase)`
- **Zero-Cost 2-Octave Self-Blend** (breaks any 64-frame loop periodicity completely):
  `ΔP_rustle = Amp * (0.72 * SampleRustleVat(v, u0) + 0.28 * SampleRustleVat(v, fract(u0 * 1.61803 + 0.37)))`
- **Fits Existing `VertexAtlasFleetTable` Budget With Room to Spare**:
  - `3` species × `2` VAT clips (`1` Directional Sway LUT + `1` Rustle VAT) = **`6 / 16` slots used** (`~4.2 MB` total VRAM for `10,000+` forest trees!).

---

## 🏆 4. Cluster Pyramid (Nanite-Class VisBuffer), UV Unwrap, Fracture & Wind-SDF Contracts

| Subsystem | Existing Frontier Unit Reused | Exact Phase-1 Extension for the 3 Trees |
| :--- | :--- | :--- |
| **LOD Clusters (No Separate Nanite Rewrite)** | `ClusterPyramid` (`128`-tri) · `ClusterCullEncoder` · `DepthPyramidSequence` · `VisibilityClusterRasterization` · `VisibilityMaterialResolve` | (1) Expand cluster sphere radius by `MaxSwayRadius` in `ClusterCullEncoder`; (2) area-preserving leaf quad scale (`sqrt(N_before / N_after)`) during LOD1–LOD3 decimation; (3) `64 B` `BotanicalInstanceGpuRecord` indirect compaction for `10,000+` instances. |
| **UV Unwrap & OpenPBR Materials** | `UVWorkspace` · `UVWorkspaceRasterizer` · `OpenPbrDirect` | Cylindrical `UDIM 1001` bark UV (`U = integer circumference wrap`, `V = arcLengthMeters`) + `UDIM 1002` `[0,1]²` species foliage spray sheet (`BaseColor`, `AlphaCutout`, `Normal`, `SubsurfaceTranslucency`). |
| **Fracture & Falling Crown** | `EngineeringBakedFracture` · `FastRuntimeFracture` · `ConvexPartitioning` · `SimulationFractureCouplingBridge` | Pre-authored Voronoi break collar at trunk base (`15–35%` H) with `HeartwoodSplinter` slot; on Jolt crash or WindField torque `> BreakMomentNm`, splits into `RootedStump` + falling `DetachedCrown` rigid body + `16–32` splinter shards and detaches upper SDF capsules to follow the falling body. |
| **Direct Shadows (`ContactPCSS`)** | `SolarShadowSequence` (`SolarShadowAlgorithm::ContactPcss` / `PcssPlaneAware`) | Render wind-displaced bark + alpha-tested foliage clusters directly into Frontier's existing 2-cascade `ContactPCSS` shadow maps for crisp, dappled individual leaf shadows and distance-soft penumbras — **zero blobby SDF shadow artifacts**. |
| **Proper Wood SDF + Instanced 1-Leaf Baked SDF → GDF & Emissive Radiosity** | `IProceduralGeometryGenerator::EvaluateAnalyticSdf` · `VulkanWorldRenderSequence` (`GdfFineImage3D` / `GdfCoarseImage3D` / `RadiosityMip0Tex3D`) | (1) **Branches & Trunk**: `~64` proper tapered analytical `CapsuleSdf` segments following the exact Weber & Penn skeleton; (2) **Leaves (1 Memory, Instanced `N` Times)**: bake **1 single analytical thick-leaf SDF per species** (`32×16×32 R16_SFLOAT` = **`32 KB` total**) with physical blade thickness and species silhouette (Oak lobed, Pine needle fascicle, Aspen heart) and **instance it across the canopy** via inverse wind-displaced leaf transforms (just like instanced geometry — zero blobby spheres!); (3) **Emissive Foliage GI**: injects `Mat.EmissiveRadiancePad.xyz` + two-sided translucent bounce into `RadiosityBakeDstImg3D` (`WorldViewportRadiometricCompute.slang:5038`), lighting up the trunk, branches, and ground around glowing canopies. |
| **Outliner & Inspector UI (`Projects/Untitled`)** | `OutlinerPanel` · `InspectorPanel` · `WorldInstanceRegister` | Reuse Frontier's Project Zero C++ `OutlinerPanel` directly via `WorldInstanceRegister`; expose the 6 Project Zero Inspector PageCarousel sections (`Overview`, `Botany`, `Wind`, `Cluster·UV`, `Fracture`, `SDF·GDF`) matching `Experimental/BotanicalWorkbench/`. |

---

## 🚧 5. Ordered Execution Milestones (Ready to Implement)

| Phase | Deliverable | Verification Gate |
| :---: | :--- | :--- |
| **Phase 1** | Port Weber & Penn core + **3 presets (`English Oak`, `Scots Pine`, `Quaking Aspen`)** into `Engine/GeometrySpace/Procedural/` (`+Z` Up), replacing starter `ProceduralTreeGenerator` (`"procedural.tree"`). | `Exhibits/BotanicalProceduralValidation` emits deterministic `.png` proofs of all 3 species (OpenPBR, Cylindrical UV Checker, 128-Tri Clusters, SDF Capsules+Lobes). |
| **Phase 2** | Implement `BotanicalWindVatBaker` (**1 `+X` Direction Sway LUT + 1 Rustle VAT** per species = `6` clips in `VertexAtlasFleetTable`) + `BotanicalWindDeform.slang` driven by `ForceFieldGpuUploader`. | Multi-azimuth (`0°`, `90°`, `180°`, `270°`) + Calm-vs-Gale `.png` contact sheet verifying identical sway direction across wood, leaves, and analytical SDF proxies. |
| **Phase 3** | Wire `ClusterPyramid` wind-padded bounds + area-preserving foliage LOD (`Tier 0 / Tier 1 / Tier 2`) + `GdfFineImage3D` / `GdfCoarseImage3D` analytical proxy compute splat + `Projects/Untitled/` `MainWorld.WorldSpace` Outliner/Inspector integration. | `10,000`-instance forest benchmark (`< 2 ms` cull + wind + GDF proxy splat) + interactive `Frontier` Editor & Voronoi trunk fracture verification. |
