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

[[nodiscard]] constexpr std::array<float, 3> InterpolateVector3(
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

} // namespace Frontier::Mathematics
