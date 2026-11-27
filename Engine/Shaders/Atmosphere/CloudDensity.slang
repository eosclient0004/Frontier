/*==========================================================================================================================================
                                                             CLOUDDENSITY.SLANG
==========================================================================================================================================*/
// 🧩 Celestial volumetric cloud density across all 12 Frontier CloudCategoryType formations + Orbital deck (single-source Slang module).

#ifndef FRONTIER_PLANETARY_CLOUD_DENSITY
#define FRONTIER_PLANETARY_CLOUD_DENSITY

#include "CloudNoise.slang"
#include "../Shared/ShaderMathConstants.slang"


struct CloudSpecification
{
    float  RadiusM;
    float  Fraction;
    float  Extinction;
    float  FloorM;
    float  ThicknessM;
    float  FeatureScale;
    float  Anvil;
    float  PhaseG;
    float  BackPhaseG;
    float  PhaseMix;
    float  Absorption;
    float  SkyAmbientGain;
    float  PowderStrength;
    float  ErosionDetail;
    float3 CloudAlbedo;
    float  Seed;
    float  PixelAngle;
    float  UniformExtinction;
    float  TimeSeconds;
    float2 WindVelocityMps;
    float  Shear;
    float  Veer;
    float  Turbulence;
    float  CycloneSpeedMps;
    float  CycloneEyeM;
    float3 CycloneAxis;
    float3 SunDirection;
    float3 SolarIrradiance;
    float3 SkyAmbient;
    float3 AtmosphereStrengths;
    float2 AirScaleHeightsM;
    uint   Steps;
    uint   LightSteps;
    uint   Formation;
    bool   UseClouds;
    bool   UseCloudShadows;
};

struct CloudTransportIntegral
{
    float3 Radiance;
    float  Transmittance;
    float  DistanceM;
};

//------------------------------------------------------------------------------------------------------------------------
//                                      CELESTIAL CLOUD HEIGHT PROFILE & WIND ADVECTION
//------------------------------------------------------------------------------------------------------------------------

float CelestialCloudHeightProfile(float Hn, uint Formation, float Anvil)
{
    // Formations 0..5 match Celestial Demo clHeightProfile verbatim
    if (Formation == 0u) // Stratus: flat sheet filling the slab
    {
        return smoothstep(0.0, 0.08, Hn) * (1.0 - smoothstep(0.75, 1.0, Hn));
    }
    if (Formation == 1u) // Stratocumulus: flat base, lumpy top
    {
        return smoothstep(0.0, 0.12, Hn) * (1.0 - smoothstep(0.50, 0.95, Hn));
    }
    if (Formation == 2u) // Cumulus: flat base, tall bulging tops
    {
        return smoothstep(0.0, 0.07, Hn) * (1.0 - smoothstep(0.35, 1.0, Hn)) * 1.15;
    }
    if (Formation == 3u) // Cumulonimbus: full column + anvil spread
    {
        return smoothstep(0.0, 0.05, Hn) * (1.0 - smoothstep(0.85, 1.0, Hn)) *
               lerp(1.0, 1.6, smoothstep(0.70, 1.0, Hn) * Anvil);
    }
    if (Formation == 4u) // Altostratus: thin translucent mid layer
    {
        return smoothstep(0.0, 0.30, Hn) * (1.0 - smoothstep(0.60, 1.0, Hn)) * 0.70;
    }
    if (Formation == 5u) // Cirrus: wispy, low density ice veil
    {
        return smoothstep(0.0, 0.40, Hn) * (1.0 - smoothstep(0.50, 1.0, Hn)) * 0.35;
    }

    // Extended Frontier formations 6..12 (clean organic vertical profiles, zero zebra banding)
    if (Formation == 6u) // Lenticular: smooth laminar lens stack
    {
        float CenterPeak = exp(-pow((Hn - 0.50) / 0.24, 2.0));
        float Upper      = exp(-pow((Hn - 0.76) / 0.13, 2.0)) * 0.55;
        return max(CenterPeak, Upper) * smoothstep(0.04, 0.20, Hn) * (1.0 - smoothstep(0.82, 0.98, Hn));
    }
    if (Formation == 7u) // Mammatus: dense overhanging storm underside with soft rounded lobes
    {
        return smoothstep(0.0, 0.22, Hn) * (1.0 - smoothstep(0.62, 0.96, Hn)) * 1.10;
    }
    if (Formation == 8u) // KelvinHelmholtz: wind-sheared breaking wave crest
    {
        return smoothstep(0.05, 0.28, Hn) * (1.0 - smoothstep(0.58, 0.95, Hn)) * 1.05;
    }
    if (Formation == 9u) // Asperitas: undulating heavy low cloud ceiling
    {
        return smoothstep(0.0, 0.16, Hn) * (1.0 - smoothstep(0.55, 0.95, Hn)) * 1.10;
    }
    if (Formation == 10u) // Nacreous: thin polar stratospheric mother-of-pearl veil
    {
        return smoothstep(0.0, 0.32, Hn) * (1.0 - smoothstep(0.56, 1.0, Hn)) * 0.48;
    }
    if (Formation == 11u) // Pyrocumulus: dense towering convective column + broad anvil
    {
        return smoothstep(0.0, 0.06, Hn) * (1.0 - smoothstep(0.86, 1.0, Hn)) *
               lerp(1.10, 1.65, smoothstep(0.68, 1.0, Hn) * max(0.5, Anvil));
    }

    // Formation 12: Orbital planetary blanket
    return smoothstep(0.0, 0.10, Hn) * (1.0 - smoothstep(0.45, 0.96, Hn)) * 1.10;
}

float2 CelestialWindVelocityAtAltitude(CloudSpecification Spec, float AltM)
{
    float AltKm   = max(0.0, AltM) * 0.001;
    float SpScale = 1.0 + max(0.0, Spec.Shear) * AltKm;
    float VeerAng = Spec.Veer * AltKm;
    float CosV    = cos(VeerAng);
    float SinV    = sin(VeerAng);
    float2 V      = Spec.WindVelocityMps;
    return float2(V.x * CosV - V.y * SinV, V.x * SinV + V.y * CosV) * SpScale;
}

float CelestialCloudDensity(CloudSpecification Spec, float3 PlanetPosition, float LodFar)
{
    float AltM = length(PlanetPosition) - Spec.RadiusM;
    float Hn   = clamp((AltM - Spec.FloorM) / max(1.0, Spec.ThicknessM), 0.0, 1.0);
    float Prof = CelestialCloudHeightProfile(Hn, Spec.Formation, Spec.Anvil);
    if (Prof <= 0.0)
    {
        return 0.0;
    }

    // Sea-level nadir local coordinates (Z-up) preserving full float32 precision near ground
    float3 LocalP = PlanetPosition - float3(0.0, 0.0, Spec.RadiusM);
    float2 WVel   = CelestialWindVelocityAtAltitude(Spec, Spec.FloorM + Hn * Spec.ThicknessM);
    float2 WDisp  = WVel * Spec.TimeSeconds * 0.8;
    float  WLen   = length(WVel);
    float2 WDir   = (WLen > 1.0e-3) ? (WVel / WLen) : float2(0.0, 0.0);

    float3 Q = LocalP;
    Q.xy -= WDisp;
    float ShearFactor = (Spec.Formation == 3u || Spec.Formation == 8u || Spec.Formation == 11u) ? Spec.Anvil : 0.20;
    Q.xy -= WDir * (Hn * Spec.ThicknessM * 0.35 * ShearFactor);

    // Subtle organic turbulence eddy warp when wind turbulence is active
    if (Spec.Turbulence > 0.01 && WLen > 0.1)
    {
        float3 TCoord = Q * 0.00055 + float3(Spec.TimeSeconds * 0.012, -Spec.TimeSeconds * 0.009, 0.0);
        float  Ex     = CelestialScalarNoise(TCoord + float3(0.0, 0.5, 0.0)) - CelestialScalarNoise(TCoord - float3(0.0, 0.5, 0.0));
        float  Ey     = CelestialScalarNoise(TCoord + float3(0.5, 0.0, 0.0)) - CelestialScalarNoise(TCoord - float3(0.5, 0.0, 0.0));
        Q.xy += float2(Ex, -Ey) * (Spec.Turbulence * min(WLen, 35.0) * 18.0);
    }

    // Exact Celestial Demo 4-octave shape noise (sc = 1 / (uCLScale * 900), Z-up)
    float CellKm = max(0.30, Spec.FeatureScale);
    float Sc     = 1.0 / (CellKm * 900.0);
    float3 S     = Q * Sc;

    // Extended formation spatial shaping (organic domain warp, zero periodic zebra stripes)
    if (Spec.Formation == 6u) // Lenticular: smooth horizontally stretched laminar disc puffs
    {
        S = float3(S.x * 0.65, S.y * 0.65, S.z * 2.2);
    }
    else if (Spec.Formation == 8u) // KelvinHelmholtz: wind-aligned crest shear warp
    {
        float Warp = (CelestialScalarNoise(float3(S.xy * 0.75, 0.5)) - 0.5) * 1.4;
        S.xy += float2(0.8, 0.6) * (sin(Hn * 3.14159) * 0.55 + Warp * 0.45);
        S.z  *= 1.6;
    }
    else if (Spec.Formation == 9u) // Asperitas: undulating wave height displacement
    {
        float Wave = (CelestialScalarNoise(float3(S.xy * 1.35, 1.7)) - 0.5) * 0.34;
        float HnMod = clamp(Hn + Wave, 0.0, 1.0);
        Prof = CelestialCloudHeightProfile(HnMod, Spec.Formation, Spec.Anvil);
        if (Prof <= 0.0) return 0.0;
    }
    else if (Spec.Formation == 10u) // Nacreous: elongated silky stratospheric ribbons
    {
        S = float3(S.x * 0.32, S.y * 1.85, S.z * 2.8);
    }

    float Shape = CelestialScalarNoise(S) * 0.50
                + CelestialScalarNoise(S * 2.02 + float3(3.1, 9.2, 1.7)) * 0.25
                + CelestialScalarNoise(S * 4.10 + float3(7.7, 1.1, 2.2)) * 0.125
                + CelestialScalarNoise(S * 8.30 + float3(1.3, 4.4, 8.8)) * 0.0625;
    Shape /= 0.9375;

    // Cirrus ice-crystal high-altitude wind streaks (exact Celestial Demo formula, Z-up)
    if (Spec.Formation == 5u)
    {
        float CirrusStreak = CelestialScalarNoise(float3(S.x * 0.25, S.y * 6.0, S.z * 4.0));
        Shape = Shape * 0.60 + CirrusStreak * 0.50;
    }
    else if (Spec.Formation == 7u) // Mammatus hanging cellular pouches along lower half
    {
        float CellN = CelestialScalarNoise(S * 2.8 + float3(2.4, 6.1, 0.8));
        float Pouch = pow(saturate(CellN), 1.35) * (1.0 - smoothstep(0.05, 0.55, Hn));
        Shape = saturate(Shape * 0.78 + Pouch * 0.42);
    }

    float Cov  = clamp(Spec.Fraction, 0.0, 1.0);
    float DeckDensity = saturate((Shape - (1.0 - Cov)) / max(1.0e-3, Cov)) * Prof;
    if (DeckDensity <= 0.0 || LodFar >= 0.99)
    {
        return DeckDensity * Spec.Extinction;
    }

    // Exact Celestial Demo 2-octave height-modulated edge erosion, smoothly attenuated with distance to avoid horizon shimmer
    float DetScale = (Spec.Formation == 6u || Spec.Formation == 10u) ? (Spec.ErosionDetail * 0.35) : Spec.ErosionDetail;
    float Det      = CelestialScalarNoise(S * 9.0 + float3(Spec.TimeSeconds * 0.02)) * 0.60
                   + CelestialScalarNoise(S * 19.0 + float3(5.0, 5.0, 5.0)) * 0.40;
    float Erode    = lerp(Det, 1.0 - Det, saturate(Hn * 3.0)) * DetScale * 0.45 * (1.0 - LodFar);
    return saturate(DeckDensity - Erode * (1.0 - DeckDensity)) * Spec.Extinction;
}

#endif
