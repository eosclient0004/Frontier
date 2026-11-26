# Unified Botanical & Foliage System Plan (Procedural Geometry + Baked 2.5D Leaf & Noisy-Capsule SDF + `WorldPartition` GPU↔CPU Authority)

> **Status:** Approved Blueprint (Updated with User Corrections) · **Branch:** `arena/f490d972-frontier`  
> **Scope:** Unifies Frontier's procedural botanical geometry (`Engine/GeometrySpace/Procedural/`), `SultanAladin/Frontier-1`'s baked 2.5D Thin-Leaf & Watertight Noisy-Capsule SDF pipeline, `WorldPartition` GPU↔Deterministic-CPU seamless promotion/demotion, 3-state falling/sleeping leaves, AutoSpace Lobby 2 fan `ForceField` + ferns, baked rendered Outliner plant thumbnails + SVG folder icons, and native Geometry-style Inspector cards using Frontier's existing C++ controls.

---

## 1. Asset Format & Naming Decision (No `.toml` Suffix!)

Frontier assets serialize TOML internally via `toml++` under a **single semantic domain suffix** (matching `*.SoftBody`, `*.FluidGrid`, `*.GeometrySpace`, `*.WorldSpace`, `*.VolumetricSpace` — **never** `.toml` or `.Foliage.toml`).

### Does `.Foliage` Really Cover Everything in a ~500-Species Library?

| Candidate Extension | Scope Coverage (~500 Species) | Pros | Trade-Offs |
| :--- | :--- | :--- | :--- |
| **`.Foliage`** *(Active Default)* | Trees, ferns, shrubs, grasses, palms, vines, bushes | Instant artist familiarity across game engines; aligns with existing `foliage` editor terminology; clean single-word suffix matching `.SoftBody` / `.FluidGrid`. | Strictly speaking, *"foliage"* botanically denotes leaves/fronds — slightly narrow for bare winter trunks, stumps/deadwood, roots, cacti, succulents, reeds, and moss. |
| **`.Botanical`** *(Strong Alternative)* | **100% of plant life** (trunks, roots, branches, bark, deadwood/stumps, ferns, palms, cacti, moss, grasses, underwater flora) | Exact taxonomic coverage; matches Frontier's existing `ProceduralGeometryDomain::Botanical`, `BotanicalFerns.SoftBody`, and `BotanicalForestArchitecturePlan.md`. | Slightly longer filename than `.Foliage`. |
| **`.BotanicalSpace`** | Same 100% coverage | Matches Frontier's `*Space` container family (`DataDrivenSpaceContentFormattingPlan.md`: `.GeometrySpace`, `.WorldSpace`, `.MaterialSpace`, `.VolumetricSpace`). | Longer suffix for a 500-file species directory. |

**Locked Suffix:** **`.botanical`** (lowercase, TOML-serialized via `toml++`, never `.toml`) — covers trees, ferns, palms, shrubs, grasses, roots, bark, deadwood, and moss cleanly under one semantic suffix. Starter library lives in `EngineContent/Botanical/Species/*.botanical`.

### Canonical `.Foliage` Species Schema (Supports ~500 Species via `ParentArchetype` Inheritance)

```ini
# Frontier Engine — Botanical Species Asset (.Foliage)
[Container]
FormatSuffix    = ".Foliage"
SchemaVersion   = 1
SpeciesId       = "pteridium_aquilinum"
CommonName      = "Bracken Fern"
BotanicalName   = "Pteridium aquilinum"
GrowthForm      = "Fern"              # Tree | Fern | Shrub | Grass | Palm | Vine | Succulent | Moss | Deadwood
ParentArchetype = "BaseFern"          # Prototype inheritance so 500 species avoid boilerplate
ThumbnailAsset  = "EngineContent/Thumbnails/Botanical/pteridium_aquilinum.png"

[Trunk]                               # Primary trunk for Trees/Shrubs; rhizome crown for Ferns
HeightMeters       = 1.15
BaseRadiusMeters   = 0.025
TaperExponent      = 0.85
FlareRatio         = 1.15
GnarlAmplitude     = 0.18
BarkNoiseAmp       = 0.25             # [0..1] coherent 3D FBM radial bark displacement (rest-space!)
BarkNoiseFreq      = 12.0             # [1/m]  3D furrow/ridge spatial frequency
BreakCollarHeight  = 0.0              # >0 enables deterministic Voronoi trunk fracture collar

[Roots]
Count              = 0
SpreadMeters       = 0.25
ButtressHeight     = 0.0

[[StemLevels]]                        # Level 0: Fern arched rachis (or Tree primary boughs)
Level              = 0
Count              = 9
LengthRatio        = 1.0
DownAngleDeg       = 38.0
SpreadAngleDeg     = 40.0
CurveDeg           = 55.0
CurveBackDeg       = -10.0
CapsuleSegments    = 6
InternodeSpacing   = 0.09

[[StemLevels]]                        # Level 1: Paired pinnae side stems (or Tree secondary branches/twigs)
Level              = 1
Count              = 14
LengthRatio        = 0.38
DownAngleDeg       = 68.0
PhyllotaxisDeg     = 180.0            # 180 deg opposite pairs for ferns; 137.5 deg golden angle for trees
CapsuleSegments    = 2

[Leaves]
LeafArchetypeSdf   = "fern_pinna"     # Key in baked 64x64 2D EDT Leaf Archetype Atlas
BladeScaleMeters   = 0.14
BladeAspect        = 0.42
HalfThicknessMm    = 0.6
CamberX            = 0.12
CamberZ            = 0.28
LeavesPerNode      = 2
CanDetach          = true             # Enables wind/impact shedding into WorldPartition leaf lifecycle
DetachWindMps      = 14.0

[Materials]
WoodMaterial       = "EngineContent/Materials/Botanical/FernStem.Material"
WoodUdimTile       = 1001
LeafMaterial       = "EngineContent/Materials/Botanical/BrackenFrond.Material"
LeafUdimTile       = 1002
Translucency       = 0.58

[WindAndMotion]
DefaultMotionMode  = "ForceField"     # ForceField | Vat | HeroPhysics
WindSensitivity    = 1.35
StemStiffness      = 18.0
StemDamping        = 2.6
RustleFrequencyHz  = 2.8
VatAssetPath       = ""               # Optional baked 2-channel VAT path for Tier 1/2 trees

[Collision]
CollisionClass     = "CompliantSpring" # SolidWoodCapsules | CompliantSpring | DecorativeNone
CapsuleCount       = 0
BendLimitDeg       = 65.0
VehiclePushGain    = 1.25
ReplicateOverNet   = true

[Sdf]
UseNoisyCapsules   = true
CanopyBrickRes     = 16
LodConeBias        = 1.0
```

- **`BotanicalSpeciesRegistry`** (`Engine/GeometrySpace/Procedural/Botanical/BotanicalSpeciesRegistry.{h,cpp}`) scans `EngineContent/Foliage/Species/*.Foliage`, resolves `ParentArchetype` inheritance chains, validates bounds, caches baked `64×64` 2D leaf SDF archetypes, and serves both Editor and Runtime with zero code changes as the library grows to ~500 species.

---

## 2. Seamless Merge of the Two Foliage Systems

### 2.1 System A — Procedural Botanical Geometry, Wind & Two-Channel VAT (`Engine/GeometrySpace/Procedural/Botanical/`)
Extends Frontier's existing `Engine/GeometrySpace/Procedural/ProceduralGeometry.{h,cpp}` (`ProceduralGeometryDomain::Botanical`, `IProceduralGeometryGenerator`, `EvaluateAnalyticSdf`):
- **`BotanicalSkeletonBuilder.{h,cpp}`** (Right-Hand `+Z` Up):
  - Generates deterministic **Trunk → Buttress Roots → Stem Levels (`0..3`) / Nodes / Internodes** for trees/shrubs, and **Rhizome → Arched Rachis → Paired Pinnae** for ferns/palms/grasses.
  - Emits:
    1. `PolygonNetwork` (`UDIM 1001` cylindrical bark UV + `UDIM 1002` leaf/frond UV) + `FacePartitionRegister` + `ClusterPyramid` (with wind-padded cull spheres and area-preserving foliage LOD).
    2. **Ball-Jointed Tapered Wood Capsules** (`[P_k, R_k] → [P_{k+1}, R_{k+1}]`) with exact shared joint spheres.
    3. **Instanced Leaf/Frond Attachments** (`center, basis, half-span s_i, camber (kappa_x, kappa_z), archetypeId, windFlex, detachThreshold`).
- **3 Unified Motion Modes (All Driven by 1 Shared `ForceField` System — `Engine/SimulationSpace/ForceFields/`):**
  1. **Procedural `ForceField` Wind Sway:** Analytic cantilever + gust wave driven directly by `ForceModelRegistry` / `ForceFieldSampler` (supports directional wind, gusts, vortices, and Lobby 2's `FlowStream` fan jet).
  2. **Two-Channel VAT (`VertexMotionAtlas` / `VertexAtlasFleetTable`):**
     - **1 Canonical `+X` Directional Sway LUT** (`32` wind-speed rows, azimuth-rotated at runtime by `R_z(theta)` for full 360° wind & vortex response).
     - **1 Seamless Leaf Rustle VAT** (`64` loop slices, wind-scaled amplitude + golden-ratio 2-octave self-blend to eliminate sync across 10,000+ instances).
  3. **Hero Deterministic Physics (`WorldPartition` CPU-Promoted Cells):** Jolt wood capsules + `120 Hz` deterministic `CompliantSpring` fronds (Section 3).

### 2.2 System B — Baked 2.5D Thin-Leaf SDF & Watertight Noisy Wood-Capsule SDF (Ported from `SultanAladin/Frontier-1`)
Directly ports `ThinLeafSdfCompiler.js` + `ThinLeafWgslShaders.js` (`arena/a053c75c-frontier-1`) into C++20 + Slang (`Engine/Shaders/Botanical/`):

1. **Watertight Noisy Wood Capsules (Trunk & Branches — Zero Joint Cracks, Zero Wind Swimming):**
   - Every trunk, primary bough, fork, and twig is a chain of ball-jointed tapered capsules sharing exact waypoints and radii `(P_k, R_k)`.
   - Both the closed 3D mesh and analytic SDF evaluate the exact same 2-octave sin-free modulo-289 3D bark FBM on the **rest-pose capsule skin projection `p_skin_rest`**:
     $$R_{\text{bark}}(s, \hat{\mathbf{n}}) = R(s)\,\Bigl(1 + 0.18\,\alpha_{\text{bark}}\,\text{EvalBarkFbm3d}(\mathbf{p}_{\text{skin,rest}}, f_{\text{bark}})\Bigr), \qquad d_{\text{bark}}(\mathbf{p}) = \|\mathbf{p} - \mathbf{c}(s)\| - R_{\text{bark}}(s, \hat{\mathbf{n}})$$
   - Evaluating `p_skin_rest` in the **rest-pose local capsule frame** prevents bark noise from swimming through world coordinates during wind sway while preserving `0.000 mm` seam-free shared joints (`R_bark >= 0.82 * R(s) > 0`) and `5.06e-8 m` mesh-to-SDF match.
   - Upfront analytic ray-capsule closest-approach (`EvalRayWoodCapsuleShadow`) prevents `14–34 mm` twigs from dropping out inside coarse voxel grids.

2. **Baked `64×64×K` 2D Thin-Sheet Leaf Archetype SDF + `16³` Canopy Bricks + Cone-Width LOD:**
   - **Baker (`BotanicalThinLeafSdfBaker.{h,cpp}`):** Rasterizes each leaf/frond silhouette archetype at `256×256` super-resolution, runs exact O(N²) **Felzenszwalb–Huttenlocher 2D Squared Euclidean Distance Transform**, and downsamples to a `64×64×K` `R16_SFLOAT`/`RGBA16_SFLOAT` 2D texture array. Across ~500 species sharing ~32–64 reusable silhouettes (Beech, Oak, Maple, Pine fascicle, Fern pinna, Birch, Willow, Palm frond, Grass blade, etc.), total atlas VRAM is only **`128–256 KB`**.
   - **Curved 2.5D Thin-Sheet Extrusion:** Rays stepping through an occupied `16³` canopy brick test sign changes in curved sheet height `phi(t) = y(t) - (kappa_x u(t)^2 + kappa_z w(t)^2)`, locate the exact crossing `t*` via 1 secant+Newton step, and sample `LeafArchetype2dTex` — guaranteeing **100.0% thin-leaf survival (`h = 0.6 mm`)**, **0.0% meatball dilation bloat**, and **0.71% shadow RMSE**.
   - **Continuous Cone-Width Beer–Lambert LOD:** Each `16³` brick stores precomputed 3-axis directional extinction `(sigma_x, sigma_y, sigma_z)` so distant shadow rays smoothly transition from individual serrated silhouettes to single-lookup anisotropic transmittance.
   - **1:1 Wind-Sway Sync (`BotanicalSdfWindPoseCompute.slang`):** Applies the exact active wind/VAT/`WorldPartition` pose to `WoodCapsules` and `LeafInstances` each frame so geometry, shadows, and GDF/GI remain locked together.

---

## 3. `WorldPartition` — Unified Spatial Grid, Seamless GPU↔Deterministic-CPU Authority & Falling Leaves

All spatial partitioning for plants, deterministic collision authority, and falling/sleeping leaves is unified under **`BotanicalWorldPartition`** (chunked world-space `WorldPartitionCell` grid, e.g., `16m × 16m` simulation cells subdivided into `4m × 4m` leaf-rest subcells).

### 3.1 Dynamic GPU ↔ Deterministic CPU Promotion/Demotion (Zero-Snap Guarantee!)

Instead of running non-deterministic GPU soft bodies for gameplay collision (or wasting CPU cycles on distant forest plants), **`WorldPartition`** switches authority cleanly per cell based on player, character, vehicle, and network Area-of-Interest (AOI):

| `WorldPartitionCell` State | Trigger Condition | Simulation Backend | Cost & Replication |
| :--- | :--- | :--- | :--- |
| **`GpuAmbient`** *(Unoccupied / Distant)* | No player, character, vehicle, or networked actor within `R_demote` | **100% GPU** (`ForceFieldGpuUploader` + Two-Channel VAT / GPU Wind + GPU Leaf Particles + Sleeping Subcell Indirect Draw) | **0 CPU cost**, **0 network bytes/sec** |
| **`CpuDeterministic`** *(Occupied / Network-Relevant)* | Player, character, vehicle, or networked AOI enters cell (`dist <= R_promote`) | **Deterministic `120 Hz` CPU** (`JPH_CROSS_PLATFORM_DETERMINISTIC` solid wood capsules + deterministic CPU `CompliantSpring` fronds + deterministic CPU `ForceFieldSampler`) | Bit-identical cross-platform; **0 bytes/sec predicted** + optional 4-byte quantized `(thetaX, thetaZ)` AOI reconcile or discrete fracture event via `DifferentialWorldEncoder` |

#### How We Guarantee Transitions Are **NEVER Snappy** (`C¹` Continuous Hand-Off):
1. **Spatial Hysteresis Deadzone (Zero Border Thrashing):**
   - **Promote Radius `R_promote`:** Active cell + 1-ring border (`~24 m` around player/vehicle).
   - **Demote Radius `R_demote`:** `~32 m` (`8 m` hysteresis gap so walking back and forth across a cell seam never oscillates between GPU and CPU).
2. **Bit-Exact GPU→CPU State Seeding (`t₀` Pose & Velocity Match):**
   - At the exact promotion tick `t₀`, we evaluate the **CPU analytical mirror** of the GPU wind/VAT displacement and its time derivative `(theta_gpu(t₀), omega_gpu(t₀))`, and initialize the CPU `CompliantSpring` directly to `(theta_cpu = theta_gpu(t₀), omega_cpu = omega_gpu(t₀))`.
   - A per-plant authority weight `w_cpu` ramps smoothly from `0.0 → 1.0` over **`0.30 s`** via Hermite smoothstep (`3u² - 2u³`). Because pose AND velocity already match at `t₀`, **there is zero visual discontinuity!**
3. **Settled CPU→GPU Demotion Cross-Fade:**
   - A cell only begins demoting once: (a) all tracked actors exceed `R_demote`, AND (b) no rigid-body contact has occurred for `>= 0.50 s`, AND (c) spring deviation from the ambient wind pose is below threshold (or decays smoothly during a **`0.35 s`** Hermite blend `w_cpu: 1.0 → 0.0`).
   - Result: A fern bent by a car springs back smoothly under CPU physics first, and only hands back to GPU wind once aligned — **zero snap ever!**

### 3.2 Deterministic Collision Rules Inside `CpuDeterministic` Cells
- **Trees / Solid Woody Trunks (`CollisionClass = SolidWoodCapsules`):** 1–6 smooth deterministic Jolt `CapsuleShape` bodies for trunk & primary low limbs (never noisy SDF or leaf blades). On Jolt impact or wind torque `> BreakMomentNm`, triggers deterministic Voronoi fracture (`RootedStump` + falling `DetachedCrown` + `DifferentialWorldEncoder` event).
- **Ferns / Shrubs / Tall Grass (`CollisionClass = CompliantSpring`):** Deterministic `120 Hz` 2-DOF damped angular spring per clump (or 3-segment rigid-rod chain for Hero ferns), driven by Jolt overlap push torque (vehicles/characters) + deterministic CPU `ForceFieldSampler` drag torque (wind/fans). GPU mesh + 2.5D SDF read `(thetaX, thetaZ)` directly!
- **Canopy Leaves (`CollisionClass = DecorativeNone`):** Canopy AABB / `16³` brick overlap triggers deterministic shedding count & aerodynamic wake impulse — zero Jolt rigid bodies per leaf.

### 3.3 3-State `WorldPartition` Falling & Sleeping Leaf Lifecycle (Zero Particle Budget Bloat)
Reuses Frontier's existing **`ParticleSpace`** + **`ParticleSpatialHash`** inside `4m × 4m` `WorldPartition` leaf subcells:
1. **State 0 — `Attached Canopy Leaf`:** Lives in the plant's instanced canopy + `16³` Canopy Brick SDF (**0 particle cost**). Detaches when `|ForceField| > DetachWindMps` or vehicle sweeps the brick.
2. **State 1 — `Active Falling / Blowing Leaf`:** Fixed-cap active pool (`<= 8,192` slots in `ParticleSpace`) with flat-plate aerodynamic flutter/glide, tumbling torque, live `ForceField` + vehicle fluid/SDF wake sampling, and ground/chassis SDF collision.
3. **State 2 — `Sleeping Ground Leaf` (`4m × 4m` `WorldPartition` Subcell):**
   - When an active leaf rests (`speed < 0.06 m/s` for `0.45 s`), it **leaves the active particle pool** (freeing its slot!) and packs into its `WorldPartition` subcell as a `32-byte` GPU instance (`WorldPos`, quantized orientation, `ArchetypeId`, `Scale`, `Tint`, capped at `256` leaves/subcell with LRU replacement).
   - **Zero static-mesh rebuild:** All visible sleeping subcells draw in **1 single indirect instanced draw call** with **zero per-frame simulation ALU**.
   - **Instant Wake-Up:** A lightweight subcell cull pass checks only the `4m × 4m` subcells touched by active `ForceFields` (`|F| > WakeThreshold`) or moving vehicle wakes (typically `2–12` cells); disturbed leaves immediately promote back into State 1 (`Active`), swirl through the air, and re-sleep wherever they land!

---

## 4. AutoSpace Lobby 2 Integration (`Lobby2ForestStageLevel.cpp`)

- Bind a live **`ForceModelNode` (`FlowStream` directional jet cone)** to **`l2-studio-fan`** aligned to its `AimFan` vector and blower housing nozzle.
- Connect **`l2-dust-particles`** (`ParticleSpace`) to sample the fan `ForceField` so sunbeam dust motes stream authentically from the blower.
- Spawn procedural **Bracken Fern** (`.Foliage`) clumps around the forest stage inside the fan's airstream:
  - `WorldPartition` keeps stage ferns smoothly interactive (`CompliantSpring` deterministic collision + live fan `ForceField` bend) with baked 2.5D thin-sheet frond SDF shadows + noisy rachis capsules.
  - Preserves `LobbyPolicy::EnableHeroVehiclePhysics = false` by default.

---

## 5. Outliner Icons & Native Geometry-Style Inspector UI

### 5.1 Outliner Visual Hierarchy (`OutlinerPanel` + `EditorIconAtlas` + `BotanicalThumbnailAtlas`)
- **Plant / Botanical Folders in Outliner → Official SVG Icon:**
  - Bake & add **`EngineContent/Icons/Editor/folder-botanical.svg`** (matching `folder-environment.svg` & `folder-water.svg` stroke weight and color language) registered as `EnvironmentSymbolCategory::FolderBotanical` in `EditorIconAtlas` (ThorVG `64×64` atlas).
- **Individual Plant Instances in Outliner → Baked General 3D Plant Render Thumbnail (NOT SVG!):**
  - Build a lightweight offline/asset **`BotanicalThumbnailBaker`** (`Engine/GeometrySpace/Procedural/Botanical/BotanicalThumbnailBaker.{h,cpp}`) that renders each `.Foliage` species from a clean 3/4 studio camera (transparent background, soft key+rim lighting, auto-framed bounding sphere) into **`EngineContent/Thumbnails/Botanical/<SpeciesId>.png`** (`128×128` RGBA) and loads them into a runtime `BotanicalThumbnailAtlas` consumed directly by `OutlinerPanel` rows!
  - Even with ~500 species, thumbnails load lazily on demand and stay crisp at Outliner row resolution (`18–24 px`) and Inspector header preview (`64–96 px`).

### 5.2 Inspector Panel Cards (`InspectorPanel.{h,cpp}`) — Native Controls Only!
- **Controls Rule:** Strictly use Frontier's existing native C++ controls in `Engine/Components/Controls/` (`ScalarSlider`, `DropdownSelector`, `ExclusiveSelector`, `Vector3CapsuleInput`, `ColourSwatchPicker`, `ScalarCurveEditor`, `CapsuleButton`) — **zero** custom HTML/React demo slider styles.
- **Card Architecture:** Follow Frontier's existing **Geometry / Transform Inspector card language** (`WorkspaceSurfaceColors::GenericSurface`, `22 px` rounded card radius, `WorkspaceSurfaceColors::HairlineLight` `1 px` stroke, `FontWeightClass::Light` `14 px` card title + right-aligned `8.5 px` uppercase space/category tag):
  1. **Transform Card** *(existing native Geometry card — Position / Rotation / Scale in Local/World Space)*.
  2. **Botanical Asset Card** *(Geometry-style header card)*: Baked 3D plant render thumbnail preview, Species `.Foliage` `DropdownSelector`, `Seed` `ScalarSlider`, `Scale` `ScalarSlider`, live mesh/capsule/leaf count summary strip, and `Re-Bake Thumbnail` `CapsuleButton`.
  3. **Trunk, Roots & Stems Card**: `Height`, `Base Radius`, `Gnarl`, `Bark Noise Amp`, `Bark Noise Freq`, `Branch Levels` (`ScalarSlider`).
  4. **Leaves & Shedding Card**: `Leaf Archetype` (`DropdownSelector`), `Blade Scale`, `Camber`, `Density`, `Detach Wind Threshold` (`ScalarSlider`), `Can Detach` toggle.
  5. **Wind, `WorldPartition` & Collision Card**: `Motion Mode` (`ExclusiveSelector`: `ForceField` / `VAT` / `Hero`), `Collision Class` (`DropdownSelector`), `Wind Sensitivity`, `Stem Stiffness`, `Rustle Scale`, `Promote / Demote Radius` (`ScalarSlider`), live `WorldPartition` authority badge (`GPU AMBIENT` / `BLENDING` / `CPU DETERMINISTIC`).
  6. **Baked SDF & Shadows Card**: `Noisy Capsules` toggle, `Canopy Brick Res` (`DropdownSelector`), `Cone LOD Bias` (`ScalarSlider`), live SDF stats strip (`Wood Capsules · Occupied Bricks · Archetype VRAM`).

---

## 6. Phased Implementation Roadmap & Verification Gates

| Phase | Deliverables | Verification & Proof Artifacts |
| :---: | :--- | :--- |
| **Phase 1 — `.Foliage` Format, Registry & Procedural Botanical Core** | `BotanicalSpeciesSchema.{h,cpp}`, `BotanicalSpeciesRegistry.{h,cpp}` (`.Foliage` loader with `ParentArchetype` inheritance), `BotanicalSkeletonBuilder.{h,cpp}` (Trunk + Roots + Stem Levels + Fern Fronds + Leaves + UVs + Ball-Jointed Capsules), starter `.Foliage` species (`English Oak`, `Scots Pine`, `Quaking Aspen`, `Bracken Fern`, `Silver Birch`, `Tufted Hairgrass`), wired into `ProceduralGeometryRegistry`. | CTest exhibit validating `.Foliage` parse/inheritance round-trip, deterministic geometry/UV generation, and visual renders of Trees + Ferns. |
| **Phase 2 — Noisy Wood-Capsule SDF + Baked 2.5D Thin-Leaf SDF + Wind Sync** | Port `ThinLeafSdfCompiler` (`64×64×K` Felzenszwalb–Huttenlocher 2D EDT + `16³` Canopy Bricks + 3-axis Beer–Lambert LOD) + rest-space 3D noisy wood capsules + `BotanicalSdfWindPose` into C++20 + Slang (`Engine/Shaders/Botanical/`). | Quantitative SDF gate proving `100%` thin-leaf survival, `0.0%` bloat, `0.000 mm` capsule joint seams, zero bark-noise swimming under sway, and animated proof GIF. |
| **Phase 3 — `WorldPartition` GPU↔CPU Authority, Deterministic Collision & Lobby 2 Ferns** | `BotanicalWorldPartition.{h,cpp}` (hysteresis `R_promote/R_demote`, `C¹` pose+velocity hand-off, `0.30–0.35 s` Hermite cross-fade, Jolt wood capsules, `120 Hz` CPU `CompliantSpring`, `DifferentialWorldEncoder` sync) + `BotanicalWindVatBaker` + Lobby 2 `l2-studio-fan` `ForceField` → Dust + Ferns. | Cross-peer determinism test, **zero-snap transition velocity-continuity assertion**, and animated proof GIFs (fan-blowing ferns, vehicle pushing through ferns, smooth GPU↔CPU hand-off). |
| **Phase 4 — `WorldPartition` 3-State Falling & Sleeping Leaves** | `WorldPartitionLeafLifecycle.{h,cpp}` (Attached → Active `ParticleSpace` pool `<= 8,192` → `4m × 4m` Sleeping Subcell → ForceField/Vehicle Wake-Up). | Budget cap test (`<= 8,192` active slots, `0` ALU on settled subcells) + animated proof GIF of leaves falling, settling, and waking on a gust / passing vehicle. |
| **Phase 5 — Baked Plant Render Thumbnails, Folder SVG & Geometry-Style Inspector Cards** | `BotanicalThumbnailBaker.{h,cpp}` → `EngineContent/Thumbnails/Botanical/*.png`, `EngineContent/Icons/Editor/folder-botanical.svg` (`EnvironmentSymbolCategory::FolderBotanical`), `OutlinerPanel` thumbnail rendering, and `InspectorPanel` Geometry-style cards using strictly existing native C++ sliders/controls. | Editor UI visual proof renders + green Windows & Linux CI. |
