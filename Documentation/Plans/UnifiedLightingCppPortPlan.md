# 🧩 Unified 3-System Lighting C++ / Vulkan / Slang Porting Plan

> **Status**: Active Implementation (`2026-11-25`)
> **Scope**: Port the Unified 3-System Lighting architecture (**Static 2-Cascade Auto-Baked Toroidal Clipmap** + **Dynamic Sparse `8×4×4` Cube-Grid Pods & Nanite-for-Lights Agglomerative Distance BVH** + **Unified Error-Guided Hybrid**) into Frontier's native C++20 / Vulkan / Slang engine, updating `Engine/World/`, `Engine/GeometricRaster/DirectLighting/`, `Engine/GeometricRaster/GlobalIllumination/`, `Engine/Shaders/DirectLighting/`, `Engine/Editor/Inspectors/`, and `Projects/AutoSpace/` scene light functions.

---

## 1. Architecture Summary & Synthesis

The C++ port synthesizes the best validated components from all three interactive prototypes:

1. **Static System — 2-Cascade Auto-Baked Toroidal Light Clipmap** (`StaticToroidalLightClipmap`)
   - Stores pre-baked `L0` irradiance, directionality `R`, dominant `L1` light direction, and per-cell `CellError` across a **Fine** toroidal cascade (snapped cleanly to world cells around camera/focus, scrolling via incremental dirty slabs with zero voxel swimming) and a **Coarse** full-world cascade.
   - **Automatic Scene-Scale Adaptation (`LightClipmapScalePreset`)**: `Auto`, `SmallInterior` (`16 m`), `MediumPlaza` (`36 m`), and `SuperLargeWorld` (`160 m+` Fine / `1024 m` Coarse).
   - **Zero Stall on Dynamic Motion**: Moving or animating a `Dynamic` light increments only `DynamicLightGeneration` and never invalidates the static toroidal clipmap.

2. **Dynamic System — Sparse `8×4×4` Cube-Grid Pods + Bottom-Up Agglomerative Distance Light BVH** (`UnifiedLightHierarchy` & `CubeGridPodAtlas`)
   - **Zero Center Collapse**: Each multi-lamp fixture (e.g., dual vehicle headlamps at `x = ±0.76 m`, 5-direction luminaires, LED strips) is partitioned into a sparse `8×4×4` spatial grid (`LOD 0` active occupied cells only; empty central columns remain `0`) → `LOD 1` bimodal poles → `LOD 2` single `12×12` `-Y`-pole octahedral tile → `LOD 3` `4×4` coarse tile → smooth quartic culling $w_{\text{inf}}(d) = \max(0, 1 - (d / R_{\text{inf}})^4)^2$ to `0` lookups.
   - **Bottom-Up Agglomerative Direction/Color-Aware Light BVH**: Keeps paired dynamic headlamps split at close range (`z ≈ 2 m`) and merges them smoothly at far range (`z ≥ 12 m`) based on flux-weighted angular spread.
   - **Background Residual-Error Optimizer**: Refines `(SubVoxelOffset Δp_c, ApertureRadius r_c, NonNegativeOctahedralDeconvolution W_c ≥ 0)` against unmerged ground truth (`29.36% → 4.42%` RelRMSE on dual headlamps; `36.29% → 1.80%` on 5-direction fixtures).

3. **Unified All-3 Hybrid (`UnifiedLightingMode::UnifiedAll3`)**
   - Evaluates the Static Toroidal Clipmap in `O(1)` for smooth cells, promotes only high-error static shadow/specular cells (`CellError > HybridErrorGate`) to the Static Light BVH cut, and adds the Dynamic Light BVH / Cube-Grid Pod cut.

---

## 2. Implementation Phases & File Map

| Phase | Target Files | Responsibility |
| :---: | :--- | :--- |
| **Phase 1** | `Engine/World/CategoryInstances.h`, `Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h`, `Engine/World/WorldSceneSyncBridge.{h,cpp}` | Add `LightMobility` (`Static` / `Dynamic`), `LightRigGroupId`, `InfluenceRadiusMeters`, `UnifiedLightingMode`, `LightClipmapScalePreset`, and independent `StaticLightGeneration` vs `DynamicLightGeneration` dirty tracking. |
| **Phase 2** | `Engine/GeometricRaster/DirectLighting/UnifiedLightHierarchy.{h,cpp}`, `Engine/GeometricRaster/DirectLighting/StaticToroidalLightClipmap.{h,cpp}`, `Engine/GeometricRaster/DirectLighting/CubeGridPodAtlas.{h,cpp}` | Core C++20 mobility partitioner, agglomerative direction/color-aware BVH, `-Y`-pole octahedral `8×4×4` Cube-Grid Pod compiler + residual-error optimizer, 2-cascade toroidal dirty-slab clipmap baker, VRAM & RelRMSE benchmark evaluator. |
| **Phase 3** | `Engine/Shaders/DirectLighting/BakeStaticLightClipmapCompute.slang`, `Engine/Shaders/DirectLighting/UnifiedLightEvaluation.slang` | Slang GPU compute baking for dirty toroidal slabs and shared 5-mode + 5-debug-view (`DebugChannel 23..27`) direct lighting evaluator (`texelFetch` bleed-free octahedral sampling). |
| **Phase 4** | `Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.{h,cpp}`, `Engine/Components/Diagnostics/ViewportTimingTelemetry.h` | Extend `PrecomputeUpdateBits`, `WorldRenderConfiguration`, and `ViewportTimingBreakdown` with static clipmap / pod / unified GPU timing, VRAM telemetry, and debug channels. |
| **Phase 5** | `Engine/Editor/Inspectors/LightInspectorShared.h` + `Projects/AutoSpace/Source/Levels/*.cpp` & `Projects/AutoSpace/Source/Utility/StudioRenderUtility.cpp` | Add shared Project Zero `Static | Dynamic` mobility toggle control for all 7 native light inspectors; update `Projects/AutoSpace` studio lobby light builders cleanly (keeping simple studio scenes lightweight with `UnifiedLightingMode::ExactReference` / direct lights, no forced baking). |
| **Phase 6** | `CMakeLists.txt` + `Tests/UnifiedLightHierarchyValidation.cpp` | Register all new C++20 sources in CMake and run automated C++20 validation verifying zero center collapse, mobility isolation, toroidal scroll stability, residual-error reduction, and AutoSpace compatibility. |

---

## 3. Acceptance Gates

1. **Zero Center Collapse**: Dual headlamps separated across `x = ±0.76 m` maintain empty central `8×4×4` voxels (`LOD 0`), stay split at near distance (`z = 2.2 m`), merge cleanly at far distance (`z = 13.0 m`), and improve significantly under residual-error optimization.
2. **Strict Mobility Isolation**: Moving/animating `Dynamic` lights produces `0` static clipmap re-bakes; toggling or editing `Static` lights triggers automatic dirty-region re-baking only.
3. **AutoSpace Simplicity Preserved**: `Projects/AutoSpace` lobby builders tag lights cleanly with explicit `LightMobility` (`Static` studio cyclorama/rig lights, `Dynamic` key/vehicle lights) while defaulting simple studio scenes to direct evaluation without mandatory clustering or performance baking overhead.
4. **Zero Regressions**: All new C++20 modules compile cleanly with `-std=c++20 -Wall -Wextra` and pass automated numerical validation.
