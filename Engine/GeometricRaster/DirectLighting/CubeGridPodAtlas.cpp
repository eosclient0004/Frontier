/*============================================================================================================================================
                                                          CUBEGRIDPODATLAS.CPP
============================================================================================================================================*/
// 🧩 Implementation of Sparse 8x4x4 Spatial-Directional Cube-Grid Pod Compiler, Continuous Nanite LOD & Residual-Error Optimizer

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/CubeGridPodAtlas.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

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

[[nodiscard]] float SmoothHermite(float X) noexcept
{
    const float T = std::clamp(X, 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] std::array<float, 3> ShadeFromRadiator(
    const std::array<float, 3>& ShadingPos,
    const std::array<float, 3>& Normal,
    const std::array<float, 3>& ViewDir,
    float                       Roughness,
    const std::array<float, 3>& Albedo,
    const std::array<float, 3>& AnchorPos,
    float                       ApertureRadiusM,
    const std::array<float, 3>& CandelaRgb) noexcept
{
    const float Dx = AnchorPos[0] - ShadingPos[0];
    const float Dy = AnchorPos[1] - ShadingPos[1];
    const float Dz = AnchorPos[2] - ShadingPos[2];
    const float DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
    const float Dist = std::sqrt(std::max(DistSq, 1.0e-5f));
    const float InvDist = 1.0f / Dist;
    const std::array<float, 3> L { Dx * InvDist, Dy * InvDist, Dz * InvDist };

    const float NdotL = std::max(Dot3(Normal, L), 0.0f);
    if (NdotL <= 0.0f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float EffRad = std::max(0.03f, ApertureRadiusM);
    const float Atten  = 1.0f / (DistSq + EffRad * EffRad);

    // Diffuse Lambertian + Karis finite-aperture GGX specular
    const std::array<float, 3> H = Normalize3({ L[0] + ViewDir[0], L[1] + ViewDir[1], L[2] + ViewDir[2] }, Normal);
    const float NdotH = std::max(Dot3(Normal, H), 0.0f);
    const float NdotV = std::max(Dot3(Normal, ViewDir), 0.05f);
    const float VdotH = std::max(Dot3(ViewDir, H), 0.0f);

    const float Alpha = std::clamp(Roughness * Roughness + EffRad / std::max(2.0f * Dist, 0.5f), 0.04f, 1.0f);
    const float A2    = Alpha * Alpha;
    const float Denom = NdotH * NdotH * (A2 - 1.0f) + 1.0f;
    const float D     = A2 / (PiF * Denom * Denom + 1.0e-5f);
    const float F     = 0.04f + 0.96f * std::pow(1.0f - VdotH, 5.0f);
    const float Spec  = (D * F) / (4.0f * NdotV * NdotL + 0.15f);

    const float DiffScale = NdotL * Atten / PiF;
    const float SpecScale = Spec * NdotL * Atten * 0.85f;

    return {
        CandelaRgb[0] * (Albedo[0] * DiffScale + SpecScale),
        CandelaRgb[1] * (Albedo[1] * DiffScale + SpecScale),
        CandelaRgb[2] * (Albedo[2] * DiffScale + SpecScale)
    };
}

[[nodiscard]] OctahedralDirectionalTile BakeRawDirectionalTile(
    std::span<const UnifiedLightSource> Sources,
    uint32_t                            Resolution,
    float                               BoxBlurMix)
{
    OctahedralDirectionalTile Tile {};
    Tile.Resolution = Resolution;
    const std::size_t TexelCount = static_cast<std::size_t>(Resolution) * Resolution;
    Tile.TexelsRgb.assign(TexelCount * 3u, 0.0f);

    for (uint32_t Y = 0u; Y < Resolution; ++Y)
    {
        for (uint32_t X = 0u; X < Resolution; ++X)
        {
            const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(Resolution);
            const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Resolution);
            const auto Dir = OctahedralDecodeMinusY(U, V);
            float R = 0.0f, G = 0.0f, B = 0.0f;
            for (const UnifiedLightSource& Src : Sources)
            {
                const auto C = EvaluateRawSourceCandela(Src, Dir);
                R += C[0];
                G += C[1];
                B += C[2];
            }
            const std::size_t Base = (static_cast<std::size_t>(Y) * Resolution + X) * 3u;
            Tile.TexelsRgb[Base + 0u] = R;
            Tile.TexelsRgb[Base + 1u] = G;
            Tile.TexelsRgb[Base + 2u] = B;
        }
    }

    if (BoxBlurMix > 0.0f && Resolution >= 4u)
    {
        std::vector<float> Filtered = Tile.TexelsRgb;
        for (uint32_t Y = 0u; Y < Resolution; ++Y)
        {
            for (uint32_t X = 0u; X < Resolution; ++X)
            {
                float Acc[3] = { 0.0f, 0.0f, 0.0f };
                float WSum = 0.0f;
                for (int Dy = -1; Dy <= 1; ++Dy)
                {
                    for (int Dx = -1; Dx <= 1; ++Dx)
                    {
                        const uint32_t Nx = static_cast<uint32_t>(std::clamp(static_cast<int>(X) + Dx, 0, static_cast<int>(Resolution) - 1));
                        const uint32_t Ny = static_cast<uint32_t>(std::clamp(static_cast<int>(Y) + Dy, 0, static_cast<int>(Resolution) - 1));
                        const std::size_t Nb = (static_cast<std::size_t>(Ny) * Resolution + Nx) * 3u;
                        Acc[0] += Tile.TexelsRgb[Nb + 0u];
                        Acc[1] += Tile.TexelsRgb[Nb + 1u];
                        Acc[2] += Tile.TexelsRgb[Nb + 2u];
                        WSum += 1.0f;
                    }
                }
                const std::size_t Base = (static_cast<std::size_t>(Y) * Resolution + X) * 3u;
                for (int C = 0; C < 3; ++C)
                {
                    Filtered[Base + C] = Tile.TexelsRgb[Base + C] * (1.0f - BoxBlurMix) + (Acc[C] / WSum) * BoxBlurMix;
                }
            }
        }
        Tile.TexelsRgb = std::move(Filtered);
    }

    return Tile;
}

struct CalibrationReceiver
{
    std::array<float, 3> Pos {};
    std::array<float, 3> Normal {};
    std::array<float, 3> ViewDir {};
    float                Roughness = 0.35f;
    std::array<float, 3> Albedo { 0.72f, 0.72f, 0.74f };
    std::array<float, 3> ExactRgb {};
};

[[nodiscard]] std::vector<CalibrationReceiver> BuildCalibrationReceivers(
    const CompiledCubeGridPod&          Pod,
    std::span<const UnifiedLightSource> GroundTruthSources)
{
    std::vector<CalibrationReceiver> Receivers;
    Receivers.reserve(256u);

    const auto& C = Pod.FluxCentroidWorld;
    // 1) Near-field floor grid directly in front & around the light pod (64 samples)
    for (int Iz = -4; Iz < 4; ++Iz)
    {
        for (int Ix = -4; Ix < 4; ++Ix)
        {
            CalibrationReceiver Rec {};
            Rec.Pos     = { C[0] + (static_cast<float>(Ix) + 0.5f) * 0.95f, 0.0f, C[2] + (static_cast<float>(Iz) + 0.5f) * 1.15f + 2.2f };
            Rec.Normal  = { 0.0f, 1.0f, 0.0f };
            Rec.ViewDir = Normalize3({ C[0] - Rec.Pos[0], 2.2f, C[2] - Rec.Pos[2] }, { 0.0f, 1.0f, 0.0f });
            Rec.Roughness = ((Ix + Iz) & 1) ? 0.22f : 0.52f;
            Receivers.push_back(Rec);
        }
    }

    // 2) Concentric 3D spherical calibration domes around pod centroid (3 rings x 32 directions = 96 samples)
    constexpr float Radii[3] = { 1.6f, 3.2f, 6.4f };
    for (float Radius : Radii)
    {
        for (uint32_t S = 0u; S < 32u; ++S)
        {
            const float Fi = (static_cast<float>(S) + 0.5f) / 32.0f;
            const float CosTheta = 1.0f - 2.0f * Fi;
            const float SinTheta = std::sqrt(std::max(0.0f, 1.0f - CosTheta * CosTheta));
            const float Phi = 2.39996323f * static_cast<float>(S); // Golden angle
            const std::array<float, 3> Dir { std::cos(Phi) * SinTheta, CosTheta, std::sin(Phi) * SinTheta };

            CalibrationReceiver Rec {};
            Rec.Pos     = { C[0] + Dir[0] * Radius, std::max(0.02f, C[1] + Dir[1] * Radius), C[2] + Dir[2] * Radius };
            Rec.Normal  = Normalize3({ C[0] - Rec.Pos[0], C[1] - Rec.Pos[1], C[2] - Rec.Pos[2] }, { 0.0f, 1.0f, 0.0f });
            Rec.ViewDir = Rec.Normal;
            Rec.Roughness = 0.34f;
            Receivers.push_back(Rec);
        }
    }

    // Evaluate exact ground truth radiance for every receiver
    for (CalibrationReceiver& Rec : Receivers)
    {
        float Tr = 0.0f, Tg = 0.0f, Tb = 0.0f;
        for (const UnifiedLightSource& Src : GroundTruthSources)
        {
            const std::array<float, 3> RayFromLight = Normalize3({
                Rec.Pos[0] - Src.PositionWorld[0],
                Rec.Pos[1] - Src.PositionWorld[1],
                Rec.Pos[2] - Src.PositionWorld[2]
            });
            const auto Candela = EvaluateRawSourceCandela(Src, RayFromLight);
            const auto Rad = ShadeFromRadiator(Rec.Pos, Rec.Normal, Rec.ViewDir, Rec.Roughness, Rec.Albedo, Src.PositionWorld, Src.ApertureRadiusM, Candela);
            Tr += Rad[0];
            Tg += Rad[1];
            Tb += Rad[2];
        }
        Rec.ExactRgb = { Tr, Tg, Tb };
    }

    return Receivers;
}

[[nodiscard]] std::pair<float, float> MeasurePodCalibrationRmse(
    const CompiledCubeGridPod&              Pod,
    std::span<const CalibrationReceiver>    Receivers) noexcept
{
    double SqErr = 0.0;
    double SqRef = 0.0;
    const auto Tm = [](float X) noexcept { return X / (1.0f + std::max(0.0f, X)); };

    for (const CalibrationReceiver& Rec : Receivers)
    {
        // Evaluate LOD 0 active spatial cells directly during calibration
        float Ar = 0.0f, Ag = 0.0f, Ab = 0.0f;
        for (const CubeGridActiveCell& Cell : Pod.ActiveCellsLod0)
        {
            const std::array<float, 3> Anchor {
                Cell.VoxelCenterWorld[0] + Cell.SubVoxelOffset[0],
                Cell.VoxelCenterWorld[1] + Cell.SubVoxelOffset[1],
                Cell.VoxelCenterWorld[2] + Cell.SubVoxelOffset[2]
            };
            const auto RayFromLight = Normalize3({ Rec.Pos[0] - Anchor[0], Rec.Pos[1] - Anchor[1], Rec.Pos[2] - Anchor[2] });
            const auto Candela = SampleOctahedralTileBilinear(Cell.TileLod0, RayFromLight);
            const auto Rad = ShadeFromRadiator(Rec.Pos, Rec.Normal, Rec.ViewDir, Rec.Roughness, Rec.Albedo, Anchor, Cell.ApertureRadiusM, Candela);
            Ar += Rad[0];
            Ag += Rad[1];
            Ab += Rad[2];
        }
        const float Dr = Tm(Rec.ExactRgb[0]) - Tm(Ar);
        const float Dg = Tm(Rec.ExactRgb[1]) - Tm(Ag);
        const float Db = Tm(Rec.ExactRgb[2]) - Tm(Ab);
        SqErr += static_cast<double>(Dr * Dr + Dg * Dg + Db * Db) / 3.0;
        const float Rr = Tm(Rec.ExactRgb[0]), Rg = Tm(Rec.ExactRgb[1]), Rb = Tm(Rec.ExactRgb[2]);
        SqRef += static_cast<double>(Rr * Rr + Rg * Rg + Rb * Rb) / 3.0;
    }

    const double N = static_cast<double>(std::max<std::size_t>(1u, Receivers.size()));
    const float Rmse = static_cast<float>(std::sqrt(SqErr / N));
    const float RefRms = static_cast<float>(std::sqrt(std::max(1.0e-8, SqRef / N)));
    const float RelRmsePct = (Rmse / RefRms) * 100.0f;
    const float Psnr = (Rmse > 1.0e-6f) ? (20.0f * std::log10(1.0f / Rmse)) : 99.9f;
    return { RelRmsePct, Psnr };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                      -Y-POLE OCTAHEDRAL CODEC & BILINEAR SAMPLER
//------------------------------------------------------------------------------------------------------------------------

std::array<float, 2> OctahedralEncodeMinusY(const std::array<float, 3>& UnitDir) noexcept
{
    const float Qx = UnitDir[0];
    const float Qy = -UnitDir[1]; // Pole +Qy corresponds to downward world -Y
    const float Qz = UnitDir[2];
    const float InvL1 = 1.0f / std::max(1.0e-6f, std::fabs(Qx) + std::fabs(Qy) + std::fabs(Qz));
    float Ox = Qx * InvL1;
    float Oz = Qz * InvL1;
    if (Qy < 0.0f)
    {
        const float NewX = (1.0f - std::fabs(Oz)) * (Ox >= 0.0f ? 1.0f : -1.0f);
        const float NewZ = (1.0f - std::fabs(Ox)) * (Oz >= 0.0f ? 1.0f : -1.0f);
        Ox = NewX;
        Oz = NewZ;
    }
    return { Ox * 0.5f + 0.5f, Oz * 0.5f + 0.5f };
}

std::array<float, 3> OctahedralDecodeMinusY(float U, float V) noexcept
{
    float Ox = U * 2.0f - 1.0f;
    float Oz = V * 2.0f - 1.0f;
    const float Qy = 1.0f - std::fabs(Ox) - std::fabs(Oz);
    if (Qy < 0.0f)
    {
        const float NewX = (1.0f - std::fabs(Oz)) * (Ox >= 0.0f ? 1.0f : -1.0f);
        const float NewZ = (1.0f - std::fabs(Ox)) * (Oz >= 0.0f ? 1.0f : -1.0f);
        Ox = NewX;
        Oz = NewZ;
    }
    return Normalize3({ Ox, -Qy, Oz });
}

std::array<float, 3> EvaluateRawSourceCandela(
    const UnifiedLightSource&   Source,
    const std::array<float, 3>& UnitRayFromLight) noexcept
{
    float AngularFactor = 1.0f;
    if (Source.CosOuterCone > -0.95f)
    {
        const float CosAngle = Dot3(UnitRayFromLight, Source.DirectionWorld);
        const float Edge0 = Source.CosOuterCone;
        const float Edge1 = std::min(0.992f, Source.CosOuterCone + 0.28f);
        AngularFactor = SmoothHermite((CosAngle - Edge0) / std::max(1.0e-4f, Edge1 - Edge0));
        if (AngularFactor <= 1.0e-4f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }
    }

    if (Source.EceLowBeamCutoff)
    {
        const std::array<float, 3> WorldUp { 0.0f, 1.0f, 0.0f };
        const std::array<float, 3> RightAxis = Normalize3(Cross3(Source.DirectionWorld, WorldUp), { 1.0f, 0.0f, 0.0f });
        const std::array<float, 3> UpAxis    = Normalize3(Cross3(RightAxis, Source.DirectionWorld), WorldUp);
        const float Lat = Dot3(UnitRayFromLight, RightAxis);
        const float Elev = Dot3(UnitRayFromLight, UpAxis);
        const float CutoffLine = (Lat <= 0.0f) ? -0.018f : (-0.018f + Lat * 0.268f);
        AngularFactor *= 1.0f - SmoothHermite((Elev - CutoffLine) / 0.06f);
    }

    const float Gain = Source.LuminousFlux * AngularFactor;
    return { Source.ColorLinear[0] * Gain, Source.ColorLinear[1] * Gain, Source.ColorLinear[2] * Gain };
}

std::array<float, 3> SampleOctahedralTileBilinear(
    const OctahedralDirectionalTile& Tile,
    const std::array<float, 3>&      UnitRayFromLight) noexcept
{
    if (Tile.Resolution == 0u || Tile.TexelsRgb.empty())
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const auto [U, V] = OctahedralEncodeMinusY(UnitRayFromLight);
    const float ResF = static_cast<float>(Tile.Resolution);
    const float Fx = std::clamp(U * ResF - 0.5f, 0.0f, ResF - 1.0f);
    const float Fy = std::clamp(V * ResF - 0.5f, 0.0f, ResF - 1.0f);
    const uint32_t X0 = static_cast<uint32_t>(std::floor(Fx));
    const uint32_t Y0 = static_cast<uint32_t>(std::floor(Fy));
    const uint32_t X1 = std::min(Tile.Resolution - 1u, X0 + 1u);
    const uint32_t Y1 = std::min(Tile.Resolution - 1u, Y0 + 1u);
    const float Tx = Fx - static_cast<float>(X0);
    const float Ty = Fy - static_cast<float>(Y0);

    const auto Fetch = [&](uint32_t X, uint32_t Y, uint32_t C) noexcept -> float {
        return Tile.TexelsRgb[(static_cast<std::size_t>(Y) * Tile.Resolution + X) * 3u + C];
    };

    std::array<float, 3> Out {};
    for (uint32_t C = 0u; C < 3u; ++C)
    {
        const float Top = Fetch(X0, Y0, C) * (1.0f - Tx) + Fetch(X1, Y0, C) * Tx;
        const float Bot = Fetch(X0, Y1, C) * (1.0f - Tx) + Fetch(X1, Y1, C) * Tx;
        Out[C] = Top * (1.0f - Ty) + Bot * Ty;
    }
    return Out;
}

float EvaluateQuarticInfluenceWindow(float DistanceMeters, float InfluenceRadiusMeters) noexcept
{
    if (InfluenceRadiusMeters <= 1.0e-4f || DistanceMeters >= InfluenceRadiusMeters)
    {
        return 0.0f;
    }
    const float Ratio = DistanceMeters / InfluenceRadiusMeters;
    const float R2 = Ratio * Ratio;
    const float W = std::max(0.0f, 1.0f - R2 * R2);
    return W * W;
}

float ComputeContinuousPodLod(float DistanceMeters, float BoundingDiameterMeters, float LodBias) noexcept
{
    const float NearThreshold = std::max(2.2f * BoundingDiameterMeters, 1.8f);
    const float Ratio = std::max(DistanceMeters / NearThreshold, 1.0f);
    return std::clamp(1.15f * std::log2(Ratio) + LodBias, 0.0f, 3.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  NON-AVERAGED 8x4x4 CUBE-GRID POD COMPILER & LOD EVALUATOR
//------------------------------------------------------------------------------------------------------------------------

CompiledCubeGridPod CompileCubeGridPod(
    uint32_t                            RigGroupId,
    std::span<const UnifiedLightSource> GroupSources)
{
    CompiledCubeGridPod Pod {};
    Pod.RigGroupId  = RigGroupId;
    Pod.SourceCount = static_cast<uint32_t>(GroupSources.size());
    if (GroupSources.empty())
    {
        return Pod;
    }

    Pod.Mobility = GroupSources[0].Mobility;
    std::array<float, 3> BMin { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    std::array<float, 3> BMax { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    double Cx = 0.0, Cy = 0.0, Cz = 0.0, FluxSum = 0.0;
    float MaxInfluence = 10.0f;

    for (const UnifiedLightSource& Src : GroupSources)
    {
        for (int A = 0; A < 3; ++A)
        {
            BMin[A] = std::min(BMin[A], Src.PositionWorld[A]);
            BMax[A] = std::max(BMax[A], Src.PositionWorld[A]);
        }
        const double W = std::max(0.01, static_cast<double>(Src.LuminousFlux));
        Cx += Src.PositionWorld[0] * W;
        Cy += Src.PositionWorld[1] * W;
        Cz += Src.PositionWorld[2] * W;
        FluxSum += W;
        MaxInfluence = std::max(MaxInfluence, Src.InfluenceRadiusM);
    }

    // Pad tight dimensions slightly so degenerate single-point or coplanar rigs have valid 3D cells
    for (int A = 0; A < 3; ++A)
    {
        if (BMax[A] - BMin[A] < 0.25f)
        {
            const float Mid = 0.5f * (BMin[A] + BMax[A]);
            BMin[A] = Mid - 0.125f;
            BMax[A] = Mid + 0.125f;
        }
    }

    Pod.BoundsMinWorld    = BMin;
    Pod.BoundsMaxWorld    = BMax;
    Pod.FluxCentroidWorld = { static_cast<float>(Cx / FluxSum), static_cast<float>(Cy / FluxSum), static_cast<float>(Cz / FluxSum) };
    Pod.BoundingDiameterM = std::sqrt(
        (BMax[0] - BMin[0]) * (BMax[0] - BMin[0]) +
        (BMax[1] - BMin[1]) * (BMax[1] - BMin[1]) +
        (BMax[2] - BMin[2]) * (BMax[2] - BMin[2]));
    Pod.InfluenceRadiusM  = MaxInfluence;

    // 1) Bin sources strictly into true 8x4x4 spatial voxels (ZERO center-averaging!)
    const float CellSize[3] = {
        (BMax[0] - BMin[0]) / static_cast<float>(PodSpatialGridDimX),
        (BMax[1] - BMin[1]) / static_cast<float>(PodSpatialGridDimY),
        (BMax[2] - BMin[2]) / static_cast<float>(PodSpatialGridDimZ)
    };

    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, std::vector<UnifiedLightSource>> OccupiedBuckets;
    for (const UnifiedLightSource& Src : GroupSources)
    {
        const uint32_t Ix = std::min(PodSpatialGridDimX - 1u, static_cast<uint32_t>(std::max(0.0f, (Src.PositionWorld[0] - BMin[0]) / CellSize[0])));
        const uint32_t Iy = std::min(PodSpatialGridDimY - 1u, static_cast<uint32_t>(std::max(0.0f, (Src.PositionWorld[1] - BMin[1]) / CellSize[1])));
        const uint32_t Iz = std::min(PodSpatialGridDimZ - 1u, static_cast<uint32_t>(std::max(0.0f, (Src.PositionWorld[2] - BMin[2]) / CellSize[2])));
        OccupiedBuckets[{ Ix, Iy, Iz }].push_back(Src);
    }

    // 2) Build LOD 0 Active Spatial Cells (only occupied voxels — empty center columns remain strictly empty!)
    Pod.ActiveCellsLod0.reserve(OccupiedBuckets.size());
    for (const auto& [Coord, Bucket] : OccupiedBuckets)
    {
        const auto [Ix, Iy, Iz] = Coord;
        CubeGridActiveCell Cell {};
        Cell.GridCoord[0] = Ix;
        Cell.GridCoord[1] = Iy;
        Cell.GridCoord[2] = Iz;
        Cell.VoxelCenterWorld = {
            BMin[0] + (static_cast<float>(Ix) + 0.5f) * CellSize[0],
            BMin[1] + (static_cast<float>(Iy) + 0.5f) * CellSize[1],
            BMin[2] + (static_cast<float>(Iz) + 0.5f) * CellSize[2]
        };
        Cell.SubVoxelOffset  = { 0.0f, 0.0f, 0.0f };
        Cell.ApertureRadiusM = 0.24f;
        Cell.TileLod0        = BakeRawDirectionalTile(Bucket, PodTileResLod0, 0.20f);

        float Dx = 0.0f, Dy = 0.0f, Dz = 0.0f;
        for (const UnifiedLightSource& S : Bucket)
        {
            Cell.TotalFlux += S.LuminousFlux;
            Dx += S.DirectionWorld[0] * S.LuminousFlux;
            Dy += S.DirectionWorld[1] * S.LuminousFlux;
            Dz += S.DirectionWorld[2] * S.LuminousFlux;
        }
        Cell.DominantDirWorld = Normalize3({ Dx, Dy, Dz });
        Pod.ActiveCellsLod0.push_back(std::move(Cell));
    }

    // 3) Build LOD 1 Bimodal Split Poles along the widest separation axis
    int SplitAxis = 0;
    const float SpanX = BMax[0] - BMin[0];
    const float SpanY = BMax[1] - BMin[1];
    const float SpanZ = BMax[2] - BMin[2];
    if (SpanY > SpanX && SpanY >= SpanZ) SplitAxis = 1;
    else if (SpanZ > SpanX && SpanZ > SpanY) SplitAxis = 2;

    const float MidCoord = 0.5f * (BMin[SplitAxis] + BMax[SplitAxis]);
    std::vector<UnifiedLightSource> NegHalf, PosHalf;
    for (const UnifiedLightSource& Src : GroupSources)
    {
        if (Src.PositionWorld[SplitAxis] <= MidCoord) NegHalf.push_back(Src);
        else PosHalf.push_back(Src);
    }

    const auto BuildPole = [&](std::span<const UnifiedLightSource> HalfSources) -> CubeGridBimodalPole {
        CubeGridBimodalPole Pole {};
        double Px = 0.0, Py = 0.0, Pz = 0.0, WSum = 0.0;
        for (const UnifiedLightSource& S : HalfSources)
        {
            const double W = std::max(0.01, static_cast<double>(S.LuminousFlux));
            Px += S.PositionWorld[0] * W;
            Py += S.PositionWorld[1] * W;
            Pz += S.PositionWorld[2] * W;
            WSum += W;
        }
        Pole.AnchorWorld     = { static_cast<float>(Px / WSum), static_cast<float>(Py / WSum), static_cast<float>(Pz / WSum) };
        Pole.ApertureRadiusM = 0.28f;
        Pole.TileLod1        = BakeRawDirectionalTile(HalfSources, PodTileResLod1, 0.12f);
        return Pole;
    };
    if (!NegHalf.empty()) Pod.BimodalPolesLod1.push_back(BuildPole(NegHalf));
    if (!PosHalf.empty()) Pod.BimodalPolesLod1.push_back(BuildPole(PosHalf));

    // 4) Build LOD 2 (12x12 far-field single tile) and LOD 3 (4x4 distant coarse tile)
    Pod.UnifiedTileLod2 = BakeRawDirectionalTile(GroupSources, PodTileResLod2, 0.10f);
    Pod.CoarseTileLod3  = BakeRawDirectionalTile(GroupSources, PodTileResLod3, 0.08f);
    Pod.EstimatedVramKb = OneCubeGridPodVramKilobytes;

    return Pod;
}

std::array<float, 3> EvaluateCubeGridPodAtSurface(
    const CompiledCubeGridPod&  Pod,
    const std::array<float, 3>& ShadingPos,
    const std::array<float, 3>& SurfaceNormal,
    const std::array<float, 3>& ViewDir,
    float                       Roughness,
    const std::array<float, 3>& Albedo,
    float                       LodBias,
    uint32_t*                   OutTileLookupCount) noexcept
{
    const float Dist = std::sqrt(
        (ShadingPos[0] - Pod.FluxCentroidWorld[0]) * (ShadingPos[0] - Pod.FluxCentroidWorld[0]) +
        (ShadingPos[1] - Pod.FluxCentroidWorld[1]) * (ShadingPos[1] - Pod.FluxCentroidWorld[1]) +
        (ShadingPos[2] - Pod.FluxCentroidWorld[2]) * (ShadingPos[2] - Pod.FluxCentroidWorld[2]));

    const float Window = EvaluateQuarticInfluenceWindow(Dist, Pod.InfluenceRadiusM);
    if (Window <= 1.0e-4f)
    {
        if (OutTileLookupCount) *OutTileLookupCount = 0u;
        return { 0.0f, 0.0f, 0.0f };
    }

    const float Lod = ComputeContinuousPodLod(Dist, Pod.BoundingDiameterM, LodBias);
    const uint32_t Tier0 = std::min(3u, static_cast<uint32_t>(std::floor(Lod)));
    const uint32_t Tier1 = std::min(3u, Tier0 + 1u);
    const float Blend = SmoothHermite(Lod - static_cast<float>(Tier0));

    uint32_t Lookups = 0u;
    const auto EvalTier = [&](uint32_t Tier) noexcept -> std::array<float, 3> {
        if (Tier == 0u)
        {
            std::array<float, 3> Sum { 0.0f, 0.0f, 0.0f };
            for (const CubeGridActiveCell& Cell : Pod.ActiveCellsLod0)
            {
                const std::array<float, 3> Anchor {
                    Cell.VoxelCenterWorld[0] + Cell.SubVoxelOffset[0],
                    Cell.VoxelCenterWorld[1] + Cell.SubVoxelOffset[1],
                    Cell.VoxelCenterWorld[2] + Cell.SubVoxelOffset[2]
                };
                const auto Ray = Normalize3({ ShadingPos[0] - Anchor[0], ShadingPos[1] - Anchor[1], ShadingPos[2] - Anchor[2] });
                const auto Cd = SampleOctahedralTileBilinear(Cell.TileLod0, Ray);
                const auto Rad = ShadeFromRadiator(ShadingPos, SurfaceNormal, ViewDir, Roughness, Albedo, Anchor, Cell.ApertureRadiusM, Cd);
                Sum[0] += Rad[0]; Sum[1] += Rad[1]; Sum[2] += Rad[2];
                ++Lookups;
            }
            return Sum;
        }
        if (Tier == 1u && !Pod.BimodalPolesLod1.empty())
        {
            std::array<float, 3> Sum { 0.0f, 0.0f, 0.0f };
            for (const CubeGridBimodalPole& Pole : Pod.BimodalPolesLod1)
            {
                const auto Ray = Normalize3({ ShadingPos[0] - Pole.AnchorWorld[0], ShadingPos[1] - Pole.AnchorWorld[1], ShadingPos[2] - Pole.AnchorWorld[2] });
                const auto Cd = SampleOctahedralTileBilinear(Pole.TileLod1, Ray);
                const auto Rad = ShadeFromRadiator(ShadingPos, SurfaceNormal, ViewDir, Roughness, Albedo, Pole.AnchorWorld, Pole.ApertureRadiusM, Cd);
                Sum[0] += Rad[0]; Sum[1] += Rad[1]; Sum[2] += Rad[2];
                ++Lookups;
            }
            return Sum;
        }
        const OctahedralDirectionalTile& Tile = (Tier == 2u) ? Pod.UnifiedTileLod2 : Pod.CoarseTileLod3;
        const auto Ray = Normalize3({ ShadingPos[0] - Pod.FluxCentroidWorld[0], ShadingPos[1] - Pod.FluxCentroidWorld[1], ShadingPos[2] - Pod.FluxCentroidWorld[2] });
        const auto Cd = SampleOctahedralTileBilinear(Tile, Ray);
        ++Lookups;
        return ShadeFromRadiator(ShadingPos, SurfaceNormal, ViewDir, Roughness, Albedo, Pod.FluxCentroidWorld, 0.30f, Cd);
    };

    std::array<float, 3> Rad0 = EvalTier(Tier0);
    if (Tier1 != Tier0 && Blend > 0.01f && Blend < 0.99f)
    {
        const auto Rad1 = EvalTier(Tier1);
        for (int C = 0; C < 3; ++C)
        {
            Rad0[C] = Rad0[C] * (1.0f - Blend) + Rad1[C] * Blend;
        }
    }
    else if (Tier1 != Tier0 && Blend >= 0.99f)
    {
        Rad0 = EvalTier(Tier1);
    }

    if (OutTileLookupCount) *OutTileLookupCount = Lookups;
    return { Rad0[0] * Window, Rad0[1] * Window, Rad0[2] * Window };
}

//------------------------------------------------------------------------------------------------------------------------
//                                  ITERATIVE RESIDUAL-ERROR OPTIMIZER & GPU SHEET PACKER
//------------------------------------------------------------------------------------------------------------------------

ResidualOptimizationReport OptimizeCubeGridPodResidual(
    CompiledCubeGridPod&                InOutPod,
    std::span<const UnifiedLightSource> GroundTruthGroupSources,
    uint32_t                            Iterations)
{
    ResidualOptimizationReport Report {};
    if (InOutPod.ActiveCellsLod0.empty() || GroundTruthGroupSources.empty())
    {
        return Report;
    }

    const auto Receivers = BuildCalibrationReceivers(InOutPod, GroundTruthGroupSources);
    const auto [InitRmse, InitPsnr] = MeasurePodCalibrationRmse(InOutPod, Receivers);
    Report.InitialRelativeRmsePercent = InitRmse;
    Report.InitialPsnrDecibels        = InitPsnr;

    const float HalfCell[3] = {
        0.5f * (InOutPod.BoundsMaxWorld[0] - InOutPod.BoundsMinWorld[0]) / static_cast<float>(PodSpatialGridDimX),
        0.5f * (InOutPod.BoundsMaxWorld[1] - InOutPod.BoundsMinWorld[1]) / static_cast<float>(PodSpatialGridDimY),
        0.5f * (InOutPod.BoundsMaxWorld[2] - InOutPod.BoundsMinWorld[2]) / static_cast<float>(PodSpatialGridDimZ)
    };

    float CurrentRmse = InitRmse;
    for (uint32_t Iter = 0u; Iter < Iterations; ++Iter)
    {
        const float Decay = 1.0f / (1.0f + 0.18f * static_cast<float>(Iter));

        // 1) Monotonic coordinate line search for Sub-Voxel Optical Anchor Offset & Aperture Radius
        for (CubeGridActiveCell& Cell : InOutPod.ActiveCellsLod0)
        {
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                const float Step = std::max(0.012f, HalfCell[Axis] * 0.45f * Decay);
                const float Orig = Cell.SubVoxelOffset[Axis];
                float BestVal = Orig;
                for (float Sign : { -1.0f, 1.0f })
                {
                    Cell.SubVoxelOffset[Axis] = std::clamp(Orig + Sign * Step, -HalfCell[Axis], HalfCell[Axis]);
                    const float Trial = MeasurePodCalibrationRmse(InOutPod, Receivers).first;
                    if (Trial < CurrentRmse)
                    {
                        CurrentRmse = Trial;
                        BestVal = Cell.SubVoxelOffset[Axis];
                    }
                }
                Cell.SubVoxelOffset[Axis] = BestVal;
            }

            const float OrigRad = Cell.ApertureRadiusM;
            float BestRad = OrigRad;
            const float RadStep = 0.045f * Decay;
            for (float Sign : { -1.0f, 1.0f })
            {
                Cell.ApertureRadiusM = std::clamp(OrigRad + Sign * RadStep, 0.04f, 0.48f);
                const float Trial = MeasurePodCalibrationRmse(InOutPod, Receivers).first;
                if (Trial < CurrentRmse)
                {
                    CurrentRmse = Trial;
                    BestRad = Cell.ApertureRadiusM;
                }
            }
            Cell.ApertureRadiusM = BestRad;
        }

        // 2) Projected Non-Negative Octahedral Deconvolution (W_c(u, v) >= 0)
        const float LearningRate = 0.42f * Decay;
        for (CubeGridActiveCell& Cell : InOutPod.ActiveCellsLod0)
        {
            const uint32_t Res = Cell.TileLod0.Resolution;
            std::vector<float> GradNum(static_cast<std::size_t>(Res) * Res * 3u, 0.0f);
            std::vector<float> GradDen(static_cast<std::size_t>(Res) * Res, 1.0e-4f);

            const std::array<float, 3> Anchor {
                Cell.VoxelCenterWorld[0] + Cell.SubVoxelOffset[0],
                Cell.VoxelCenterWorld[1] + Cell.SubVoxelOffset[1],
                Cell.VoxelCenterWorld[2] + Cell.SubVoxelOffset[2]
            };

            for (const CalibrationReceiver& Rec : Receivers)
            {
                float Pred[3] = { 0.0f, 0.0f, 0.0f };
                for (const CubeGridActiveCell& Other : InOutPod.ActiveCellsLod0)
                {
                    const std::array<float, 3> OA {
                        Other.VoxelCenterWorld[0] + Other.SubVoxelOffset[0],
                        Other.VoxelCenterWorld[1] + Other.SubVoxelOffset[1],
                        Other.VoxelCenterWorld[2] + Other.SubVoxelOffset[2]
                    };
                    const auto Ray = Normalize3({ Rec.Pos[0] - OA[0], Rec.Pos[1] - OA[1], Rec.Pos[2] - OA[2] });
                    const auto Cd = SampleOctahedralTileBilinear(Other.TileLod0, Ray);
                    const auto Rad = ShadeFromRadiator(Rec.Pos, Rec.Normal, Rec.ViewDir, Rec.Roughness, Rec.Albedo, OA, Other.ApertureRadiusM, Cd);
                    Pred[0] += Rad[0]; Pred[1] += Rad[1]; Pred[2] += Rad[2];
                }

                // Transfer Jacobian for this cell
                const auto UnitTransfer = ShadeFromRadiator(Rec.Pos, Rec.Normal, Rec.ViewDir, Rec.Roughness, Rec.Albedo, Anchor, Cell.ApertureRadiusM, { 1.0f, 1.0f, 1.0f });
                const float KWeight = (UnitTransfer[0] + UnitTransfer[1] + UnitTransfer[2]) / 3.0f;
                if (KWeight <= 1.0e-5f)
                {
                    continue;
                }

                const auto Ray = Normalize3({ Rec.Pos[0] - Anchor[0], Rec.Pos[1] - Anchor[1], Rec.Pos[2] - Anchor[2] });
                const auto [U, V] = OctahedralEncodeMinusY(Ray);
                const float ResF = static_cast<float>(Res);
                const float Fx = std::clamp(U * ResF - 0.5f, 0.0f, ResF - 1.0f);
                const float Fy = std::clamp(V * ResF - 0.5f, 0.0f, ResF - 1.0f);
                const uint32_t X0 = static_cast<uint32_t>(std::floor(Fx));
                const uint32_t Y0 = static_cast<uint32_t>(std::floor(Fy));
                const uint32_t X1 = std::min(Res - 1u, X0 + 1u);
                const uint32_t Y1 = std::min(Res - 1u, Y0 + 1u);
                const float Tx = Fx - static_cast<float>(X0);
                const float Ty = Fy - static_cast<float>(Y0);

                const float Residual[3] = {
                    Rec.ExactRgb[0] - Pred[0],
                    Rec.ExactRgb[1] - Pred[1],
                    Rec.ExactRgb[2] - Pred[2]
                };

                const uint32_t Xs[2] = { X0, X1 };
                const uint32_t Ys[2] = { Y0, Y1 };
                const float Wx[2] = { 1.0f - Tx, Tx };
                const float Wy[2] = { 1.0f - Ty, Ty };
                for (int Iy = 0; Iy < 2; ++Iy)
                {
                    for (int Ix = 0; Ix < 2; ++Ix)
                    {
                        const float Bw = Wx[Ix] * Wy[Iy];
                        const std::size_t Idx = static_cast<std::size_t>(Ys[Iy]) * Res + Xs[Ix];
                        GradDen[Idx] += Bw * KWeight * KWeight;
                        for (int C = 0; C < 3; ++C)
                        {
                            GradNum[Idx * 3u + C] += Bw * KWeight * Residual[C];
                        }
                    }
                }
            }

            std::vector<float> Backup = Cell.TileLod0.TexelsRgb;
            for (std::size_t Idx = 0u; Idx < GradDen.size(); ++Idx)
            {
                if (GradDen[Idx] <= 1.0e-3f)
                {
                    continue;
                }
                for (int C = 0; C < 3; ++C)
                {
                    const float Updated = Cell.TileLod0.TexelsRgb[Idx * 3u + C] + LearningRate * (GradNum[Idx * 3u + C] / GradDen[Idx]);
                    Cell.TileLod0.TexelsRgb[Idx * 3u + C] = std::max(0.0f, Updated); // Strict non-negativity (zero Gibbs ringing!)
                }
            }
            const float TrialRmse = MeasurePodCalibrationRmse(InOutPod, Receivers).first;
            if (TrialRmse <= CurrentRmse)
            {
                CurrentRmse = TrialRmse;
            }
            else
            {
                Cell.TileLod0.TexelsRgb = std::move(Backup);
            }
        }
        Report.CompletedIterations = Iter + 1u;
    }

    const auto [FinalRmse, FinalPsnr] = MeasurePodCalibrationRmse(InOutPod, Receivers);
    Report.OptimizedRelativeRmsePercent = FinalRmse;
    Report.OptimizedPsnrDecibels        = FinalPsnr;
    return Report;
}

PackedPodAtlasSheet PackPodAtlasSheetRgba32F(
    std::span<CompiledCubeGridPod> Pods,
    uint32_t                       SheetResolution)
{
    PackedPodAtlasSheet Sheet {};
    Sheet.WidthPx  = SheetResolution;
    Sheet.HeightPx = SheetResolution;
    Sheet.Rgba32F.assign(static_cast<std::size_t>(SheetResolution) * SheetResolution * 4u, 0.0f);

    uint32_t CursorX = 0u;
    uint32_t CursorY = 0u;
    uint32_t RowHeight = 0u;

    const auto BlitTile = [&](OctahedralDirectionalTile& Tile) {
        const uint32_t Res = Tile.Resolution;
        if (Res == 0u || Tile.TexelsRgb.empty())
        {
            return;
        }
        if (CursorX + Res > SheetResolution)
        {
            CursorX = 0u;
            CursorY += RowHeight;
            RowHeight = 0u;
        }
        if (CursorY + Res > SheetResolution)
        {
            return; // Atlas full
        }
        Tile.AtlasOriginX = CursorX;
        Tile.AtlasOriginY = CursorY;
        for (uint32_t Y = 0u; Y < Res; ++Y)
        {
            for (uint32_t X = 0u; X < Res; ++X)
            {
                const std::size_t SrcBase = (static_cast<std::size_t>(Y) * Res + X) * 3u;
                const std::size_t DstBase = (static_cast<std::size_t>(CursorY + Y) * SheetResolution + (CursorX + X)) * 4u;
                Sheet.Rgba32F[DstBase + 0u] = Tile.TexelsRgb[SrcBase + 0u];
                Sheet.Rgba32F[DstBase + 1u] = Tile.TexelsRgb[SrcBase + 1u];
                Sheet.Rgba32F[DstBase + 2u] = Tile.TexelsRgb[SrcBase + 2u];
                Sheet.Rgba32F[DstBase + 3u] = 1.0f;
            }
        }
        CursorX += Res;
        RowHeight = std::max(RowHeight, Res);
    };

    for (CompiledCubeGridPod& Pod : Pods)
    {
        for (CubeGridActiveCell& Cell : Pod.ActiveCellsLod0) BlitTile(Cell.TileLod0);
        for (CubeGridBimodalPole& Pole : Pod.BimodalPolesLod1) BlitTile(Pole.TileLod1);
        BlitTile(Pod.UnifiedTileLod2);
        BlitTile(Pod.CoarseTileLod3);
    }

    return Sheet;
}

} // namespace Frontier::GeometricRaster
