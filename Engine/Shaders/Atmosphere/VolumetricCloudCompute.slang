/*============================================================================================================================================
                                                       VOLUMETRICCLOUDCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Celestial GPU Volumetric Clouds (Global Spherical Shell) & Unified Local Media (LocalClouds + LocalFogs + Shared Shadow + Lamp Scatter)

#ifndef FRONTIER_PLANETARY_CLOUD_AERIAL
#include "CloudAerialPerspective.slang"
#endif

struct GpuLocalCloudVolume
{
    float4 CenterShape;           // xyz = CenterWorld, w = Shape (0=Box..7=HalfSphere)
    float4 HalfExtentYaw;         // xyz = HalfExtentM, w = YawRad
    float4 DensityCovScaleSides;  // x = Extinction, y = Fraction, z = FeatureScaleMeters, w = float(PrismSides)
    float4 WindVelCurl;           // xyz = ResolvedWindVelMps, w = EdgeSoftness
    float4 FormErosionAnisoSteps; // x = Form (0=Stratiform,1=Cumuliform,2=Wispy), y = ErosionDetail, z = ForwardAnisoG, w = MarchSteps
};

struct VolumetricCloudUniformBlock
{
    float4              OriginTimeVector;       // xyz = CameraOriginWorld, w = SimulationSeconds
    float4              SunAxisToggle;          // xyz = SunDirection, w = CloudsEnabled (0/1)
    float4              SolarIrradianceExt;     // xyz = CloudSolarIrradiance, w = PlanetRadiusM
    float4              DeckGeometry;           // x = FloorM, y = ThicknessM, z = CloudFraction, w = Extinction
    float4              FormationOptics;        // x = Category (0..12), y = FeatureScaleMultiplier, z = ForwardPhaseG, w = LocalCloudCount (0..8)
    float4              WindVectorOcclude;      // xy = WindVelMps, z = CloudShadowsEnabled, w = CloudShadowStrength
    float4              CloudPassControl;       // x = CloudWidth, y = CloudHeight, z = CloudMarchSteps, w = LightMarchTaps
    float4              WindForceMeta;          // x = ShearPerKm, y = VeerRadPerKm, z = Turbulence, w = ObserverAltitudeMeters
    float4              CloudAtmoStrengths;     // x = RayleighStr, y = MieStr, z = OzoneStr, w = AtmosphereThicknessM
    float4              CloudAirScalesCyclone;  // x = RayleighScaleH, y = MieScaleH, z = AnvilTopSpread, w = SkyAmbientGain
    float4              CloudPhaseAbsorbPowder; // x = BackPhaseG, y = PhaseMix, z = AbsorptionRatio, w = PowderStrength
    float4              CloudAlbedoErosion;     // xyz = CloudAlbedoLinear, w = ErosionDetail
    float4              CloudSkyAmbient;        // xyz = LiveHemisphericalSkyAmbient, w = Reserved
    GpuLocalCloudVolume LocalClouds[8];
};

CloudSpecification BuildCloudSpecificationGpu(
    VolumetricCloudUniformBlock Cloud,
    float3                      SkyAmbientOverride,
    uint                        FormationOverride = 255u)
{
    CloudSpecification Spec;
    Spec.RadiusM             = max(500000.0, Cloud.SolarIrradianceExt.w);
    Spec.FloorM              = Cloud.DeckGeometry.x;
    Spec.ThicknessM          = max(50.0, Cloud.DeckGeometry.y);
    Spec.Fraction            = saturate(Cloud.DeckGeometry.z);
    Spec.Extinction          = max(0.0, Cloud.DeckGeometry.w);
    Spec.FeatureScale        = max(0.30, Cloud.FormationOptics.y);
    Spec.Anvil               = saturate(Cloud.CloudAirScalesCyclone.z);
    Spec.PhaseG              = clamp(Cloud.FormationOptics.z, 0.0, 0.95);
    Spec.BackPhaseG          = clamp(Cloud.CloudPhaseAbsorbPowder.x, 0.0, 0.90);
    Spec.PhaseMix            = clamp(Cloud.CloudPhaseAbsorbPowder.y, 0.0, 1.0);
    Spec.Absorption          = max(0.0, Cloud.CloudPhaseAbsorbPowder.z);
    Spec.SkyAmbientGain      = max(0.0, Cloud.CloudAirScalesCyclone.w);
    Spec.PowderStrength      = saturate(Cloud.CloudPhaseAbsorbPowder.w);
    Spec.ErosionDetail       = saturate(Cloud.CloudAlbedoErosion.w);
    Spec.CloudAlbedo         = max(float3(0.0), Cloud.CloudAlbedoErosion.xyz);
    Spec.Seed                = 0.0;
    Spec.PixelAngle          = 1.0 / max(240.0, Cloud.CloudPassControl.y);
    Spec.UniformExtinction   = -1.0;
    Spec.TimeSeconds         = Cloud.OriginTimeVector.w;
    Spec.WindVelocityMps     = Cloud.WindVectorOcclude.xy;
    Spec.Shear               = Cloud.WindForceMeta.x;
    Spec.Veer                = Cloud.WindForceMeta.y;
    Spec.Turbulence          = Cloud.WindForceMeta.z;
    Spec.CycloneSpeedMps     = 0.0;
    Spec.CycloneEyeM         = 80000.0;
    Spec.CycloneAxis         = float3(0.0, 0.0, 1.0);
    Spec.SunDirection        = normalize(Cloud.SunAxisToggle.xyz);
    Spec.SolarIrradiance     = Cloud.SolarIrradianceExt.xyz;
    Spec.SkyAmbient          = SkyAmbientOverride;
    Spec.AtmosphereStrengths = max(float3(0.0), Cloud.CloudAtmoStrengths.xyz);
    Spec.AirScaleHeightsM    = float2(max(400.0, Cloud.CloudAirScalesCyclone.x), max(200.0, Cloud.CloudAirScalesCyclone.y));
    Spec.Steps               = clamp(uint(Cloud.CloudPassControl.z + 0.5), 16u, 64u);
    Spec.LightSteps          = clamp(uint(Cloud.CloudPassControl.w + 0.5), 3u, 5u);
    Spec.Formation           = (FormationOverride < 13u) ? FormationOverride : min(12u, uint(Cloud.FormationOptics.x + 0.5));
    Spec.UseClouds           = (Cloud.SunAxisToggle.w > 0.5);
    Spec.UseCloudShadows     = Spec.UseClouds && (Cloud.WindVectorOcclude.z > 0.5);
    return Spec;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      PLANETARY CLOUD DECK EVALUATION (GROUND & ORBITAL)
//------------------------------------------------------------------------------------------------------------------------

float4 EvaluatePlanetaryCloudsOnlyGpu(
    VolumetricCloudUniformBlock Cloud,
    float3                      RayOriginWorld,
    float3                      RayDir,
    float2                      PixelCoord,
    float                       MaxDistanceM,
    float3                      SkyAmbientLive)
{
    if (Cloud.SunAxisToggle.w < 0.5 || Cloud.DeckGeometry.z <= 0.001 || Cloud.DeckGeometry.w <= 0.001)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    CloudSpecification Spec = BuildCloudSpecificationGpu(Cloud, SkyAmbientLive);
    float  ObsAltM          = max(0.0, Cloud.WindForceMeta.w);
    float3 PlanetOrigin     = float3(RayOriginWorld.x, RayOriginWorld.y, Spec.RadiusM + ObsAltM + RayOriginWorld.z);
    float  GroundM          = PlanetCloudGroundDistance(PlanetOrigin, RayDir, Spec.RadiusM);
    float  LimitM           = min(MaxDistanceM, GroundM);

    CloudTransportIntegral Integral = TracePlanetaryClouds(Spec, PlanetOrigin, RayDir, PixelCoord, LimitM);

    // Orbital space viewpoints (> 80 km altitude) keep Rayleigh air limb over planetary clouds
    float AltitudeM = length(PlanetOrigin) - Spec.RadiusM;
    float3 FinalRad = (AltitudeM > 80000.0)
        ? CloudRadianceThroughAir(Spec, PlanetOrigin, RayDir, Integral, 12u)
        : Integral.Radiance;
    return float4(max(float3(0.0), FinalRad), clamp(Integral.Transmittance, 0.0, 1.0));
}

//------------------------------------------------------------------------------------------------------------------------
//                                  CELESTIAL SINGLE LOCAL CLOUD VOLUME DENSITY (lcDensity)
//------------------------------------------------------------------------------------------------------------------------

float EvaluateLocalCloudShapeMaskGpu(
    float3 PointWorld,
    float3 CenterWorld,
    float3 HalfExt,
    uint   Shape,
    float  YawRad,
    uint   PrismSides,
    float  EdgeSoftness,
    out float OutHeightNorm)
{
    float3 SafeH = max(HalfExt, float3(0.2, 0.2, 0.2));
    float3 Rel   = PointWorld - CenterWorld;
    float  C     = cos(-YawRad);
    float  S     = sin(-YawRad);
    float3 Local = float3(C * Rel.x - S * Rel.y, S * Rel.x + C * Rel.y, Rel.z);

    float  Soft  = clamp(EdgeSoftness, 0.02, 1.0);
    float3 U     = Local / SafeH;
    OutHeightNorm = clamp(0.5 * (U.z + 1.0), 0.0, 1.0);

    float DistNorm = 1.0e6;
    if (Shape == 0u) // Box
    {
        DistNorm = max(abs(U.x), max(abs(U.y), abs(U.z)));
    }
    else if (Shape == 1u) // Sphere
    {
        float MinExt = min(SafeH.x, min(SafeH.y, SafeH.z));
        DistNorm = length(Local) / max(0.2, MinExt);
    }
    else if (Shape == 2u) // Ellipsoid
    {
        DistNorm = length(U);
    }
    else if (Shape == 3u) // Cylinder
    {
        DistNorm = max(length(U.xy), abs(U.z));
    }
    else if (Shape == 4u) // Cone
    {
        float Taper = clamp(0.5 * (1.0 - U.z), 0.0, 1.0);
        float Rad   = length(U.xy);
        DistNorm    = max(abs(U.z), (Taper > 1.0e-3) ? (Rad / Taper) : (Rad > 1.0e-4 ? 2.0 : 1.0));
    }
    else if (Shape == 5u) // Diamond
    {
        DistNorm = abs(U.x) + abs(U.y) + abs(U.z);
    }
    else if (Shape == 6u) // N-sided Prism
    {
        uint  Sides  = clamp(PrismSides, 3u, 16u);
        float Sector = (2.0 * CloudPiConst) / float(Sides);
        float Angle  = atan2(U.y, U.x);
        float ModA   = fmod(Angle + 4.0 * CloudPiConst + 0.5 * Sector, Sector) - 0.5 * Sector;
        float PolyR  = (length(U.xy) * cos(ModA)) / max(0.25, cos(0.5 * Sector));
        DistNorm     = max(PolyR, abs(U.z));
    }
    else // Shape == 7u: HalfSphere
    {
        if (U.z < -0.14) return 0.0;
        float BottomF = smoothstep(-0.14, 0.06, U.z);
        float RadialD = length(float3(U.x, U.y, max(0.0, U.z)));
        OutHeightNorm = clamp(max(0.0, U.z), 0.0, 1.0);
        float Margin  = saturate(1.0 - RadialD);
        return smoothstep(0.0, max(Soft, 0.02), Margin) * BottomF;
    }

    float Margin = saturate(1.0 - DistNorm);
    if (Margin <= 0.0) return 0.0;
    return smoothstep(0.0, max(Soft, 0.02), Margin);
}

float EvaluateSingleLocalCloudDensityGpu(
    GpuLocalCloudVolume Vol,
    float3              PointWorld,
    float               TimeSec,
    bool                ApplyDetailErosion,
    out float           OutHeightNorm)
{
    OutHeightNorm = 0.5;
    if (Vol.DensityCovScaleSides.x <= 1.0e-4 || Vol.DensityCovScaleSides.y <= 1.0e-4) return 0.0;

    float Soft = clamp(Vol.WindVelCurl.w, 0.02, 1.0);
    float Mask = EvaluateLocalCloudShapeMaskGpu(
        PointWorld,
        Vol.CenterShape.xyz,
        Vol.HalfExtentYaw.xyz,
        uint(Vol.CenterShape.w + 0.5),
        Vol.HalfExtentYaw.w,
        uint(Vol.DensityCovScaleSides.w + 0.5),
        Soft,
        OutHeightNorm);
    if (Mask <= 1.0e-4) return 0.0;

    // Form vertical profile (exact Celestial Demo lcDensity: 0=Stratiform, 1=Cumuliform, 2=Wispy)
    uint  Form = min(2u, uint(Vol.FormErosionAnisoSteps.x + 0.5));
    float Prof = 1.0;
    if (Form == 0u) // Stratiform
    {
        Prof = smoothstep(0.0, 0.15, OutHeightNorm) * (1.0 - smoothstep(0.75, 1.0, OutHeightNorm));
    }
    else if (Form == 1u) // Cumuliform
    {
        Prof = smoothstep(0.0, 0.08, OutHeightNorm) * (1.0 - smoothstep(0.35, 1.0, OutHeightNorm)) * 1.25;
    }
    else // Wispy
    {
        Prof = smoothstep(0.0, 0.30, OutHeightNorm) * (1.0 - smoothstep(0.55, 1.0, OutHeightNorm)) * 0.55;
    }
    if (Prof <= 1.0e-4) return 0.0;

    // Wind advection + curl swirl (Z-up)
    float3 AdvP = PointWorld;
    float2 WVel = Vol.WindVelCurl.xy;
    AdvP.xy -= WVel * TimeSec * 0.85;
    float  WLen = length(WVel);
    if (WLen > 0.5)
    {
        float3 SwirlC = AdvP * 0.045 + float3(TimeSec * 0.05, -TimeSec * 0.04, 0.0);
        float  Sx     = CelestialScalarNoise(SwirlC + float3(0.0, 0.5, 0.0)) - CelestialScalarNoise(SwirlC - float3(0.0, 0.5, 0.0));
        float  Sy     = CelestialScalarNoise(SwirlC + float3(0.5, 0.0, 0.0)) - CelestialScalarNoise(SwirlC - float3(0.5, 0.0, 0.0));
        AdvP.xy += float2(Sx, -Sy) * min(WLen * 0.12, 3.5);
    }

    // Exact Celestial Demo 4-octave local cloud shape + 2-octave height-modulated erosion
    float ScaleM = max(0.5, Vol.DensityCovScaleSides.z);
    float3 S     = (AdvP - Vol.CenterShape.xyz) / ScaleM + float3(TimeSec * 0.015, TimeSec * 0.008, 0.0);
    float ShapeN = CelestialScalarNoise(S) * 0.50
                 + CelestialScalarNoise(S * 2.02 + float3(3.1, 9.2, 1.7)) * 0.25
                 + CelestialScalarNoise(S * 4.10 + float3(7.7, 1.1, 2.2)) * 0.125
                 + CelestialScalarNoise(S * 8.30 + float3(1.3, 4.4, 8.8)) * 0.0625;
    ShapeN /= 0.9375;

    float Cov  = clamp(Vol.DensityCovScaleSides.y, 0.01, 1.0);
    float LocalDensity = saturate((ShapeN - (1.0 - Cov)) / max(1.0e-3, Cov)) * Prof * Mask;
    if (LocalDensity <= 1.0e-4) return 0.0;

    if (ApplyDetailErosion)
    {
        float ErodeK = saturate(Vol.FormErosionAnisoSteps.y);
        float Det    = CelestialScalarNoise(S * 9.0 + float3(TimeSec * 0.02)) * 0.60
                     + CelestialScalarNoise(S * 19.0 + float3(5.0, 5.0, 5.0)) * 0.40;
        float Erode  = lerp(Det, 1.0 - Det, saturate(OutHeightNorm * 3.0)) * ErodeK * 0.45;
        LocalDensity = saturate(LocalDensity - Erode * (1.0 - LocalDensity));
    }

    return LocalDensity * Vol.DensityCovScaleSides.x * 0.14;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  LEGACY WRAPPER & STANDALONE CLOUD COMPUTE ENTRYPOINT
//------------------------------------------------------------------------------------------------------------------------

float4 EvaluateLocalCloudsGpu(
    VolumetricCloudUniformBlock Cloud,
    float3                      RayOrigin,
    float3                      RayDir,
    float                       MaxDistanceM,
    float2                      PixelCoord,
    float3                      SkyAmbientLive)
{
    uint VolCount = min(8u, uint(Cloud.FormationOptics.w + 0.5));
    if (VolCount == 0u || MaxDistanceM <= 0.1) return float4(0.0, 0.0, 0.0, 1.0);

    float3 SunDir   = normalize(Cloud.SunAxisToggle.xyz);
    float3 SunL     = Cloud.SolarIrradianceExt.xyz * saturate(SunDir.z * 4.0 + 0.15);
    float  CosS     = dot(RayDir, SunDir);
    float  BackG    = clamp(Cloud.CloudPhaseAbsorbPowder.x, 0.0, 0.90);
    float  PhaseMix = clamp(Cloud.CloudPhaseAbsorbPowder.y, 0.0, 1.0);
    float  SigA     = max(0.0, Cloud.CloudPhaseAbsorbPowder.z);
    float  PowderK  = saturate(Cloud.CloudPhaseAbsorbPowder.w);
    float  AmbGain  = max(0.0, Cloud.CloudAirScalesCyclone.w);
    float3 ClAlbedo = max(float3(0.0), Cloud.CloudAlbedoErosion.xyz);
    float  TimeSec  = Cloud.OriginTimeVector.w;
    float  Jitter   = CelestialPixelJitter(PixelCoord, TimeSec);
    float3 InvD     = 1.0 / select(abs(RayDir) > float3(1.0e-6), RayDir, float3(1.0e-6));

    float3 Acc   = float3(0.0, 0.0, 0.0);
    float  Trans = 1.0;

    for (uint I = 0u; I < VolCount; ++I)
    {
        GpuLocalCloudVolume Vol = Cloud.LocalClouds[I];
        if (Vol.DensityCovScaleSides.x <= 1.0e-4 || Vol.DensityCovScaleSides.y <= 1.0e-4) continue;
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
        float  TX     = min(MaxDistanceM, min(max(T0.x, T1.x), min(max(T0.y, T1.y), max(T0.z, T1.z))));
        if (TX <= TE) continue;

        uint  Steps = clamp(uint(Vol.FormErosionAnisoSteps.w + 0.5), 16u, 48u);
        float Dt    = (TX - TE) / float(Steps);
        float G1    = clamp(Vol.FormErosionAnisoSteps.z, 0.0, 0.95);
        float Ph    = lerp(CelestialPhaseHG(CosS, G1), CelestialPhaseHG(CosS, -BackG), PhaseMix) * (4.0 * CloudPiConst);
        float St    = max(0.5, max(HalfE.x, max(HalfE.y, HalfE.z)) * 0.45);

        for (uint S = 0u; S < 48u; ++S)
        {
            if (S >= Steps || Trans < 0.015) break;
            float  T   = TE + (float(S) + Jitter) * Dt;
            float3 P   = RayOrigin + RayDir * T;
            float  Hn  = 0.5;
            float  Rho = EvaluateSingleLocalCloudDensityGpu(Vol, P, TimeSec, true, Hn);
            if (Rho > 1.0e-5)
            {
                float Od = 0.0;
                [unroll]
                for (uint K = 1u; K <= 4u; ++K)
                {
                    float FK = float(K);
                    float D  = St * FK * FK * 0.14;
                    float TH = 0.5;
                    Od += EvaluateSingleLocalCloudDensityGpu(Vol, P + SunDir * D, TimeSec, false, TH) * D * 0.70;
                }
                float  Sh     = exp(-Od * (1.0 + SigA));
                float  Ms     = Sh + 0.55 * sqrt(Sh) + 0.30 * pow(max(1.0e-6, Sh), 0.25);
                float  Powder = 1.0 - PowderK * exp(-Rho * Dt * 2.5 * (1.0 + SigA)) * (1.0 - 0.5 * saturate(CosS));
                float3 Li     = SunL * (Ph * Ms * Powder) + SkyAmbientLive * AmbGain * lerp(0.35, 1.0, Hn);
                float  SigT   = Rho * (1.0 + SigA);
                float  Ts     = exp(-SigT * Dt);
                float3 Sc     = Li * ClAlbedo * Rho;
                Acc   += Trans * (Sc - Sc * Ts) / max(SigT, 1.0e-6);
                Trans *= Ts;
            }
        }
    }

    return float4(max(float3(0.0), Acc), clamp(Trans, 0.0, 1.0));
}

#ifndef FRONTIER_ATMOSPHERE_EMBEDDED
[[vk::binding(0, 0)]] ConstantBuffer<VolumetricCloudUniformBlock> CloudUniforms;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>                 OutCloudRgba;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 Tid : SV_DispatchThreadID)
{
    uint W = max(1u, uint(CloudUniforms.CloudPassControl.x + 0.5));
    uint H = max(1u, uint(CloudUniforms.CloudPassControl.y + 0.5));
    if (Tid.x >= W || Tid.y >= H) return;
    float3 Dir = normalize(float3(float(Tid.x) / float(W) - 0.5, 1.0, float(Tid.y) / float(H) - 0.5));
    OutCloudRgba[Tid.y * W + Tid.x] = EvaluatePlanetaryCloudsOnlyGpu(
        CloudUniforms,
        CloudUniforms.OriginTimeVector.xyz,
        Dir,
        float2(Tid.xy),
        1.0e7,
        CloudUniforms.CloudSkyAmbient.xyz);
}
#endif
