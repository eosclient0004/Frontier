/*============================================================================================================================================
                                                        VOLUMETRICFOGCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Celestial Analytic Media (Exp² Height Fog + Spectral Rayleigh/Mie Aerial Perspective) & Multi-Shape Local Fog Volumes

#include "CloudNoise.slang"
#include "../Shared/ShaderMathConstants.slang"


struct GpuLocalFogVolume
{
    float4 CenterShape;          // xyz = CenterWorld, w = Shape (0=Box..7=HalfSphere)
    float4 HalfExtentYaw;        // xyz = HalfExtentM, w = YawRad
    float4 DensityCovScaleSides; // x = Density, y = Coverage/Heterogeneity, z = FeatureScaleMeters, w = float(PrismSides)
    float4 WindVelCurl;          // xyz = ResolvedWindVelMps, w = EdgeSoftness (per-volume!)
    float4 TintAniso;            // xyz = FogTintLinear, w = AnisotropyG
    float4 AbsorbShadowFalloffSteps;     // x = AbsorptionRatio, y = SelfShadow (0/1), z = DensityFalloff (0/1), w = MarchSteps
};

struct VolumetricFogUniformBlock
{
    float4            HeightParams0; // x = Enabled, y = Density, z = FalloffHeightM, w = FloorAltM
    float4            HeightParams1; // xyz = HeightColour, w = SunScatterIntensity
    float4            AerialParams;  // x = Enabled, y = DensityMul, z = StartDistM, w = MieBlend
    float4            FogWindClock;  // x = HeightFollowWind, y = HeightWindVX, z = HeightWindVY, w = SimulationSeconds
    float4            FogMeta;       // x = LocalFogCount (0..8), y = AerialScaleHeightM, z = AerialAnisotropyG, w = AerialSkyLightGain
    float4            GodRayParams0; // x = Enabled, y = Intensity, z = AnisotropyG, w = Density
    float4            GodRayParams1; // x = MaxDistanceM, y = SampleSteps, z = InteractWithClouds, w = InteractWithFog
    float4            GodRayTint;    // xyz = ShaftTint, w = Decay
    float4            AerialTintPad; // xyz = AerialTintLinear, w = Reserved
    GpuLocalFogVolume LocalFogs[8];
};

//------------------------------------------------------------------------------------------------------------------------
//                                      EXACT HEIGHT INTEGRAL & ELEVATION-AWARE SKY RANGE
//------------------------------------------------------------------------------------------------------------------------

float EvaluateExpHeightIntegralGpu(float ScaleHeightM, float OriginZ, float EndZ, float FloorAltM)
{
    float H  = max(1.0, ScaleHeightM);
    float DZ = EndZ - OriginZ;
    float A0 = (OriginZ - FloorAltM) / H;
    if (abs(DZ) < 0.01)
    {
        return exp(-clamp(A0, -10.0, 40.0));
    }
    float A1 = (EndZ - FloorAltM) / H;
    return max(0.0, (exp(-clamp(A0, -10.0, 40.0)) - exp(-clamp(A1, -10.0, 40.0))) / (DZ / H));
}

float ComputeSkyFogPathMetersGpu(VolumetricFogUniformBlock Fog, float OriginZ, float RayDirZ)
{
    float HeightRef = (Fog.HeightParams0.x > 0.5) ? (max(1.0, Fog.HeightParams0.z) * 4.0) : 0.0;
    float AerialRef = (Fog.AerialParams.x > 0.5)  ? (max(10.0, Fog.FogMeta.y) * 5.0)       : 0.0;
    float TopRef    = max(HeightRef, AerialRef);
    if (TopRef <= 1.0) TopRef = 6000.0;
    if (RayDirZ > 0.002)
    {
        return min(25000.0, TopRef / RayDirZ);
    }
    if (RayDirZ < -0.002)
    {
        float FloorAlt = (Fog.HeightParams0.x > 0.5) ? Fog.HeightParams0.w : 0.0;
        float AboveM  = max(2.0, OriginZ - FloorAlt);
        return min(25000.0, AboveM / (-RayDirZ));
    }
    return 25000.0;
}

float EvaluateHeightFogOpticalDepthGpu(
    float OriginZ,
    float DirZ,
    float SegM,
    float Density,
    float FalloffM,
    float FloorAltM)
{
    if (Density <= 1.0e-6 || SegM <= 1.0e-4) return 0.0;
    float K = EvaluateExpHeightIntegralGpu(FalloffM, OriginZ, OriginZ + DirZ * SegM, FloorAltM);
    return max(0.0, clamp(Density, 0.0, 0.5) * SegM * K);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  BOUNDED SHAPE MASK (8 SHAPES, Z-UP, PER-VOLUME SOFTNESS)
//------------------------------------------------------------------------------------------------------------------------

float EvaluateFogVolumeShapeMaskGpu(
    float3 PointWorld,
    float3 CenterWorld,
    float3 HalfExt,
    uint   Shape,
    float  YawRad,
    uint   PrismSides,
    float  EdgeSoftness,
    bool   DensityFalloff,
    out float OutMargin,
    out float OutHeightNorm)
{
    float3 SafeH = max(HalfExt, float3(0.1, 0.1, 0.1));
    float3 Rel   = PointWorld - CenterWorld;
    float  C     = cos(-YawRad);
    float  S     = sin(-YawRad);
    float3 Local = float3(C * Rel.x - S * Rel.y, S * Rel.x + C * Rel.y, Rel.z);

    float  Soft = clamp(EdgeSoftness, 0.01, 1.0);
    float3 U    = Local / SafeH;
    OutHeightNorm = clamp(0.5 * (U.z + 1.0), 0.0, 1.0);

    float DistNorm = 1.0e6;
    if (Shape == 0u) // Box
    {
        DistNorm = max(abs(U.x), max(abs(U.y), abs(U.z)));
    }
    else if (Shape == 1u) // Sphere
    {
        float MinExt = min(SafeH.x, min(SafeH.y, SafeH.z));
        DistNorm = length(Local) / max(0.1, MinExt);
    }
    else if (Shape == 2u) // Ellipsoid
    {
        DistNorm = length(U);
    }
    else if (Shape == 3u) // Cylinder (Z-up)
    {
        DistNorm = max(length(U.xy), abs(U.z));
    }
    else if (Shape == 4u) // Cone (Z-up: wide base U.z = -1, apex U.z = +1)
    {
        float Taper = clamp(0.5 * (1.0 - U.z), 0.0, 1.0);
        float Rad   = length(U.xy);
        DistNorm    = max(abs(U.z), (Taper > 1.0e-3) ? (Rad / Taper) : (Rad > 1.0e-4 ? 2.0 : 1.0));
    }
    else if (Shape == 5u) // Diamond (octahedron)
    {
        DistNorm = abs(U.x) + abs(U.y) + abs(U.z);
    }
    else if (Shape == 6u) // N-sided Prism
    {
        uint  Sides  = clamp(PrismSides, 3u, 16u);
        float Sector = (2.0 * FrontierPi) / float(Sides);
        float Angle  = atan2(U.y, U.x);
        float ModA   = fmod(Angle + 4.0 * FrontierPi + 0.5 * Sector, Sector) - 0.5 * Sector;
        float PolyR  = (length(U.xy) * cos(ModA)) / max(0.25, cos(0.5 * Sector));
        DistNorm     = max(PolyR, abs(U.z));
    }
    else // Shape == 7u: HalfSphere Dome (flat bottom at CenterWorld.z)
    {
        if (U.z < -0.14) { OutMargin = 0.0; return 0.0; }
        float BottomF = smoothstep(-0.14, 0.06, U.z);
        float RadialD = length(float3(U.x, U.y, max(0.0, U.z)));
        OutHeightNorm = clamp(max(0.0, U.z), 0.0, 1.0);
        float Margin  = saturate(1.0 - RadialD);
        OutMargin     = Margin * BottomF;
        if (OutMargin <= 0.0) return 0.0;
        if (DensityFalloff)
        {
            return pow(smoothstep(0.0, 1.0, Margin), lerp(0.35, 3.0, Soft)) * BottomF;
        }
        return smoothstep(0.0, max(Soft, 0.01), Margin) * BottomF;
    }

    float Margin = saturate(1.0 - DistNorm);
    OutMargin = Margin;
    if (Margin <= 0.0) return 0.0;

    if (DensityFalloff)
    {
        return pow(smoothstep(0.0, 1.0, Margin), lerp(0.35, 3.0, Soft));
    }
    return smoothstep(0.0, max(Soft, 0.01), Margin);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  CELESTIAL SINGLE LOCAL FOG VOLUME DENSITY (lfDensity)
//------------------------------------------------------------------------------------------------------------------------

float EvaluateSingleLocalFogDensityGpu(
    VolumetricFogUniformBlock Fog,
    GpuLocalFogVolume         Vol,
    float3                    PointWorld,
    float                     TimeSec,
    out float                 OutHeightNorm)
{
    OutHeightNorm = 0.5;
    if (Vol.DensityCovScaleSides.x <= 1.0e-4) return 0.0;

    float Margin = 0.0;
    float Soft   = clamp(Vol.WindVelCurl.w, 0.01, 1.0);
    bool  Fall   = (Vol.AbsorbShadowFalloffSteps.z > 0.5);
    float Mask   = EvaluateFogVolumeShapeMaskGpu(
        PointWorld,
        Vol.CenterShape.xyz,
        Vol.HalfExtentYaw.xyz,
        uint(Vol.CenterShape.w + 0.5),
        Vol.HalfExtentYaw.w,
        uint(Vol.DensityCovScaleSides.w + 0.5),
        Soft,
        Fall,
        Margin,
        OutHeightNorm);
    if (Mask <= 1.0e-4) return 0.0;

    // Wind advection + gentle animated drift (Z-up)
    float3 AdvP = PointWorld;
    float2 WVel = Vol.WindVelCurl.xy;
    AdvP.xy -= WVel * TimeSec * 0.85;
    float  WLen = length(WVel);
    if (WLen > 0.5)
    {
        float3 SwirlC = AdvP * 0.08 + float3(TimeSec * 0.06, -TimeSec * 0.05, 0.0);
        float  Sx     = CelestialScalarNoise(SwirlC + float3(0.0, 0.4, 0.0)) - CelestialScalarNoise(SwirlC - float3(0.0, 0.4, 0.0));
        float  Sy     = CelestialScalarNoise(SwirlC + float3(0.4, 0.0, 0.0)) - CelestialScalarNoise(SwirlC - float3(0.4, 0.0, 0.0));
        AdvP.xy += float2(Sx, -Sy) * min(WLen * 0.08, 2.2);
    }

    float ScaleM = max(0.25, Vol.DensityCovScaleSides.z);
    float3 P = AdvP / ScaleM + float3(TimeSec * 0.04, TimeSec * 0.02, TimeSec * 0.015);
    float  N = CelestialScalarNoise(P) * 0.55
             + CelestialScalarNoise(P * 2.1 + float3(2.3, 4.7, 1.1)) * 0.30
             + CelestialScalarNoise(P * 4.4 + float3(6.1, 0.9, 3.8)) * 0.15;

    // Heterogeneity + cold-pool vertical gravity settling (exact Celestial Demo lfDensity)
    float Hetero = saturate(Vol.DensityCovScaleSides.y);
    float Mod    = lerp(1.0, smoothstep(0.18, 0.86, N) * 1.45, Hetero);
    float Grav   = 1.0 - 0.35 * OutHeightNorm;
    return Vol.DensityCovScaleSides.x * 0.16 * Mask * Mod * Grav;
}

float EvaluateFogMediumDensityAtPointGpu(VolumetricFogUniformBlock Fog, float3 WorldPoint)
{
    float Sigma = 0.0;
    if (Fog.HeightParams0.x > 0.5 && Fog.HeightParams0.y > 1.0e-6)
    {
        float SafeH = max(1.0, Fog.HeightParams0.z);
        float Rel   = (WorldPoint.z - Fog.HeightParams0.w) / SafeH;
        Sigma += clamp(Fog.HeightParams0.y, 0.0, 0.5) * exp(-clamp(Rel, -10.0, 40.0));
    }
    uint  LocalCount = min(8u, uint(Fog.FogMeta.x + 0.5));
    float TimeSec    = Fog.FogWindClock.w;
    for (uint I = 0u; I < LocalCount; ++I)
    {
        float Hn = 0.5;
        Sigma += EvaluateSingleLocalFogDensityGpu(Fog, Fog.LocalFogs[I], WorldPoint, TimeSec, Hn);
    }
    return Sigma;
}

//------------------------------------------------------------------------------------------------------------------------
//                           CELESTIAL ANALYTIC MEDIA (EXP² HEIGHT FOG + SPECTRAL AERIAL PERSPECTIVE)
//------------------------------------------------------------------------------------------------------------------------

float3 ApplyCelestialAnalyticMediaGpu(
    VolumetricFogUniformBlock Fog,
    float3                    BackgroundHdr,
    float3                    RayOrigin,
    float3                    RayDir,
    float                     DistanceM,
    float3                    SunDir,
    float3                    SolarIrrad,
    float3                    SkyAmbient,
    float3                    LowBandGlow,
    out float3                OutTransRgb)
{
    OutTransRgb = float3(1.0, 1.0, 1.0);
    float SafeDist = max(0.0, DistanceM);
    if (SafeDist <= 1.0e-3) return BackgroundHdr;

    float  CosS  = dot(RayDir, SunDir);
    float  SunUp = saturate(SunDir.z * 4.0 + 0.15);
    float3 SunL  = SolarIrrad * SunUp;

    // 1. Exponential-squared Height Fog (exact Celestial Demo formula, Z-up)
    float  Tf = 1.0;
    float3 Lf = float3(0.0, 0.0, 0.0);
    if (Fog.HeightParams0.x > 0.5 && Fog.HeightParams0.y > 1.0e-6)
    {
        float Z0 = RayOrigin.z;
        float Z1 = RayOrigin.z + RayDir.z * SafeDist;
        float Od = clamp(Fog.HeightParams0.y, 0.0, 0.5) * SafeDist *
                   EvaluateExpHeightIntegralGpu(Fog.HeightParams0.z, Z0, Z1, Fog.HeightParams0.w);

        if (Fog.FogWindClock.x > 0.5)
        {
            float  TimeS = Fog.FogWindClock.w;
            float3 RefP  = RayOrigin + RayDir * min(SafeDist * 0.35, 80.0);
            float  Drift = 0.88 + 0.24 * CelestialScalarNoise(float3(
                (RefP.x - Fog.FogWindClock.y * TimeS) * 0.03,
                (RefP.y - Fog.FogWindClock.z * TimeS) * 0.03,
                TimeS * 0.15));
            Od *= Drift;
        }

        Tf = exp(-Od * Od);
        float  FogPh  = min(CelestialPhaseHG(CosS, 0.55) * (4.0 * FrontierPi), 6.0);
        float  SunMul = max(0.0, Fog.HeightParams1.w);
        float3 FogLit = Fog.HeightParams1.xyz * (SkyAmbient * 0.48 + LowBandGlow * 0.20 + SunL * FogPh * 0.22 * SunMul);
        Lf = FogLit;
    }

    // 2. True Spectral RGB Rayleigh/Mie Atmospheric Aerial Perspective (exact Celestial Demo formula, Z-up)
    float3 Ta = float3(1.0, 1.0, 1.0);
    float3 La = float3(0.0, 0.0, 0.0);
    if (Fog.AerialParams.x > 0.5 && Fog.AerialParams.y > 1.0e-5)
    {
        float StartM = max(0.0, Fog.AerialParams.z);
        float D2     = max(0.0, SafeDist - StartM);
        if (D2 > 1.0e-3)
        {
            float  Z0      = RayOrigin.z + RayDir.z * StartM;
            float  Z1      = RayOrigin.z + RayDir.z * SafeDist;
            float  ScaleH  = max(50.0, Fog.FogMeta.y);
            float  Beta0   = Fog.AerialParams.y * 0.000060;
            float  OdA     = Beta0 * D2 * EvaluateExpHeightIntegralGpu(ScaleH, Z0, Z1, 0.0);
            float  MieB    = saturate(Fog.AerialParams.w);
            float3 SpecB   = lerp(float3(5.8, 13.5, 33.1) / 13.5, float3(1.0, 1.0, 1.0), MieB);
            Ta             = exp(-SpecB * OdA);

            float  AnisoG  = clamp(Fog.FogMeta.z, 0.0, 0.95);
            float  SkyGain = max(0.0, Fog.FogMeta.w);
            float  PhRay   = 0.75 * (1.0 + CosS * CosS);
            float  PhMie   = min(CelestialPhaseHG(CosS, AnisoG) * (4.0 * FrontierPi), 8.0);
            float3 AerLit  = (SkyAmbient * 0.48 * SkyGain + LowBandGlow * 0.28 * SkyGain) * lerp(1.0, 0.8, MieB)
                           + SunL * lerp(PhRay * 0.18, PhMie * 0.32, MieB);
            La = AerLit * Fog.AerialTintPad.xyz;
        }
    }

    // Symmetric two-medium RGB composite (exact Celestial Demo applyMedia)
    OutTransRgb = Ta * Tf;
    float3 InScatter = La * (float3(1.0, 1.0, 1.0) - Ta) * lerp(1.0, Tf, 0.5)
                     + Lf * (1.0 - Tf) * lerp(float3(1.0, 1.0, 1.0), Ta, 0.5);
    return BackgroundHdr * OutTransRgb + InScatter;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  STANDALONE VOLUMETRIC FOG ENTRYPOINT (BACKWARD COMPAT)
//------------------------------------------------------------------------------------------------------------------------

float3 EvaluateVolumetricFogGpu(
    VolumetricFogUniformBlock Fog,
    float3                    RayOrigin,
    float3                    RayDir,
    float                     MaxDistM,
    float3                    SunDir,
    float3                    SolarIrrad,
    float3                    SkyColor,
    float2                    PixelCoord,
    out float                 OutTransmittance)
{
    float3 TransRgb = float3(1.0, 1.0, 1.0);
    float3 Analytic = ApplyCelestialAnalyticMediaGpu(
        Fog,
        float3(0.0, 0.0, 0.0),
        RayOrigin,
        RayDir,
        MaxDistM,
        SunDir,
        SolarIrrad,
        SkyColor,
        SkyColor,
        TransRgb);

    OutTransmittance = clamp(dot(TransRgb, float3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
    float3 InScatter = Analytic;

    // March local fog volumes standalone when invoked outside the unified local media path
    uint  LocalCount = min(8u, uint(Fog.FogMeta.x + 0.5));
    if (LocalCount == 0u || MaxDistM <= 1.0e-3) return InScatter;

    float  TimeSec = Fog.FogWindClock.w;
    float  Jitter  = CelestialPixelJitter(PixelCoord, TimeSec);
    float  CosS    = dot(RayDir, SunDir);
    float  SunUp   = saturate(SunDir.z * 4.0 + 0.15);
    float3 SunL    = SolarIrrad * SunUp;
    float3 InvD    = 1.0 / select(abs(RayDir) > float3(1.0e-6), RayDir, float3(1.0e-6));

    float TMin = 1.0e9;
    float TMax = 0.0;
    uint  Steps = 24u;
    for (uint I = 0u; I < LocalCount; ++I)
    {
        GpuLocalFogVolume Vol = Fog.LocalFogs[I];
        if (Vol.DensityCovScaleSides.x <= 1.0e-4) continue;
        float3 Center = Vol.CenterShape.xyz;
        float3 HalfE  = Vol.HalfExtentYaw.xyz;
        float  MaxXY  = max(HalfE.x, HalfE.y) * 1.45;
        uint   ShapeI = uint(Vol.CenterShape.w + 0.5);
        float  MinZ   = (ShapeI == 7u) ? (0.14 * HalfE.z) : HalfE.z;
        float3 BMin   = Center - float3(MaxXY, MaxXY, MinZ);
        float3 BMax   = Center + float3(MaxXY, MaxXY, HalfE.z);
        float3 T0     = (BMin - RayOrigin) * InvD;
        float3 T1     = (BMax - RayOrigin) * InvD;
        float  TE     = max(0.05, max(min(T0.x, T1.x), max(min(T0.y, T1.y), min(T0.z, T1.z))));
        float  TX     = min(MaxDistM, min(max(T0.x, T1.x), min(max(T0.y, T1.y), max(T0.z, T1.z))));
        if (TX > TE)
        {
            TMin  = min(TMin, TE);
            TMax  = max(TMax, TX);
            Steps = max(Steps, clamp(uint(Vol.AbsorbShadowFalloffSteps.w + 0.5), 12u, 48u));
        }
    }
    if (TMax <= TMin) return InScatter;

    float  Dt   = (TMax - TMin) / float(Steps);
    float  T    = TMin + Dt * Jitter;
    float3 Acc  = float3(0.0, 0.0, 0.0);
    float  Tr   = 1.0;

    for (uint S = 0u; S < 48u; ++S)
    {
        if (S >= Steps || Tr < 0.015) break;
        float3 P = RayOrigin + RayDir * T;
        float  SumSigT = 0.0;
        float3 SumSc   = float3(0.0, 0.0, 0.0);

        for (uint I = 0u; I < LocalCount; ++I)
        {
            GpuLocalFogVolume Vol = Fog.LocalFogs[I];
            float Hn = 0.5;
            float Rho = EvaluateSingleLocalFogDensityGpu(Fog, Vol, P, TimeSec, Hn);
            if (Rho <= 1.0e-5) continue;

            float Sh = 1.0;
            if (Vol.AbsorbShadowFalloffSteps.y > 0.5)
            {
                float Od = 0.0;
                float St = max(Vol.HalfExtentYaw.x, max(Vol.HalfExtentYaw.y, Vol.HalfExtentYaw.z)) * 0.45;
                [unroll]
                for (uint K = 1u; K <= 4u; ++K)
                {
                    float FK  = float(K);
                    float D   = St * FK * 0.38;
                    float THn = 0.5;
                    Od += EvaluateSingleLocalFogDensityGpu(Fog, Vol, P + SunDir * D, TimeSec, THn) * D * 0.65;
                }
                Sh = exp(-Od * (1.0 + max(0.0, Vol.AbsorbShadowFalloffSteps.x)));
            }

            float  Ph  = CelestialPhaseHG(CosS, Vol.TintAniso.w) * (4.0 * FrontierPi);
            float  Ms  = Sh + 0.45 * sqrt(Sh);
            float3 Li  = SunL * (Ph * Ms * 0.70) + SkyColor * (0.65 + 0.35 * Hn);
            float  SigA = max(0.0, Vol.AbsorbShadowFalloffSteps.x);
            SumSigT += Rho * (1.0 + SigA);
            SumSc   += Li * Vol.TintAniso.xyz * Rho;
        }

        if (SumSigT > 1.0e-5)
        {
            float Ts = exp(-SumSigT * Dt);
            Acc += Tr * (SumSc - SumSc * Ts) / max(SumSigT, 1.0e-6);
            Tr  *= Ts;
        }
        T += Dt;
    }

    InScatter        += OutTransmittance * Acc;
    OutTransmittance *= Tr;
    return InScatter;
}

#ifndef FRONTIER_ATMOSPHERE_EMBEDDED
[[vk::binding(0, 0)]] ConstantBuffer<VolumetricFogUniformBlock> FogUniforms;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>               OutFogRgba;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 Tid : SV_DispatchThreadID)
{
    uint W = max(1u, uint(FogUniforms.FogMeta.z + 0.5));
    uint H = max(1u, uint(FogUniforms.FogMeta.w + 0.5));
    if (Tid.x >= W || Tid.y >= H) return;
    float  Trans = 1.0;
    float3 Dir   = normalize(float3(float(Tid.x) / float(W) - 0.5, 1.0, float(Tid.y) / float(H) - 0.5));
    float3 FogC  = EvaluateVolumetricFogGpu(
        FogUniforms,
        float3(0.0, 0.0, 2.0),
        Dir,
        600.0,
        normalize(float3(0.4, 0.6, 0.68)),
        float3(1.0, 0.95, 0.88),
        float3(0.28, 0.42, 0.65),
        float2(Tid.xy),
        Trans);
    OutFogRgba[Tid.y * W + Tid.x] = float4(FogC, Trans);
}
#endif
