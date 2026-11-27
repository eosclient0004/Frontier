/*============================================================================================================================================
                                                          SHADERMATHCONSTANTS.SLANG
============================================================================================================================================*/
// 🧩 Canonical GPU compile-time mathematical and photometric constants shared across Frontier .slang shaders

#pragma once
#ifndef FRONTIER_SHADER_MATH_CONSTANTS_SLANG
#define FRONTIER_SHADER_MATH_CONSTANTS_SLANG 1

static const float  FrontierPi               = 3.14159265359;       // [rad]   - Half-turn circle ratio
static const float  FrontierTau              = 6.28318530718;       // [rad]   - Full-turn circle ratio
static const float  FrontierHalfPi           = 1.57079632679;       // [rad]   - Quarter-turn right angle
static const float  FrontierQuarterPi        = 0.78539816340;       // [rad]   - Eighth-turn angle
static const float  FrontierInvPi            = 0.31830988618;       // [1/rad] - Reciprocal of Pi
static const float  FrontierInvTau           = 0.15915494309;       // [1/rad] - Reciprocal of Tau
static const float  FrontierFourPi           = 12.5663706144;       // [sr]    - Full solid angle of a unit sphere
static const float  FrontierInvFourPi        = 0.07957747155;       // [1/sr]  - Isotropic phase normalization factor
static const float  FrontierDegToRad         = 0.01745329252;       // [rad/°] - Degrees to radians factor
static const float  FrontierRadToDeg         = 57.2957795131;       // [°/rad] - Radians to degrees factor
static const float  FrontierGoldenRatio      = 1.61803398875;       // [-]     - Algebraic golden ratio
static const float  FrontierInvGoldenRatio   = 0.61803398875;       // [-]     - Conjugate golden ratio (phi - 1)
static const float  FrontierGoldenAngle      = 2.39996322973;       // [rad]   - Fibonacci golden angle
static const float3 FrontierRec709Luminance  = float3(0.2126, 0.7152, 0.0722); // [-] - ITU-R BT.709 photopic luminance weights

#endif // FRONTIER_SHADER_MATH_CONSTANTS_SLANG
