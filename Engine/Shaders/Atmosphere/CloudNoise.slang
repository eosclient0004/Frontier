/*==========================================================================================================================================
                                                              CLOUDNOISE.SLANG
==========================================================================================================================================*/
// 🧩 Celestial volumetric noise, Henyey-Greenstein scattering phase, and unbounded lattice utilities (single-source Slang module).

#ifndef FRONTIER_PLANETARY_CLOUD_NOISE
#define FRONTIER_PLANETARY_CLOUD_NOISE 1
#include "../Shared/ShaderMathConstants.slang"


//------------------------------------------------------------------------------------------------------------------------
//                                           CELESTIAL VALUE NOISE & PHASE KERNELS
//------------------------------------------------------------------------------------------------------------------------

float CelestialHash13(float3 Coord)
{
    float3 P = frac(Coord * 0.1031);
    P += dot(P, P.zyx + 31.32);
    return frac((P.x + P.y) * P.z);
}

float CelestialScalarNoise(float3 Coord)
{
    float3 Cell = floor(Coord);
    float3 Fr   = frac(Coord);
    Fr          = Fr * Fr * (3.0 - 2.0 * Fr);

    float N000 = CelestialHash13(Cell + float3(0.0, 0.0, 0.0));
    float N100 = CelestialHash13(Cell + float3(1.0, 0.0, 0.0));
    float N010 = CelestialHash13(Cell + float3(0.0, 1.0, 0.0));
    float N110 = CelestialHash13(Cell + float3(1.0, 1.0, 0.0));
    float N001 = CelestialHash13(Cell + float3(0.0, 0.0, 1.0));
    float N101 = CelestialHash13(Cell + float3(1.0, 0.0, 1.0));
    float N011 = CelestialHash13(Cell + float3(0.0, 1.0, 1.0));
    float N111 = CelestialHash13(Cell + float3(1.0, 1.0, 1.0));

    return lerp(
        lerp(lerp(N000, N100, Fr.x), lerp(N010, N110, Fr.x), Fr.y),
        lerp(lerp(N001, N101, Fr.x), lerp(N011, N111, Fr.x), Fr.y),
        Fr.z);
}

float CelestialPhaseHG(float CosTheta, float AnisotropyG)
{
    float G   = clamp(AnisotropyG, -0.95, 0.95);
    float G2  = G * G;
    float Den = max(1.0e-4, 1.0 + G2 - 2.0 * G * CosTheta);
    return (1.0 - G2) / (4.0 * FrontierPi * pow(Den, 1.5));
}

float CelestialPixelJitter(float2 PixelCoord, float TimeSeconds)
{
    float Temporal = frac(TimeSeconds * 3.0);
    return CelestialHash13(float3(PixelCoord, Temporal));
}

//------------------------------------------------------------------------------------------------------------------------
//                                      COMPATIBILITY LATTICE NOISE (ORBITAL GLOBE)
//------------------------------------------------------------------------------------------------------------------------

uint CloudLatticeHash(int3 Cell)
{
    uint Word = uint(Cell.x) * 0x9e3779b1u ^ uint(Cell.y) * 0x85ebca77u ^ uint(Cell.z) * 0xc2b2ae3du;
    Word      = (Word ^ (Word >> 16u)) * 0x7feb352du;
    Word      = (Word ^ (Word >> 15u)) * 0x846ca68bu;
    return Word ^ (Word >> 16u);
}

float CloudLatticeCorner(int3 Cell)
{
    return float(CloudLatticeHash(Cell) >> 8u) / 16777216.0;
}

float3 CloudQuintic(float3 Fraction)
{
    return Fraction * Fraction * Fraction * (Fraction * (6.0 * Fraction - 15.0) + 10.0);
}

float CloudLatticeNoise(float3 Coordinate)
{
    int3   Cell     = int3(floor(Coordinate));
    float3 Fraction = CloudQuintic(Coordinate - float3(Cell));
    float  X00      = lerp(CloudLatticeCorner(Cell), CloudLatticeCorner(Cell + int3(1, 0, 0)), Fraction.x);
    float  X10      = lerp(CloudLatticeCorner(Cell + int3(0, 1, 0)), CloudLatticeCorner(Cell + int3(1, 1, 0)), Fraction.x);
    float  X01      = lerp(CloudLatticeCorner(Cell + int3(0, 0, 1)), CloudLatticeCorner(Cell + int3(1, 0, 1)), Fraction.x);
    float  X11      = lerp(CloudLatticeCorner(Cell + int3(0, 1, 1)), CloudLatticeCorner(Cell + int3(1, 1, 1)), Fraction.x);
    return lerp(lerp(X00, X10, Fraction.y), lerp(X01, X11, Fraction.y), Fraction.z);
}

float4 SampleCloudShape(float3 Coordinate, float FootprintM, float PeriodM)
{
    float N0 = CelestialScalarNoise(Coordinate * 4.0);
    float N1 = CelestialScalarNoise(Coordinate * 8.0 + float3(3.1, 1.7, 9.2));
    return float4(N0, N1, N0 * 0.7 + N1 * 0.3, 1.0);
}

float4 SampleCloudErosion(float3 Coordinate, float FootprintM, float PeriodM)
{
    float N0 = CelestialScalarNoise(Coordinate * 9.0);
    float N1 = CelestialScalarNoise(Coordinate * 19.0 + float3(5.0, 5.0, 5.0));
    return float4(N0 * 0.6 + N1 * 0.4, N1, N0, 1.0);
}

float CloudBlueJitter(float2 Pixel)
{
    if (Pixel.x < 0.0)
    {
        return clamp(Pixel.y, 0.0, 0.9999);
    }
    uint X = uint(floor(Pixel.x)) & 3u;
    uint Y = uint(floor(Pixel.y)) & 3u;
    uint B = ((X ^ Y) & 1u) * 8u + (Y & 1u) * 4u + (((X ^ Y) >> 1u) & 1u) * 2u + ((Y >> 1u) & 1u);
    return (float(B) + 0.5) * 0.0625;
}

#endif
