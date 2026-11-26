/*============================================================================================================================================
                                                      STATICTOROIDALLIGHTCLIPMAP.CPP
============================================================================================================================================*/
// 🧩 Implementation of Automatic 2-Cascade Toroidal Light Clipmap (Fine + Coarse) with Dirty-Slab Incremental Scrolling

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/StaticToroidalLightClipmap.h"
#include "Engine/GeometricRaster/DirectLighting/CubeGridPodAtlas.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace Frontier::GeometricRaster
{

namespace
{

inline constexpr float PiF = 3.14159265358979323846f;

[[nodiscard]] uint32_t WrapToroidalIndex(int32_t Coord, uint32_t Resolution) noexcept
{
    const int32_t R = static_cast<int32_t>(Resolution);
    int32_t Mod = Coord % R;
    if (Mod < 0)
    {
        Mod += R;
    }
    return static_cast<uint32_t>(Mod);
}

[[nodiscard]] float SmoothStep(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-5f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

struct FloorEvalAccumulator
{
    float Irr[3] { 0.0f, 0.0f, 0.0f };
    float Vec[3] { 0.0f, 0.0f, 0.0f };
    float LumaSum = 0.0f;
};

[[nodiscard]] FloorEvalAccumulator EvaluateStaticSourcesAtFloor(
    float                               WorldX,
    float                               WorldZ,
    std::span<const UnifiedLightSource> StaticSources,
    const ClipmapVisibilityCallback&    VisibilityFn)
{
    FloorEvalAccumulator Acc {};
    const std::array<float, 3> RecPos { WorldX, 0.02f, WorldZ };

    for (const UnifiedLightSource& Src : StaticSources)
    {
        if (Src.Mobility != LightMobility::Static)
        {
            continue;
        }
        const float Lx = Src.PositionWorld[0] - WorldX;
        const float Ly = Src.PositionWorld[1] - 0.02f;
        const float Lz = Src.PositionWorld[2] - WorldZ;
        const float DistSq = Lx * Lx + Ly * Ly + Lz * Lz + 0.35f;
        const float Dist = std::sqrt(DistSq);
        const float InvD = 1.0f / Dist;
        const float Wx = Lx * InvD, Wy = Ly * InvD, Wz = Lz * InvD;
        if (Wy <= 0.0f)
        {
            continue;
        }

        const float Window = EvaluateQuarticInfluenceWindow(Dist, Src.InfluenceRadiusM);
        if (Window <= 1.0e-4f)
        {
            continue;
        }

        const auto Candela = EvaluateRawSourceCandela(Src, { -Wx, -Wy, -Wz });
        if (Candela[0] + Candela[1] + Candela[2] <= 1.0e-5f)
        {
            continue;
        }

        const float Vis = VisibilityFn ? VisibilityFn(RecPos, Src.PositionWorld) : 1.0f;
        if (Vis <= 0.0f)
        {
            continue;
        }

        const float Atten = (Wy * Vis * Window) / (DistSq * PiF);
        const float R = Candela[0] * Atten;
        const float G = Candela[1] * Atten;
        const float B = Candela[2] * Atten;
        const float Lum = 0.2126f * R + 0.7152f * G + 0.0722f * B;

        Acc.Irr[0] += R;
        Acc.Irr[1] += G;
        Acc.Irr[2] += B;
        Acc.Vec[0] += Wx * Lum;
        Acc.Vec[1] += Wy * Lum;
        Acc.Vec[2] += Wz * Lum;
        Acc.LumaSum += Lum;
    }

    return Acc;
}

[[nodiscard]] ToroidalClipmapCell SampleCascadeNearestWrapped(
    const ToroidalLightCascade& Cascade,
    float                       WorldX,
    float                       WorldZ) noexcept
{
    if (Cascade.Resolution == 0u || Cascade.Cells.empty())
    {
        return {};
    }
    const int32_t WorldCellX = static_cast<int32_t>(std::floor(WorldX / Cascade.CellSizeMeters));
    const int32_t WorldCellZ = static_cast<int32_t>(std::floor(WorldZ / Cascade.CellSizeMeters));
    const uint32_t Tx = WrapToroidalIndex(WorldCellX, Cascade.Resolution);
    const uint32_t Tz = WrapToroidalIndex(WorldCellZ, Cascade.Resolution);
    return Cascade.Cells[static_cast<std::size_t>(Tz) * Cascade.Resolution + Tx];
}

void AllocateCascadeStorage(ToroidalLightCascade& Cascade, uint32_t Resolution, float HalfSpanMeters)
{
    Cascade.Resolution     = std::max(4u, Resolution);
    Cascade.HalfSpanMeters = std::max(1.0f, HalfSpanMeters);
    Cascade.CellSizeMeters = (2.0f * Cascade.HalfSpanMeters) / static_cast<float>(Cascade.Resolution);
    const std::size_t Count = static_cast<std::size_t>(Cascade.Resolution) * Cascade.Resolution;
    Cascade.Cells.assign(Count, ToroidalClipmapCell {});
    Cascade.TexIrradianceRgba32F.assign(Count * 4u, 0.0f);
    Cascade.TexDirectionRgba32F.assign(Count * 4u, 0.0f);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           STATIC TOROIDAL LIGHT CLIPMAP
//------------------------------------------------------------------------------------------------------------------------

void StaticToroidalLightClipmap::BakeSingleCell(
    ToroidalLightCascade&               Cascade,
    int32_t                             WorldCellX,
    int32_t                             WorldCellZ,
    std::span<const UnifiedLightSource> StaticSources,
    const ClipmapVisibilityCallback&    VisibilityFn)
{
    const float Cx = (static_cast<float>(WorldCellX) + 0.5f) * Cascade.CellSizeMeters;
    const float Cz = (static_cast<float>(WorldCellZ) + 0.5f) * Cascade.CellSizeMeters;
    const float SubOffset = Cascade.CellSizeMeters * 0.32f;

    const FloorEvalAccumulator Center = EvaluateStaticSourcesAtFloor(Cx, Cz, StaticSources, VisibilityFn);

    // Measure sub-cell irradiance & shadow-edge variance across 4 corners to drive Unified All-3 error-guided promotion
    constexpr float CornerDx[4] = { -1.0f, 1.0f, -1.0f, 1.0f };
    constexpr float CornerDz[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    float VarianceSum = 0.0f;
    for (int K = 0; K < 4; ++K)
    {
        const FloorEvalAccumulator Sub = EvaluateStaticSourcesAtFloor(
            Cx + CornerDx[K] * SubOffset,
            Cz + CornerDz[K] * SubOffset,
            StaticSources,
            VisibilityFn);
        const float Dr = Sub.Irr[0] - Center.Irr[0];
        const float Dg = Sub.Irr[1] - Center.Irr[1];
        const float Db = Sub.Irr[2] - Center.Irr[2];
        VarianceSum += std::sqrt(Dr * Dr + Dg * Dg + Db * Db);
    }

    const float VecLen = std::sqrt(Center.Vec[0] * Center.Vec[0] + Center.Vec[1] * Center.Vec[1] + Center.Vec[2] * Center.Vec[2]);
    const float DirCoherence = std::clamp(VecLen / std::max(1.0e-5f, Center.LumaSum), 0.0f, 1.0f);
    std::array<float, 3> DomDir { 0.0f, 1.0f, 0.0f };
    if (VecLen > 1.0e-5f)
    {
        DomDir = { Center.Vec[0] / VecLen, Center.Vec[1] / VecLen, Center.Vec[2] / VecLen };
    }

    const uint32_t Tx = WrapToroidalIndex(WorldCellX, Cascade.Resolution);
    const uint32_t Tz = WrapToroidalIndex(WorldCellZ, Cascade.Resolution);
    const std::size_t Idx = static_cast<std::size_t>(Tz) * Cascade.Resolution + Tx;

    ToroidalClipmapCell& Cell = Cascade.Cells[Idx];
    Cell.WorldCenter    = { Cx, 0.02f, Cz };
    Cell.IrradianceL0   = { Center.Irr[0], Center.Irr[1], Center.Irr[2] };
    Cell.DominantDirL1  = DomDir;
    Cell.Directionality = DirCoherence;
    Cell.CellError      = VarianceSum * 0.25f;

    Cascade.TexIrradianceRgba32F[Idx * 4u + 0u] = Cell.IrradianceL0[0];
    Cascade.TexIrradianceRgba32F[Idx * 4u + 1u] = Cell.IrradianceL0[1];
    Cascade.TexIrradianceRgba32F[Idx * 4u + 2u] = Cell.IrradianceL0[2];
    Cascade.TexIrradianceRgba32F[Idx * 4u + 3u] = Cell.Directionality;

    Cascade.TexDirectionRgba32F[Idx * 4u + 0u]  = Cell.DominantDirL1[0];
    Cascade.TexDirectionRgba32F[Idx * 4u + 1u]  = Cell.DominantDirL1[1];
    Cascade.TexDirectionRgba32F[Idx * 4u + 2u]  = Cell.DominantDirL1[2];
    Cascade.TexDirectionRgba32F[Idx * 4u + 3u]  = Cell.CellError;
}

void StaticToroidalLightClipmap::RefreshCascadeNormalization(ToroidalLightCascade& Cascade) noexcept
{
    float MaxIrr = 0.25f;
    float MaxErr = 0.20f;
    for (const ToroidalClipmapCell& C : Cascade.Cells)
    {
        MaxIrr = std::max({ MaxIrr, C.IrradianceL0[0], C.IrradianceL0[1], C.IrradianceL0[2] });
        MaxErr = std::max(MaxErr, C.CellError);
    }
    Cascade.MaxIrradiance = MaxIrr;
    Cascade.MaxCellError  = MaxErr;
}

bool StaticToroidalLightClipmap::UpdateAutomatic(
    std::span<const UnifiedLightSource> StaticSources,
    uint64_t                            StaticLightGeneration,
    const std::array<float, 3>&         FocusWorldPos,
    const LightClipmapScaleConfig&      ScaleConfig,
    bool                                ForceFullRebake,
    const ClipmapVisibilityCallback&    VisibilityFn)
{
    const bool ScaleChanged = !Initialized
        || (ActiveConfig.FineResolution != ScaleConfig.FineResolution)
        || (ActiveConfig.CoarseResolution != ScaleConfig.CoarseResolution)
        || (std::fabs(ActiveConfig.FineHalfSpanMeters - ScaleConfig.FineHalfSpanMeters) > 1.0e-3f)
        || (std::fabs(ActiveConfig.CoarseHalfSpanMeters - ScaleConfig.CoarseHalfSpanMeters) > 1.0e-3f);

    const bool StaticLightsDirty = !Initialized || (Telemetry.LastBakedStaticGeneration != StaticLightGeneration);

    if (ScaleChanged)
    {
        AllocateCascadeStorage(FineCascade, ScaleConfig.FineResolution, ScaleConfig.FineHalfSpanMeters);
        AllocateCascadeStorage(CoarseCascade, ScaleConfig.CoarseResolution, ScaleConfig.CoarseHalfSpanMeters);
        ActiveConfig = ScaleConfig;
    }

    const int32_t NewFineOriginX = static_cast<int32_t>(std::llround(FocusWorldPos[0] / FineCascade.CellSizeMeters));
    const int32_t NewFineOriginZ = static_cast<int32_t>(std::llround(FocusWorldPos[2] / FineCascade.CellSizeMeters));
    const int32_t DeltaX = NewFineOriginX - FineCascade.OriginCellX;
    const int32_t DeltaZ = NewFineOriginZ - FineCascade.OriginCellZ;
    const bool FocusScrolled = Initialized && (DeltaX != 0 || DeltaZ != 0);

    // Fast path: nothing static changed and focus remained inside the same snapped toroidal cell -> 0 cells baked!
    if (!ForceFullRebake && !ScaleChanged && !StaticLightsDirty && !FocusScrolled)
    {
        Telemetry.LastUpdatedCellCount = 0u;
        Telemetry.LastBakeMilliseconds = 0.0f;
        return false;
    }

    const auto StartTime = std::chrono::steady_clock::now();
    uint32_t UpdatedCells = 0u;

    const int32_t HalfFine = static_cast<int32_t>(FineCascade.Resolution) / 2;
    const int32_t HalfCoarse = static_cast<int32_t>(CoarseCascade.Resolution) / 2;

    const bool NeedFullFineBake = ForceFullRebake || ScaleChanged || StaticLightsDirty
        || (std::abs(DeltaX) >= static_cast<int32_t>(FineCascade.Resolution))
        || (std::abs(DeltaZ) >= static_cast<int32_t>(FineCascade.Resolution));

    if (NeedFullFineBake)
    {
        FineCascade.OriginCellX  = NewFineOriginX;
        FineCascade.OriginCellZ  = NewFineOriginZ;
        FineCascade.CenterWorldX = static_cast<float>(NewFineOriginX) * FineCascade.CellSizeMeters;
        FineCascade.CenterWorldZ = static_cast<float>(NewFineOriginZ) * FineCascade.CellSizeMeters;

        for (int32_t Dz = -HalfFine; Dz < HalfFine; ++Dz)
        {
            for (int32_t Dx = -HalfFine; Dx < HalfFine; ++Dx)
            {
                BakeSingleCell(FineCascade, NewFineOriginX + Dx, NewFineOriginZ + Dz, StaticSources, VisibilityFn);
                ++UpdatedCells;
            }
        }

        // Also refresh world-covering Coarse cascade on full/static bake
        CoarseCascade.OriginCellX  = 0;
        CoarseCascade.OriginCellZ  = 0;
        CoarseCascade.CenterWorldX = 0.0f;
        CoarseCascade.CenterWorldZ = 0.0f;
        for (int32_t Dz = -HalfCoarse; Dz < HalfCoarse; ++Dz)
        {
            for (int32_t Dx = -HalfCoarse; Dx < HalfCoarse; ++Dx)
            {
                BakeSingleCell(CoarseCascade, Dx, Dz, StaticSources, VisibilityFn);
                ++UpdatedCells;
            }
        }
        RefreshCascadeNormalization(CoarseCascade);
        ++Telemetry.TotalFullBakeCount;
    }
    else if (FocusScrolled)
    {
        // Incremental Toroidal Dirty-Slab Scroll: re-bake ONLY newly entered boundary cells in FineCascade!
        const int32_t OldMinX = FineCascade.OriginCellX - HalfFine;
        const int32_t OldMaxX = FineCascade.OriginCellX + HalfFine - 1;
        const int32_t OldMinZ = FineCascade.OriginCellZ - HalfFine;
        const int32_t OldMaxZ = FineCascade.OriginCellZ + HalfFine - 1;

        FineCascade.OriginCellX  = NewFineOriginX;
        FineCascade.OriginCellZ  = NewFineOriginZ;
        FineCascade.CenterWorldX = static_cast<float>(NewFineOriginX) * FineCascade.CellSizeMeters;
        FineCascade.CenterWorldZ = static_cast<float>(NewFineOriginZ) * FineCascade.CellSizeMeters;

        for (int32_t Dz = -HalfFine; Dz < HalfFine; ++Dz)
        {
            const int32_t Wz = NewFineOriginZ + Dz;
            for (int32_t Dx = -HalfFine; Dx < HalfFine; ++Dx)
            {
                const int32_t Wx = NewFineOriginX + Dx;
                const bool WasInsideOld = (Wx >= OldMinX && Wx <= OldMaxX && Wz >= OldMinZ && Wz <= OldMaxZ);
                if (!WasInsideOld)
                {
                    BakeSingleCell(FineCascade, Wx, Wz, StaticSources, VisibilityFn);
                    ++UpdatedCells;
                }
            }
        }
        ++Telemetry.TotalDirtySlabScrollCount;
    }

    RefreshCascadeNormalization(FineCascade);
    Initialized = true;

    const auto EndTime = std::chrono::steady_clock::now();
    Telemetry.LastBakedStaticGeneration = StaticLightGeneration;
    Telemetry.LastUpdatedCellCount      = UpdatedCells;
    Telemetry.LastBakeMilliseconds      = std::chrono::duration<float, std::milli>(EndTime - StartTime).count();

    // Estimate 3D GPU clipmap footprint (Fine + Coarse x 2 RGBA16F/RGBA8 textures x 16 vertical slices)
    const float FineBytes   = static_cast<float>(FineCascade.Resolution * FineCascade.Resolution) * 16.0f * 16.0f;
    const float CoarseBytes = static_cast<float>(CoarseCascade.Resolution * CoarseCascade.Resolution) * 16.0f * 16.0f;
    Telemetry.TotalVramKilobytes = (FineBytes + CoarseBytes) / 1024.0f;
    return true;
}

ToroidalClipmapSample StaticToroidalLightClipmap::SampleAtWorldXZ(float WorldX, float WorldZ) const noexcept
{
    ToroidalClipmapSample Out {};
    if (!Initialized)
    {
        return Out;
    }

    const float Fu = (WorldX - (FineCascade.CenterWorldX - FineCascade.HalfSpanMeters)) / (2.0f * FineCascade.HalfSpanMeters);
    const float Fv = (WorldZ - (FineCascade.CenterWorldZ - FineCascade.HalfSpanMeters)) / (2.0f * FineCascade.HalfSpanMeters);
    const float EdgeDist = std::min({ Fu, 1.0f - Fu, Fv, 1.0f - Fv });
    const float FineW = SmoothStep(0.0f, 0.14f, EdgeDist);
    Out.FineWeight = FineW;

    const ToroidalClipmapCell CoarseCell = SampleCascadeNearestWrapped(CoarseCascade, WorldX, WorldZ);
    if (FineW <= 0.001f)
    {
        Out.IrradianceL0   = CoarseCell.IrradianceL0;
        Out.DominantDirL1  = CoarseCell.DominantDirL1;
        Out.Directionality = CoarseCell.Directionality;
        Out.CellError      = CoarseCell.CellError;
        return Out;
    }

    const ToroidalClipmapCell FineCell = SampleCascadeNearestWrapped(FineCascade, WorldX, WorldZ);
    for (int C = 0; C < 3; ++C)
    {
        Out.IrradianceL0[C]  = CoarseCell.IrradianceL0[C]  * (1.0f - FineW) + FineCell.IrradianceL0[C]  * FineW;
        Out.DominantDirL1[C] = CoarseCell.DominantDirL1[C] * (1.0f - FineW) + FineCell.DominantDirL1[C] * FineW;
    }
    const float Len = std::sqrt(
        Out.DominantDirL1[0] * Out.DominantDirL1[0] +
        Out.DominantDirL1[1] * Out.DominantDirL1[1] +
        Out.DominantDirL1[2] * Out.DominantDirL1[2]);
    if (Len > 1.0e-5f)
    {
        Out.DominantDirL1[0] /= Len;
        Out.DominantDirL1[1] /= Len;
        Out.DominantDirL1[2] /= Len;
    }
    Out.Directionality = CoarseCell.Directionality * (1.0f - FineW) + FineCell.Directionality * FineW;
    Out.CellError      = CoarseCell.CellError      * (1.0f - FineW) + FineCell.CellError      * FineW;
    return Out;
}

} // namespace Frontier::GeometricRaster
