/*==========================================================================================================================================
                                                            CLOUDTRANSPORT.SLANG
==========================================================================================================================================*/
// 🧩 Celestial volumetric cloud spherical shell raymarch: 5-tap quadratic light march, 3-oct Wrenninge multi-scatter,
//    dual-lobe HG, Beer-Powder crevice darkening, analytical step integration, and live hemispherical sky ambient.

#ifndef FRONTIER_PLANETARY_CLOUD_TRANSPORT
#define FRONTIER_PLANETARY_CLOUD_TRANSPORT

#include "CloudDensity.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                  SPHERICAL INTERVALS
//------------------------------------------------------------------------------------------------------------------------

float PlanetCloudAltitude(float3 Position, float PlanetRadiusM)
{
    return length(Position) - PlanetRadiusM;
}

float2 PlanetCloudSphere(float3 Position, float3 Direction, float RadiusM)
{
    float RadialDistanceM   = length(Position);
    float RayProjection     = dot(Position, Direction);
    float QuadraticConstant = (RadialDistanceM - RadiusM) * (RadialDistanceM + RadiusM);
    float Discriminant      = RayProjection * RayProjection - QuadraticConstant;
    if (Discriminant < 0.0) return float2(-1.0, -1.0);
    float SqrtD             = sqrt(Discriminant);
    float StableRoot        = -RayProjection - ((RayProjection < 0.0) ? -1.0 : 1.0) * SqrtD;
    float CompanionRoot     = (abs(StableRoot) > 1.0e-6) ? QuadraticConstant / StableRoot : StableRoot;
    return float2(min(StableRoot, CompanionRoot), max(StableRoot, CompanionRoot));
}

float PlanetCloudGroundDistance(float3 Origin, float3 Direction, float RadiusM)
{
    float2 Ground = PlanetCloudSphere(Origin, Direction, RadiusM);
    if (Ground.y <= 0.0) return 1.0e9;
    if (Ground.x > 0.0) return Ground.x;
    return (dot(Origin, Direction) < 0.0) ? 0.0 : 1.0e9;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      ATMOSPHERIC TRANSMITTANCE (ORBITAL SUPPORT)
//------------------------------------------------------------------------------------------------------------------------

float3 QueryCloudRayleighExtinction(CloudSpecification Specification)
{
    return float3(5.802e-6, 13.558e-6, 33.1e-6) * max(0.0, Specification.AtmosphereStrengths.x);
}

float3 QueryCloudAerosolExtinction(CloudSpecification Specification)
{
    return float3(4.40e-6) * max(0.0, Specification.AtmosphereStrengths.y);
}

float CloudAirColumn(float ScaleHeightM, float RadiusM, float AltitudeM, float Mu)
{
    float AbsoluteCosine = abs(Mu);
    float UpwardColumnM =
        2.0 * ScaleHeightM /
        (AbsoluteCosine + sqrt(AbsoluteCosine * AbsoluteCosine + 8.0 * ScaleHeightM / (FrontierPi * RadiusM)));
    float ClosestApproachRiseM = (RadiusM + AltitudeM) * Mu * Mu / (1.0 + sqrt(saturate(1.0 - Mu * Mu)));
    float DownwardColumnM =
        sqrt(2.0 * FrontierPi * (RadiusM + AltitudeM) * ScaleHeightM) * exp(min(9.0, ClosestApproachRiseM / ScaleHeightM)) -
        UpwardColumnM;
    return exp(-max(0.0, AltitudeM) / ScaleHeightM) * ((Mu < 0.0) ? DownwardColumnM : UpwardColumnM);
}

float3 CloudSolarAirTransmittance(CloudSpecification Specification, float3 Position)
{
    float RadiusM         = length(Position);
    float AltitudeM       = max(0.0, RadiusM - Specification.RadiusM);
    float Mu              = dot(Position, Specification.SunDirection) / max(1.0, RadiusM);
    float TangentBoundary = -sqrt(saturate(AltitudeM * (RadiusM + Specification.RadiusM) / max(1.0, RadiusM * RadiusM)));
    float SolarVisibility = smoothstep(-0.00465, 0.00465, Mu - TangentBoundary);
    if (SolarVisibility <= 0.0) return float3(0.0, 0.0, 0.0);
    float RayleighColumnM =
        CloudAirColumn(max(400.0, Specification.AirScaleHeightsM.x), Specification.RadiusM, AltitudeM, Mu);
    float AerosolColumnM =
        CloudAirColumn(max(200.0, Specification.AirScaleHeightsM.y), Specification.RadiusM, AltitudeM, Mu);
    float  OzoneColumnM = 15000.0 * exp(-max(0.0, AltitudeM - 25000.0) / 8000.0) / max(0.15, abs(Mu));
    float3 Tau          = QueryCloudRayleighExtinction(Specification) * RayleighColumnM +
                          QueryCloudAerosolExtinction(Specification) * AerosolColumnM +
                          Specification.AtmosphereStrengths.z * float3(0.650e-6, 1.881e-6, 0.085e-6) * OzoneColumnM;
    return SolarVisibility * exp(-Tau);
}

float CloudTransportHG(float CosTheta, float G)
{
    return CelestialPhaseHG(CosTheta, G);
}

//------------------------------------------------------------------------------------------------------------------------
//                               CELESTIAL 5-TAP QUADRATIC LIGHT MARCH & SHADOW DEPTH
//------------------------------------------------------------------------------------------------------------------------

float CelestialCloudLightOpticalDepth(CloudSpecification Spec, float3 PlanetPosition)
{
    float Od       = 0.0;
    float EffThick = min(Spec.ThicknessM, 1800.0);
    float St       = EffThick * 0.12;
    uint  Taps     = clamp(Spec.LightSteps, 3u, 5u);
    for (uint I = 1u; I <= 5u; ++I)
    {
        if (I > Taps) break;
        float FI  = float(I);
        float D   = St * FI * FI * 0.35;
        float Lod = (I > 2u) ? 1.0 : 0.0;
        Od += CelestialCloudDensity(Spec, PlanetPosition + Spec.SunDirection * D, Lod) * D * 0.8;
    }
    return Od;
}

float CloudSolarOpticalDepth(
    CloudSpecification Specification,
    float3             Position,
    uint               Steps = 4u,
    float              FootprintM = 20.0)
{
    if (!Specification.UseCloudShadows || Specification.Fraction <= 0.001 || Specification.Extinction <= 0.001)
    {
        return 0.0;
    }
    float R0 = Specification.RadiusM + Specification.FloorM;
    float R1 = R0 + Specification.ThicknessM;
    float H  = length(Position) - Specification.RadiusM;
    float2 I0 = PlanetCloudSphere(Position, Specification.SunDirection, R0);
    float2 I1 = PlanetCloudSphere(Position, Specification.SunDirection, R1);

    float T0 = 0.0;
    float T1 = 0.0;
    if (H < Specification.FloorM)
    {
        if (I1.y < 0.0) return 0.0;
        T0 = max(I0.y, 0.0);
        T1 = I1.y;
    }
    else if (H < Specification.FloorM + Specification.ThicknessM)
    {
        T0 = 0.0;
        T1 = (I0.x > 0.0) ? I0.x : I1.y;
    }
    else
    {
        return 0.0;
    }

    T1 = min(T1, T0 + min(Specification.ThicknessM * 6.0, 12000.0));
    if (T1 <= T0) return 0.0;

    uint  Count       = clamp(Steps, 2u, 6u);
    float Dt          = (T1 - T0) / float(Count);
    float SigA        = max(0.0, Specification.Absorption);
    float ThickNorm   = clamp(1400.0 / max(1400.0, Specification.ThicknessM), 0.30, 1.0);
    float Tau         = 0.0;
    for (uint S = 0u; S < 6u; ++S)
    {
        if (S >= Count) break;
        float  T   = T0 + (float(S) + 0.5) * Dt;
        float  Rho = CelestialCloudDensity(Specification, Position + Specification.SunDirection * T, 1.0);
        Tau += Rho * (1.0 + SigA) * 0.012 * ThickNorm * Dt;
        if (Tau >= 6.0) return 6.0;
    }
    return Tau;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  CELESTIAL VOLUMETRIC CLOUD MARCH (cloudMarch)
//------------------------------------------------------------------------------------------------------------------------

CloudTransportIntegral TracePlanetaryClouds(
    CloudSpecification Specification,
    float3             Origin,
    float3             Direction,
    float2             Pixel,
    float              LimitM)
{
    CloudTransportIntegral Integral;
    Integral.Radiance      = float3(0.0, 0.0, 0.0);
    Integral.Transmittance = 1.0;
    Integral.DistanceM     = LimitM;

    if (!Specification.UseClouds || Specification.Fraction <= 0.001 || Specification.Extinction <= 0.001)
    {
        return Integral;
    }

    float  R0 = Specification.RadiusM + Specification.FloorM;
    float  R1 = R0 + Specification.ThicknessM;
    float  H  = length(Origin) - Specification.RadiusM;
    float2 I0 = PlanetCloudSphere(Origin, Direction, R0);
    float2 I1 = PlanetCloudSphere(Origin, Direction, R1);
    float2 Ig = PlanetCloudSphere(Origin, Direction, Specification.RadiusM);

    float T0 = 0.0;
    float T1 = 0.0;
    if (H < Specification.FloorM)
    {
        if (I1.y < 0.0) return Integral;
        T0 = max(I0.y, 0.0);
        T1 = I1.y;
    }
    else if (H > Specification.FloorM + Specification.ThicknessM)
    {
        if (I1.x < 0.0) return Integral;
        T0 = I1.x;
        T1 = (I0.x > 0.0) ? I0.x : I1.y;
        if (Ig.x > 0.0 && Ig.x < T0) return Integral;
    }
    else
    {
        T0 = 0.0;
        T1 = (I0.x > 0.0) ? I0.x : I1.y;
    }

    if (Ig.x > 0.0) T1 = min(T1, Ig.x);
    T1 = min(T1, LimitM);

    float FarCap = (H > Specification.FloorM + Specification.ThicknessM)
        ? min(max(60000.0, Specification.ThicknessM * 24.0), 120000.0)
        : min(Specification.ThicknessM * 14.0, 15000.0);
    T1 = min(T1, T0 + FarCap);
    if (T1 <= T0)
    {
        return Integral;
    }

    uint  StepCount = (Specification.ThicknessM > 2200.0)
        ? max(clamp(Specification.Steps, 16u, 64u), 64u)
        : clamp(Specification.Steps, 16u, 64u);
    float Dt        = (T1 - T0) / float(StepCount);
    float Jitter    = CloudBlueJitter(Pixel);
    float T         = T0 + Dt * Jitter;
    float LodFar    = smoothstep(5500.0, 24000.0, T0);

    float CosS      = dot(Direction, Specification.SunDirection);
    float G1        = clamp(Specification.PhaseG, 0.0, 0.95);
    float G2        = clamp(Specification.BackPhaseG, 0.0, 0.90);
    float MixLobe   = clamp(Specification.PhaseMix, 0.0, 1.0);
    float Ph        = min(lerp(CelestialPhaseHG(CosS, G1), CelestialPhaseHG(CosS, -G2), MixLobe) * (4.0 * FrontierPi), 14.0);

    float  SunUp    = saturate(Specification.SunDirection.z * 4.0 + 0.15);
    float3 SunL     = Specification.SolarIrradiance * SunUp;
    float3 Amb      = Specification.SkyAmbient * max(0.0, Specification.SkyAmbientGain) * 0.62;
    float  SigA     = max(0.0, Specification.Absorption) + ((Specification.Formation == 11u) ? 0.12 : 0.0);
    float  PowderK  = clamp(Specification.PowderStrength, 0.0, 1.0);
    float3 Albedo   = max(float3(0.0), Specification.CloudAlbedo);
    float  ExtScale = 0.06 * clamp(1400.0 / max(1400.0, Specification.ThicknessM), 0.22, 1.0);

    // Subtle pearlescent scattering tint for stratospheric Nacreous clouds (Formation 10)
    if (Specification.Formation == 10u)
    {
        float Ang = acos(clamp(CosS, -1.0, 1.0));
        float3 Pearl = float3(
            0.88 + 0.22 * cos(Ang * 5.5 + 0.0),
            0.88 + 0.20 * cos(Ang * 5.5 + 2.1),
            0.92 + 0.18 * cos(Ang * 5.5 + 4.2));
        Albedo *= lerp(float3(1.0, 1.0, 1.0), Pearl, 0.55);
    }

    float3 Acc      = float3(0.0, 0.0, 0.0);
    float  Trans    = 1.0;
    float  WeightD  = 0.0;
    float  WeightS  = 0.0;

    for (uint I = 0u; I < 64u; ++I)
    {
        if (I >= StepCount || Trans < 0.015) break;
        float3 Pos = Origin + Direction * T;
        float  Rho = CelestialCloudDensity(Specification, Pos, LodFar);
        if (Rho > 1.0e-3)
        {
            float Od = lerp(CelestialCloudLightOpticalDepth(Specification, Pos), Rho * min(Specification.ThicknessM, 1800.0) * 0.3, LodFar);
            float Hn = clamp((length(Pos) - Specification.RadiusM - Specification.FloorM) / max(1.0, Specification.ThicknessM), 0.0, 1.0);

            // 3-octave Wrenninge multi-scattering (exact Celestial Demo weights: a*=0.5, b*=0.55)
            float Ms = 0.0;
            float A  = 1.0;
            float B  = 1.0;
            [unroll]
            for (uint O = 0u; O < 3u; ++O)
            {
                Ms += B * exp(-Od * A * (1.0 + SigA));
                A *= 0.50;
                B *= 0.55;
            }

            float  Powder = 1.0 - PowderK * exp(-Rho * Dt * (ExtScale / 0.06) * 2.0 * (1.0 + SigA)) * (1.0 - 0.5 * clamp(CosS, 0.0, 1.0));
            float3 Li     = SunL * (Ph * Ms * Powder) + Amb * lerp(0.35, 1.0, Hn);

            // Pyrocumulus subtle warm convective core near volcanic base (Formation 11)
            if (Specification.Formation == 11u)
            {
                float Ember = pow(saturate(1.0 - Hn * 7.0), 2.2) * smoothstep(0.02, 0.35, Rho);
                Li += float3(1.10, 0.34, 0.07) * Ember * 0.18;
            }

            float  SigT = Rho * (1.0 + SigA) * ExtScale;
            float  Ts   = exp(-SigT * Dt);
            float3 Sc   = Li * Albedo * (Rho * ExtScale);
            Acc += Trans * (Sc - Sc * Ts) / max(SigT, 1.0e-6);

            float AlphaStep = Trans * (1.0 - Ts);
            WeightD += AlphaStep * T;
            WeightS += AlphaStep;
            Trans   *= Ts;
        }
        T += Dt;
    }

    // Distance aerial haze blend only when looking through lower air from below the cloud layer
    float Above = step(Specification.FloorM + Specification.ThicknessM, H);
    float Aer   = (1.0 - exp(-T0 * 6.0e-5)) * (1.0 - Above);
    Acc = lerp(Acc, Specification.SkyAmbient * (1.0 - Trans) * 0.72, Aer * 0.68);

    Integral.Radiance      = max(float3(0.0), Acc);
    Integral.Transmittance = clamp(Trans, 0.0, 1.0);
    Integral.DistanceM     = (WeightS > 1.0e-5) ? (WeightD / WeightS) : T0;
    return Integral;
}

#endif
