# Frontier World Rendering & Unified Global Distance Field Replacement Plan

## 0. Non-Negotiable Engineering Constitution (Zero-Drift Rules)

Every commit and shader across Phases 1–5 MUST satisfy ALL eight rules below without exception:

1. **Single Global Distance Field — Zero Per-Ray Instance Loops**
   - Runtime shaders (`WorldViewportRadiometricCompute.slang`, `HalfResIndirectEntry`, `HighEndBvhComputeEntry`, and all `Engine/Shaders/DistanceField/*.slang` kernels) MUST **never** loop over `DynamicLocalSdfInstanceBuffer` or `DeformingBoneCapsuleBuffer` during shadow, ambient occlusion, emissive penumbra, or indirect radiosity marching.
   - Every runtime spatial distance lookup MUST execute strictly in `O(1)` via hardware `VK_IMAGE_TYPE_3D` trilinear texture fetches across the 4-cascade toroidal Global Distance Field (plus analytic ground separation).

2. **Unreal-Grade Sparse 8×8×8 Brick Dirty-AABB Updating — Zero Full-Grid Rebakes**
   - Each of the 4 cascades (`128³` `VK_FORMAT_R16_SFLOAT`) is partitioned into `16³ = 4,096` bricks (`8×8×8` voxels each) paired with a `16³` Brick Minimum-Distance Summary `VK_IMAGE_TYPE_3D` for empty-space acceleration.
   - Camera scrolling queues ONLY the newly exposed toroidal boundary bricks (`<= 5%` of volume).
   - Moving, rotating, scaling, spawning, hiding, or deforming geometry queues ONLY the dirty bricks intersecting `PreviousDilatedBounds ∪ CurrentDilatedBounds`.
   - GPU brick updating MUST run two focused compute dispatches:
     1. `GlobalDistanceBrickCullCompute.slang`: tests active instances & deforming bone capsules against each dirty brick's dilated bounds, packs a compact per-brick culled index list (`0..16` entries), writes empty bricks directly to far distance, and increments an atomic indirect dispatch counter.
     2. `GlobalDistanceBrickBuildCompute.slang` (via `vkCmdDispatchIndirect`): executes ONLY on non-empty dirty bricks (`numthreads(4,4,4)` writing `2×2×2` sub-blocks per thread across the `8×8×8` brick), evaluates ONLY that brick's culled `0..16` local distance volumes / analytical primitives / bone capsules, writes `min()` into the 3D cascade texture, and records the brick's minimum distance into the `16³` summary volume.

3. **True Signed Distance Fields & Analytic Ground Separation — Zero Light Leaks, Zero Floor Blotches**
   - Every baked `LocalSignedDistanceVolume` MUST store true signed distance (`< 0` inside solid and enclosed interiors via 5-axis irrational ray-parity + upper-enclosure sign classification so open-underbody vehicles have solid negative interiors and zero roof light leaks).
   - Analytic horizontal ground planes remain separated from the interior occluder distance field so floors never self-shadow, bloat, or bleed dark blotches.

4. **Zero Hard Cutoffs, Zero Contact-Gap Hacks, Zero Fake Ambient Cheats**
   - Ban `StartT = 0.26 m` gaps and `ClearWeight` shadow-killing hacks.
   - Ban hard luminance thresholds (`> 0.005`, `< 0.008`, `<= 0.004`) that stamp oval/egg cutouts on glossy floors.
   - Spherical emissive luminaires MUST use exact analytical spherical form-factor + Karis spherical representative-point GGX with area energy normalization `(alpha / alpha')²`.
   - Polygonal emissive luminaires MUST use exact Stokes / Lambert spherical-polygon contour integration (`StokesContourIrradiance`, GI Candidate 1 winner) + representative-point GGX specular.

5. **Full Parity Across Both Low-End GTX and High-End GTX — Zero Compromises**
   - **Single 4-Cascade Global Distance Field (`16 MB` total `R16_SFLOAT`)**, **Unreal Analytical 9-Cone DFAO + Bent Normal**, and **Unified SDF Emissive Lighting (direct + GDF soft penumbrae + 2-bounce Surfel/Radiosity GI)** run cleanly on **BOTH** Low-End GTX (`RealTimeGiGtxLowEnd`, `OpenPbrDirect`) **AND** High-End GTX (`HighEndBvh4`).
   - **Low-End GTX:** `0` BVH shadow rays — 100% pure `O(1)` 4-Cascade Global Distance Field shadows + DFAO + SDF Emissive lighting + near Stokes Contour + compact Surfel Radiosity Lattice (GI Candidate 2 winner).
   - **High-End GTX:** Same unified 4-Cascade Global Distance Field for DFAO + SDF Emissive penumbrae + multi-bounce GI, combined with `BVH4` / RayQuery primary solar/mirror precision (`0` compromises).

6. **Proper Unreal Analytical Distance Field Ambient Occlusion (DFAO + Bent Normal)**
   - Deterministic 9-cone hemisphere integration (1 vertical cone + 8 azimuthal cones, 6 exponential steps `0.08 m → 2.40 m`) sampling ONLY the `O(1)` 4-Cascade Global Distance Field — zero random ray noise, zero shimmer, zero per-instance loop.
   - Produces both world-space occlusion `[0, 1]` and bent normal, visibly grounding contact crevices under vehicles, props, and walls in BOTH outdoor and dark studio lighting, with working `FilteredAo` and `RawUdfAo` debug views on both Low-End GTX and High-End GTX.

7. **Strict Performance Discipline — Zero Hot-Path Bottlenecks or Synchronous Stalls**
   - Zero per-frame CPU triangle loops, zero Edit-mode full `SynchronizeSceneResources` calls on static frames, zero synchronous GPU stalls on live interactive updates, and zero duplicate GI gathers.

8. **Frontier Standards, Naming Purity, and Visual Proof Verification**
   - Zero new `Documentation/BannedWordsVocabulary.md` violations in any added or edited C++/Slang identifier or filename.
   - Right-Hand Rule (`+X` Forward, `+Y` Left, `+Z` Up), clean Vulkan 1.3 synchronization, and mandatory compilation + Exhibit execution + direct visual PNG inspection before closing every phase.

---

## 1. Execution Roadmap

### Phase 1 — Complete Replacement of the SDF Engine (Immediate Priority)
1. **Upgrade `LocalSignedDistanceVolume::BakeSignedFromTrianglesAspect`** (`Engine/DistanceFieldSpace/LocalSignedDistanceVolume.cpp`):
   - Upgrade inside/outside classification to 5 irrational parity axes + 5-axis upper-enclosure solid fill for open-bottom shells (e.g. `ControlVehicle`), guaranteeing leak-free negative interiors.
2. **Build Dedicated 4-Cascade Toroidal Global Distance Field Subsystem** (`Engine/DistanceFieldSpace/GlobalDistanceFieldVolume.{h,cpp}`):
   - 4 cascades (`128³` `VK_FORMAT_R16_SFLOAT` hardware 3D textures):
     - Cascade 0: `±4.0 m` (`6.25 cm` pitch)
     - Cascade 1: `±12.0 m` (`18.75 cm` pitch)
     - Cascade 2: `±36.0 m` (`56.25 cm` pitch)
     - Cascade 3: `±128.0 m` (`2.00 m` pitch)
   - 4 matching `16³` Brick Minimum-Distance Summary `VK_IMAGE_TYPE_3D` textures (`VK_FORMAT_R16_SFLOAT`) for empty-space stepping.
   - Toroidal camera-scroll brick queue + dynamic instance/bone-capsule dirty-AABB (`PreviousDilatedBounds ∪ CurrentDilatedBounds`) brick queue.
3. **Author Modular Slang Global Distance Field Shaders** (`Engine/Shaders/DistanceField/`):
   - `GlobalDistanceFieldSampling.slang`: `O(1)` 4-cascade hardware trilinear sampling (`SampleGlobalDistanceField`, `SampleGlobalDistanceCascadeFast`), analytical 9-cone DFAO + bent normal (`EvaluateDistanceFieldConeOcclusion`), leak-free pure-SDF solar & emissive cone penumbra (`TraceGlobalDistanceSolarShadow`, `TraceGlobalDistanceEmissiveShadow`, `EvaluateEmissiveSphereDirectRadiometry`).
   - `GlobalDistanceBrickCullCompute.slang`: GPU Pass 1 dirty-brick instance + bone-capsule culling & indirect dispatch count builder.
   - `GlobalDistanceBrickBuildCompute.slang`: GPU Pass 2 `vkCmdDispatchIndirect` brick evaluator writing `8×8×8` voxels (`numthreads(4,4,4)` × `2×2×2`) + `16³` brick minimum summary + fine cascade surface shell normals.
4. **Replace Legacy SDF / DFAO / Emissive Paths in `VulkanWorldRenderSequence` & `WorldViewportRadiometricCompute.slang`**:
   - Bind all 4 `128³` `GdfCascadeTex3D[4]` + `16³` `GdfBrickMinTex3D[4]` hardware textures (replacing the legacy 2-cascade `96³` SSBO + 2-texture path).
   - **Delete** the lazy `for (InstIdx = 0..LocalSdfInstCount)` and `for (C = 0..BoneCapCount)` loops from `SampleShadowSignedDistance()`.
   - Wire `TraceGlobalDistanceSolarShadow` & `TraceGlobalDistanceEmissiveShadow` cleanly (replacing `StartT = 0.26 m`, `ClearWeight`, 1-ray noisy emissive BVH, and `0.005`/`0.008`/`0.004` oval cutoffs).
   - Wire `EvaluateDistanceFieldConeOcclusion` (9-cone DFAO + Bent Normal) cleanly on **BOTH** Low-End GTX and High-End GTX, properly modulating indirect GI, sky fill, studio ambient bounce, and specular occlusion.
   - Wire dynamic transform dirty-AABB brick updates so moving any mesh or character rig updates ONLY overlapping dirty bricks in `< 0.1 ms`.
5. **Compile, Validate, and Visually Inspect**:
   - Build & run Vulkan validation exhibits (`DirectPbrWorldValidation`, `AutoSpaceStudioValidation`, `GtxSdfGiShadowValidation`, `SdfEmissivePenumbraValidation`, plus a new `GlobalDistanceFieldValidation` exhibit verifying 0-loop `O(1)` sampling, dirty-AABB moving-object updates, DFAO on both Low-End & High-End GTX, and oval-free emissive penumbrae).

### Phase 2 — FPS & Stutter Elimination + Build-Time Commit Stamp
- Remove Edit-mode per-tick full `SynchronizeSceneResources` & CPU triangle scans, eliminate synchronous frame/transform/upload stalls, remove duplicate GI gather in `HalfResIndirectEntry`, add GPU indirect visbuffer culling, cache static emissives, and compute `FRONTIER_GIT_COMMIT_HASH` at build time.

### Phase 3 — AutoSpace Scene & Photometric Light Synchronization
- Bind & sync `PhotometricRadiators` and `CeilingSoftboxToken`, couple luminaire diffuser visibility to light-folder visibility, include photometric/emissive clusters in `LastRadiosityLightingSignature`, and preserve user Edit-mode gizmo transforms on `Dynamic Emissive Orb`.

### Phase 4 — Disk-Backed Native Space Resources
- Serialize AutoSpace scenes, materials, geometry (+ embedded signed `LocalSignedDistanceVolume`), and photometry into native `.WorldSpace`, `.MaterialSpace`, `.GeometrySpace`, and `.PhotometrySpace` files and load them from disk.

### Phase 5 — Baked Per-Object Outliner Icons
- Bake unique per-object GPU specimen icons via `ThumbnailSheetRepository` + `ThumbnailSpecimenCompute.slang` and display them in `OutlinerPanel.cpp`.
