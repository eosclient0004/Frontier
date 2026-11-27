/*============================================================================================================================================
                                                      VOLUMETRICCLOUDSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Single-source Slang Celestial Volumetric Clouds (PlanetaryCloudGenerated.hpp), Live Hemispherical Sky Ambient & Unified Local Media

#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/Mathematics/ScalarMath.h"

#include <algorithm>
#include <array>
#include <cmath>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "PlanetaryCloudGenerated.hpp"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace Frontier::GeometricRaster
{
namespace
{



[[nodiscard]] inline float CelestialHash13Cpu(float Px, float Py, float Pz) noexcept
{
    float X = Frontier::Mathematics::EvaluateFractionalPart(Px * 0.1031f);
    float Y = Frontier::Mathematics::EvaluateFractionalPart(Py * 0.1031f);
    float Z = Frontier::Mathematics::EvaluateFractionalPart(Pz * 0.1031f);
    const float D = X * (Z + 31.32f) + Y * (Y + 31.32f) + Z * (X + 31.32f);
    X += D;
    Y += D;
    Z += D;
    return Frontier::Mathematics::EvaluateFractionalPart((X + Y) * Z);
}

[[nodiscard]] float CelestialScalarNoiseCpu(float Px, float Py, float Pz) noexcept
{
    const float Ix = std::floor(Px);
    const float Iy = std::floor(Py);
    const float Iz = std::floor(Pz);
    float Fx = Px - Ix;
    float Fy = Py - Iy;
    float Fz = Pz - Iz;
    Fx = Fx * Fx * (3.0f - 2.0f * Fx);
    Fy = Fy * Fy * (3.0f - 2.0f * Fy);
    Fz = Fz * Fz * (3.0f - 2.0f * Fz);

    const float N000 = CelestialHash13Cpu(Ix,        Iy,        Iz);
    const float N100 = CelestialHash13Cpu(Ix + 1.0f, Iy,        Iz);
    const float N010 = CelestialHash13Cpu(Ix,        Iy + 1.0f, Iz);
    const float N110 = CelestialHash13Cpu(Ix + 1.0f, Iy + 1.0f, Iz);
    const float N001 = CelestialHash13Cpu(Ix,        Iy,        Iz + 1.0f);
    const float N101 = CelestialHash13Cpu(Ix + 1.0f, Iy,        Iz + 1.0f);
    const float N011 = CelestialHash13Cpu(Ix,        Iy + 1.0f, Iz + 1.0f);
    const float N111 = CelestialHash13Cpu(Ix + 1.0f, Iy + 1.0f, Iz + 1.0f);

    const float X00 = N000 + (N100 - N000) * Fx;
    const float X10 = N010 + (N110 - N010) * Fx;
    const float X01 = N001 + (N101 - N001) * Fx;
    const float X11 = N011 + (N111 - N011) * Fx;
    const float Y0  = X00 + (X10 - X00) * Fy;
    const float Y1  = X01 + (X11 - X01) * Fy;
    return Y0 + (Y1 - Y0) * Fz;
}

[[nodiscard]] inline float CelestialPhaseHGCpu(float CosTheta, float AnisotropyG) noexcept
{
    const float G   = std::clamp(AnisotropyG, -0.95f, 0.95f);
    const float G2  = G * G;
    const float Den = std::max(1.0e-4f, 1.0f + G2 - 2.0f * G * CosTheta);
    return (1.0f - G2) / (4.0f * Constants::PiFloat * std::pow(Den, 1.5f));
}

[[nodiscard]] Vector<float, 3> ToSlangVec3(const Mathematics::Axis3D& V) noexcept
{
    return Vector<float, 3>{ V.X, V.Y, V.Z };
}

[[nodiscard]] Vector<float, 2> PixelFromJitter(float PixelJitter01, const Mathematics::Axis3D& Rd) noexcept
{
    const float J = (PixelJitter01 >= 0.0f)
        ? std::clamp(PixelJitter01, 0.0f, 0.9999f)
        : Frontier::Mathematics::EvaluateFractionalPart(52.9829189f * Frontier::Mathematics::EvaluateFractionalPart(Rd.X * 193.713f + Rd.Y * 371.419f + Rd.Z * 547.823f));
    // Direct scalar handoff to CloudBlueJitter (Pixel.x < 0 passes Pixel.y verbatim)
    return Vector<float, 2>{ -1.0f, J };
}

[[nodiscard]] CloudSpecification_0 BuildCpuCloudSpec(
    const CloudDeckConfiguration&         Deck,
    const AtmosphericMediumConfiguration& Medium,
    const Mathematics::Axis3D&            SunDir,
    const Mathematics::LinearRGB&         CloudSolarIrrad,
    const Mathematics::LinearRGB&         SkyAmbient,
    float                                 WindVX,
    float                                 WindVY,
    float                                 SimulationSeconds,
    uint32_t                              FormationOverride = 0xFFFFFFFFu) noexcept
{
    CloudSpecification_0 Spec {};
    Spec.RadiusM_0             = std::max(500000.0f, Medium.PlanetRadiusMeters);
    Spec.Fraction_0            = std::clamp(Deck.CloudFraction, 0.0f, 1.0f);
    Spec.Extinction_0          = std::max(0.0f, Deck.ExtinctionMultiplier);
    Spec.FloorM_0              = std::clamp(Deck.CloudFloorMeters, 50.0f, std::max(200.0f, Deck.DeckCeilingMeters - 50.0f));
    Spec.ThicknessM_0          = std::max(50.0f, Deck.LayerThicknessMeters);
    Spec.FeatureScale_0        = std::max(0.30f, Deck.FeatureScaleMultiplier);
    Spec.Anvil_0               = std::clamp(Deck.AnvilTopSpread, 0.0f, 1.0f);
    Spec.PhaseG_0              = std::clamp(Deck.ForwardScatterAnisotropy, 0.0f, 0.95f);
    Spec.BackPhaseG_0          = std::clamp(Deck.BackScatterAnisotropy, 0.0f, 0.90f);
    Spec.PhaseMix_0            = std::clamp(Deck.PhaseLobeBlend, 0.0f, 1.0f);
    Spec.Absorption_0          = std::max(0.0f, Deck.AbsorptionRatio);
    Spec.SkyAmbientGain_0      = std::max(0.0f, Deck.SkyAmbientGain);
    Spec.PowderStrength_0      = std::clamp(Deck.PowderStrength, 0.0f, 1.0f);
    Spec.ErosionDetail_0       = std::clamp(Deck.ErosionDetail, 0.0f, 1.0f);
    Spec.CloudAlbedo_0         = Vector<float, 3>{
        std::max(0.0f, Deck.CloudAlbedo.R),
        std::max(0.0f, Deck.CloudAlbedo.G),
        std::max(0.0f, Deck.CloudAlbedo.B)
    };
    Spec.Seed_0                = 0.0f;
    Spec.PixelAngle_0          = 1.0f / 480.0f;
    Spec.UniformExtinction_0   = -1.0f;
    Spec.TimeSeconds_0         = Deck.FollowWindEnabled ? SimulationSeconds : 0.0f;
    Spec.WindVelocityMps_0     = Vector<float, 2>{ WindVX, WindVY };
    Spec.Shear_0               = Deck.WindShearPerKilometer;
    Spec.Veer_0                = Deck.WindVeerDegreesPerKm * Constants::DegToRadFloat;
    Spec.Turbulence_0          = Deck.WindTurbulence;
    Spec.CycloneSpeedMps_0     = 0.0f;
    Spec.CycloneEyeM_0         = 80000.0f;
    Spec.CycloneAxis_0         = Vector<float, 3>{ 0.0f, 0.0f, 1.0f };
    Spec.SunDirection_0        = normalize_0(ToSlangVec3(SunDir));
    Spec.SolarIrradiance_0     = Vector<float, 3>{ CloudSolarIrrad.R, CloudSolarIrrad.G, CloudSolarIrrad.B };
    Spec.SkyAmbient_0          = Vector<float, 3>{ SkyAmbient.R, SkyAmbient.G, SkyAmbient.B };
    Spec.AtmosphereStrengths_0 = Vector<float, 3>{
        std::max(0.0f, Medium.RayleighStrength),
        std::max(0.0f, Medium.MieStrength),
        std::max(0.0f, Medium.OzoneStrength)
    };
    Spec.AirScaleHeightsM_0    = Vector<float, 2>{
        std::max(400.0f, Medium.RayleighScaleHeightMeters),
        std::max(200.0f, Medium.MieScaleHeightMeters)
    };
    Spec.Steps_0               = std::clamp(Deck.CloudMarchSteps, 16u, 64u);
    Spec.LightSteps_0          = std::clamp(Deck.LightMarchTaps, 3u, 5u);
    Spec.Formation_0           = (FormationOverride != 0xFFFFFFFFu)
        ? FormationOverride
        : std::min<uint32_t>(12u, static_cast<uint32_t>(Deck.Category));
    Spec.UseClouds_0           = Deck.CloudsEnabled;
    Spec.UseCloudShadows_0     = Deck.CloudsEnabled && Deck.CloudShadow.ShadowEnabled;
    return Spec;
}

void ResolveDeckWindVelocity(
    const CloudDeckConfiguration&    Deck,
    const AtmosphericWindParameters* OptionalGlobalWind,
    float                            SimulationSeconds,
    float&                           OutVX,
    float&                           OutVY) noexcept
{
    if (!Deck.FollowWindEnabled)
    {
        const float BearRad = Deck.WindBearingDegrees * Constants::DegToRadFloat;
        OutVX = std::cos(BearRad) * Deck.WindSpeedMetersPerSecond;
        OutVY = -std::sin(BearRad) * Deck.WindSpeedMetersPerSecond;
        return;
    }
    if (OptionalGlobalWind != nullptr)
    {
        const ResolvedWindSample S = AtmosphericWindSolver::ResolveWindSample(
            *OptionalGlobalWind,
            Deck.WindSpeedMetersPerSecond,
            Deck.WindBearingDegrees,
            Deck.WindSource,
            { 0.0f, 0.0f, Deck.CloudFloorMeters },
            Deck.CloudFloorMeters,
            SimulationSeconds);
        OutVX = S.VelocityMetersPerSec.X;
        OutVY = S.VelocityMetersPerSec.Y;
        return;
    }
    const float BearRad = Deck.WindBearingDegrees * Constants::DegToRadFloat;
    OutVX = std::cos(BearRad) * Deck.WindSpeedMetersPerSecond;
    OutVY = -std::sin(BearRad) * Deck.WindSpeedMetersPerSecond;
}

[[nodiscard]] float EvaluateBoundedShapeMaskCpu(
    const Mathematics::Axis3D&   PointWorld,
    const Mathematics::Axis3D&   CenterWorld,
    const Mathematics::Scalar3D& HalfExtent,
    AtmosphericVolumeShape       ShapeEnum,
    float                        YawDegrees,
    uint32_t                     PrismSides,
    float                        EdgeSoftness,
    bool                         DensityFalloff,
    float&                       OutHeightNorm) noexcept
{
    const float Hx = std::max(0.1f, HalfExtent.X);
    const float Hy = std::max(0.1f, HalfExtent.Y);
    const float Hz = std::max(0.1f, HalfExtent.Z);
    const float Rx = PointWorld.X - CenterWorld.X;
    const float Ry = PointWorld.Y - CenterWorld.Y;
    const float Rz = PointWorld.Z - CenterWorld.Z;

    const float YawRad = YawDegrees * Constants::DegToRadFloat;
    const float C      = std::cos(-YawRad);
    const float S      = std::sin(-YawRad);
    const float Lx     = C * Rx - S * Ry;
    const float Ly     = S * Rx + C * Ry;
    const float Lz     = Rz;

    const float Soft = std::clamp(EdgeSoftness, 0.01f, 1.0f);
    const float Ux   = Lx / Hx;
    const float Uy   = Ly / Hy;
    const float Uz   = Lz / Hz;
    OutHeightNorm    = std::clamp(0.5f * (Uz + 1.0f), 0.0f, 1.0f);

    const uint32_t Shape = static_cast<uint32_t>(ShapeEnum);
    float DistNorm = 1.0e6f;
    if (Shape == 0u)
    {
        DistNorm = std::max(std::abs(Ux), std::max(std::abs(Uy), std::abs(Uz)));
    }
    else if (Shape == 1u)
    {
        const float MinExt = std::min(Hx, std::min(Hy, Hz));
        DistNorm = std::sqrt(Lx * Lx + Ly * Ly + Lz * Lz) / std::max(0.1f, MinExt);
    }
    else if (Shape == 2u)
    {
        DistNorm = std::sqrt(Ux * Ux + Uy * Uy + Uz * Uz);
    }
    else if (Shape == 3u)
    {
        DistNorm = std::max(std::sqrt(Ux * Ux + Uy * Uy), std::abs(Uz));
    }
    else if (Shape == 4u)
    {
        const float Taper = std::clamp(0.5f * (1.0f - Uz), 0.0f, 1.0f);
        const float Rad   = std::sqrt(Ux * Ux + Uy * Uy);
        DistNorm = std::max(std::abs(Uz), (Taper > 1.0e-3f) ? (Rad / Taper) : (Rad > 1.0e-4f ? 2.0f : 1.0f));
    }
    else if (Shape == 5u)
    {
        DistNorm = std::abs(Ux) + std::abs(Uy) + std::abs(Uz);
    }
    else if (Shape == 6u)
    {
        const uint32_t Sides = std::clamp(PrismSides, 3u, 16u);
        const float Sector   = (2.0f * Constants::PiFloat) / static_cast<float>(Sides);
        const float Angle    = std::atan2(Uy, Ux);
        const float ModA     = std::fmod(Angle + 4.0f * Constants::PiFloat + 0.5f * Sector, Sector) - 0.5f * Sector;
        const float PolyR    = (std::sqrt(Ux * Ux + Uy * Uy) * std::cos(ModA)) / std::max(0.25f, std::cos(0.5f * Sector));
        DistNorm = std::max(PolyR, std::abs(Uz));
    }
    else // HalfSphere
    {
        if (Uz < -0.14f) return 0.0f;
        const float BottomF = Frontier::Mathematics::EvaluateSmoothStep(-0.14f, 0.06f, Uz);
        const float Pz      = std::max(0.0f, Uz);
        const float RadialD = std::sqrt(Ux * Ux + Uy * Uy + Pz * Pz);
        OutHeightNorm       = std::clamp(Pz, 0.0f, 1.0f);
        const float Margin  = std::clamp(1.0f - RadialD, 0.0f, 1.0f);
        if (Margin * BottomF <= 0.0f) return 0.0f;
        if (DensityFalloff)
        {
            const float ExpP = 0.35f + (3.0f - 0.35f) * Soft;
            return std::pow(Frontier::Mathematics::EvaluateSmoothStep(0.0f, 1.0f, Margin), ExpP) * BottomF;
        }
        return Frontier::Mathematics::EvaluateSmoothStep(0.0f, std::max(Soft, 0.01f), Margin) * BottomF;
    }

    const float Margin = std::clamp(1.0f - DistNorm, 0.0f, 1.0f);
    if (Margin <= 0.0f) return 0.0f;
    if (DensityFalloff)
    {
        const float ExpP = 0.35f + (3.0f - 0.35f) * Soft;
        return std::pow(Frontier::Mathematics::EvaluateSmoothStep(0.0f, 1.0f, Margin), ExpP);
    }
    return Frontier::Mathematics::EvaluateSmoothStep(0.0f, std::max(Soft, 0.01f), Margin);
}

[[nodiscard]] float EvaluateSingleLocalCloudDensityCpu(
    const BoundedCloudVolume&           Vol,
    const Mathematics::Axis3D&          PointWorld,
    const WorldAtmosphereConfiguration& Atmo,
    bool                                ApplyDetailErosion,
    float&                              OutHeightNorm) noexcept
{
    OutHeightNorm = 0.5f;
    if (!Vol.LocalEnabled || Vol.ExtinctionMultiplier <= 1.0e-4f || Vol.CloudFraction <= 1.0e-4f) return 0.0f;

    const float Mask = EvaluateBoundedShapeMaskCpu(
        PointWorld, Vol.VolumeCenterMeters, Vol.HalfExtentMeters, Vol.Shape,
        Vol.YawDegrees, Vol.PrismSides, std::clamp(Vol.EdgeSoftness, 0.02f, 1.0f), false, OutHeightNorm);
    if (Mask <= 1.0e-4f) return 0.0f;

    float Prof = 1.0f;
    if (Vol.Form == LocalCloudForm::Stratiform)
    {
        Prof = Frontier::Mathematics::EvaluateSmoothStep(0.0f, 0.15f, OutHeightNorm) * (1.0f - Frontier::Mathematics::EvaluateSmoothStep(0.75f, 1.0f, OutHeightNorm));
    }
    else if (Vol.Form == LocalCloudForm::Cumuliform)
    {
        Prof = Frontier::Mathematics::EvaluateSmoothStep(0.0f, 0.08f, OutHeightNorm) * (1.0f - Frontier::Mathematics::EvaluateSmoothStep(0.35f, 1.0f, OutHeightNorm)) * 1.25f;
    }
    else
    {
        Prof = Frontier::Mathematics::EvaluateSmoothStep(0.0f, 0.30f, OutHeightNorm) * (1.0f - Frontier::Mathematics::EvaluateSmoothStep(0.55f, 1.0f, OutHeightNorm)) * 0.55f;
    }
    if (Prof <= 1.0e-4f) return 0.0f;

    const float TimeSec = Atmo.SimulationSeconds;
    float AdvX = PointWorld.X;
    float AdvY = PointWorld.Y;
    float AdvZ = PointWorld.Z;
    if (Vol.FollowWindEnabled)
    {
        const ResolvedWindSample Wind = AtmosphericWindSolver::ResolveWindSample(
            Atmo.Wind, Atmo.Clouds.WindSpeedMetersPerSecond, Atmo.Clouds.WindBearingDegrees,
            Vol.WindSource, Vol.VolumeCenterMeters, Vol.VolumeCenterMeters.Z, TimeSec);
        const float Wx = Wind.VelocityMetersPerSec.X;
        const float Wy = Wind.VelocityMetersPerSec.Y;
        AdvX -= Wx * TimeSec * 0.85f;
        AdvY -= Wy * TimeSec * 0.85f;
        const float WLen = std::sqrt(Wx * Wx + Wy * Wy);
        if (WLen > 0.5f)
        {
            const float Cx = AdvX * 0.045f + TimeSec * 0.05f;
            const float Cy = AdvY * 0.045f - TimeSec * 0.04f;
            const float Cz = AdvZ * 0.045f;
            const float Sx = CelestialScalarNoiseCpu(Cx, Cy + 0.5f, Cz) - CelestialScalarNoiseCpu(Cx, Cy - 0.5f, Cz);
            const float Sy = CelestialScalarNoiseCpu(Cx + 0.5f, Cy, Cz) - CelestialScalarNoiseCpu(Cx - 0.5f, Cy, Cz);
            const float Sw = std::min(WLen * 0.12f, 3.5f);
            AdvX += Sx * Sw;
            AdvY -= Sy * Sw;
        }
    }

    const float ScaleM = std::max(0.5f, Vol.FeatureScaleMeters);
    const float Sx = (AdvX - Vol.VolumeCenterMeters.X) / ScaleM + TimeSec * 0.015f;
    const float Sy = (AdvY - Vol.VolumeCenterMeters.Y) / ScaleM + TimeSec * 0.008f;
    const float Sz = (AdvZ - Vol.VolumeCenterMeters.Z) / ScaleM;

    float ShapeN = CelestialScalarNoiseCpu(Sx, Sy, Sz) * 0.50f
                 + CelestialScalarNoiseCpu(Sx * 2.02f + 3.1f, Sy * 2.02f + 9.2f, Sz * 2.02f + 1.7f) * 0.25f
                 + CelestialScalarNoiseCpu(Sx * 4.10f + 7.7f, Sy * 4.10f + 1.1f, Sz * 4.10f + 2.2f) * 0.125f
                 + CelestialScalarNoiseCpu(Sx * 8.30f + 1.3f, Sy * 8.30f + 4.4f, Sz * 8.30f + 8.8f) * 0.0625f;
    ShapeN /= 0.9375f;

    const float Cov  = std::clamp(Vol.CloudFraction, 0.01f, 1.0f);
    float LocalDensity = std::clamp((ShapeN - (1.0f - Cov)) / std::max(1.0e-3f, Cov), 0.0f, 1.0f) * Prof * Mask;
    if (LocalDensity <= 1.0e-4f) return 0.0f;

    if (ApplyDetailErosion)
    {
        const float ErodeK = std::clamp(Vol.ErosionDetail, 0.0f, 1.0f);
        const float Det    = CelestialScalarNoiseCpu(Sx * 9.0f + TimeSec * 0.02f, Sy * 9.0f + TimeSec * 0.02f, Sz * 9.0f + TimeSec * 0.02f) * 0.60f
                           + CelestialScalarNoiseCpu(Sx * 19.0f + 5.0f, Sy * 19.0f + 5.0f, Sz * 19.0f + 5.0f) * 0.40f;
        const float MixH   = std::clamp(OutHeightNorm * 3.0f, 0.0f, 1.0f);
        const float Erode  = (Det + (1.0f - 2.0f * Det) * MixH) * ErodeK * 0.45f;
        LocalDensity       = std::clamp(LocalDensity - Erode * (1.0f - LocalDensity), 0.0f, 1.0f);
    }

    return LocalDensity * Vol.ExtinctionMultiplier * 0.14f;
}

[[nodiscard]] float EvaluateSingleLocalFogDensityUnifiedCpu(
    const LocalFogVolume&               Vol,
    const Mathematics::Axis3D&          PointWorld,
    const WorldAtmosphereConfiguration& Atmo,
    float&                              OutHeightNorm) noexcept
{
    OutHeightNorm = 0.5f;
    if (!Vol.Enabled || Vol.Density <= 1.0e-4f) return 0.0f;

    const float Mask = EvaluateBoundedShapeMaskCpu(
        PointWorld, Vol.VolumeCenterMeters, Vol.HalfExtentMeters, Vol.Shape,
        Vol.YawDegrees, Vol.PrismSides, std::clamp(Vol.EdgeSoftness, 0.01f, 1.0f),
        Vol.DensityFalloffEnabled, OutHeightNorm);
    if (Mask <= 1.0e-4f) return 0.0f;

    const float TimeSec = Atmo.SimulationSeconds;
    float AdvX = PointWorld.X;
    float AdvY = PointWorld.Y;
    float AdvZ = PointWorld.Z;
    if (Vol.FollowWind)
    {
        const ResolvedWindSample Wind = AtmosphericWindSolver::ResolveWindSample(
            Atmo.Wind, Atmo.Clouds.WindSpeedMetersPerSecond, Atmo.Clouds.WindBearingDegrees,
            Vol.WindSource, Vol.VolumeCenterMeters, Vol.VolumeCenterMeters.Z, TimeSec);
        const float Wx = Wind.VelocityMetersPerSec.X;
        const float Wy = Wind.VelocityMetersPerSec.Y;
        AdvX -= Wx * TimeSec * 0.85f;
        AdvY -= Wy * TimeSec * 0.85f;
        const float WLen = std::sqrt(Wx * Wx + Wy * Wy);
        if (WLen > 0.5f)
        {
            const float Cx = AdvX * 0.08f + TimeSec * 0.06f;
            const float Cy = AdvY * 0.08f - TimeSec * 0.05f;
            const float Cz = AdvZ * 0.08f;
            const float Sx = CelestialScalarNoiseCpu(Cx, Cy + 0.4f, Cz) - CelestialScalarNoiseCpu(Cx, Cy - 0.4f, Cz);
            const float Sy = CelestialScalarNoiseCpu(Cx + 0.4f, Cy, Cz) - CelestialScalarNoiseCpu(Cx - 0.4f, Cy, Cz);
            const float Sw = std::min(WLen * 0.08f, 2.2f);
            AdvX += Sx * Sw;
            AdvY -= Sy * Sw;
        }
    }

    const float ScaleM = std::max(0.25f, Vol.FeatureScaleMeters);
    const float Px     = AdvX / ScaleM + TimeSec * 0.04f;
    const float Py     = AdvY / ScaleM + TimeSec * 0.02f;
    const float Pz     = AdvZ / ScaleM + TimeSec * 0.015f;
    const float N      = CelestialScalarNoiseCpu(Px, Py, Pz) * 0.55f
                       + CelestialScalarNoiseCpu(Px * 2.1f + 2.3f, Py * 2.1f + 4.7f, Pz * 2.1f + 1.1f) * 0.30f
                       + CelestialScalarNoiseCpu(Px * 4.4f + 6.1f, Py * 4.4f + 0.9f, Pz * 4.4f + 3.8f) * 0.15f;

    const float Hetero = std::clamp(Vol.Coverage, 0.0f, 1.0f);
    const float Mod    = (1.0f - Hetero) + Hetero * (Frontier::Mathematics::EvaluateSmoothStep(0.18f, 0.86f, N) * 1.45f);
    const float Grav   = 1.0f - 0.35f * OutHeightNorm;
    return Vol.Density * 0.16f * Mask * Mod * Grav;
}

void GatherActiveLocalClouds(
    const WorldAtmosphereConfiguration& Atmo,
    std::array<const BoundedCloudVolume*, 8u>& OutPtrs,
    uint32_t&                                  OutCount) noexcept
{
    OutCount = 0u;
    if (Atmo.LocalCloud.LocalEnabled && OutCount < 8u)
    {
        OutPtrs[OutCount++] = &Atmo.LocalCloud;
    }
    for (const auto& V : Atmo.LocalClouds)
    {
        if (V.LocalEnabled && OutCount < 8u)
        {
            OutPtrs[OutCount++] = &V;
        }
    }
}

[[nodiscard]] Mathematics::LinearRGB EvaluateDeckViaPlanetarySlang(
    const Mathematics::Axis3D&            RayOriginWorld,
    const Mathematics::Axis3D&            RayDirection,
    const Mathematics::Axis3D&            SunDirection,
    const Mathematics::LinearRGB&         CloudSolarIrrad,
    const Mathematics::LinearRGB&         SkyAmbient,
    const CloudDeckConfiguration&         Deck,
    const AtmosphericMediumConfiguration& Medium,
    const AtmosphericWindParameters*      OptionalGlobalWind,
    float                                 SimulationSeconds,
    float&                                OutTransmittance,
    float                                 PixelJitter01) noexcept
{
    OutTransmittance = 1.0f;
    if (!Deck.CloudsEnabled || Deck.CloudFraction <= 0.001f || Deck.ExtinctionMultiplier <= 0.001f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    float WindVX = 0.0f, WindVY = 0.0f;
    ResolveDeckWindVelocity(Deck, OptionalGlobalWind, SimulationSeconds, WindVX, WindVY);

    CloudSpecification_0 Spec = BuildCpuCloudSpec(
        Deck, Medium, SunDirection, CloudSolarIrrad, SkyAmbient, WindVX, WindVY, SimulationSeconds);

    const float ObsAltM = std::max(0.0f, Medium.ObserverAltitudeMeters);
    const Vector<float, 3> Origin { RayOriginWorld.X, RayOriginWorld.Y, Spec.RadiusM_0 + ObsAltM + RayOriginWorld.Z };
    const Vector<float, 3> Dir    = normalize_0(ToSlangVec3(RayDirection));
    const float GroundM           = PlanetCloudGroundDistance_0(Origin, Dir, Spec.RadiusM_0);
    CloudTransportIntegral_0 Integral = TracePlanetaryClouds_0(&Spec, Origin, Dir, PixelFromJitter(PixelJitter01, RayDirection), GroundM);

    OutTransmittance = std::clamp(Integral.Transmittance_0, 0.0f, 1.0f);
    if (ObsAltM + RayOriginWorld.Z > 80000.0f)
    {
        const Vector<float, 3> Rad = CloudRadianceThroughAir_0(&Spec, Origin, Dir, &Integral, 12u);
        return { std::max(0.0f, Rad.x), std::max(0.0f, Rad.y), std::max(0.0f, Rad.z) };
    }
    return {
        std::max(0.0f, Integral.Radiance_0.x),
        std::max(0.0f, Integral.Radiance_0.y),
        std::max(0.0f, Integral.Radiance_0.z)
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            CANONICAL CLOUD CATEGORY PRESETS
//------------------------------------------------------------------------------------------------------------------------

void VolumetricCloudSequence::ApplyCloudCategoryPreset(
    CloudDeckConfiguration& InOutDeck,
    CloudCategoryType       Category) noexcept
{
    InOutDeck.Category                 = Category;
    InOutDeck.ForwardScatterAnisotropy = 0.80f;
    InOutDeck.BackScatterAnisotropy    = 0.30f;
    InOutDeck.PhaseLobeBlend           = 0.30f;
    InOutDeck.AbsorptionRatio          = 0.05f;
    InOutDeck.SkyAmbientGain           = 0.90f;
    InOutDeck.PowderStrength           = 0.60f;
    InOutDeck.ErosionDetail            = 0.60f;
    InOutDeck.CloudAlbedo              = { 1.00f, 1.00f, 1.00f };
    InOutDeck.DeckCeilingMeters        = 18000.0f;

    switch (Category)
    {
        case CloudCategoryType::Stratus: // Celestial Demo Type 0
            InOutDeck.CloudFloorMeters       = 400.0f;
            InOutDeck.LayerThicknessMeters   = 350.0f;
            InOutDeck.CloudFraction          = 0.72f;
            InOutDeck.ExtinctionMultiplier   = 0.85f;
            InOutDeck.FeatureScaleMultiplier = 1.60f;
            InOutDeck.AnvilTopSpread         = 0.15f;
            InOutDeck.ErosionDetail          = 0.35f;
            break;
        case CloudCategoryType::Stratocumulus: // Celestial Demo Type 1
            InOutDeck.CloudFloorMeters       = 900.0f;
            InOutDeck.LayerThicknessMeters   = 600.0f;
            InOutDeck.CloudFraction          = 0.58f;
            InOutDeck.ExtinctionMultiplier   = 1.00f;
            InOutDeck.FeatureScaleMultiplier = 1.50f;
            InOutDeck.AnvilTopSpread         = 0.25f;
            break;
        case CloudCategoryType::Cumulus: // Celestial Demo Type 2
            InOutDeck.CloudFloorMeters       = 1500.0f;
            InOutDeck.LayerThicknessMeters   = 900.0f;
            InOutDeck.CloudFraction          = 0.45f;
            InOutDeck.ExtinctionMultiplier   = 1.00f;
            InOutDeck.FeatureScaleMultiplier = 1.40f;
            InOutDeck.AnvilTopSpread         = 0.30f;
            break;
        case CloudCategoryType::Cumulonimbus: // Celestial Demo Type 3
            InOutDeck.CloudFloorMeters       = 1200.0f;
            InOutDeck.LayerThicknessMeters   = 7000.0f;
            InOutDeck.CloudFraction          = 0.50f;
            InOutDeck.ExtinctionMultiplier   = 1.25f;
            InOutDeck.FeatureScaleMultiplier = 1.80f;
            InOutDeck.AnvilTopSpread         = 0.85f;
            InOutDeck.AbsorptionRatio        = 0.12f;
            break;
        case CloudCategoryType::Altostratus: // Celestial Demo Type 4
            InOutDeck.CloudFloorMeters       = 4000.0f;
            InOutDeck.LayerThicknessMeters   = 1200.0f;
            InOutDeck.CloudFraction          = 0.60f;
            InOutDeck.ExtinctionMultiplier   = 0.70f;
            InOutDeck.FeatureScaleMultiplier = 1.90f;
            InOutDeck.AnvilTopSpread         = 0.20f;
            InOutDeck.ErosionDetail          = 0.40f;
            break;
        case CloudCategoryType::Cirrus: // Celestial Demo Type 5
            InOutDeck.CloudFloorMeters       = 8500.0f;
            InOutDeck.LayerThicknessMeters   = 900.0f;
            InOutDeck.CloudFraction          = 0.42f;
            InOutDeck.ExtinctionMultiplier   = 0.65f;
            InOutDeck.FeatureScaleMultiplier = 2.20f;
            InOutDeck.AnvilTopSpread         = 0.20f;
            InOutDeck.ErosionDetail          = 0.45f;
            break;
        case CloudCategoryType::Lenticular:
            InOutDeck.CloudFloorMeters       = 3600.0f;
            InOutDeck.LayerThicknessMeters   = 650.0f;
            InOutDeck.CloudFraction          = 0.42f;
            InOutDeck.ExtinctionMultiplier   = 1.15f;
            InOutDeck.FeatureScaleMultiplier = 1.35f;
            InOutDeck.AnvilTopSpread         = 0.25f;
            InOutDeck.ErosionDetail          = 0.25f;
            break;
        case CloudCategoryType::Mammatus:
            InOutDeck.CloudFloorMeters       = 2200.0f;
            InOutDeck.LayerThicknessMeters   = 1600.0f;
            InOutDeck.CloudFraction          = 0.64f;
            InOutDeck.ExtinctionMultiplier   = 1.30f;
            InOutDeck.FeatureScaleMultiplier = 1.30f;
            InOutDeck.AnvilTopSpread         = 0.65f;
            InOutDeck.AbsorptionRatio        = 0.10f;
            break;
        case CloudCategoryType::KelvinHelmholtz:
            InOutDeck.CloudFloorMeters       = 2400.0f;
            InOutDeck.LayerThicknessMeters   = 850.0f;
            InOutDeck.CloudFraction          = 0.46f;
            InOutDeck.ExtinctionMultiplier   = 1.10f;
            InOutDeck.FeatureScaleMultiplier = 1.25f;
            InOutDeck.AnvilTopSpread         = 0.72f;
            break;
        case CloudCategoryType::Asperitas:
            InOutDeck.CloudFloorMeters       = 1100.0f;
            InOutDeck.LayerThicknessMeters   = 1150.0f;
            InOutDeck.CloudFraction          = 0.70f;
            InOutDeck.ExtinctionMultiplier   = 1.35f;
            InOutDeck.FeatureScaleMultiplier = 1.45f;
            InOutDeck.AnvilTopSpread         = 0.45f;
            InOutDeck.AbsorptionRatio        = 0.14f;
            break;
        case CloudCategoryType::Nacreous:
            InOutDeck.CloudFloorMeters       = 11500.0f;
            InOutDeck.LayerThicknessMeters   = 1400.0f;
            InOutDeck.DeckCeilingMeters      = 19000.0f;
            InOutDeck.CloudFraction          = 0.48f;
            InOutDeck.ExtinctionMultiplier   = 0.85f;
            InOutDeck.FeatureScaleMultiplier = 2.00f;
            InOutDeck.AnvilTopSpread         = 0.25f;
            InOutDeck.ErosionDetail          = 0.30f;
            break;
        case CloudCategoryType::Pyrocumulus:
            InOutDeck.CloudFloorMeters       = 800.0f;
            InOutDeck.LayerThicknessMeters   = 5200.0f;
            InOutDeck.CloudFraction          = 0.48f;
            InOutDeck.ExtinctionMultiplier   = 1.45f;
            InOutDeck.FeatureScaleMultiplier = 1.45f;
            InOutDeck.AnvilTopSpread         = 0.80f;
            InOutDeck.AbsorptionRatio        = 0.18f;
            InOutDeck.CloudAlbedo            = { 0.82f, 0.78f, 0.74f };
            break;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      LIVE 3-DIRECTION HEMISPHERICAL SKY AMBIENT
//------------------------------------------------------------------------------------------------------------------------

Mathematics::LinearRGB VolumetricCloudSequence::ComputeHemisphericalSkyAmbient(
    const AtmosphericSkySequence& SkySeq,
    const Mathematics::Axis3D&    SunDirection,
    bool                          AtmosphereEnabled,
    const Mathematics::LinearRGB& FallbackSkyRadiance) noexcept
{
    if (!AtmosphereEnabled)
    {
        return FallbackSkyRadiance;
    }
    const float HLen = std::sqrt(SunDirection.X * SunDirection.X + SunDirection.Y * SunDirection.Y);
    const float Hx   = (HLen > 1.0e-4f) ? (SunDirection.X / HLen) : 1.0f;
    const float Hy   = (HLen > 1.0e-4f) ? (SunDirection.Y / HLen) : 0.0f;

    auto NormDir = [](float X, float Y, float Z) noexcept -> Mathematics::Axis3D
    {
        const float L = std::sqrt(X * X + Y * Y + Z * Z);
        return (L > 1.0e-6f) ? Mathematics::Axis3D{ X / L, Y / L, Z / L } : Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f };
    };

    const Mathematics::LinearRGB Overhead = SkySeq.SampleSkyViewDirection({ 0.0f, 0.0f, 1.0f });
    const Mathematics::LinearRGB SunSide  = SkySeq.SampleSkyViewDirection(NormDir(Hx, Hy, 0.08f));
    const Mathematics::LinearRGB OppSide  = SkySeq.SampleSkyViewDirection(NormDir(-Hx, -Hy, 0.08f));
    return {
        std::max(0.0f, Overhead.R * 0.40f + SunSide.R * 0.35f + OppSide.R * 0.25f),
        std::max(0.0f, Overhead.G * 0.40f + SunSide.G * 0.35f + OppSide.G * 0.25f),
        std::max(0.0f, Overhead.B * 0.40f + SunSide.B * 0.35f + OppSide.B * 0.25f)
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                  CELESTIAL UNIFIED LOCAL MEDIA (marchLocal + uniShadow)
//------------------------------------------------------------------------------------------------------------------------

FogEvaluationResult VolumetricCloudSequence::EvaluateUnifiedLocalMedia(
    const Mathematics::Axis3D&            RayOriginWorld,
    const Mathematics::Axis3D&            RayDirection,
    float                                 MaxDistanceMeters,
    const Mathematics::Axis3D&            SunDirection,
    const Mathematics::LinearRGB&         SolarIrradiance,
    const Mathematics::LinearRGB&         HemisphericalSkyAmbient,
    const Mathematics::LinearRGB&         LowBandSkyGlow,
    const WorldAtmosphereConfiguration&   Atmosphere,
    std::span<const LocalMediaLampSample> Lamps,
    float                                 PixelJitter01) noexcept
{
    FogEvaluationResult Result {};
    const uint32_t FogCount = std::min<uint32_t>(
        VolumetricFogSequence::MaxGpuLocalFogVolumes,
        static_cast<uint32_t>(Atmosphere.Fog.LocalFogs.size()));

    std::array<const BoundedCloudVolume*, 8u> CloudPtrs {};
    uint32_t CloudCount = 0u;
    GatherActiveLocalClouds(Atmosphere, CloudPtrs, CloudCount);

    if ((FogCount == 0u && CloudCount == 0u) || MaxDistanceMeters <= 0.05f)
    {
        return Result;
    }

    const float InvX = (std::abs(RayDirection.X) > 1.0e-6f) ? (1.0f / RayDirection.X) : 1.0e6f;
    const float InvY = (std::abs(RayDirection.Y) > 1.0e-6f) ? (1.0f / RayDirection.Y) : 1.0e6f;
    const float InvZ = (std::abs(RayDirection.Z) > 1.0e-6f) ? (1.0f / RayDirection.Z) : 1.0e6f;

    float    TMin   = 1.0e9f;
    float    TMax   = 0.0f;
    uint32_t Steps  = 24u;
    float    MaxRad = 4.0f;

    auto ExpandBounds = [&](const Mathematics::Axis3D& Center, const Mathematics::Scalar3D& HalfE,
                            AtmosphericVolumeShape Shape, uint32_t VolSteps)
    {
        const float MaxXY = std::max(HalfE.X, HalfE.Y) * 1.45f;
        const float MinZ  = (Shape == AtmosphericVolumeShape::HalfSphere) ? (0.14f * HalfE.Z) : HalfE.Z;
        const float T0x = (Center.X - MaxXY - RayOriginWorld.X) * InvX;
        const float T1x = (Center.X + MaxXY - RayOriginWorld.X) * InvX;
        const float T0y = (Center.Y - MaxXY - RayOriginWorld.Y) * InvY;
        const float T1y = (Center.Y + MaxXY - RayOriginWorld.Y) * InvY;
        const float T0z = (Center.Z - MinZ  - RayOriginWorld.Z) * InvZ;
        const float T1z = (Center.Z + HalfE.Z - RayOriginWorld.Z) * InvZ;
        const float TE  = std::max(0.05f, std::max(std::min(T0x, T1x), std::max(std::min(T0y, T1y), std::min(T0z, T1z))));
        const float TX  = std::min(MaxDistanceMeters, std::min(std::max(T0x, T1x), std::min(std::max(T0y, T1y), std::max(T0z, T1z))));
        if (TX > TE)
        {
            TMin   = std::min(TMin, TE);
            TMax   = std::max(TMax, TX);
            Steps  = std::max(Steps, std::clamp(VolSteps, 16u, 48u));
            MaxRad = std::max(MaxRad, std::max(HalfE.X, std::max(HalfE.Y, HalfE.Z)));
        }
    };

    for (uint32_t I = 0u; I < FogCount; ++I)
    {
        const auto& Vol = Atmosphere.Fog.LocalFogs[I];
        if (!Vol.Enabled || Vol.Density <= 1.0e-4f) continue;
        ExpandBounds(Vol.VolumeCenterMeters, Vol.HalfExtentMeters, Vol.Shape, Vol.MarchSteps);
    }
    for (uint32_t J = 0u; J < CloudCount; ++J)
    {
        const auto& Vol = *CloudPtrs[J];
        if (Vol.ExtinctionMultiplier <= 1.0e-4f || Vol.CloudFraction <= 1.0e-4f) continue;
        ExpandBounds(Vol.VolumeCenterMeters, Vol.HalfExtentMeters, Vol.Shape, Vol.MarchSteps);
    }

    if (TMax <= TMin) return Result;

    const float Jitter    = (PixelJitter01 >= 0.0f) ? std::clamp(PixelJitter01, 0.0f, 1.0f) : 0.5f;
    const float Dt        = (TMax - TMin) / static_cast<float>(Steps);
    float       T         = TMin + Dt * Jitter;
    const float CosS      = RayDirection.X * SunDirection.X + RayDirection.Y * SunDirection.Y + RayDirection.Z * SunDirection.Z;
    const float SunUp     = std::clamp(SunDirection.Z * 4.0f + 0.15f, 0.0f, 1.0f);
    const float SunR      = SolarIrradiance.R * SunUp;
    const float SunG      = SolarIrradiance.G * SunUp;
    const float SunB      = SolarIrradiance.B * SunUp;
    const float BackG     = std::clamp(Atmosphere.Clouds.BackScatterAnisotropy, 0.0f, 0.90f);
    const float PhaseMix  = std::clamp(Atmosphere.Clouds.PhaseLobeBlend, 0.0f, 1.0f);
    const float CloudSigA = std::max(0.0f, Atmosphere.Clouds.AbsorptionRatio);
    const float PowderK   = std::clamp(Atmosphere.Clouds.PowderStrength, 0.0f, 1.0f);
    const float AmbGain   = std::max(0.0f, Atmosphere.Clouds.SkyAmbientGain);
    const auto  ClAlbedo  = Atmosphere.Clouds.CloudAlbedo;
    const float ShStep    = std::clamp(MaxRad * 0.45f, 0.8f, 35.0f);

    float AccR = 0.0f, AccG = 0.0f, AccB = 0.0f;
    float Trans = 1.0f;

    std::array<float, 8u> FogRhos {};
    std::array<float, 8u> FogHns {};
    std::array<float, 8u> CloudRhos {};
    std::array<float, 8u> CloudHns {};

    for (uint32_t S = 0u; S < Steps && Trans >= 0.015f; ++S)
    {
        const Mathematics::Axis3D P {
            RayOriginWorld.X + RayDirection.X * T,
            RayOriginWorld.Y + RayDirection.Y * T,
            RayOriginWorld.Z + RayDirection.Z * T
        };

        bool ShadowRequired = false;
        for (uint32_t I = 0u; I < FogCount; ++I)
        {
            float Hn = 0.5f;
            const float R = EvaluateSingleLocalFogDensityUnifiedCpu(Atmosphere.Fog.LocalFogs[I], P, Atmosphere, Hn);
            FogRhos[I] = R;
            FogHns[I]  = Hn;
            if (R > 1.0e-5f && Atmosphere.Fog.LocalFogs[I].SelfShadowEnabled) ShadowRequired = true;
        }
        for (uint32_t J = 0u; J < CloudCount; ++J)
        {
            float Hn = 0.5f;
            const float R = EvaluateSingleLocalCloudDensityCpu(*CloudPtrs[J], P, Atmosphere, true, Hn);
            CloudRhos[J] = R;
            CloudHns[J]  = Hn;
            if (R > 1.0e-5f) ShadowRequired = true;
        }

        float SharedSh = 1.0f;
        if (ShadowRequired)
        {
            float Od = 0.0f;
            for (uint32_t K = 1u; K <= 4u; ++K)
            {
                const float FK = static_cast<float>(K);
                const float D  = ShStep * FK * FK * 0.14f;
                const Mathematics::Axis3D SP { P.X + SunDirection.X * D, P.Y + SunDirection.Y * D, P.Z + SunDirection.Z * D };
                float RhoSum = 0.0f;
                for (uint32_t I = 0u; I < FogCount; ++I)
                {
                    if (!Atmosphere.Fog.LocalFogs[I].Enabled) continue;
                    float TH = 0.5f;
                    const float R = EvaluateSingleLocalFogDensityUnifiedCpu(Atmosphere.Fog.LocalFogs[I], SP, Atmosphere, TH);
                    RhoSum += R * (1.0f + std::max(0.0f, Atmosphere.Fog.LocalFogs[I].AbsorptionRatio));
                }
                for (uint32_t J = 0u; J < CloudCount; ++J)
                {
                    float TH = 0.5f;
                    const float R = EvaluateSingleLocalCloudDensityCpu(*CloudPtrs[J], SP, Atmosphere, false, TH);
                    RhoSum += R * (1.0f + CloudSigA);
                }
                Od += RhoSum * D * 0.70f;
                if (Od > 10.0f) break;
            }
            SharedSh = std::exp(-Od);
        }

        float FogSigT = 0.0f, CloudSigT = 0.0f;
        float FogScR = 0.0f, FogScG = 0.0f, FogScB = 0.0f;
        float CloudScR = 0.0f, CloudScG = 0.0f, CloudScB = 0.0f;

        for (uint32_t I = 0u; I < FogCount; ++I)
        {
            const float Rho = FogRhos[I];
            if (Rho <= 1.0e-5f) continue;

            const auto& Vol = Atmosphere.Fog.LocalFogs[I];
            const float Sh  = Vol.SelfShadowEnabled ? SharedSh : 1.0f;
            const float Ph  = std::min(CelestialPhaseHGCpu(CosS, Vol.AnisotropyG) * (4.0f * Constants::PiFloat), 6.5f);
            const float Ms  = Sh + 0.35f * std::sqrt(Sh);
            const float Hw  = 0.38f + 0.22f * FogHns[I];
            float LiR = SunR * (Ph * Ms * 0.22f) + HemisphericalSkyAmbient.R * Hw + LowBandSkyGlow.R * 0.12f;
            float LiG = SunG * (Ph * Ms * 0.22f) + HemisphericalSkyAmbient.G * Hw + LowBandSkyGlow.G * 0.12f;
            float LiB = SunB * (Ph * Ms * 0.22f) + HemisphericalSkyAmbient.B * Hw + LowBandSkyGlow.B * 0.12f;

            for (const auto& Lamp : Lamps)
            {
                const float Dx = Lamp.WorldPosition.X - P.X;
                const float Dy = Lamp.WorldPosition.Y - P.Y;
                const float Dz = Lamp.WorldPosition.Z - P.Z;
                const float Dist2 = Dx * Dx + Dy * Dy + Dz * Dz;
                const float Reach = std::max(2.0f, Lamp.ReachMeters);
                if (Dist2 < Reach * Reach && Dist2 > 1.0e-4f)
                {
                    const float Dist   = std::sqrt(Dist2);
                    const float InvLen = 1.0f / Dist;
                    const float Lx = Dx * InvLen, Ly = Dy * InvLen, Lz = Dz * InvLen;
                    const float RangeF = std::clamp(1.0f - Dist / Reach, 0.0f, 1.0f);
                    const float Atten  = (RangeF * RangeF) / std::max(0.35f, Dist2);
                    const float LPh    = CelestialPhaseHGCpu(RayDirection.X * Lx + RayDirection.Y * Ly + RayDirection.Z * Lz, Vol.AnisotropyG) * (4.0f * Constants::PiFloat);
                    const float Gain   = Atten * LPh * 0.08f;
                    LiR += Lamp.RadianceRgb.R * Gain;
                    LiG += Lamp.RadianceRgb.G * Gain;
                    LiB += Lamp.RadianceRgb.B * Gain;
                }
            }

            const float SigA = std::max(0.0f, Vol.AbsorptionRatio);
            FogSigT += Rho * (1.0f + SigA);
            FogScR  += LiR * Vol.FogTint.R * Rho;
            FogScG  += LiG * Vol.FogTint.G * Rho;
            FogScB  += LiB * Vol.FogTint.B * Rho;
        }

        for (uint32_t J = 0u; J < CloudCount; ++J)
        {
            const float Rho = CloudRhos[J];
            if (Rho <= 1.0e-5f) continue;

            const auto& Vol  = *CloudPtrs[J];
            const float G1   = std::clamp(Vol.ForwardScatterAnisotropy, 0.0f, 0.95f);
            const float Ph1  = CelestialPhaseHGCpu(CosS, G1);
            const float Ph2  = CelestialPhaseHGCpu(CosS, -BackG);
            const float Ph   = std::min((Ph1 + (Ph2 - Ph1) * PhaseMix) * (4.0f * Constants::PiFloat), 10.0f);
            const float Ms   = SharedSh + 0.45f * std::sqrt(SharedSh) + 0.22f * std::pow(std::max(1.0e-6f, SharedSh), 0.25f);
            const float Powd = 1.0f - PowderK * std::exp(-Rho * Dt * 2.5f * (1.0f + CloudSigA)) * (1.0f - 0.5f * std::clamp(CosS, 0.0f, 1.0f));
            const float AmbW = AmbGain * (0.28f + 0.40f * CloudHns[J]);
            const float LiR  = SunR * (Ph * Ms * Powd * 0.55f) + HemisphericalSkyAmbient.R * AmbW;
            const float LiG  = SunG * (Ph * Ms * Powd * 0.55f) + HemisphericalSkyAmbient.G * AmbW;
            const float LiB  = SunB * (Ph * Ms * Powd * 0.55f) + HemisphericalSkyAmbient.B * AmbW;

            CloudSigT += Rho * (1.0f + CloudSigA);
            CloudScR  += LiR * ClAlbedo.R * Rho;
            CloudScG  += LiG * ClAlbedo.G * Rho;
            CloudScB  += LiB * ClAlbedo.B * Rho;
        }

        const float TotalSigT = FogSigT + CloudSigT;
        if (TotalSigT > 1.0e-5f)
        {
            const float Ts = std::exp(-TotalSigT * Dt);
            const float W  = Trans * (1.0f - Ts) / std::max(TotalSigT, 1.0e-6f);
            AccR  += (FogScR + CloudScR) * W;
            AccG  += (FogScG + CloudScG) * W;
            AccB  += (FogScB + CloudScB) * W;
            Trans *= Ts;
        }
        T += Dt;
    }

    Result.InScatterHdr     = { std::max(0.0f, AccR), std::max(0.0f, AccG), std::max(0.0f, AccB) };
    Result.Transmittance    = std::clamp(Trans, 0.0f, 1.0f);
    Result.TransmittanceRgb = { Result.Transmittance, Result.Transmittance, Result.Transmittance };
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  PUBLIC CLOUD EVALUATORS (BACKWARD COMPATIBLE)
//------------------------------------------------------------------------------------------------------------------------

Mathematics::LinearRGB VolumetricCloudSequence::EvaluateLocalCloudsOverSegment(
    const Mathematics::Axis3D&          RayOriginWorld,
    const Mathematics::Axis3D&          RayDirection,
    float                               MaxDistanceMeters,
    const Mathematics::Axis3D&          SunDirection,
    const Mathematics::LinearRGB&       SolarIrradiance,
    const Mathematics::LinearRGB&       SkyRadiance,
    const WorldAtmosphereConfiguration& Atmosphere,
    float&                              InOutTransmittance,
    float                               PixelJitter01) noexcept
{
    WorldAtmosphereConfiguration CloudOnly = Atmosphere;
    CloudOnly.Fog.LocalFogs.clear();
    const Mathematics::LinearRGB CloudSun {
        SolarIrradiance.R * 0.44f,
        SolarIrradiance.G * 0.44f,
        SolarIrradiance.B * 0.44f
    };
    const FogEvaluationResult Res = EvaluateUnifiedLocalMedia(
        RayOriginWorld, RayDirection, MaxDistanceMeters, SunDirection,
        CloudSun, SkyRadiance, SkyRadiance, CloudOnly, {}, PixelJitter01);
    const float PriorT = InOutTransmittance;
    InOutTransmittance = PriorT * Res.Transmittance;
    return {
        Res.InScatterHdr.R * PriorT,
        Res.InScatterHdr.G * PriorT,
        Res.InScatterHdr.B * PriorT
    };
}

Mathematics::LinearRGB VolumetricCloudSequence::EvaluateVolumetricClouds(
    const Mathematics::Axis3D&            RayOriginWorld,
    const Mathematics::Axis3D&            RayDirection,
    const Mathematics::Axis3D&            SunDirection,
    const Mathematics::LinearRGB&         SolarIrradiance,
    const Mathematics::LinearRGB&         SkyRadiance,
    const CloudDeckConfiguration&         Deck,
    const BoundedCloudVolume&             LocalVolume,
    const AtmosphericMediumConfiguration& Medium,
    float                                 SimulationSeconds,
    float&                                OutTransmittance,
    float                                 PixelJitter01) noexcept
{
    WorldAtmosphereConfiguration TempAtmo {};
    TempAtmo.Medium            = Medium;
    TempAtmo.Clouds            = Deck;
    TempAtmo.LocalCloud        = LocalVolume;
    TempAtmo.SimulationSeconds = SimulationSeconds;
    return EvaluateVolumetricCloudsWithWind(
        RayOriginWorld, RayDirection, SunDirection, SolarIrradiance, SkyRadiance,
        TempAtmo, OutTransmittance, PixelJitter01);
}

Mathematics::LinearRGB VolumetricCloudSequence::EvaluateVolumetricCloudsWithWind(
    const Mathematics::Axis3D&          RayOriginWorld,
    const Mathematics::Axis3D&          RayDirection,
    const Mathematics::Axis3D&          SunDirection,
    const Mathematics::LinearRGB&       SolarIrradiance,
    const Mathematics::LinearRGB&       SkyRadiance,
    const WorldAtmosphereConfiguration& Atmosphere,
    float&                              OutTransmittance,
    float                               PixelJitter01) noexcept
{
    OutTransmittance = 1.0f;
    const Mathematics::LinearRGB CloudSun {
        SolarIrradiance.R * 0.44f,
        SolarIrradiance.G * 0.44f,
        SolarIrradiance.B * 0.44f
    };

    Mathematics::LinearRGB TotalRad { 0.0f, 0.0f, 0.0f };
    if (Atmosphere.Clouds.CloudsEnabled &&
        Atmosphere.Clouds.CloudFraction > 0.001f &&
        Atmosphere.Clouds.ExtinctionMultiplier > 0.001f)
    {
        float DeckTrans = 1.0f;
        TotalRad = EvaluateDeckViaPlanetarySlang(
            RayOriginWorld, RayDirection, SunDirection, CloudSun, SkyRadiance,
            Atmosphere.Clouds, Atmosphere.Medium, &Atmosphere.Wind,
            Atmosphere.SimulationSeconds, DeckTrans, PixelJitter01);
        OutTransmittance = DeckTrans;
    }

    // If callers invoke EvaluateVolumetricCloudsWithWind standalone with active local clouds
    if (Atmosphere.LocalCloud.LocalEnabled || !Atmosphere.LocalClouds.empty())
    {
        WorldAtmosphereConfiguration CloudOnly = Atmosphere;
        CloudOnly.Fog.LocalFogs.clear();
        const FogEvaluationResult LocalRes = EvaluateUnifiedLocalMedia(
            RayOriginWorld, RayDirection, 60000.0f, SunDirection,
            CloudSun, SkyRadiance, SkyRadiance, CloudOnly, {}, PixelJitter01);
        TotalRad.R = TotalRad.R * LocalRes.Transmittance + LocalRes.InScatterHdr.R;
        TotalRad.G = TotalRad.G * LocalRes.Transmittance + LocalRes.InScatterHdr.G;
        TotalRad.B = TotalRad.B * LocalRes.Transmittance + LocalRes.InScatterHdr.B;
        OutTransmittance *= LocalRes.Transmittance;
    }

    return TotalRad;
}

float VolumetricCloudSequence::EvaluateSolarCloudTransmittance(
    const Mathematics::Axis3D&    WorldPos,
    const Mathematics::Axis3D&    SunDirection,
    const CloudDeckConfiguration& Deck,
    float                         PlanetRadiusMeters,
    float                         SimulationSeconds) noexcept
{
    if (!Deck.CloudsEnabled || !Deck.CloudShadow.ShadowEnabled || SunDirection.Z <= 0.01f)
    {
        return 1.0f;
    }
    AtmosphericMediumConfiguration Med {};
    Med.PlanetRadiusMeters = std::max(500000.0f, PlanetRadiusMeters);
    float WindVX = 0.0f, WindVY = 0.0f;
    ResolveDeckWindVelocity(Deck, nullptr, SimulationSeconds, WindVX, WindVY);

    CloudSpecification_0 Spec = BuildCpuCloudSpec(
        Deck, Med, SunDirection, { 0.5f, 0.5f, 0.5f }, { 0.25f, 0.36f, 0.56f }, WindVX, WindVY, SimulationSeconds);
    const Vector<float, 3> Pos { WorldPos.X, WorldPos.Y, Spec.RadiusM_0 + std::max(0.0f, Med.ObserverAltitudeMeters) + WorldPos.Z };
    const float Tau      = CloudSolarOpticalDepth_0(&Spec, Pos, 4u, 20.0f);
    const float Strength = std::clamp(Deck.CloudShadow.ShadowDensityMultiplier, 0.0f, 1.0f);
    return std::clamp(1.0f + (std::exp(-Tau) - 1.0f) * Strength, 0.08f, 1.0f);
}

float VolumetricCloudSequence::EvaluateAllSolarCloudTransmittance(
    const Mathematics::Axis3D&          WorldPos,
    const Mathematics::Axis3D&          SunDirection,
    const WorldAtmosphereConfiguration& Atmosphere) noexcept
{
    if (SunDirection.Z <= 0.01f) return 1.0f;
    float Trans = 1.0f;

    const auto& Deck = Atmosphere.Clouds;
    if (Deck.CloudsEnabled && Deck.CloudShadow.ShadowEnabled)
    {
        float WindVX = 0.0f, WindVY = 0.0f;
        ResolveDeckWindVelocity(Deck, &Atmosphere.Wind, Atmosphere.SimulationSeconds, WindVX, WindVY);
        CloudSpecification_0 Spec = BuildCpuCloudSpec(
            Deck, Atmosphere.Medium, SunDirection, { 0.5f, 0.5f, 0.5f }, { 0.25f, 0.36f, 0.56f },
            WindVX, WindVY, Atmosphere.SimulationSeconds);
        const float ObsAltM = std::max(0.0f, Atmosphere.Medium.ObserverAltitudeMeters);
        const Vector<float, 3> Pos { WorldPos.X, WorldPos.Y, Spec.RadiusM_0 + ObsAltM + WorldPos.Z };
        const float Tau      = CloudSolarOpticalDepth_0(&Spec, Pos, 4u, 20.0f);
        const float Strength = std::clamp(Deck.CloudShadow.ShadowDensityMultiplier, 0.0f, 1.0f);
        Trans *= (1.0f + (std::exp(-Tau) - 1.0f) * Strength);
    }

    std::array<const BoundedCloudVolume*, 8u> CloudPtrs {};
    uint32_t CloudCount = 0u;
    GatherActiveLocalClouds(Atmosphere, CloudPtrs, CloudCount);
    if (CloudCount > 0u)
    {
        const float InvX = (std::abs(SunDirection.X) > 1.0e-6f) ? (1.0f / SunDirection.X) : 1.0e6f;
        const float InvY = (std::abs(SunDirection.Y) > 1.0e-6f) ? (1.0f / SunDirection.Y) : 1.0e6f;
        const float InvZ = (std::abs(SunDirection.Z) > 1.0e-6f) ? (1.0f / SunDirection.Z) : 1.0e6f;
        const float SigA = std::max(0.0f, Deck.AbsorptionRatio);

        for (uint32_t J = 0u; J < CloudCount; ++J)
        {
            const auto& Vol = *CloudPtrs[J];
            const float MaxXY = std::max(Vol.HalfExtentMeters.X, Vol.HalfExtentMeters.Y) * 1.45f;
            const float T0x = (Vol.VolumeCenterMeters.X - MaxXY - WorldPos.X) * InvX;
            const float T1x = (Vol.VolumeCenterMeters.X + MaxXY - WorldPos.X) * InvX;
            const float T0y = (Vol.VolumeCenterMeters.Y - MaxXY - WorldPos.Y) * InvY;
            const float T1y = (Vol.VolumeCenterMeters.Y + MaxXY - WorldPos.Y) * InvY;
            const float T0z = (Vol.VolumeCenterMeters.Z - Vol.HalfExtentMeters.Z - WorldPos.Z) * InvZ;
            const float T1z = (Vol.VolumeCenterMeters.Z + Vol.HalfExtentMeters.Z - WorldPos.Z) * InvZ;
            const float TEn = std::max(0.0f, std::max(std::min(T0x, T1x), std::max(std::min(T0y, T1y), std::min(T0z, T1z))));
            const float TEx = std::min(std::max(T0x, T1x), std::min(std::max(T0y, T1y), std::max(T0z, T1z)));
            if (TEx > TEn)
            {
                const float Dt = (TEx - TEn) * 0.25f;
                float Od = 0.0f;
                for (uint32_t K = 0u; K < 4u; ++K)
                {
                    const float Dist = TEn + (static_cast<float>(K) + 0.5f) * Dt;
                    const Mathematics::Axis3D SP {
                        WorldPos.X + SunDirection.X * Dist,
                        WorldPos.Y + SunDirection.Y * Dist,
                        WorldPos.Z + SunDirection.Z * Dist
                    };
                    float Hn = 0.5f;
                    Od += EvaluateSingleLocalCloudDensityCpu(Vol, SP, Atmosphere, false, Hn) * (1.0f + SigA) * Dt;
                }
                Trans *= std::exp(-Od);
            }
        }
    }

    return std::clamp(Trans, 0.26f, 1.0f);
}

VolumetricCloudSequence::OrbitalCloudEvaluation VolumetricCloudSequence::EvaluateOrbitalPlanetClouds(
    const Mathematics::Axis3D&            CameraPlanetOriginMeters,
    const Mathematics::Axis3D&            RayDirection,
    const Mathematics::Axis3D&            SunDirection,
    const Mathematics::LinearRGB&         EffectiveSolarTint,
    float                                 SolarIntensityMultiplier,
    const AtmosphericMediumConfiguration& Medium,
    const CloudDeckConfiguration&         Deck,
    float                                 SimulationSeconds,
    float                                 PixelJitter01) noexcept
{
    OrbitalCloudEvaluation Out {};

    const float CloudSolarScale = std::max(0.0f, SolarIntensityMultiplier * Medium.SkyBrightnessMultiplier * 0.030f);
    const Mathematics::LinearRGB SunIrrad {
        EffectiveSolarTint.R * CloudSolarScale,
        EffectiveSolarTint.G * CloudSolarScale,
        EffectiveSolarTint.B * CloudSolarScale,
    };
    const Mathematics::LinearRGB SkyAmbient {
        0.24f * Medium.SkyTint.R,
        0.36f * Medium.SkyTint.G,
        0.56f * Medium.SkyTint.B,
    };

    float WindVX = 0.0f, WindVY = 0.0f;
    ResolveDeckWindVelocity(Deck, nullptr, SimulationSeconds, WindVX, WindVY);

    const uint32_t DeckFormation = static_cast<uint32_t>(Deck.Category);
    CloudSpecification_0 OrbSpec = BuildCpuCloudSpec(
        Deck, Medium, SunDirection, SunIrrad, SkyAmbient, WindVX, WindVY,
        SimulationSeconds, (DeckFormation == 2u) ? 12u : DeckFormation);
    OrbSpec.Steps_0      = std::clamp(OrbSpec.Steps_0, 24u, 48u);
    OrbSpec.LightSteps_0 = 4u;

    const Vector<float, 3> Origin  = ToSlangVec3(CameraPlanetOriginMeters);
    const Vector<float, 3> Dir     = normalize_0(ToSlangVec3(RayDirection));
    const float            GroundM = PlanetCloudGroundDistance_0(Origin, Dir, OrbSpec.RadiusM_0);
    Out.GroundDistanceM = GroundM;

    CloudTransportIntegral_0 Cloud = TracePlanetaryClouds_0(&OrbSpec, Origin, Dir, PixelFromJitter(PixelJitter01, RayDirection), GroundM);
    const CloudAirSegment_0  Air   = IntegrateCloudAir_0(&OrbSpec, Origin, Dir, GroundM, 24u, false);
    Out.AirRadianceHdr   = { Air.Radiance_1.x, Air.Radiance_1.y, Air.Radiance_1.z };
    Out.AirTransmittance = { Air.Transmittance_1.x, Air.Transmittance_1.y, Air.Transmittance_1.z };
    Out.Transmittance    = std::clamp(Cloud.Transmittance_0, 0.0f, 1.0f);

    if (Out.Transmittance < 0.999999f)
    {
        const Vector<float, 3> Cr = CloudRadianceThroughAir_0(&OrbSpec, Origin, Dir, &Cloud, 16u);
        Out.RadianceHdr = { Cr.x, Cr.y, Cr.z };
    }

    if (GroundM < 1.0e8f && Deck.CloudsEnabled)
    {
        const Vector<float, 3> HitP = Origin + Dir * Vector<float, 3>(GroundM);
        const float FootprintM = std::max(1.0f, GroundM * OrbSpec.PixelAngle_0);
        const float Tau        = CloudSolarOpticalDepth_0(&OrbSpec, HitP, 6u, FootprintM);
        Out.SurfaceCloudShadow = std::exp(-Tau);
    }

    return Out;
}

} // namespace Frontier::GeometricRaster
