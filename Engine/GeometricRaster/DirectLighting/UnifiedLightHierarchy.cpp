/*============================================================================================================================================
                                                         UNIFIEDLIGHTHIERARCHY.CPP
============================================================================================================================================*/
// 🧩 Implementation of Unified 3-System Lighting Coordinator, Bottom-Up Agglomerative Direction/Color-Aware Light BVH & Benchmarks

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/UnifiedLightHierarchy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_set>

namespace Frontier::GeometricRaster
{

namespace
{

inline constexpr float PiF = 3.14159265358979323846f;

[[nodiscard]] float Dot3(const std::array<float, 3>& A, const std::array<float, 3>& B) noexcept
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

[[nodiscard]] std::array<float, 3> Normalize3(const std::array<float, 3>& V, const std::array<float, 3>& Fallback = { 0.0f, -1.0f, 0.0f }) noexcept
{
    const float Len = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
    if (Len <= 1.0e-6f)
    {
        return Fallback;
    }
    const float Inv = 1.0f / Len;
    return { V[0] * Inv, V[1] * Inv, V[2] * Inv };
}

[[nodiscard]] std::array<float, 3> Cross3(const std::array<float, 3>& A, const std::array<float, 3>& B) noexcept
{
    return {
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0]
    };
}

[[nodiscard]] GpuLightHierarchyNode PackHostNodeToGpu(const LightHierarchyNodeHost& N) noexcept
{
    GpuLightHierarchyNode G {};
    G.PositionFlux[0]  = N.PositionWorld[0];
    G.PositionFlux[1]  = N.PositionWorld[1];
    G.PositionFlux[2]  = N.PositionWorld[2];
    G.PositionFlux[3]  = N.LuminousFlux;
    G.DirectionCone[0] = N.DirectionWorld[0];
    G.DirectionCone[1] = N.DirectionWorld[1];
    G.DirectionCone[2] = N.DirectionWorld[2];
    G.DirectionCone[3] = N.CosOuterCone;
    G.ColorRadius[0]   = N.ColorLinear[0];
    G.ColorRadius[1]   = N.ColorLinear[1];
    G.ColorRadius[2]   = N.ColorLinear[2];
    G.ColorRadius[3]   = N.ClusterRadiusM;
    G.Topology[0]      = (N.LeftChild >= 0) ? static_cast<uint32_t>(N.LeftChild) : 0xFFFFFFFFu;
    G.Topology[1]      = (N.RightChild >= 0) ? static_cast<uint32_t>(N.RightChild) : 0xFFFFFFFFu;
    G.Topology[2]      = (N.LeafSourceIndex >= 0) ? static_cast<uint32_t>(N.LeafSourceIndex) : 0xFFFFFFFFu;
    G.Topology[3]      = (static_cast<uint32_t>(N.Mobility) << 24u) | (N.MemberCount & 0x00FFFFFFu);
    return G;
}

[[nodiscard]] std::array<float, 3> EvaluateNodeIrradianceAtFloor(
    float                             Px,
    float                             Pz,
    const std::array<float, 3>&       LightPos,
    const std::array<float, 3>&       LightDir,
    const std::array<float, 3>&       ColorLinear,
    float                             Flux,
    float                             CosOuterCone,
    float                             InfluenceRadiusM,
    const ClipmapVisibilityCallback&  VisibilityFn)
{
    const float Lx = LightPos[0] - Px;
    const float Ly = LightPos[1] - 0.02f;
    const float Lz = LightPos[2] - Pz;
    const float DistSq = Lx * Lx + Ly * Ly + Lz * Lz + 0.35f;
    const float Dist = std::sqrt(DistSq);
    const float InvD = 1.0f / Dist;
    const float Wx = Lx * InvD, Wy = Ly * InvD, Wz = Lz * InvD;
    if (Wy <= 0.0f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float Window = EvaluateQuarticInfluenceWindow(Dist, InfluenceRadiusM);
    if (Window <= 1.0e-4f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    float Spot = 1.0f;
    if (CosOuterCone > -0.95f)
    {
        const float CosAngle = -(Wx * LightDir[0] + Wy * LightDir[1] + Wz * LightDir[2]);
        const float Edge0 = CosOuterCone;
        const float Edge1 = std::min(0.99f, CosOuterCone + 0.30f);
        const float T = std::clamp((CosAngle - Edge0) / std::max(1.0e-4f, Edge1 - Edge0), 0.0f, 1.0f);
        Spot = T * T * (3.0f - 2.0f * T);
        if (Spot <= 1.0e-4f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }
    }

    const float Vis = VisibilityFn ? VisibilityFn({ Px, 0.02f, Pz }, LightPos) : 1.0f;
    if (Vis <= 0.0f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float Atten = (Flux * Wy * Spot * Vis * Window) / (DistSq * PiF);
    return { ColorLinear[0] * Atten, ColorLinear[1] * Atten, ColorLinear[2] * Atten };
}

[[nodiscard]] std::vector<UnifiedLightSource> BuildNaiveMergedSources(std::span<const UnifiedLightSource> RawSources)
{
    std::map<uint32_t, std::vector<UnifiedLightSource>> ByRig;
    for (const UnifiedLightSource& S : RawSources)
    {
        ByRig[S.RigGroupId].push_back(S);
    }

    std::vector<UnifiedLightSource> Out;
    uint32_t Id = 0u;
    for (const auto& [RigId, List] : ByRig)
    {
        float Px = 0.0f, Py = 0.0f, Pz = 0.0f;
        float Dx = 0.0f, Dy = 0.0f, Dz = 0.0f;
        float Cr = 0.0f, Cg = 0.0f, Cb = 0.0f;
        float FluxSum = 0.0f;
        float MinCos = 1.0f;
        float MaxInf = 10.0f;
        for (const UnifiedLightSource& L : List)
        {
            Px += L.PositionWorld[0]; Py += L.PositionWorld[1]; Pz += L.PositionWorld[2];
            Dx += L.DirectionWorld[0]; Dy += L.DirectionWorld[1]; Dz += L.DirectionWorld[2];
            Cr += L.ColorLinear[0] * L.LuminousFlux;
            Cg += L.ColorLinear[1] * L.LuminousFlux;
            Cb += L.ColorLinear[2] * L.LuminousFlux;
            FluxSum += L.LuminousFlux;
            MinCos = std::min(MinCos, L.CosOuterCone);
            MaxInf = std::max(MaxInf, L.InfluenceRadiusM);
        }
        const float InvN = 1.0f / static_cast<float>(List.size());
        const float InvF = 1.0f / std::max(1.0e-4f, FluxSum);
        const float DLen = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);

        UnifiedLightSource Merged {};
        Merged.SourceId        = Id++;
        Merged.RigGroupId      = RigId;
        Merged.Mobility        = List[0].Mobility;
        Merged.PositionWorld   = { Px * InvN, Py * InvN, Pz * InvN };
        Merged.DirectionWorld  = (DLen > 0.05f) ? std::array<float, 3>{ Dx / DLen, Dy / DLen, Dz / DLen } : std::array<float, 3>{ 0.0f, -1.0f, 0.0f };
        Merged.ColorLinear     = { Cr * InvF, Cg * InvF, Cb * InvF };
        Merged.LuminousFlux    = FluxSum;
        Merged.CosOuterCone    = (DLen > 0.05f) ? MinCos : -1.0f;
        Merged.InfluenceRadiusM = MaxInf;
        Out.push_back(Merged);
    }
    return Out;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                  SCENE LIGHT EXTRACTION & AGGLOMERATIVE LIGHT BVH
//------------------------------------------------------------------------------------------------------------------------

std::vector<UnifiedLightSource> ExtractSceneUnifiedLights(
    std::span<const DirectPointLightSpec>     PointLights,
    std::span<const PhotometricPointRadiator> PhotometricRadiators,
    std::span<const PolygonalAreaQuadLight>   AreaLights)
{
    std::vector<UnifiedLightSource> Out;
    Out.reserve(PointLights.size() + PhotometricRadiators.size() + AreaLights.size());
    uint32_t NextId = 0u;

    for (const DirectPointLightSpec& P : PointLights)
    {
        UnifiedLightSource Src {};
        Src.SourceId        = NextId++;
        Src.RigGroupId      = P.LightRigGroupId;
        Src.SubFixtureId    = Src.SourceId;
        Src.Mobility        = P.Mobility;
        Src.PositionWorld   = { P.PositionWorld.X, P.PositionWorld.Y, P.PositionWorld.Z };
        Src.DirectionWorld  = { 0.0f, 0.0f, -1.0f };
        Src.ColorLinear     = { P.Color.R, P.Color.G, P.Color.B };
        Src.LuminousFlux    = std::max(0.01f, P.Intensity);
        Src.CosOuterCone    = -1.0f;
        Src.ApertureRadiusM = std::max(0.04f, P.RadiusMeters);
        Src.InfluenceRadiusM = std::max(1.0f, P.RangeMeters);
        Out.push_back(Src);
    }

    for (const PhotometricPointRadiator& L : PhotometricRadiators)
    {
        const float PeakMag = std::max({ L.PeakIntensityRgb.R, L.PeakIntensityRgb.G, L.PeakIntensityRgb.B, 1.0e-4f });
        UnifiedLightSource Src {};
        Src.SourceId        = NextId++;
        Src.RigGroupId      = L.LightRigGroupId;
        Src.SubFixtureId    = Src.SourceId;
        Src.Mobility        = L.Mobility;
        Src.PositionWorld   = { L.PositionWorld.X, L.PositionWorld.Y, L.PositionWorld.Z };
        Src.DirectionWorld  = Normalize3({ L.BeamAxis.X, L.BeamAxis.Y, L.BeamAxis.Z }, { 0.0f, 0.0f, -1.0f });
        Src.ColorLinear     = { L.PeakIntensityRgb.R / PeakMag, L.PeakIntensityRgb.G / PeakMag, L.PeakIntensityRgb.B / PeakMag };
        Src.LuminousFlux    = PeakMag;
        Src.CosOuterCone    = 0.50f;
        Src.ApertureRadiusM = std::max(0.04f, L.RadiusMeters);
        Src.InfluenceRadiusM = std::max(1.0f, L.RangeMeters);
        Out.push_back(Src);
    }

    for (const PolygonalAreaQuadLight& Q : AreaLights)
    {
        const float Area = 4.0f * Q.HalfWidthMeters * Q.HalfHeightMeters;
        const float PeakMag = std::max({ Q.EmittedRadiance.R, Q.EmittedRadiance.G, Q.EmittedRadiance.B, 1.0e-4f });
        const auto Normal = Normalize3(Cross3(
            { Q.RightTangent.X, Q.RightTangent.Y, Q.RightTangent.Z },
            { Q.UpBitangent.X, Q.UpBitangent.Y, Q.UpBitangent.Z }), { 0.0f, 0.0f, -1.0f });

        UnifiedLightSource Src {};
        Src.SourceId        = NextId++;
        Src.RigGroupId      = Q.LightRigGroupId;
        Src.SubFixtureId    = Src.SourceId;
        Src.Mobility        = Q.Mobility;
        Src.PositionWorld   = { Q.CenterWorld.X, Q.CenterWorld.Y, Q.CenterWorld.Z };
        Src.DirectionWorld  = Normal;
        Src.ColorLinear     = { Q.EmittedRadiance.R / PeakMag, Q.EmittedRadiance.G / PeakMag, Q.EmittedRadiance.B / PeakMag };
        Src.LuminousFlux    = PeakMag * std::max(0.25f, Area);
        Src.CosOuterCone    = Q.TwoSidedEmission ? -1.0f : 0.05f;
        Src.ApertureRadiusM = std::clamp(0.5f * std::sqrt(Area), 0.08f, 1.5f);
        Src.InfluenceRadiusM = 28.0f;
        Out.push_back(Src);
    }

    return Out;
}

AgglomerativeLightBvh BuildAgglomerativeLightBvh(std::span<const UnifiedLightSource> Sources)
{
    AgglomerativeLightBvh Bvh {};
    if (Sources.empty())
    {
        return Bvh;
    }

    Bvh.Nodes.reserve(Sources.size() * 2u);
    std::vector<int32_t> ActivePool;
    ActivePool.reserve(Sources.size());

    // 1) Initialize leaf nodes
    for (std::size_t I = 0u; I < Sources.size(); ++I)
    {
        const UnifiedLightSource& S = Sources[I];
        LightHierarchyNodeHost Leaf {};
        Leaf.NodeIndex       = static_cast<uint32_t>(Bvh.Nodes.size());
        Leaf.LeafSourceIndex = static_cast<int32_t>(I);
        Leaf.Mobility        = S.Mobility;
        Leaf.RigGroupId      = S.RigGroupId;
        Leaf.SubFixtureId    = S.SubFixtureId;
        Leaf.MemberCount     = 1u;
        Leaf.PositionWorld   = S.PositionWorld;
        Leaf.DirectionWorld  = Normalize3(S.DirectionWorld);
        Leaf.ColorLinear     = S.ColorLinear;
        Leaf.LuminousFlux    = S.LuminousFlux;
        Leaf.CosOuterCone    = S.CosOuterCone;
        Leaf.ClusterRadiusM  = std::max(0.05f, S.ApertureRadiusM);
        Leaf.InfluenceRadiusM = S.InfluenceRadiusM;
        Leaf.MemberSubFixtureIds.push_back(S.SubFixtureId);

        ActivePool.push_back(static_cast<int32_t>(Leaf.NodeIndex));
        Bvh.Nodes.push_back(std::move(Leaf));
    }

    // 2) Bottom-up agglomerative pairing minimizing spatial distance + cone-axis divergence + chromatic distance
    while (ActivePool.size() > 1u)
    {
        std::size_t BestI = 0u;
        std::size_t BestJ = 1u;
        float BestCost = std::numeric_limits<float>::max();

        for (std::size_t I = 0u; I < ActivePool.size(); ++I)
        {
            const LightHierarchyNodeHost& A = Bvh.Nodes[static_cast<std::size_t>(ActivePool[I])];
            for (std::size_t J = I + 1u; J < ActivePool.size(); ++J)
            {
                const LightHierarchyNodeHost& B = Bvh.Nodes[static_cast<std::size_t>(ActivePool[J])];
                const float Dx = A.PositionWorld[0] - B.PositionWorld[0];
                const float Dy = A.PositionWorld[1] - B.PositionWorld[1];
                const float Dz = A.PositionWorld[2] - B.PositionWorld[2];
                const float SpatialDist = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);

                const float DirAlign = Dot3(A.DirectionWorld, B.DirectionWorld);
                const float DirPenalty = (1.0f - DirAlign) * 3.8f;

                const float Cr = A.ColorLinear[0] - B.ColorLinear[0];
                const float Cg = A.ColorLinear[1] - B.ColorLinear[1];
                const float Cb = A.ColorLinear[2] - B.ColorLinear[2];
                const float ColorPenalty = std::sqrt(Cr * Cr + Cg * Cg + Cb * Cb) * 2.5f;

                // Slight affinity when both belong to the same fixture rig so pairs (e.g., L+R headlamps) share a parent cleanly
                const float SameRigBonus = (A.RigGroupId != 0u && A.RigGroupId == B.RigGroupId) ? 0.85f : 1.0f;
                const float Cost = (SpatialDist + DirPenalty + ColorPenalty) * SameRigBonus;
                if (Cost < BestCost)
                {
                    BestCost = Cost;
                    BestI = I;
                    BestJ = J;
                }
            }
        }

        const int32_t IdxA = ActivePool[BestI];
        const int32_t IdxB = ActivePool[BestJ];
        const LightHierarchyNodeHost& A = Bvh.Nodes[static_cast<std::size_t>(IdxA)];
        const LightHierarchyNodeHost& B = Bvh.Nodes[static_cast<std::size_t>(IdxB)];

        const float TotalFlux = std::max(1.0e-4f, A.LuminousFlux + B.LuminousFlux);
        const float Wa = A.LuminousFlux / TotalFlux;
        const float Wb = B.LuminousFlux / TotalFlux;

        LightHierarchyNodeHost Parent {};
        Parent.NodeIndex       = static_cast<uint32_t>(Bvh.Nodes.size());
        Parent.LeftChild       = IdxA;
        Parent.RightChild      = IdxB;
        Parent.LeafSourceIndex = -1;
        Parent.Mobility        = A.Mobility;
        Parent.RigGroupId      = (A.RigGroupId == B.RigGroupId) ? A.RigGroupId : 0u;
        Parent.SubFixtureId    = (A.SubFixtureId == B.SubFixtureId) ? A.SubFixtureId : 0u;
        Parent.MemberCount     = A.MemberCount + B.MemberCount;
        Parent.LuminousFlux    = TotalFlux;
        Parent.PositionWorld   = {
            A.PositionWorld[0] * Wa + B.PositionWorld[0] * Wb,
            A.PositionWorld[1] * Wa + B.PositionWorld[1] * Wb,
            A.PositionWorld[2] * Wa + B.PositionWorld[2] * Wb
        };
        Parent.ColorLinear = {
            A.ColorLinear[0] * Wa + B.ColorLinear[0] * Wb,
            A.ColorLinear[1] * Wa + B.ColorLinear[1] * Wb,
            A.ColorLinear[2] * Wa + B.ColorLinear[2] * Wb
        };
        const std::array<float, 3> RawDir {
            A.DirectionWorld[0] * Wa + B.DirectionWorld[0] * Wb,
            A.DirectionWorld[1] * Wa + B.DirectionWorld[1] * Wb,
            A.DirectionWorld[2] * Wa + B.DirectionWorld[2] * Wb
        };
        const float DirLen = std::sqrt(RawDir[0] * RawDir[0] + RawDir[1] * RawDir[1] + RawDir[2] * RawDir[2]);
        Parent.DirectionWorld = (DirLen > 0.12f) ? Normalize3(RawDir) : std::array<float, 3>{ 0.0f, -1.0f, 0.0f };
        Parent.CosOuterCone   = (DirLen > 0.12f) ? std::min(A.CosOuterCone, B.CosOuterCone) : -1.0f;

        const float DistToA = std::sqrt(
            (A.PositionWorld[0] - Parent.PositionWorld[0]) * (A.PositionWorld[0] - Parent.PositionWorld[0]) +
            (A.PositionWorld[1] - Parent.PositionWorld[1]) * (A.PositionWorld[1] - Parent.PositionWorld[1]) +
            (A.PositionWorld[2] - Parent.PositionWorld[2]) * (A.PositionWorld[2] - Parent.PositionWorld[2])) + A.ClusterRadiusM;
        const float DistToB = std::sqrt(
            (B.PositionWorld[0] - Parent.PositionWorld[0]) * (B.PositionWorld[0] - Parent.PositionWorld[0]) +
            (B.PositionWorld[1] - Parent.PositionWorld[1]) * (B.PositionWorld[1] - Parent.PositionWorld[1]) +
            (B.PositionWorld[2] - Parent.PositionWorld[2]) * (B.PositionWorld[2] - Parent.PositionWorld[2])) + B.ClusterRadiusM;
        Parent.ClusterRadiusM   = std::max(DistToA, DistToB);
        Parent.InfluenceRadiusM = std::max(A.InfluenceRadiusM, B.InfluenceRadiusM);

        Parent.MemberSubFixtureIds.reserve(A.MemberSubFixtureIds.size() + B.MemberSubFixtureIds.size());
        Parent.MemberSubFixtureIds.insert(Parent.MemberSubFixtureIds.end(), A.MemberSubFixtureIds.begin(), A.MemberSubFixtureIds.end());
        Parent.MemberSubFixtureIds.insert(Parent.MemberSubFixtureIds.end(), B.MemberSubFixtureIds.begin(), B.MemberSubFixtureIds.end());

        const int32_t ParentIdx = static_cast<int32_t>(Parent.NodeIndex);
        Bvh.Nodes.push_back(std::move(Parent));

        ActivePool[BestI] = ParentIdx;
        ActivePool.erase(ActivePool.begin() + static_cast<std::ptrdiff_t>(BestJ));
    }

    Bvh.RootIndex = ActivePool[0];
    Bvh.GpuNodes.reserve(Bvh.Nodes.size());
    for (const LightHierarchyNodeHost& N : Bvh.Nodes)
    {
        Bvh.GpuNodes.push_back(PackHostNodeToGpu(N));
    }
    return Bvh;
}

std::vector<LightHierarchyNodeHost> ResolveLightCutForReceiver(
    const AgglomerativeLightBvh& Bvh,
    const std::array<float, 3>&  ReceiverWorldPos,
    float                        ErrorBudgetRadians,
    uint32_t                     MaxCutNodes)
{
    std::vector<LightHierarchyNodeHost> Cut;
    if (Bvh.RootIndex < 0 || Bvh.Nodes.empty())
    {
        return Cut;
    }

    auto ComputeNodeError = [&](const LightHierarchyNodeHost& N) noexcept -> float {
        if (N.LeftChild < 0 || N.RightChild < 0)
        {
            return 0.0f; // Leaf cannot split further
        }
        const float Dx = N.PositionWorld[0] - ReceiverWorldPos[0];
        const float Dy = N.PositionWorld[1] - ReceiverWorldPos[1];
        const float Dz = N.PositionWorld[2] - ReceiverWorldPos[2];
        const float Dist = std::max(0.45f, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
        const float FluxScale = std::sqrt(std::max(1.0f, N.LuminousFlux) / 14.0f);
        return (N.ClusterRadiusM / Dist) * FluxScale;
    };

    std::vector<int32_t> Frontier { Bvh.RootIndex };
    while (Frontier.size() < MaxCutNodes)
    {
        int32_t BestSlot = -1;
        float BestErr = ErrorBudgetRadians;
        for (std::size_t I = 0u; I < Frontier.size(); ++I)
        {
            const LightHierarchyNodeHost& N = Bvh.Nodes[static_cast<std::size_t>(Frontier[I])];
            const float Err = ComputeNodeError(N);
            if (Err > BestErr)
            {
                BestErr = Err;
                BestSlot = static_cast<int32_t>(I);
            }
        }
        if (BestSlot < 0)
        {
            break;
        }
        const LightHierarchyNodeHost& Parent = Bvh.Nodes[static_cast<std::size_t>(Frontier[static_cast<std::size_t>(BestSlot)])];
        Frontier[static_cast<std::size_t>(BestSlot)] = Parent.LeftChild;
        Frontier.push_back(Parent.RightChild);
    }

    Cut.reserve(Frontier.size());
    for (int32_t Idx : Frontier)
    {
        Cut.push_back(Bvh.Nodes[static_cast<std::size_t>(Idx)]);
    }
    return Cut;
}

UnifiedLightModeMetrics ComputeUnifiedModeVram(
    UnifiedLightingMode               Mode,
    uint32_t                          ViewportWidthPx,
    uint32_t                          ViewportHeightPx,
    uint32_t                          TotalRawLightCount,
    uint32_t                          ActivePodCount,
    const StaticToroidalLightClipmap* OptionalClipmap) noexcept
{
    UnifiedLightModeMetrics Metrics {};
    Metrics.Mode = Mode;

    // Base G-Buffer + HDR + Depth footprint (42 bytes/pixel)
    const float BaseRtMb = std::max(4.0f, (static_cast<float>(ViewportWidthPx) * static_cast<float>(ViewportHeightPx) * 42.0f) / (1024.0f * 1024.0f));
    const float ClipKb   = (OptionalClipmap && OptionalClipmap->QueryInitialized())
        ? OptionalClipmap->QueryTelemetry().TotalVramKilobytes
        : 256.0f;

    float LightAtlasKb = 0.0f;
    switch (Mode)
    {
        case UnifiedLightingMode::ExactReference:
            LightAtlasKb = (static_cast<float>(TotalRawLightCount) * 64.0f) / 1024.0f;
            break;
        case UnifiedLightingMode::NaivePointMerge:
            LightAtlasKb = (6.0f * 64.0f) / 1024.0f;
            break;
        case UnifiedLightingMode::CubeGridPodsOnly:
            LightAtlasKb = static_cast<float>(ActivePodCount) * OneCubeGridPodVramKilobytes;
            break;
        case UnifiedLightingMode::ToroidalClipmap:
            LightAtlasKb = ClipKb + static_cast<float>(ActivePodCount) * OneCubeGridPodVramKilobytes * 0.25f;
            break;
        case UnifiedLightingMode::UnifiedAll3:
        default:
            LightAtlasKb = ClipKb + static_cast<float>(std::max(1u, ActivePodCount)) * OneCubeGridPodVramKilobytes + 48.0f;
            break;
    }

    Metrics.LightAtlasVramKb = LightAtlasKb;
    Metrics.TotalVramMb      = BaseRtMb + LightAtlasKb / 1024.0f;
    return Metrics;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             UNIFIED LIGHTING SYSTEM HOST
//------------------------------------------------------------------------------------------------------------------------

void UnifiedLightingSystem::Synchronize(
    std::span<const UnifiedLightSource> AllSources,
    uint64_t                            StaticLightGeneration,
    uint64_t                            DynamicLightGeneration,
    const std::array<float, 3>&         FocusWorldPos,
    const UnifiedLightingSyncOptions&   Options,
    const ClipmapVisibilityCallback&    VisibilityFn)
{
    RawSources.assign(AllSources.begin(), AllSources.end());
    StaticSources.clear();
    DynamicSources.clear();
    for (const UnifiedLightSource& S : RawSources)
    {
        if (S.Mobility == LightMobility::Static) StaticSources.push_back(S);
        else DynamicSources.push_back(S);
    }

    // Simple scenes (e.g., AutoSpace studio lobbies) using ExactReference skip clustering & static baking completely!
    if (Options.Mode == UnifiedLightingMode::ExactReference)
    {
        LastStaticGen  = StaticLightGeneration;
        LastDynamicGen = DynamicLightGeneration;
        return;
    }

    // 1) Dynamic Pipeline: Agglomerative Light BVH + Dynamic Cube-Grid Pods (never invalidates static clipmap!)
    if (LastDynamicGen != DynamicLightGeneration || DynamicBvh.Nodes.empty())
    {
        DynamicBvh = BuildAgglomerativeLightBvh(DynamicSources);
        LastDynamicGen = DynamicLightGeneration;
    }

    // 2) Static Pipeline: Agglomerative Static BVH (for rare high-error cell promotion) + Auto-Baked Toroidal Clipmap
    const bool StaticDirty = (LastStaticGen != StaticLightGeneration) || StaticBvh.Nodes.empty() || Options.ForceStaticRebake;
    if (StaticDirty)
    {
        StaticBvh = BuildAgglomerativeLightBvh(StaticSources);
        LastStaticGen = StaticLightGeneration;
    }

    if (Options.AutoBakeStaticClipmap || Options.ForceStaticRebake || !StaticClipmap.QueryInitialized())
    {
        const LightClipmapScaleConfig ScaleCfg = ResolveClipmapScaleConfig(Options.ScalePreset, Options.SceneHalfExtentMeters);
        (void)StaticClipmap.UpdateAutomatic(
            StaticSources,
            StaticLightGeneration,
            FocusWorldPos,
            ScaleCfg,
            Options.ForceStaticRebake,
            VisibilityFn);
    }

    // 3) Compile Sparse 8x4x4 Cube-Grid Pods grouped by RigGroupId
    AllPods.clear();
    DynamicPods.clear();
    std::map<uint32_t, std::vector<UnifiedLightSource>> RigBuckets;
    uint32_t StandaloneRigId = 10000u;
    for (const UnifiedLightSource& S : RawSources)
    {
        const uint32_t Key = (S.RigGroupId != 0u) ? S.RigGroupId : StandaloneRigId++;
        RigBuckets[Key].push_back(S);
    }
    for (const auto& [RigId, Bucket] : RigBuckets)
    {
        CompiledCubeGridPod Pod = CompileCubeGridPod(RigId, Bucket);
        if (Options.OptimizePodResiduals && Bucket.size() > 1u)
        {
            (void)OptimizeCubeGridPodResidual(Pod, Bucket, Options.PodOptimizationSteps);
        }
        if (Pod.Mobility == LightMobility::Dynamic)
        {
            DynamicPods.push_back(Pod);
        }
        AllPods.push_back(std::move(Pod));
    }

    PodAtlasSheet = PackPodAtlasSheetRgba32F(AllPods, 256u);
}

std::array<UnifiedLightModeMetrics, 5u> UnifiedLightingSystem::EvaluateAllFiveModeMetrics(
    uint32_t                          ViewportWidthPx,
    uint32_t                          ViewportHeightPx,
    float                             EvalHalfExtentMeters,
    const UnifiedLightingSyncOptions& Options,
    const ClipmapVisibilityCallback&  VisibilityFn) const
{
    std::array<UnifiedLightModeMetrics, 5u> Table {};
    const std::vector<UnifiedLightSource> NaiveLights = BuildNaiveMergedSources(RawSources);

    constexpr uint32_t GridSamples = 14u;
    const float Step = (2.0f * EvalHalfExtentMeters) / static_cast<float>(GridSamples);
    const auto Tm = [](float V) noexcept { return V / (1.0f + std::max(0.0f, V)); };

    const UnifiedLightingMode Modes[5] = {
        UnifiedLightingMode::ExactReference,
        UnifiedLightingMode::NaivePointMerge,
        UnifiedLightingMode::CubeGridPodsOnly,
        UnifiedLightingMode::ToroidalClipmap,
        UnifiedLightingMode::UnifiedAll3
    };

    for (std::size_t M = 0u; M < 5u; ++M)
    {
        const UnifiedLightingMode Mode = Modes[M];
        const uint32_t ActivePodCount = (Mode == UnifiedLightingMode::ToroidalClipmap || Mode == UnifiedLightingMode::UnifiedAll3)
            ? static_cast<uint32_t>(DynamicPods.size())
            : static_cast<uint32_t>(AllPods.size());

        UnifiedLightModeMetrics Row = ComputeUnifiedModeVram(
            Mode, ViewportWidthPx, ViewportHeightPx,
            static_cast<uint32_t>(RawSources.size()), ActivePodCount, &StaticClipmap);

        double SqErrSum = 0.0;
        double SqRefSum = 0.0;
        double EvalsSum = 0.0;

        for (uint32_t Sz = 0u; Sz < GridSamples; ++Sz)
        {
            for (uint32_t Sx = 0u; Sx < GridSamples; ++Sx)
            {
                const float Px = -EvalHalfExtentMeters + (static_cast<float>(Sx) + 0.5f) * Step;
                const float Pz = -EvalHalfExtentMeters + (static_cast<float>(Sz) + 0.5f) * Step;
                const std::array<float, 3> Pos { Px, 0.02f, Pz };

                float Tr = 0.0f, Tg = 0.0f, Tb = 0.0f;
                for (const UnifiedLightSource& L : RawSources)
                {
                    const auto C = EvaluateNodeIrradianceAtFloor(Px, Pz, L.PositionWorld, L.DirectionWorld, L.ColorLinear, L.LuminousFlux, L.CosOuterCone, L.InfluenceRadiusM, VisibilityFn);
                    Tr += C[0]; Tg += C[1]; Tb += C[2];
                }

                float Ar = 0.0f, Ag = 0.0f, Ab = 0.0f;
                double Evals = 0.0;

                if (Mode == UnifiedLightingMode::ExactReference)
                {
                    Ar = Tr; Ag = Tg; Ab = Tb;
                    Evals = static_cast<double>(RawSources.size());
                }
                else if (Mode == UnifiedLightingMode::NaivePointMerge)
                {
                    Evals = static_cast<double>(NaiveLights.size());
                    for (const UnifiedLightSource& L : NaiveLights)
                    {
                        const auto C = EvaluateNodeIrradianceAtFloor(Px, Pz, L.PositionWorld, L.DirectionWorld, L.ColorLinear, L.LuminousFlux, L.CosOuterCone, L.InfluenceRadiusM, VisibilityFn);
                        Ar += C[0]; Ag += C[1]; Ab += C[2];
                    }
                }
                else if (Mode == UnifiedLightingMode::CubeGridPodsOnly)
                {
                    for (const CompiledCubeGridPod& Pod : AllPods)
                    {
                        uint32_t PodLookups = 0u;
                        const auto C = EvaluateCubeGridPodAtSurface(Pod, Pos, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.6f, { 1.0f, 1.0f, 1.0f }, 0.0f, &PodLookups);
                        Ar += C[0]; Ag += C[1]; Ab += C[2];
                        Evals += static_cast<double>(PodLookups);
                    }
                }
                else if (Mode == UnifiedLightingMode::ToroidalClipmap)
                {
                    const ToroidalClipmapSample S = StaticClipmap.SampleAtWorldXZ(Px, Pz);
                    Ar += S.IrradianceL0[0]; Ag += S.IrradianceL0[1]; Ab += S.IrradianceL0[2];
                    Evals += 1.0;
                    for (const CompiledCubeGridPod& Pod : DynamicPods)
                    {
                        uint32_t PodLookups = 0u;
                        const auto C = EvaluateCubeGridPodAtSurface(Pod, Pos, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.6f, { 1.0f, 1.0f, 1.0f }, 0.0f, &PodLookups);
                        Ar += C[0]; Ag += C[1]; Ab += C[2];
                        Evals += static_cast<double>(PodLookups);
                    }
                }
                else // UnifiedAll3
                {
                    const ToroidalClipmapSample S = StaticClipmap.SampleAtWorldXZ(Px, Pz);
                    if (S.CellError <= Options.StaticClipmapErrorGate)
                    {
                        Ar += S.IrradianceL0[0]; Ag += S.IrradianceL0[1]; Ab += S.IrradianceL0[2];
                        Evals += 1.0;
                    }
                    else
                    {
                        const auto StaticCut = ResolveLightCutForReceiver(StaticBvh, Pos, Options.NaniteErrorBudget * 0.85f, Options.MaxActiveLightCut);
                        Evals += static_cast<double>(StaticCut.size());
                        for (const LightHierarchyNodeHost& N : StaticCut)
                        {
                            const auto C = EvaluateNodeIrradianceAtFloor(Px, Pz, N.PositionWorld, N.DirectionWorld, N.ColorLinear, N.LuminousFlux, N.CosOuterCone, N.InfluenceRadiusM, VisibilityFn);
                            Ar += C[0]; Ag += C[1]; Ab += C[2];
                        }
                    }

                    const auto DynCut = ResolveLightCutForReceiver(DynamicBvh, Pos, Options.NaniteErrorBudget * 0.75f, Options.MaxActiveLightCut);
                    Evals += static_cast<double>(DynCut.size());
                    for (const LightHierarchyNodeHost& N : DynCut)
                    {
                        const auto C = EvaluateNodeIrradianceAtFloor(Px, Pz, N.PositionWorld, N.DirectionWorld, N.ColorLinear, N.LuminousFlux, N.CosOuterCone, N.InfluenceRadiusM, VisibilityFn);
                        Ar += C[0]; Ag += C[1]; Ab += C[2];
                    }
                }

                const float Rt = Tm(Tr), Gt = Tm(Tg), Bt = Tm(Tb);
                const float Ra = Tm(Ar), Ga = Tm(Ag), Ba = Tm(Ab);
                const float Dr = Rt - Ra, Dg = Gt - Ga, Db = Bt - Ba;
                SqErrSum += static_cast<double>(Dr * Dr + Dg * Dg + Db * Db) / 3.0;
                SqRefSum += static_cast<double>(Rt * Rt + Gt * Gt + Bt * Bt) / 3.0;
                EvalsSum += Evals;
            }
        }

        const double N = static_cast<double>(GridSamples * GridSamples);
        const float Rmse = static_cast<float>(std::sqrt(SqErrSum / N));
        const float RefRms = static_cast<float>(std::sqrt(std::max(1.0e-8, SqRefSum / N)));
        Row.AvgEvalsPerPixel    = static_cast<float>(EvalsSum / N);
        Row.RelativeRmsePercent = (Mode == UnifiedLightingMode::ExactReference) ? 0.0f : (Rmse / RefRms) * 100.0f;
        Row.PsnrDecibels        = (Rmse > 1.0e-6f) ? (20.0f * std::log10(1.0f / Rmse)) : 99.9f;
        Row.SpeedupVsExact      = static_cast<float>(RawSources.size()) / std::max(1.0f, Row.AvgEvalsPerPixel);
        Table[M] = Row;
    }

    return Table;
}

} // namespace Frontier::GeometricRaster
