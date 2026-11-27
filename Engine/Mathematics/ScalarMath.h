/*============================================================================================================================================
                                                                SCALARMATH.H
============================================================================================================================================*/
// 🧩 Canonical scalar, angular, alignment, and 3-lane array math primitives shared across every Frontier subsystem

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/EngineConstants.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Frontier::Mathematics
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 SCALAR & ANGULAR PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr float ClampScalar(float Sample, float LowerBound, float UpperBound) noexcept
{
    return std::clamp(Sample, LowerBound, UpperBound);
}

[[nodiscard]] constexpr double ClampScalar(double Sample, double LowerBound, double UpperBound) noexcept
{
    return std::clamp(Sample, LowerBound, UpperBound);
}

[[nodiscard]] constexpr float ClampUnit(float Sample) noexcept
{
    return std::clamp(Sample, 0.0f, 1.0f);
}

[[nodiscard]] constexpr double ClampUnit(double Sample) noexcept
{
    return std::clamp(Sample, 0.0, 1.0);
}

[[nodiscard]] inline float ClampFinite(float Sample, float LowerBound, float UpperBound, float Fallback) noexcept
{
    return std::isfinite(Sample) ? std::clamp(Sample, LowerBound, UpperBound) : Fallback;
}

[[nodiscard]] constexpr float InterpolateScalar(float Start, float Finish, float Factor) noexcept
{
    return Start + (Finish - Start) * Factor;
}

[[nodiscard]] constexpr double InterpolateScalar(double Start, double Finish, double Factor) noexcept
{
    return Start + (Finish - Start) * Factor;
}

[[nodiscard]] constexpr float EvaluateStep(float Edge, float Sample) noexcept
{
    return Sample < Edge ? 0.0f : 1.0f;
}

[[nodiscard]] constexpr float EvaluateSmoothStep(float Edge0, float Edge1, float Sample) noexcept
{
    const float Span = Edge1 - Edge0;
    if (Span == 0.0f)
    {
        return Sample < Edge0 ? 0.0f : 1.0f;
    }
    const float Normalized = std::clamp((Sample - Edge0) / Span, 0.0f, 1.0f);
    return Normalized * Normalized * (3.0f - 2.0f * Normalized);
}

[[nodiscard]] constexpr double EvaluateSmoothStep(double Edge0, double Edge1, double Sample) noexcept
{
    const double Span = Edge1 - Edge0;
    if (Span == 0.0)
    {
        return Sample < Edge0 ? 0.0 : 1.0;
    }
    const double Normalized = std::clamp((Sample - Edge0) / Span, 0.0, 1.0);
    return Normalized * Normalized * (3.0 - 2.0 * Normalized);
}

[[nodiscard]] inline float EvaluateFractionalPart(float Sample) noexcept
{
    return Sample - std::floor(Sample);
}

[[nodiscard]] inline double EvaluateFractionalPart(double Sample) noexcept
{
    return Sample - std::floor(Sample);
}

[[nodiscard]] inline float WrapDegrees360(float Degrees) noexcept
{
    float Wrapped = std::fmod(Degrees, 360.0f);
    if (Wrapped < 0.0f)
    {
        Wrapped += 360.0f;
    }
    return Wrapped;
}

[[nodiscard]] inline double WrapDegrees360(double Degrees) noexcept
{
    double Wrapped = std::fmod(Degrees, 360.0);
    if (Wrapped < 0.0)
    {
        Wrapped += 360.0;
    }
    return Wrapped;
}

[[nodiscard]] constexpr std::size_t AlignUpBytes(
    std::size_t ByteCount,
    std::size_t AlignmentBytes = Constants::ContainerBinaryAlignmentBytes) noexcept
{
    if (AlignmentBytes == 0u)
    {
        return ByteCount;
    }
    const std::size_t Remainder = ByteCount % AlignmentBytes;
    return Remainder == 0u ? ByteCount : (ByteCount + (AlignmentBytes - Remainder));
}

//------------------------------------------------------------------------------------------------------------------------
//                                              3-LANE ARRAY VECTOR PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr float EvaluateDot3(const float Left[3], const float Right[3]) noexcept
{
    return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2];
}

[[nodiscard]] constexpr float EvaluateDot3(const std::array<float, 3>& Left, const std::array<float, 3>& Right) noexcept
{
    return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2];
}

[[nodiscard]] constexpr std::array<float, 3> EvaluateCross3(
    const std::array<float, 3>& Left,
    const std::array<float, 3>& Right) noexcept
{
    return {
        Left[1] * Right[2] - Left[2] * Right[1],
        Left[2] * Right[0] - Left[0] * Right[2],
        Left[0] * Right[1] - Left[1] * Right[0],
    };
}

[[nodiscard]] constexpr float EvaluateLengthSquared3(const std::array<float, 3>& Vector) noexcept
{
    return EvaluateDot3(Vector, Vector);
}

[[nodiscard]] inline float EvaluateLength3(const std::array<float, 3>& Vector) noexcept
{
    return std::sqrt(EvaluateLengthSquared3(Vector));
}

[[nodiscard]] inline std::array<float, 3> NormalizeVector3(
    const std::array<float, 3>& Vector,
    const std::array<float, 3>& Fallback = { 0.0f, 0.0f, 1.0f }) noexcept
{
    const float LengthSq = EvaluateLengthSquared3(Vector);
    if (LengthSq <= Constants::SingleEpsilon * Constants::SingleEpsilon)
    {
        return Fallback;
    }
    const float InvLength = 1.0f / std::sqrt(LengthSq);
    return { Vector[0] * InvLength, Vector[1] * InvLength, Vector[2] * InvLength };
}

[[nodiscard]] constexpr float EvaluateSmoothHermite(float Sample) noexcept
{
    const float Clamped = std::clamp(Sample, 0.0f, 1.0f);
    return Clamped * Clamped * (3.0f - 2.0f * Clamped);
}

[[nodiscard]] constexpr double EvaluateSmoothHermite(double Sample) noexcept
{
    const double Clamped = std::clamp(Sample, 0.0, 1.0);
    return Clamped * Clamped * (3.0 - 2.0 * Clamped);
}

[[nodiscard]] inline uint8_t QuantizeUnitToByte(float UnitChannel) noexcept
{
    const float Clamped = std::clamp(UnitChannel, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::lround(Clamped * 255.0f));
}

[[nodiscard]] inline uint8_t QuantizeFloatToByte(float PixelChannel) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(PixelChannel), 0L, 255L));
}

[[nodiscard]] inline std::array<float, 3> InterpolateVector3(
    const std::array<float, 3>& Start,
    const std::array<float, 3>& Finish,
    float                       Factor) noexcept
{
    return {
        InterpolateScalar(Start[0], Finish[0], Factor),
        InterpolateScalar(Start[1], Finish[1], Factor),
        InterpolateScalar(Start[2], Finish[2], Factor),
    };
}

[[nodiscard]] inline std::array<float, 3> EvaluateAnyPerpendicular3(const std::array<float, 3>& UnitNormal) noexcept
{
    const std::array<float, 3> ReferenceAxis =
        (std::fabs(UnitNormal[2]) < 0.9f) ? std::array<float, 3>{ 0.0f, 0.0f, 1.0f } : std::array<float, 3>{ 1.0f, 0.0f, 0.0f };
    return NormalizeVector3(EvaluateCross3(UnitNormal, ReferenceAxis), { 1.0f, 0.0f, 0.0f });
}

//------------------------------------------------------------------------------------------------------------------------
//                                        PROCEDURAL LATTICE & PHASE PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline float EvaluateLatticeHash13(float CoordX, float CoordY, float CoordZ) noexcept
{
    float X = EvaluateFractionalPart(CoordX * 0.1031f);
    float Y = EvaluateFractionalPart(CoordY * 0.1031f);
    float Z = EvaluateFractionalPart(CoordZ * 0.1031f);
    const float DotOffset = X * (Z + 31.32f) + Y * (Y + 31.32f) + Z * (X + 31.32f);
    X += DotOffset;
    Y += DotOffset;
    Z += DotOffset;
    return EvaluateFractionalPart((X + Y) * Z);
}

[[nodiscard]] inline float EvaluateLatticeNoise3D(float CoordX, float CoordY, float CoordZ) noexcept
{
    const float FloorX = std::floor(CoordX);
    const float FloorY = std::floor(CoordY);
    const float FloorZ = std::floor(CoordZ);
    const float FracX  = CoordX - FloorX;
    const float FracY  = CoordY - FloorY;
    const float FracZ  = CoordZ - FloorZ;
    const float WeightX = FracX * FracX * (3.0f - 2.0f * FracX);
    const float WeightY = FracY * FracY * (3.0f - 2.0f * FracY);
    const float WeightZ = FracZ * FracZ * (3.0f - 2.0f * FracZ);

    const float C000 = EvaluateLatticeHash13(FloorX,        FloorY,        FloorZ);
    const float C100 = EvaluateLatticeHash13(FloorX + 1.0f, FloorY,        FloorZ);
    const float C010 = EvaluateLatticeHash13(FloorX,        FloorY + 1.0f, FloorZ);
    const float C110 = EvaluateLatticeHash13(FloorX + 1.0f, FloorY + 1.0f, FloorZ);
    const float C001 = EvaluateLatticeHash13(FloorX,        FloorY,        FloorZ + 1.0f);
    const float C101 = EvaluateLatticeHash13(FloorX + 1.0f, FloorY,        FloorZ + 1.0f);
    const float C011 = EvaluateLatticeHash13(FloorX,        FloorY + 1.0f, FloorZ + 1.0f);
    const float C111 = EvaluateLatticeHash13(FloorX + 1.0f, FloorY + 1.0f, FloorZ + 1.0f);

    const float LowZ  = InterpolateScalar(InterpolateScalar(C000, C100, WeightX), InterpolateScalar(C010, C110, WeightX), WeightY);
    const float HighZ = InterpolateScalar(InterpolateScalar(C001, C101, WeightX), InterpolateScalar(C011, C111, WeightX), WeightY);
    return InterpolateScalar(LowZ, HighZ, WeightZ);
}

[[nodiscard]] inline float EvaluateHenyeyGreensteinPhase(float CosTheta, float AsymmetryG) noexcept
{
    const float ClampedG = std::clamp(AsymmetryG, -0.95f, 0.95f);
    const float G2       = ClampedG * ClampedG;
    const float Denom    = std::max(1.0e-4f, 1.0f + G2 - 2.0f * ClampedG * CosTheta);
    return (1.0f - G2) / (Constants::FourPiFloat * std::pow(Denom, 1.5f));
}

} // namespace Frontier::Mathematics
