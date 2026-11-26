/*============================================================================================================================================
                                                          UNIFIEDLIGHTTYPES.H
============================================================================================================================================*/
// 🧩 Shared enums, GPU-aligned structures & telemetry types for Frontier's Unified 3-System Lighting Architecture:
//    (1) Static Lights  -> Auto-Baked 2-Cascade Toroidal Clipmap (Fine + Coarse)
//    (2) Dynamic Lights -> Sparse 8x4x4 Cube-Grid Pods (95.1 KB/pod) + Bottom-Up Agglomerative Distance Light BVH
//    (3) Unified All-3  -> Static Toroidal Clipmap + Error-Guided Promotion + Dynamic Nanite BVH/Pods

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr float    OneCubeGridPodVramKilobytes = 95.1f; // [KB]  - Compact multi-LOD octahedral pod footprint
inline constexpr uint32_t PodSpatialGridDimX          = 8u;    // [cnt] - Lateral separation cells (zero center collapse)
inline constexpr uint32_t PodSpatialGridDimY          = 4u;    // [cnt] - Vertical spatial cells
inline constexpr uint32_t PodSpatialGridDimZ          = 4u;    // [cnt] - Longitudinal spatial cells
inline constexpr uint32_t PodTotalSpatialCells        = PodSpatialGridDimX * PodSpatialGridDimY * PodSpatialGridDimZ; // 128

inline constexpr uint32_t PodTileResLod0              = 16u;   // [px]  - Near-field octahedral tile per occupied cell
inline constexpr uint32_t PodTileResLod1              = 12u;   // [px]  - Mid-field bimodal pole octahedral tile
inline constexpr uint32_t PodTileResLod2              = 12u;   // [px]  - Far-field 1-sample octahedral tile
inline constexpr uint32_t PodTileResLod3              = 4u;    // [px]  - Distant coarse directional tile

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class LightMobility : uint8_t
{
    Static  = 0u, // Auto-baked into 2-Cascade Toroidal Light Clipmap (O(1) lookup, incremental dirty-slab re-bake)
    Dynamic = 1u, // Evaluated via Sparse 8x4x4 Cube-Grid Pods + Nanite Distance Light BVH (0 static re-bakes on motion)
};

enum class UnifiedLightingMode : uint8_t
{
    ExactReference   = 0u, // Brute-force direct evaluation of every raw emitter (used by simple scenes like AutoSpace & Error x8 ref)
    NaivePointMerge  = 1u, // Center-averaged diagnostic baseline (demonstrates why naive merging fails)
    CubeGridPodsOnly = 2u, // Sparse 8x4x4 Cube-Grid Pods with continuous LOD 0..3 Hermite transitions & quartic culling
    ToroidalClipmap  = 3u, // Static 2-Cascade Toroidal Clipmap + Dynamic Cube-Grid Pods
    UnifiedAll3      = 4u, // Default large-scene production mode: Static Toroidal Clipmap + Error Promotion + Dynamic Nanite BVH/Pods
};

enum class LightClipmapScalePreset : uint8_t
{
    Auto            = 0u, // Automatically adapts Fine/Coarse cascade extents to active WorldSceneSpecification bounds
    SmallInterior   = 1u, // 16 m Fine span, high-density indoor cell resolution
    MediumPlaza     = 2u, // 36 m Fine span, balanced exterior / courtyard resolution
    SuperLargeWorld = 3u, // 160 m Fine span + 1024 m Coarse cascade + streaming toroidal dirty slabs
};

// Extends WorldRenderConfiguration::DebugChannel (0..22 existing -> 23..27 unified lighting diagnostics)
enum class UnifiedLightDebugChannel : uint32_t
{
    LightingErrorHeatmapX8        = 23u, // |ExactReference - ActiveMode| * ErrorHeatmapGain Inferno ramp
    ToroidalLightClipmapCascades  = 24u, // Fine (Cyan) vs Coarse (Amber) cascade boundaries + L1 dominant direction
    StaticVsDynamicSplit          = 25u, // Static Toroidal Clipmap (Cyan) vs Dynamic Pods/BVH (Gold)
    CubeGridPodsAndNaniteLod      = 26u, // Pod bounds, LOD 0/1/2/3 tiers & high-error cell promotions
    SplitCompareExactVsActive     = 27u, // Left ExactReference vs Right ActiveMode split comparison
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LightClipmapScaleConfig
{
    LightClipmapScalePreset Preset               = LightClipmapScalePreset::MediumPlaza;
    float                   FineHalfSpanMeters   = 6.0f;   // [m]   - Half-extent of toroidal Fine cascade around focus
    float                   CoarseHalfSpanMeters = 18.0f;  // [m]   - Half-extent of world-covering Coarse cascade
    uint32_t                FineResolution       = 16u;    // [cnt] - Cells per horizontal axis in Fine cascade
    uint32_t                CoarseResolution     = 16u;    // [cnt] - Cells per horizontal axis in Coarse cascade
    float                   DefaultNaniteBudget  = 0.24f;  // [rad] - Default dynamic BVH angular error threshold
    float                   DefaultErrorGate     = 0.18f;  // [-]   - Static clipmap error promotion threshold
};

// Canonical light primitive consumed by StaticToroidalLightClipmap, CubeGridPodAtlas, and UnifiedLightHierarchy
struct UnifiedLightSource
{
    uint32_t             SourceId        = 0u;                      // [idx] - Stable source identifier
    uint32_t             RigGroupId      = 0u;                      // [idx] - Multi-lamp fixture / pod group ID (0 = standalone)
    uint32_t             SubFixtureId    = 0u;                      // [idx] - Sub-fixture cluster tag (e.g. Left vs Right headlamp)
    LightMobility        Mobility        = LightMobility::Dynamic;  // [-]   - Routes light to Static Clipmap vs Dynamic Pod/BVH
    std::array<float, 3> PositionWorld   { 0.0f, 0.0f, 0.0f };      // [m]   - World-space emitter origin
    std::array<float, 3> DirectionWorld  { 0.0f, 0.0f, -1.0f };     // [-]   - Unit primary emission axis
    std::array<float, 3> ColorLinear     { 1.0f, 1.0f, 1.0f };      // [lin] - Normalized linear RGB chromaticity
    float                LuminousFlux    = 10.0f;                   // [lm]  - Scalar radiometric power / intensity weight
    float                CosOuterCone    = -1.0f;                   // [-]   - Cosine of outer cone half-angle (-1 = omnidirectional)
    float                ApertureRadiusM = 0.08f;                   // [m]   - Effective finite source radius for GGX / falloff
    float                InfluenceRadiusM = 20.0f;                  // [m]   - Smooth quartic distance culling radius R_inf
    bool                 EceLowBeamCutoff = false;                  // [-]   - Optional ECE R112 asymmetric low-beam cutoff
};

// 64-byte GPU-aligned dynamic/static light BVH node matching UnifiedLightEvaluation.slang
struct alignas(16) GpuLightHierarchyNode
{
    float    PositionFlux[4]   {}; // xyz = flux-weighted world centroid, w = summed luminous flux
    float    DirectionCone[4]  {}; // xyz = flux-weighted unit cone axis, w = cos(effective cone)
    float    ColorRadius[4]    {}; // rgb = flux-weighted linear color, w = cluster bounding radius [m]
    uint32_t Topology[4]       {}; // x = leftChild (or ~0u if leaf), y = rightChild, z = leafSourceIndex, w = flags(mobility|memberCount)
};
static_assert(sizeof(GpuLightHierarchyNode) == 64u, "GpuLightHierarchyNode must be exactly 64 bytes for Slang SSBO alignment.");

// Side-by-Side GPU Cost, Evals/px, VRAM & RMSE% row for diagnostics and Inspector UI
struct UnifiedLightModeMetrics
{
    UnifiedLightingMode Mode                 = UnifiedLightingMode::UnifiedAll3;
    float               GpuPassMilliseconds  = 0.0f; // [ms]  - Measured GPU pass time (VkQueryPool)
    float               SpeedupVsExact       = 1.0f; // [x]   - ExactGpuMs / ModeGpuMs
    float               AvgEvalsPerPixel     = 0.0f; // [cnt] - Average direct/clipmap evaluations per shaded pixel
    float               LightAtlasVramKb     = 0.0f; // [KB]  - Light buffers + Pod atlas + Toroidal clipmap VRAM
    float               TotalVramMb          = 0.0f; // [MB]  - Base G-Buffer/HDR + LightAtlasVram
    float               RelativeRmsePercent  = 0.0f; // [%]   - Relative RMSE against ExactReference
    float               PsnrDecibels         = 99.9f;// [dB]  - Peak signal-to-noise ratio vs ExactReference
};

[[nodiscard]] inline LightClipmapScaleConfig ResolveClipmapScaleConfig(
    LightClipmapScalePreset Preset,
    float                   SceneMaxHalfExtentMeters = 18.0f) noexcept
{
    LightClipmapScalePreset Resolved = Preset;
    if (Resolved == LightClipmapScalePreset::Auto)
    {
        if (SceneMaxHalfExtentMeters <= 10.0f)
        {
            Resolved = LightClipmapScalePreset::SmallInterior;
        }
        else if (SceneMaxHalfExtentMeters <= 45.0f)
        {
            Resolved = LightClipmapScalePreset::MediumPlaza;
        }
        else
        {
            Resolved = LightClipmapScalePreset::SuperLargeWorld;
        }
    }

    switch (Resolved)
    {
        case LightClipmapScalePreset::SmallInterior:
            return { Resolved, 4.5f, 9.0f, 16u, 12u, 0.16f, 0.12f };
        case LightClipmapScalePreset::SuperLargeWorld:
            return { Resolved, 16.0f, 80.0f, 16u, 16u, 0.34f, 0.24f };
        case LightClipmapScalePreset::MediumPlaza:
        default:
            return { Resolved, 6.0f, 18.0f, 16u, 16u, 0.24f, 0.18f };
    }
}

} // namespace Frontier::GeometricRaster
