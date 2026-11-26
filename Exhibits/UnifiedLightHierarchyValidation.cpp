/*============================================================================================================================================
                                                    UNIFIEDLIGHTHIERARCHYVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Deterministic C++20 validation exhibit for Frontier's Unified 3-System Lighting Architecture:
//    (1) -Y-Pole Octahedral round-trip & bleed-free bilinear sampling
//    (2) Non-Averaged Sparse 8x4x4 Cube-Grid Pod (zero center collapse) + Residual-Error Optimizer
//    (3) Continuous Nanite Pod LOD (0..3) & smooth quartic influence culling to 0 lookups
//    (4) Bottom-Up Agglomerative Dynamic Light BVH (near split / far merge for dual headlamps)
//    (5) Static vs Dynamic mobility isolation + incremental toroidal dirty-slab scrolling
//    (6) AutoSpace ExactReference zero-bake bypass & 5-Mode VRAM/RMSE% benchmark

#include "Engine/GeometricRaster/DirectLighting/CubeGridPodAtlas.h"
#include "Engine/GeometricRaster/DirectLighting/StaticToroidalLightClipmap.h"
#include "Engine/GeometricRaster/DirectLighting/UnifiedLightHierarchy.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{

using namespace Frontier::GeometricRaster;

void Require(bool Condition, const char* Message)
{
    if (!Condition)
    {
        std::fprintf(stderr, "[FAIL] %s\n", Message);
        std::exit(1);
    }
}

std::vector<UnifiedLightSource> BuildUnifiedTestScene()
{
    std::vector<UnifiedLightSource> Lights;
    uint32_t Id = 0u;

    // 1) Dynamic Dual Headlamps (RigGroupId = 1; Left SubFixture = 10, Right SubFixture = 20)
    for (int I = 0; I < 3; ++I)
    {
        const float OffX = (static_cast<float>(I) - 1.0f) * 0.08f;
        UnifiedLightSource L {};
        L.SourceId        = Id++;
        L.RigGroupId      = 1u;
        L.SubFixtureId    = 10u; // LeftHeadlight
        L.Mobility        = LightMobility::Dynamic;
        L.PositionWorld   = { -0.76f + OffX, 0.72f, -2.0f };
        L.DirectionWorld  = { 0.0f, -0.15f, 0.988686f };
        L.ColorLinear     = { 1.0f, 0.94f, 0.80f };
        L.LuminousFlux    = 8.5f;
        L.CosOuterCone    = 0.56f;
        L.ApertureRadiusM = 0.06f;
        L.InfluenceRadiusM = 22.0f;
        L.EceLowBeamCutoff = true;
        Lights.push_back(L);
    }
    for (int I = 0; I < 3; ++I)
    {
        const float OffX = (static_cast<float>(I) - 1.0f) * 0.08f;
        UnifiedLightSource R {};
        R.SourceId        = Id++;
        R.RigGroupId      = 1u;
        R.SubFixtureId    = 20u; // RightHeadlight
        R.Mobility        = LightMobility::Dynamic;
        R.PositionWorld   = { +0.76f + OffX, 0.72f, -2.0f };
        R.DirectionWorld  = { 0.0f, -0.15f, 0.988686f };
        R.ColorLinear     = { 1.0f, 0.94f, 0.80f };
        R.LuminousFlux    = 8.5f;
        R.CosOuterCone    = 0.56f;
        R.ApertureRadiusM = 0.06f;
        R.InfluenceRadiusM = 22.0f;
        R.EceLowBeamCutoff = true;
        Lights.push_back(R);
    }

    // 2) Static Architectural Overhead IES Downlights (RigGroupId = 2)
    constexpr float GridCoords[4][2] = { { -4.5f, -3.5f }, { 4.5f, -3.5f }, { -4.5f, 4.5f }, { 4.5f, 4.5f } };
    for (int I = 0; I < 4; ++I)
    {
        UnifiedLightSource S {};
        S.SourceId        = Id++;
        S.RigGroupId      = 2u;
        S.SubFixtureId    = 30u + static_cast<uint32_t>(I);
        S.Mobility        = LightMobility::Static;
        S.PositionWorld   = { GridCoords[I][0], 5.2f, GridCoords[I][1] };
        S.DirectionWorld  = { 0.0f, -1.0f, 0.0f };
        S.ColorLinear     = { 0.92f, 0.95f, 1.0f };
        S.LuminousFlux    = 14.0f;
        S.CosOuterCone    = 0.32f;
        S.ApertureRadiusM = 0.14f;
        S.InfluenceRadiusM = 24.0f;
        Lights.push_back(S);
    }

    // 3) Static Warm Lantern Cluster (RigGroupId = 3)
    for (int I = 0; I < 4; ++I)
    {
        const float Angle = 1.5707963f * static_cast<float>(I);
        UnifiedLightSource L {};
        L.SourceId        = Id++;
        L.RigGroupId      = 3u;
        L.SubFixtureId    = 40u;
        L.Mobility        = LightMobility::Static;
        L.PositionWorld   = { -5.2f + std::cos(Angle) * 0.35f, 2.5f, -1.8f + std::sin(Angle) * 0.35f };
        L.DirectionWorld  = { 0.0f, -1.0f, 0.0f };
        L.ColorLinear     = { 1.0f, 0.72f, 0.38f };
        L.LuminousFlux    = 7.0f;
        L.CosOuterCone    = -1.0f;
        L.ApertureRadiusM = 0.08f;
        L.InfluenceRadiusM = 18.0f;
        Lights.push_back(L);
    }

    return Lights;
}

} // namespace

int main()
{
    std::printf("=== Frontier C++20 Unified 3-System Lighting Validation ===\n");

    // 1) -Y-Pole Octahedral codec round-trip check
    {
        const std::array<std::array<float, 3>, 5> Dirs = {{
            { 0.0f, -1.0f, 0.0f },
            { 0.0f,  1.0f, 0.0f },
            { 0.6f, -0.2f, 0.7745967f },
            { -0.7f, -0.5f, -0.509902f },
            { 0.25f, 0.85f, -0.463681f }
        }};
        for (const auto& D : Dirs)
        {
            const auto Uv = OctahedralEncodeMinusY(D);
            const auto Dec = OctahedralDecodeMinusY(Uv[0], Uv[1]);
            const float Err = std::sqrt(
                (D[0] - Dec[0]) * (D[0] - Dec[0]) +
                (D[1] - Dec[1]) * (D[1] - Dec[1]) +
                (D[2] - Dec[2]) * (D[2] - Dec[2]));
            Require(Err < 1.0e-4f, "Octahedral -Y codec round-trip must be < 1e-4");
        }
        std::printf("[PASS] 1. -Y-Pole Octahedral codec round-trip verified\n");
    }

    const auto SceneLights = BuildUnifiedTestScene();

    // 2) Sparse 8x4x4 Cube-Grid Pod (Zero Center Collapse) + Residual-Error Optimization
    {
        std::vector<UnifiedLightSource> Headlamps;
        for (const auto& S : SceneLights)
        {
            if (S.RigGroupId == 1u) Headlamps.push_back(S);
        }
        CompiledCubeGridPod Pod = CompileCubeGridPod(1u, Headlamps);
        Require(!Pod.ActiveCellsLod0.empty(), "Headlamp pod must have occupied LOD 0 cells");
        for (const CubeGridActiveCell& C : Pod.ActiveCellsLod0)
        {
            Require(C.GridCoord[0] <= 1u || C.GridCoord[0] >= 6u,
                "Center X columns [2..5] between dual headlamps must remain strictly EMPTY (zero center collapse)");
        }
        const ResidualOptimizationReport Opt = OptimizeCubeGridPodResidual(Pod, Headlamps, 8u);
        std::printf("[PASS] 2. Sparse 8x4x4 Headlamp Pod (%zu occupied cells, empty center [2..5]) -> Residual Opt RelRMSE %.2f%% -> %.2f%% (PSNR %.2f -> %.2f dB)\n",
            Pod.ActiveCellsLod0.size(),
            Opt.InitialRelativeRmsePercent,
            Opt.OptimizedRelativeRmsePercent,
            Opt.InitialPsnrDecibels,
            Opt.OptimizedPsnrDecibels);
        Require(Opt.OptimizedRelativeRmsePercent <= Opt.InitialRelativeRmsePercent,
            "Residual-error optimizer must monotonically improve or match initial RelRMSE");

        // 3) Continuous Nanite Pod LOD & Quartic Culling to 0 lookups
        uint32_t NearLookups = 0u, FarLookups = 0u, CulledLookups = 99u;
        (void)EvaluateCubeGridPodAtSurface(Pod, { 0.0f, 0.0f, 0.5f },  { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.3f, { 0.7f, 0.7f, 0.7f }, 0.0f, &NearLookups);
        (void)EvaluateCubeGridPodAtSurface(Pod, { 0.0f, 0.0f, 14.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.3f, { 0.7f, 0.7f, 0.7f }, 0.0f, &FarLookups);
        (void)EvaluateCubeGridPodAtSurface(Pod, { 0.0f, 0.0f, 40.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.3f, { 0.7f, 0.7f, 0.7f }, 0.0f, &CulledLookups);
        std::printf("[PASS] 3. Continuous Pod LOD lookups: Near=%u, Far=%u, Culled=%u\n", NearLookups, FarLookups, CulledLookups);
        Require(NearLookups >= 2u && FarLookups <= 2u && CulledLookups == 0u,
            "Nanite Pod LOD must drop lookup count with distance and cull to 0 beyond R_inf");
    }

    // 4) Dynamic Bottom-Up Agglomerative Light BVH (Near Split / Far Merge)
    {
        std::vector<UnifiedLightSource> Dynamics;
        for (const auto& S : SceneLights)
        {
            if (S.Mobility == LightMobility::Dynamic) Dynamics.push_back(S);
        }
        const AgglomerativeLightBvh DynBvh = BuildAgglomerativeLightBvh(Dynamics);
        Require(DynBvh.GpuNodes.size() == DynBvh.Nodes.size() && DynBvh.RootIndex >= 0, "Dynamic BVH GPU packing mismatch");

        const auto ContainsBothHeadlamps = [](const std::vector<LightHierarchyNodeHost>& Cut) {
            for (const auto& N : Cut)
            {
                bool HasL = false, HasR = false;
                for (uint32_t Id : N.MemberSubFixtureIds)
                {
                    if (Id == 10u) HasL = true;
                    if (Id == 20u) HasR = true;
                }
                if (HasL && HasR) return true;
            }
            return false;
        };

        const auto NearCut = ResolveLightCutForReceiver(DynBvh, { 0.0f, 0.1f, 1.2f }, 0.24f, 12u);
        const auto FarCut  = ResolveLightCutForReceiver(DynBvh, { 0.0f, 0.1f, 14.0f }, 0.24f, 12u);
        const bool NearMerged = ContainsBothHeadlamps(NearCut);
        const bool FarMerged  = ContainsBothHeadlamps(FarCut);
        std::printf("[PASS] 4. Dynamic Agglomerative Light BVH: NearMerged=%d (cut=%zu), FarMerged=%d (cut=%zu)\n",
            NearMerged ? 1 : 0, NearCut.size(), FarMerged ? 1 : 0, FarCut.size());
        Require(!NearMerged && FarMerged, "Dual dynamic headlamps must stay SPLIT near and MERGE far");
    }

    // 5) Static vs Dynamic Mobility Isolation + Incremental Toroidal Dirty-Slab Scrolling
    {
        UnifiedLightingSystem Sys {};
        UnifiedLightingSyncOptions Opts {};
        Opts.Mode                  = UnifiedLightingMode::UnifiedAll3;
        Opts.ScalePreset           = LightClipmapScalePreset::MediumPlaza;
        Opts.AutoBakeStaticClipmap = true;

        // Initial sync: performs 1 full Fine+Coarse static bake
        Sys.Synchronize(SceneLights, 1ull, 1ull, { 0.0f, 0.0f, 0.0f }, Opts);
        const auto Tel1 = Sys.InspectStaticClipmap().QueryTelemetry();
        Require(Tel1.TotalFullBakeCount == 1u && Tel1.LastUpdatedCellCount == 512u, "Initial 16x16 Fine + 16x16 Coarse bake should update 512 cells");

        // Dynamic light motion (DynamicLightGeneration++ ONLY): 0 static cells baked!
        Sys.Synchronize(SceneLights, 1ull, 2ull, { 0.0f, 0.0f, 0.0f }, Opts);
        const auto Tel2 = Sys.InspectStaticClipmap().QueryTelemetry();
        Require(Tel2.TotalFullBakeCount == 1u && Tel2.TotalDirtySlabScrollCount == 0u && Tel2.LastUpdatedCellCount == 0u,
            "Dynamic light changes must NEVER invalidate or re-bake the Static Toroidal Clipmap");

        // Scroll focus by +1 toroidal Fine cell (0.75m): incremental dirty-slab scroll updates ONLY 16 boundary cells!
        Sys.Synchronize(SceneLights, 1ull, 2ull, { 0.80f, 0.0f, 0.0f }, Opts);
        const auto Tel3 = Sys.InspectStaticClipmap().QueryTelemetry();
        Require(Tel3.TotalDirtySlabScrollCount == 1u && Tel3.LastUpdatedCellCount == 16u,
            "1-cell toroidal scroll must update ONLY the 16 newly entered boundary cells (not 256!)");

        // Static light edit (StaticLightGeneration++): triggers automatic static re-bake
        Sys.Synchronize(SceneLights, 2ull, 2ull, { 0.80f, 0.0f, 0.0f }, Opts);
        const auto Tel4 = Sys.InspectStaticClipmap().QueryTelemetry();
        Require(Tel4.TotalFullBakeCount == 2u, "Static light edit must automatically re-bake static toroidal clipmap");

        std::printf("[PASS] 5. Mobility Isolation & Toroidal Dirty-Slab Scroll: DynamicMove=0 cells, 1-CellScroll=%u/256 cells, StaticEdit=re-baked\n",
            Tel3.LastUpdatedCellCount);

        // 6) 5-Mode Benchmark Table (Evals/px, VRAM & Relative RMSE%)
        const auto Bench = Sys.EvaluateAllFiveModeMetrics(1280u, 720u, 9.0f, Opts);
        const char* Names[5] = { "ExactReference  ", "NaivePointMerge ", "CubeGridPodsOnly", "ToroidalClipmap ", "UnifiedAll3     " };
        std::printf("--- Side-by-Side 5-Mode Benchmark ---\n");
        for (std::size_t I = 0u; I < 5u; ++I)
        {
            std::printf("  %s | Evals/px=%5.1f | Speedup=%4.1fx | Atlas=%6.1f KB | Total=%5.2f MB | RelRMSE=%5.1f%%\n",
                Names[I], Bench[I].AvgEvalsPerPixel, Bench[I].SpeedupVsExact,
                Bench[I].LightAtlasVramKb, Bench[I].TotalVramMb, Bench[I].RelativeRmsePercent);
        }
        Require(Bench[4].RelativeRmsePercent < Bench[1].RelativeRmsePercent,
            "UnifiedAll3 must beat NaivePointMerge on Relative RMSE%");
        Require(Bench[4].AvgEvalsPerPixel < Bench[0].AvgEvalsPerPixel,
            "UnifiedAll3 must require fewer evaluations/px than ExactReference");
    }

    // 7) AutoSpace Simple-Scene ExactReference Zero-Bake Bypass
    {
        UnifiedLightingSystem AutoSpaceSys {};
        UnifiedLightingSyncOptions AutoSpaceOpts {};
        AutoSpaceOpts.Mode                  = UnifiedLightingMode::ExactReference;
        AutoSpaceOpts.AutoBakeStaticClipmap = false;
        AutoSpaceSys.Synchronize(SceneLights, 1ull, 1ull, { 0.0f, 0.0f, 0.0f }, AutoSpaceOpts);
        Require(!AutoSpaceSys.InspectStaticClipmap().QueryInitialized() && AutoSpaceSys.InspectAllPods().empty(),
            "AutoSpace ExactReference mode must skip light clustering and static clipmap baking completely");
        std::printf("[PASS] 6. AutoSpace ExactReference zero-clustering / zero-bake bypass verified\n");
    }

    std::printf("=== ALL 6 UNIFIED LIGHTING C++20 ACCEPTANCE GATES PASSED ===\n");
    return 0;
}
