/*==========================================================================================================================================
                                                             CLOUDCOMPUTE.SLANG
==========================================================================================================================================*/
// 🧩 Single-source Slang entry compiled via slangc -target cpp for Frontier CPU clouds & cloud shadow queries.

[[vk::binding(1, 0)]] StructuredBuffer<uint> PlanetaryCloudNoise;
#include "CloudAerialPerspective.slang"

struct CloudCaptureFootprint
{
    float4 OriginTime;
    float4 ForwardWindX;
    float4 RightTanX;
    float4 UpTanY;
    float4 SunIntensity;
    float4 FloorThicknessFractionExtinction;
    float4 ScaleSeedPhaseExposure;
    float4 AirWindY;
    float4 WindDynamics;
    float4 Cyclone;
    float4 CycloneAxis;
    uint4  Selection;
    uint4  Size;
    uint4  Tile;
    uint4  Switches;
};

[[vk::binding(0, 0)]] ConstantBuffer<CloudCaptureFootprint> Capture;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4>            CloudPixels;
[[vk::binding(3, 0)]] StructuredBuffer<uint>                PlanetSurface;

CloudSpecification QueryCloudSpecification()
{
    CloudSpecification Specification;
    Specification.RadiusM             = Capture.Selection.x == 2u ? 1737400.0 : max(500000.0, Capture.ScaleSeedPhaseExposure.w);
    Specification.FloorM              = Capture.FloorThicknessFractionExtinction.x;
    Specification.ThicknessM          = Capture.FloorThicknessFractionExtinction.y;
    Specification.Fraction            = Capture.FloorThicknessFractionExtinction.z;
    Specification.Extinction          = Capture.FloorThicknessFractionExtinction.w;
    Specification.FeatureScale        = Capture.ScaleSeedPhaseExposure.x;
    Specification.Seed                = Capture.ScaleSeedPhaseExposure.y;
    Specification.PhaseG              = Capture.ScaleSeedPhaseExposure.z;
    Specification.BackPhaseG          = 0.30;
    Specification.PhaseMix            = 0.30;
    Specification.Absorption          = 0.05;
    Specification.SkyAmbientGain      = 0.90;
    Specification.PowderStrength      = 0.60;
    Specification.ErosionDetail       = 0.60;
    Specification.CloudAlbedo         = float3(1.0, 1.0, 1.0);
    Specification.PixelAngle          = 2.0 * Capture.UpTanY.w / max(1.0, float(Capture.Size.y));
    Specification.TimeSeconds         = Capture.OriginTime.w;
    Specification.UniformExtinction   = Capture.Switches.z != 0u ? Capture.Cyclone.z : -1.0;
    Specification.WindVelocityMps     = float2(Capture.ForwardWindX.w, Capture.AirWindY.w);
    Specification.Shear               = Capture.WindDynamics.x;
    Specification.Veer                = Capture.WindDynamics.y;
    Specification.Turbulence          = Capture.WindDynamics.z;
    Specification.Anvil               = Capture.WindDynamics.w;
    Specification.CycloneSpeedMps     = Capture.Cyclone.x;
    Specification.CycloneEyeM         = Capture.Cyclone.y;
    Specification.CycloneAxis         = normalize(Capture.CycloneAxis.xyz);
    Specification.SunDirection        = normalize(Capture.SunIntensity.xyz);
    Specification.SolarIrradiance     = float3(Capture.SunIntensity.w) * Capture.Cyclone.w;
    Specification.SkyAmbient          = float3(0.28, 0.42, 0.65) * (Capture.SunIntensity.w / 16.0);
    Specification.AtmosphereStrengths = Capture.Selection.x == 2u ? float3(0.0, 0.0, 0.0) : Capture.AirWindY.xyz;
    Specification.AirScaleHeightsM    = float2(8000.0, 1400.0);
    Specification.Formation           = Capture.Selection.w;
    Specification.Steps               = Capture.Selection.y;
    Specification.LightSteps          = Capture.Selection.z;
    Specification.UseClouds           = Capture.Selection.x != 2u && Capture.Switches.x != 0u;
    Specification.UseCloudShadows     = Specification.UseClouds && Capture.Switches.y != 0u;
    return Specification;
}

float4 IntegrateCloudCapture(
    CloudSpecification Specification,
    float3             Origin,
    float3             Direction,
    float2             Pixel)
{
    float                  GroundM = PlanetCloudGroundDistance(Origin, Direction, Specification.RadiusM);
    CloudTransportIntegral Cloud   = TracePlanetaryClouds(Specification, Origin, Direction, Pixel, GroundM);
    if (Capture.Tile.w == 1u) return float4(Cloud.Transmittance, Cloud.DistanceM, 0.0, 1.0);
    if (Capture.Tile.w == 3u) return float4(Cloud.Radiance, Cloud.Transmittance);
    float3 LayeredCloud = CloudRadianceThroughAir(Specification, Origin, Direction, Cloud, 12u);
    return float4(LayeredCloud, Cloud.Transmittance);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CloudCaptureEntry(uint3 Coordinate : SV_DispatchThreadID)
{
    if (Coordinate.x >= Capture.Size.x || Coordinate.y >= Capture.Tile.y) return;
    uint2              Pixel         = uint2(Coordinate.x, Coordinate.y + Capture.Tile.x);
    CloudSpecification Specification = QueryCloudSpecification();

    float3 Origin = Capture.OriginTime.xyz;
    Origin.z += Specification.RadiusM;
    float2 Ndc       = ((float2(Pixel) + float2(0.5, 0.5)) / float2(Capture.Size.xy) * 2.0 - 1.0) * float2(1.0, -1.0);
    float3 Direction = normalize(Capture.ForwardWindX.xyz +
                                 Ndc.x * Capture.RightTanX.w * Capture.RightTanX.xyz +
                                 Ndc.y * Capture.UpTanY.w * Capture.UpTanY.xyz);
    CloudPixels[Coordinate.y * Capture.Size.x + Coordinate.x] =
        IntegrateCloudCapture(Specification, Origin, Direction, float2(Pixel));
}
