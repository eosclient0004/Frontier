/*============================================================================================================================================
                                                          UNIFIEDLIGHTHIERARCHY.H
============================================================================================================================================*/
// 🧩 Top-Level C++20 Unified 3-System Lighting Coordinator & Bottom-Up Agglomerative Direction/Color-Aware Distance Light BVH:
//    (1) Static Lights  -> StaticToroidalLightClipmap (Auto-baked 2-Cascade Fine + Coarse toroidal dirty slabs)
//    (2) Dynamic Lights -> Sparse 8x4x4 Cube-Grid Pods (CubeGridPodAtlas) + Bottom-Up Agglomerative Distance Light BVH
//    (3) Unified All-3  -> Static Toroidal Clipmap + Error-Guided Promotion + Dynamic Nanite Light BVH/Pods

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/CubeGridPodAtlas.h"
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/DirectLighting/StaticToroidalLightClipmap.h"
#include "Engine/GeometricRaster/DirectLighting/UnifiedLightTypes.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LightHierarchyNodeHost
{
    uint32_t             NodeIndex       = 0u;
    int32_t              LeftChild       = -1; // -1 for leaf
    int32_t              RightChild      = -1; // -1 for leaf
    int32_t              LeafSourceIndex = -1; // >= 0 for leaf
    LightMobility        Mobility        = LightMobility::Dynamic;
    uint32_t             RigGroupId      = 0u;
    uint32_t             SubFixtureId    = 0u;
    uint32_t             MemberCount     = 1u;
    std::array<float, 3> PositionWorld   { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> DirectionWorld  { 0.0f, -1.0f, 0.0f };
    std::array<float, 3> ColorLinear     { 1.0f, 1.0f, 1.0f };
    float                LuminousFlux    = 0.0f;
    float                CosOuterCone    = -1.0f;
    float                ClusterRadiusM  = 0.08f;
    float                InfluenceRadiusM = 20.0f;
    std::vector<uint32_t> MemberSubFixtureIds {};
};

struct AgglomerativeLightBvh
{
    std::vector<LightHierarchyNodeHost> Nodes       {};
    std::vector<GpuLightHierarchyNode>  GpuNodes    {}; // 64-byte GPU SSBO layout matching UnifiedLightEvaluation.slang
    int32_t                             RootIndex   = -1;
};

struct UnifiedLightingSyncOptions
{
    UnifiedLightingMode     Mode                   = UnifiedLightingMode::UnifiedAll3;
    LightClipmapScalePreset ScalePreset            = LightClipmapScalePreset::Auto;
    float                   SceneHalfExtentMeters  = 18.0f;
    bool                    AutoBakeStaticClipmap  = true;
    bool                    ForceStaticRebake      = false;
    bool                    OptimizePodResiduals   = false;
    uint32_t                PodOptimizationSteps   = 6u;
    float                   NaniteErrorBudget      = 0.24f;
    float                   StaticClipmapErrorGate = 0.18f;
    uint32_t                MaxActiveLightCut      = 12u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Extracts canonical UnifiedLightSource records from Frontier's PointLights, PhotometricRadiators & PolygonalAreaQuadLights
[[nodiscard]] std::vector<UnifiedLightSource> ExtractSceneUnifiedLights(
    std::span<const DirectPointLightSpec>     PointLights,
    std::span<const PhotometricPointRadiator> PhotometricRadiators,
    std::span<const PolygonalAreaQuadLight>   AreaLights);

// Builds a Bottom-Up Agglomerative Direction- & Color-Aware Light BVH (never splits paired headlamps across median planes)
[[nodiscard]] AgglomerativeLightBvh BuildAgglomerativeLightBvh(
    std::span<const UnifiedLightSource> Sources);

// Resolves the adaptive distance-LOD light cut for a receiver point using flux-weighted angular spread
[[nodiscard]] std::vector<LightHierarchyNodeHost> ResolveLightCutForReceiver(
    const AgglomerativeLightBvh& Bvh,
    const std::array<float, 3>&  ReceiverWorldPos,
    float                        ErrorBudgetRadians = 0.24f,
    uint32_t                     MaxCutNodes        = 12u);

// Computes exact VRAM breakdown (Light+Atlas KB & Total MB) for any of the 5 UnifiedLightingMode pipelines
[[nodiscard]] UnifiedLightModeMetrics ComputeUnifiedModeVram(
    UnifiedLightingMode             Mode,
    uint32_t                        ViewportWidthPx,
    uint32_t                        ViewportHeightPx,
    uint32_t                        TotalRawLightCount,
    uint32_t                        ActivePodCount,
    const StaticToroidalLightClipmap* OptionalClipmap) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                             UNIFIED LIGHTING SYSTEM HOST
//------------------------------------------------------------------------------------------------------------------------

class UnifiedLightingSystem
{
public:
    UnifiedLightingSystem() = default;

    // Synchronizes Static + Dynamic pipelines.
    // When Mode == ExactReference (e.g., simple AutoSpace studio scenes), skips clustering & static clipmap baking completely!
    void Synchronize(
        std::span<const UnifiedLightSource> AllSources,
        uint64_t                            StaticLightGeneration,
        uint64_t                            DynamicLightGeneration,
        const std::array<float, 3>&         FocusWorldPos,
        const UnifiedLightingSyncOptions&   Options,
        const ClipmapVisibilityCallback&    VisibilityFn = nullptr);

    // Evaluates all 5 UnifiedLightingMode pipelines on the scene floor to produce quantitative Evals/px, VRAM & Relative RMSE%
    [[nodiscard]] std::array<UnifiedLightModeMetrics, 5u> EvaluateAllFiveModeMetrics(
        uint32_t                         ViewportWidthPx,
        uint32_t                         ViewportHeightPx,
        float                            EvalHalfExtentMeters,
        const UnifiedLightingSyncOptions& Options,
        const ClipmapVisibilityCallback& VisibilityFn = nullptr) const;

    [[nodiscard]] std::span<const UnifiedLightSource>    InspectStaticSources() const noexcept { return StaticSources; }
    [[nodiscard]] std::span<const UnifiedLightSource>    InspectDynamicSources() const noexcept { return DynamicSources; }
    [[nodiscard]] const AgglomerativeLightBvh&           InspectDynamicBvh() const noexcept { return DynamicBvh; }
    [[nodiscard]] const AgglomerativeLightBvh&           InspectStaticBvh() const noexcept { return StaticBvh; }
    [[nodiscard]] const StaticToroidalLightClipmap&      InspectStaticClipmap() const noexcept { return StaticClipmap; }
    [[nodiscard]] std::span<const CompiledCubeGridPod>   InspectDynamicPods() const noexcept { return DynamicPods; }
    [[nodiscard]] std::span<const CompiledCubeGridPod>   InspectAllPods() const noexcept { return AllPods; }
    [[nodiscard]] const PackedPodAtlasSheet&             InspectPodAtlasSheet() const noexcept { return PodAtlasSheet; }
    [[nodiscard]] uint64_t                               QueryLastSyncedStaticGeneration() const noexcept { return LastStaticGen; }
    [[nodiscard]] uint64_t                               QueryLastSyncedDynamicGeneration() const noexcept { return LastDynamicGen; }

private:
    std::vector<UnifiedLightSource>  RawSources     {};
    std::vector<UnifiedLightSource>  StaticSources  {};
    std::vector<UnifiedLightSource>  DynamicSources {};
    AgglomerativeLightBvh            StaticBvh      {};
    AgglomerativeLightBvh            DynamicBvh     {};
    StaticToroidalLightClipmap       StaticClipmap  {};
    std::vector<CompiledCubeGridPod> AllPods        {};
    std::vector<CompiledCubeGridPod> DynamicPods    {};
    PackedPodAtlasSheet              PodAtlasSheet  {};
    uint64_t                         LastStaticGen  = 0ull;
    uint64_t                         LastDynamicGen = 0ull;
};

} // namespace Frontier::GeometricRaster
