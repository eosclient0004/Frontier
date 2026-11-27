/*============================================================================================================================================
                                                               MATHCONSTANTS.H
============================================================================================================================================*/
// 🧩 Shared compile-time mathematical constants, angular conversion factors, and IEEE-754 comparison tolerances

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <numbers>

namespace Frontier::Constants
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double Pi                   = std::numbers::pi_v<double>;         // [rad]   - Half-turn circle ratio
inline constexpr double Tau                  = 2.0 * std::numbers::pi_v<double>;   // [rad]   - Full-turn circle ratio
inline constexpr double HalfPi               = 0.5 * std::numbers::pi_v<double>;   // [rad]   - Quarter-turn right angle
inline constexpr double QuarterPi            = 0.25 * std::numbers::pi_v<double>;  // [rad]   - Eighth-turn angle
inline constexpr double InversePi            = std::numbers::inv_pi_v<double>;     // [1/rad] - Reciprocal of Pi
inline constexpr double InverseTau           = 1.0 / Tau;                          // [1/rad] - Reciprocal of Tau
inline constexpr double SquareRootTwo        = std::numbers::sqrt2_v<double>;      // [-]     - Principal root of two
inline constexpr double GoldenRatio          = std::numbers::phi_v<double>;        // [-]     - Algebraic golden ratio
inline constexpr double DegToRad             = Pi / 180.0;                         // [rad/°] - Degrees to radians factor
inline constexpr double RadToDeg             = 180.0 / Pi;                         // [°/rad] - Radians to degrees factor
inline constexpr double Epsilon              = 1.0e-9;                             // [-]     - Double-precision geometric tolerance
inline constexpr float  SingleEpsilon        = 1.0e-5f;                            // [-]     - Single-precision geometric tolerance
inline constexpr float  PiFloat              = std::numbers::pi_v<float>;          // [rad]   - Single-precision Pi
inline constexpr float  TauFloat             = 2.0f * std::numbers::pi_v<float>;   // [rad]   - Single-precision Tau
inline constexpr float  HalfPiFloat          = 0.5f * std::numbers::pi_v<float>;   // [rad]   - Single-precision HalfPi
inline constexpr float  QuarterPiFloat       = 0.25f * std::numbers::pi_v<float>;  // [rad]   - Single-precision QuarterPi
inline constexpr float  InversePiFloat       = std::numbers::inv_pi_v<float>;      // [1/rad] - Single-precision reciprocal of Pi
inline constexpr float  InverseTauFloat      = 1.0f / TauFloat;                    // [1/rad] - Single-precision reciprocal of Tau
inline constexpr float  FourPiFloat          = 4.0f * PiFloat;                     // [sr]    - Full solid angle of a unit sphere
inline constexpr float  InverseFourPiFloat   = 1.0f / FourPiFloat;                 // [1/sr]  - Isotropic phase normalization factor
inline constexpr float  SquareRootTwoFloat   = std::numbers::sqrt2_v<float>;       // [-]     - Single-precision principal root of two
inline constexpr float  GoldenRatioFloat     = std::numbers::phi_v<float>;         // [-]     - Single-precision algebraic golden ratio
inline constexpr float  DegToRadFloat        = PiFloat / 180.0f;                   // [rad/°] - Single-precision degree factor
inline constexpr float  RadToDegFloat        = 180.0f / PiFloat;                   // [°/rad] - Single-precision radian factor
inline constexpr float  Rec709LuminanceRed   = 0.2126f;                            // [-]     - ITU-R BT.709 red photopic luminance weight
inline constexpr float  Rec709LuminanceGreen = 0.7152f;                            // [-]     - ITU-R BT.709 green photopic luminance weight
inline constexpr float  Rec709LuminanceBlue  = 0.0722f;                            // [-]     - ITU-R BT.709 blue photopic luminance weight

} // namespace Frontier::Constants
