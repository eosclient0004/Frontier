/*==========================================================================================================================================
                                                       VOLUMETRICFOGSEQUENCE.CPP
==========================================================================================================================================*/
// 🧩 Celestial Analytic Media (Exp² Height Fog + Spectral Rayleigh/Mie Aerial Perspective) & Multi-Shape Local Fog Volumes (CPU Parity)

#include "Engine/GeometricRaster/Atmosphere/VolumetricFogSequence.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/Atmosphere/AtmosphericWindSolver.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::GeometricRaster
{
namespace
{

[[nodiscard]] inline float FractScalar(float X) noexcept
{
    return X - std::floor(X);
}

[[nodiscard]] inline float CelestialHash13Cpu(float Px, float Py, float Pz) noexcept
{
    float X = FractScalar(Px * 0.1031f);
    float Y = FractScalar(Py * 0.1031f);
    float Z = FractScalar(Pz * 0.1031f);
    const float D = X * (Z + 31.32f) + Y * (Y + 31.32f) + Z * (X + 31.32f);
    X += D;
    Y += D;
    Z += D;
    return FractScalar((X + Y) * Z);
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

[[nodiscard]] inline float SmoothstepScalar(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-6f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] inline float CelestialPhaseHGCpu(float CosTheta, float AnisotropyG) noexcept
{
    const float G   = std::clamp(AnisotropyG, -0.95f, 0.95f);
    const float G2  = G * G;
    const float Den = std::max(1.0e-4f, 1.0f + G2 - 2.0f * G * CosTheta);
    return (1.0f - G2) / (4.0f * Constants::PiFloat * std::pow(Den, 1.5f));
}

[[nodiscard]] float EvaluateLocalFogShapeMaskCpu(
    const Mathematics::Axis3D&   PointWorld,
    const LocalFogVolume&        Vol,
    float&                       OutHeightNorm) noexcept
{
    const float Hx = std::max(0.1f, Vol.HalfExtentMeters.X);
    const float Hy = std::max(0.1f, Vol.HalfExtentMeters.Y);
    const float Hz = std::max(0.1f, Vol.HalfExtentMeters.Z);
    const float Rx = PointWorld.X - Vol.VolumeCenterMeters.X;
    const float Ry = PointWorld.Y - Vol.VolumeCenterMeters.Y;
    const float Rz = PointWorld.Z - Vol.VolumeCenterMeters.Z;

    const float YawRad = Vol.YawDegrees * Constants::DegToRadFloat;
    const float C      = std::cos(-YawRad);
    const float S      = std::sin(-YawRad);
    const float Lx     = C * Rx - S * Ry;
    const float Ly     = S * Rx + C * Ry;
    const float Lz     = Rz;

    const float Soft = std::clamp(Vol.EdgeSoftness, 0.01f, 1.0f);
    const float Ux   = Lx / Hx;
    const float Uy   = Ly / Hy;
    const float Uz   = Lz / Hz;
    OutHeightNorm    = std::clamp(0.5f * (Uz + 1.0f), 0.0f, 1.0f);

    const uint32_t Shape = static_cast<uint32_t>(Vol.Shape);
    float DistNorm = 1.0e6f;
    if (Shape == 0u) // Box
    {
        DistNorm = std::max(std::abs(Ux), std::max(std::abs(Uy), std::abs(Uz)));
    }
    else if (Shape == 1u) // Sphere
    {
        const float MinExt = std::min(Hx, std::min(Hy, Hz));
        DistNorm = std::sqrt(Lx * Lx + Ly * Ly + Lz * Lz) / std::max(0.1f, MinExt);
    }
    else if (Shape == 2u) // Ellipsoid
    {
        DistNorm = std::sqrt(Ux * Ux + Uy * Uy + Uz * Uz);
    }
    else if (Shape == 3u) // Cylinder
    {
        DistNorm = std::max(std::sqrt(Ux * Ux + Uy * Uy), std::abs(Uz));
    }
    else if (Shape == 4u) // Cone
    {
        const float Taper = std::clamp(0.5f * (1.0f - Uz), 0.0f, 1.0f);
        const float Rad   = std::sqrt(Ux * Ux + Uy * Uy);
        DistNorm = std::max(std::abs(Uz), (Taper > 1.0e-3f) ? (Rad / Taper) : (Rad > 1.0e-4f ? 2.0f : 1.0f));
    }
    else if (Shape == 5u) // Diamond
    {
        DistNorm = std::abs(Ux) + std::abs(Uy) + std::abs(Uz);
    }
    else if (Shape == 6u) // N-sided Prism
    {
        const uint32_t Sides = std::clamp(Vol.PrismSides, 3u, 16u);
        const float Sector   = (2.0f * Constants::PiFloat) / static_cast<float>(Sides);
        const float Angle    = std::atan2(Uy, Ux);
        const float ModA     = std::fmod(Angle + 4.0f * Constants::PiFloat + 0.5f * Sector, Sector) - 0.5f * Sector;
        const float PolyR    = (std::sqrt(Ux * Ux + Uy * Uy) * std::cos(ModA)) / std::max(0.25f, std::cos(0.5f * Sector));
        DistNorm = std::max(PolyR, std::abs(Uz));
    }
    else // HalfSphere
    {
        if (Uz < -0.14f) return 0.0f;
        const float BottomF = SmoothstepScalar(-0.14f, 0.06f, Uz);
        const float Pz      = std::max(0.0f, Uz);
        const float RadialD = std::sqrt(Ux * Ux + Uy * Uy + Pz * Pz);
        OutHeightNorm       = std::clamp(Pz, 0.0f, 1.0f);
        const float Margin  = std::clamp(1.0f - RadialD, 0.0f, 1.0f);
        if (Margin * BottomF <= 0.0f) return 0.0f;
        if (Vol.DensityFalloffEnabled)
        {
            const float ExpP = 0.35f + (3.0f - 0.35f) * Soft;
            return std::pow(SmoothstepScalar(0.0f, 1.0f, Margin), ExpP) * BottomF;
        }
        return SmoothstepScalar(0.0f, std::max(Soft, 0.01f), Margin) * BottomF;
    }

    const float Margin = std::clamp(1.0f - DistNorm, 0.0f, 1.0f);
    if (Margin <= 0.0f) return 0.0f;

    if (Vol.DensityFalloffEnabled)
    {
        const float ExpP = 0.35f + (3.0f - 0.35f) * Soft;
        return std::pow(SmoothstepScalar(0.0f, 1.0f, Margin), ExpP);
    }
    return SmoothstepScalar(0.0f, std::max(Soft, 0.01f), Margin);
}

[[nodiscard]] float EvaluateSingleLocalFogVolumeCpu(
    const LocalFogVolume&            Vol,
    const Mathematics::Axis3D&       PointWorld,
    const AtmosphericWindParameters& GlobalWind,
    float                            DeckWindSpeedMps,
    float                            DeckWindBearingDeg,
    float                            SimulationSeconds,
    float&                           OutHeightNorm) noexcept
{
    OutHeightNorm = 0.5f;
    if (!Vol.Enabled || Vol.Density <= 1.0e-4f) return 0.0f;

    const float Mask = EvaluateLocalFogShapeMaskCpu(PointWorld, Vol, OutHeightNorm);
    if (Mask <= 1.0e-4f) return 0.0f;

    float AdvX = PointWorld.X;
    float AdvY = PointWorld.Y;
    float AdvZ = PointWorld.Z;

    if (Vol.FollowWind)
    {
        const ResolvedWindSample Wind = AtmosphericWindSolver::ResolveWindSample(
            GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, Vol.WindSource,
            Vol.VolumeCenterMeters, Vol.VolumeCenterMeters.Z, SimulationSeconds);
        const float Wx = Wind.VelocityMetersPerSec.X;
        const float Wy = Wind.VelocityMetersPerSec.Y;
        AdvX -= Wx * SimulationSeconds * 0.85f;
        AdvY -= Wy * SimulationSeconds * 0.85f;
        const float WLen = std::sqrt(Wx * Wx + Wy * Wy);
        if (WLen > 0.5f)
        {
            const float Cx = AdvX * 0.08f + SimulationSeconds * 0.06f;
            const float Cy = AdvY * 0.08f - SimulationSeconds * 0.05f;
            const float Cz = AdvZ * 0.08f;
            const float Sx = CelestialScalarNoiseCpu(Cx, Cy + 0.4f, Cz) - CelestialScalarNoiseCpu(Cx, Cy - 0.4f, Cz);
            const float Sy = CelestialScalarNoiseCpu(Cx + 0.4f, Cy, Cz) - CelestialScalarNoiseCpu(Cx - 0.4f, Cy, Cz);
            const float Sw = std::min(WLen * 0.08f, 2.2f);
            AdvX += Sx * Sw;
            AdvY -= Sy * Sw;
        }
    }

    const float ScaleM = std::max(0.25f, Vol.FeatureScaleMeters);
    const float Px     = AdvX / ScaleM + SimulationSeconds * 0.04f;
    const float Py     = AdvY / ScaleM + SimulationSeconds * 0.02f;
    const float Pz     = AdvZ / ScaleM + SimulationSeconds * 0.015f;
    const float N      = CelestialScalarNoiseCpu(Px, Py, Pz) * 0.55f
                       + CelestialScalarNoiseCpu(Px * 2.1f + 2.3f, Py * 2.1f + 4.7f, Pz * 2.1f + 1.1f) * 0.30f
                       + CelestialScalarNoiseCpu(Px * 4.4f + 6.1f, Py * 4.4f + 0.9f, Pz * 4.4f + 3.8f) * 0.15f;

    const float Hetero = std::clamp(Vol.Coverage, 0.0f, 1.0f);
    const float Mod    = (1.0f - Hetero) + Hetero * (SmoothstepScalar(0.18f, 0.86f, N) * 1.45f);
    const float Grav   = 1.0f - 0.35f * OutHeightNorm;
    return Vol.Density * 0.16f * Mask * Mod * Grav;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                  EXACT HEIGHT INTEGRAL & ELEVATION-AWARE SKY RANGE
//------------------------------------------------------------------------------------------------------------------------

float VolumetricFogSequence::EvaluateExponentialHeightIntegral(
    float ScaleHeightMeters,
    float OriginZ,
    float EndZ,
    float BaseAltitudeMeters) noexcept
{
    const float H  = std::max(1.0f, ScaleHeightMeters);
    const float DZ = EndZ - OriginZ;
    const float A0 = (OriginZ - BaseAltitudeMeters) / H;
    if (std::abs(DZ) < 0.01f)
    {
        return std::exp(-std::clamp(A0, -10.0f, 40.0f));
    }
    const float A1 = (EndZ - BaseAltitudeMeters) / H;
    return std::max(0.0f, (std::exp(-std::clamp(A0, -10.0f, 40.0f)) - std::exp(-std::clamp(A1, -10.0f, 40.0f))) / (DZ / H));
}

float VolumetricFogSequence::ComputeSkyFogPathMeters(
    float                         OriginZ,
    float                         RayDirZ,
    const HeightFogConfiguration& HeightFog,
    const AerialFogConfiguration& AerialFog) noexcept
{
    const float HeightRef = HeightFog.Enabled ? (std::max(1.0f, HeightFog.FalloffHeightMeters) * 4.0f) : 0.0f;
    const float AerialRef = AerialFog.Enabled ? (std::max(10.0f, AerialFog.ScaleHeightMeters) * 5.0f)  : 0.0f;
    float       TopRef    = std::max(HeightRef, AerialRef);
    if (TopRef <= 1.0f) TopRef = 6000.0f;
    if (RayDirZ > 0.002f)
    {
        return std::min(25000.0f, TopRef / RayDirZ);
    }
    if (RayDirZ < -0.002f)
    {
        const float FloorAlt = HeightFog.Enabled ? HeightFog.BaseAltitudeMeters : 0.0f;
        const float AboveM  = std::max(2.0f, OriginZ - FloorAlt);
        return std::min(25000.0f, AboveM / (-RayDirZ));
    }
    return 25000.0f;
}

float VolumetricFogSequence::EvaluateHeightFogOpticalDepth(
    float                         OriginZ,
    float                         DirZ,
    float                         SegmentMeters,
    const HeightFogConfiguration& HeightConfiguration) noexcept
{
    if (!HeightConfiguration.Enabled || HeightConfiguration.Density <= 1.0e-6f || SegmentMeters <= 1.0e-4f)
    {
        return 0.0f;
    }
    const float K = EvaluateExponentialHeightIntegral(
        HeightConfiguration.FalloffHeightMeters, OriginZ, OriginZ + DirZ * SegmentMeters, HeightConfiguration.BaseAltitudeMeters);
    return std::max(0.0f, std::clamp(HeightConfiguration.Density, 0.0f, 0.5f) * SegmentMeters * K);
}

float VolumetricFogSequence::ComputeTwoPercentVisibilityRangeMeters(
    float                         AltitudeMeters,
    const HeightFogConfiguration& HeightFog) noexcept
{
    if (!HeightFog.Enabled || HeightFog.Density <= 1.0e-6f)
    {
        return 100000.0f;
    }
    const float SafeH = std::max(1.0f, HeightFog.FalloffHeightMeters);
    const float Sigma = std::clamp(HeightFog.Density, 0.0f, 0.5f) *
                        std::exp(-std::clamp((AltitudeMeters - HeightFog.BaseAltitudeMeters) / SafeH, -10.0f, 40.0f));
    if (Sigma <= 1.0e-7f) return 100000.0f;
    return 3.912023005f / Sigma; // -ln(0.02) / sigma
}

//------------------------------------------------------------------------------------------------------------------------
//                          CELESTIAL ANALYTIC MEDIA (EXP² HEIGHT FOG + SPECTRAL AERIAL PERSPECTIVE)
//------------------------------------------------------------------------------------------------------------------------

FogEvaluationResult VolumetricFogSequence::EvaluateAnalyticAtmosphericMedia(
    const Mathematics::Axis3D&         RayOriginWorld,
    const Mathematics::Axis3D&         RayDirection,
    float                              DistanceMeters,
    const Mathematics::Axis3D&         SunDirection,
    const Mathematics::LinearRGB&      SolarIrradiance,
    const Mathematics::LinearRGB&      HemisphericalSkyAmbient,
    const Mathematics::LinearRGB&      LowBandSkyGlow,
    const AtmosphericFogConfiguration& FogConfiguration,
    const AtmosphericWindParameters&   GlobalWind,
    float                              DeckWindSpeedMps,
    float                              DeckWindBearingDeg,
    float                              SimulationSeconds) noexcept
{
    FogEvaluationResult Result {};
    const float SafeDist = std::max(0.0f, DistanceMeters);
    if (SafeDist <= 1.0e-3f) return Result;

    const float CosS  = RayDirection.X * SunDirection.X + RayDirection.Y * SunDirection.Y + RayDirection.Z * SunDirection.Z;
    const float SunUp = std::clamp(SunDirection.Z * 4.0f + 0.15f, 0.0f, 1.0f);
    const float SunR  = SolarIrradiance.R * SunUp;
    const float SunG  = SolarIrradiance.G * SunUp;
    const float SunB  = SolarIrradiance.B * SunUp;

    // 1. Exponential-squared Height Fog (matches ApplyCelestialAnalyticMediaGpu verbatim)
    float Tf = 1.0f;
    float LfR = 0.0f, LfG = 0.0f, LfB = 0.0f;
    if (FogConfiguration.HeightFog.Enabled && FogConfiguration.HeightFog.Density > 1.0e-6f)
    {
        float Od = EvaluateHeightFogOpticalDepth(RayOriginWorld.Z, RayDirection.Z, SafeDist, FogConfiguration.HeightFog);
        if (FogConfiguration.HeightFog.FollowWind)
        {
            const ResolvedWindSample Wind = AtmosphericWindSolver::ResolveWindSample(
                GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, FogConfiguration.HeightFog.WindSource,
                RayOriginWorld, RayOriginWorld.Z, SimulationSeconds);
            const float SampleDistM = std::min(SafeDist * 0.35f, 80.0f);
            const float Px = RayOriginWorld.X + RayDirection.X * SampleDistM;
            const float Py = RayOriginWorld.Y + RayDirection.Y * SampleDistM;
            const float Drift = 0.88f + 0.24f * CelestialScalarNoiseCpu(
                (Px - Wind.VelocityMetersPerSec.X * SimulationSeconds) * 0.03f,
                (Py - Wind.VelocityMetersPerSec.Y * SimulationSeconds) * 0.03f,
                SimulationSeconds * 0.15f);
            Od *= Drift;
        }

        Tf = std::exp(-Od * Od);
        const float FogPh  = std::min(CelestialPhaseHGCpu(CosS, 0.55f) * (4.0f * Constants::PiFloat), 6.0f);
        const float SunMul = std::max(0.0f, FogConfiguration.HeightFog.SunScatterIntensity);
        LfR = FogConfiguration.HeightFog.HeightColour.R * (HemisphericalSkyAmbient.R * 0.48f + LowBandSkyGlow.R * 0.20f + SunR * FogPh * 0.22f * SunMul);
        LfG = FogConfiguration.HeightFog.HeightColour.G * (HemisphericalSkyAmbient.G * 0.48f + LowBandSkyGlow.G * 0.20f + SunG * FogPh * 0.22f * SunMul);
        LfB = FogConfiguration.HeightFog.HeightColour.B * (HemisphericalSkyAmbient.B * 0.48f + LowBandSkyGlow.B * 0.20f + SunB * FogPh * 0.22f * SunMul);
    }

    // 2. True Spectral RGB Rayleigh/Mie Aerial Perspective (matches ApplyCelestialAnalyticMediaGpu verbatim)
    float TaR = 1.0f, TaG = 1.0f, TaB = 1.0f;
    float LaR = 0.0f, LaG = 0.0f, LaB = 0.0f;
    if (FogConfiguration.AerialFog.Enabled && FogConfiguration.AerialFog.DensityMultiplier > 1.0e-5f)
    {
        const float StartM = std::max(0.0f, FogConfiguration.AerialFog.StartDistanceMeters);
        const float D2     = std::max(0.0f, SafeDist - StartM);
        if (D2 > 1.0e-3f)
        {
            const float Z0     = RayOriginWorld.Z + RayDirection.Z * StartM;
            const float Z1     = RayOriginWorld.Z + RayDirection.Z * SafeDist;
            const float ScaleH = std::max(50.0f, FogConfiguration.AerialFog.ScaleHeightMeters);
            const float Beta0  = FogConfiguration.AerialFog.DensityMultiplier * 0.000060f;
            const float OdA    = Beta0 * D2 * EvaluateExponentialHeightIntegral(ScaleH, Z0, Z1, 0.0f);
            const float MieB   = std::clamp(FogConfiguration.AerialFog.MieBlend, 0.0f, 1.0f);
            const float SpecR  = (5.8f  / 13.5f) * (1.0f - MieB) + MieB;
            const float SpecG  = (13.5f / 13.5f) * (1.0f - MieB) + MieB;
            const float SpecB  = (33.1f / 13.5f) * (1.0f - MieB) + MieB;
            TaR = std::exp(-SpecR * OdA);
            TaG = std::exp(-SpecG * OdA);
            TaB = std::exp(-SpecB * OdA);

            const float AnisoG  = std::clamp(FogConfiguration.AerialFog.AnisotropyG, 0.0f, 0.95f);
            const float SkyGain = std::max(0.0f, FogConfiguration.AerialFog.SkyLightGain);
            const float PhRay   = 0.75f * (1.0f + CosS * CosS);
            const float PhMie   = std::min(CelestialPhaseHGCpu(CosS, AnisoG) * (4.0f * Constants::PiFloat), 8.0f);
            const float SkyW    = (1.0f - 0.2f * MieB) * SkyGain;
            const float SunW    = PhRay * 0.18f * (1.0f - MieB) + PhMie * 0.32f * MieB;
            LaR = ((HemisphericalSkyAmbient.R * 0.48f + LowBandSkyGlow.R * 0.28f) * SkyW + SunR * SunW) * FogConfiguration.AerialFog.AerialTint.R;
            LaG = ((HemisphericalSkyAmbient.G * 0.48f + LowBandSkyGlow.G * 0.28f) * SkyW + SunG * SunW) * FogConfiguration.AerialFog.AerialTint.G;
            LaB = ((HemisphericalSkyAmbient.B * 0.48f + LowBandSkyGlow.B * 0.28f) * SkyW + SunB * SunW) * FogConfiguration.AerialFog.AerialTint.B;
        }
    }

    // Symmetric two-medium RGB composite
    const float TR = TaR * Tf;
    const float TG = TaG * Tf;
    const float TB = TaB * Tf;

    const float MixTf = 1.0f + (Tf - 1.0f) * 0.5f;
    Result.TransmittanceRgb = { TR, TG, TB };
    Result.Transmittance    = std::clamp(0.2126f * TR + 0.7152f * TG + 0.0722f * TB, 0.0f, 1.0f);
    Result.InScatterHdr     = {
        LaR * (1.0f - TaR) * MixTf + LfR * (1.0f - Tf) * (1.0f + (TaR - 1.0f) * 0.5f),
        LaG * (1.0f - TaG) * MixTf + LfG * (1.0f - Tf) * (1.0f + (TaG - 1.0f) * 0.5f),
        LaB * (1.0f - TaB) * MixTf + LfB * (1.0f - Tf) * (1.0f + (TaB - 1.0f) * 0.5f)
    };
    return Result;
}

float VolumetricFogSequence::EvaluateFogMediumDensityAtPoint(
    const Mathematics::Axis3D&         WorldPoint,
    const AtmosphericFogConfiguration& FogConfiguration,
    const AtmosphericWindParameters&   GlobalWind,
    float                              DeckWindSpeedMps,
    float                              DeckWindBearingDeg,
    float                              SimulationSeconds) noexcept
{
    float Sigma = 0.0f;
    if (FogConfiguration.HeightFog.Enabled && FogConfiguration.HeightFog.Density > 1.0e-6f)
    {
        const float SafeH = std::max(1.0f, FogConfiguration.HeightFog.FalloffHeightMeters);
        const float Rel   = (WorldPoint.Z - FogConfiguration.HeightFog.BaseAltitudeMeters) / SafeH;
        Sigma += std::clamp(FogConfiguration.HeightFog.Density, 0.0f, 0.5f) * std::exp(-std::clamp(Rel, -10.0f, 40.0f));
    }

    const uint32_t Count = std::min<uint32_t>(MaxGpuLocalFogVolumes, static_cast<uint32_t>(FogConfiguration.LocalFogs.size()));
    for (uint32_t I = 0u; I < Count; ++I)
    {
        float Hn = 0.5f;
        Sigma += EvaluateSingleLocalFogVolumeCpu(
            FogConfiguration.LocalFogs[I], WorldPoint, GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, SimulationSeconds, Hn);
    }
    return Sigma;
}

FogEvaluationResult VolumetricFogSequence::EvaluateVolumetricFog(
    const Mathematics::Axis3D&            RayOriginWorld,
    const Mathematics::Axis3D&            RayDirection,
    float                                 MaxDistanceMeters,
    const Mathematics::Axis3D&            SunDirection,
    const Mathematics::LinearRGB&         SolarIrradiance,
    const Mathematics::LinearRGB&         SkyRadiance,
    const AtmosphericFogConfiguration&    FogConfiguration,
    const AtmosphericWindParameters&      GlobalWind,
    float                                 DeckWindSpeedMps,
    float                                 DeckWindBearingDeg,
    float                                 SimulationSeconds,
    float                                 PixelJitter01,
    std::span<const LocalMediaLampSample> Lamps,
    const Mathematics::LinearRGB*         OptionalLowBandGlow) noexcept
{
    const Mathematics::LinearRGB LowGlow = (OptionalLowBandGlow != nullptr) ? *OptionalLowBandGlow : SkyRadiance;
    FogEvaluationResult Result = EvaluateAnalyticAtmosphericMedia(
        RayOriginWorld, RayDirection, MaxDistanceMeters, SunDirection, SolarIrradiance, SkyRadiance, LowGlow,
        FogConfiguration, GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, SimulationSeconds);

    const uint32_t LocalCount = std::min<uint32_t>(MaxGpuLocalFogVolumes, static_cast<uint32_t>(FogConfiguration.LocalFogs.size()));
    if (LocalCount == 0u || MaxDistanceMeters <= 0.05f) return Result;

    const float InvX = (std::abs(RayDirection.X) > 1.0e-6f) ? (1.0f / RayDirection.X) : 1.0e6f;
    const float InvY = (std::abs(RayDirection.Y) > 1.0e-6f) ? (1.0f / RayDirection.Y) : 1.0e6f;
    const float InvZ = (std::abs(RayDirection.Z) > 1.0e-6f) ? (1.0f / RayDirection.Z) : 1.0e6f;

    float TMin = 1.0e9f;
    float TMax = 0.0f;
    uint32_t Steps = 24u;
    float MaxRad = 4.0f;

    for (uint32_t I = 0u; I < LocalCount; ++I)
    {
        const LocalFogVolume& Vol = FogConfiguration.LocalFogs[I];
        if (!Vol.Enabled || Vol.Density <= 1.0e-4f) continue;
        const float MaxXY = std::max(Vol.HalfExtentMeters.X, Vol.HalfExtentMeters.Y) * 1.45f;
        const float MinZ  = (Vol.Shape == AtmosphericVolumeShape::HalfSphere)
            ? (0.14f * Vol.HalfExtentMeters.Z)
            : Vol.HalfExtentMeters.Z;
        const float T0x = (Vol.VolumeCenterMeters.X - MaxXY - RayOriginWorld.X) * InvX;
        const float T1x = (Vol.VolumeCenterMeters.X + MaxXY - RayOriginWorld.X) * InvX;
        const float T0y = (Vol.VolumeCenterMeters.Y - MaxXY - RayOriginWorld.Y) * InvY;
        const float T1y = (Vol.VolumeCenterMeters.Y + MaxXY - RayOriginWorld.Y) * InvY;
        const float T0z = (Vol.VolumeCenterMeters.Z - MinZ  - RayOriginWorld.Z) * InvZ;
        const float T1z = (Vol.VolumeCenterMeters.Z + Vol.HalfExtentMeters.Z - RayOriginWorld.Z) * InvZ;
        const float TE  = std::max(0.05f, std::max(std::min(T0x, T1x), std::max(std::min(T0y, T1y), std::min(T0z, T1z))));
        const float TX  = std::min(MaxDistanceMeters, std::min(std::max(T0x, T1x), std::min(std::max(T0y, T1y), std::max(T0z, T1z))));
        if (TX > TE)
        {
            TMin   = std::min(TMin, TE);
            TMax   = std::max(TMax, TX);
            Steps  = std::max(Steps, std::clamp(Vol.MarchSteps, 16u, 48u));
            MaxRad = std::max(MaxRad, std::max(Vol.HalfExtentMeters.X, std::max(Vol.HalfExtentMeters.Y, Vol.HalfExtentMeters.Z)));
        }
    }
    if (TMax <= TMin) return Result;

    const float Jitter = (PixelJitter01 >= 0.0f) ? std::clamp(PixelJitter01, 0.0f, 1.0f) : 0.5f;
    const float Dt     = (TMax - TMin) / static_cast<float>(Steps);
    float       T      = TMin + Dt * Jitter;
    const float CosS   = RayDirection.X * SunDirection.X + RayDirection.Y * SunDirection.Y + RayDirection.Z * SunDirection.Z;
    const float SunUp  = std::clamp(SunDirection.Z * 4.0f + 0.15f, 0.0f, 1.0f);
    const float SunR   = SolarIrradiance.R   * SunUp;
    const float SunG   = SolarIrradiance.G * SunUp;
    const float SunB   = SolarIrradiance.B  * SunUp;
    const float ShStep = std::clamp(MaxRad * 0.45f, 0.8f, 35.0f);

    float AccR = 0.0f, AccG = 0.0f, AccB = 0.0f;
    float Tr = 1.0f;

    for (uint32_t S = 0u; S < Steps && Tr >= 0.015f; ++S)
    {
        const Mathematics::Axis3D P {
            RayOriginWorld.X + RayDirection.X * T,
            RayOriginWorld.Y + RayDirection.Y * T,
            RayOriginWorld.Z + RayDirection.Z * T
        };

        float SumSigT = 0.0f;
        float SumScR = 0.0f, SumScG = 0.0f, SumScB = 0.0f;

        for (uint32_t I = 0u; I < LocalCount; ++I)
        {
            const LocalFogVolume& Vol = FogConfiguration.LocalFogs[I];
            float Hn = 0.5f;
            const float Rho = EvaluateSingleLocalFogVolumeCpu(
                Vol, P, GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, SimulationSeconds, Hn);
            if (Rho <= 1.0e-5f) continue;

            float Sh = 1.0f;
            if (Vol.SelfShadowEnabled)
            {
                float Od = 0.0f;
                for (uint32_t K = 1u; K <= 4u; ++K)
                {
                    const float FK = static_cast<float>(K);
                    const float D  = ShStep * FK * FK * 0.14f;
                    const Mathematics::Axis3D SP { P.X + SunDirection.X * D, P.Y + SunDirection.Y * D, P.Z + SunDirection.Z * D };
                    float RhoSum = 0.0f;
                    for (uint32_t J = 0u; J < LocalCount; ++J)
                    {
                        float THn = 0.5f;
                        const float R = EvaluateSingleLocalFogVolumeCpu(
                            FogConfiguration.LocalFogs[J], SP, GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, SimulationSeconds, THn);
                        RhoSum += R * (1.0f + std::max(0.0f, FogConfiguration.LocalFogs[J].AbsorptionRatio));
                    }
                    Od += RhoSum * D * 0.70f;
                    if (Od > 10.0f) break;
                }
                Sh = std::exp(-Od);
            }

            const float Ph  = CelestialPhaseHGCpu(CosS, Vol.AnisotropyG) * (4.0f * Constants::PiFloat);
            const float Ms  = Sh + 0.45f * std::sqrt(Sh);
            float LiR = SunR * (Ph * Ms * 0.72f) + SkyRadiance.R   * (0.65f + 0.35f * Hn) + LowGlow.R   * 0.25f;
            float LiG = SunG * (Ph * Ms * 0.72f) + SkyRadiance.G * (0.65f + 0.35f * Hn) + LowGlow.G * 0.25f;
            float LiB = SunB * (Ph * Ms * 0.72f) + SkyRadiance.B  * (0.65f + 0.35f * Hn) + LowGlow.B  * 0.25f;

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
                    LiR += Lamp.RadianceRgb.R   * Gain;
                    LiG += Lamp.RadianceRgb.G * Gain;
                    LiB += Lamp.RadianceRgb.B  * Gain;
                }
            }

            const float SigA = std::max(0.0f, Vol.AbsorptionRatio);
            SumSigT += Rho * (1.0f + SigA);
            SumScR  += LiR * Vol.FogTint.R   * Rho;
            SumScG  += LiG * Vol.FogTint.G * Rho;
            SumScB  += LiB * Vol.FogTint.B  * Rho;
        }

        if (SumSigT > 1.0e-5f)
        {
            const float Ts = std::exp(-SumSigT * Dt);
            const float W  = Tr * (1.0f - Ts) / std::max(SumSigT, 1.0e-6f);
            AccR += SumScR * W;
            AccG += SumScG * W;
            AccB += SumScB * W;
            Tr   *= Ts;
        }
        T += Dt;
    }

    Result.InScatterHdr.R   = Result.InScatterHdr.R   * Tr + AccR;
    Result.InScatterHdr.G = Result.InScatterHdr.G * Tr + AccG;
    Result.InScatterHdr.B  = Result.InScatterHdr.B  * Tr + AccB;
    Result.TransmittanceRgb.R   *= Tr;
    Result.TransmittanceRgb.G *= Tr;
    Result.TransmittanceRgb.B  *= Tr;
    Result.Transmittance = std::clamp(
        0.2126f * Result.TransmittanceRgb.R + 0.7152f * Result.TransmittanceRgb.G + 0.0722f * Result.TransmittanceRgb.B,
        0.0f, 1.0f);
    return Result;
}

} // namespace Frontier::GeometricRaster
