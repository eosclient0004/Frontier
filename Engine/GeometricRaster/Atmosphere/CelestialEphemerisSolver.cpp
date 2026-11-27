/*============================================================================================================================================
                                                     CELESTIALEPHEMERISSOLVER.CPP
============================================================================================================================================*/
// 🧩 Astronomical solar ephemeris solver, Kelvin Planckian tint conversion, and Right-Handed (+Z Up) sky direction mapping

#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/ColourSpace/TransferFunction.h"
#include "Engine/Mathematics/ScalarMath.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{



} // namespace

Mathematics::Axis3D CelestialEphemerisSolver::ConvertAzimuthElevationToWorldAxis(
    float AzimuthDegrees,
    float ElevationDegrees) noexcept
{
    const float AzRad = AzimuthDegrees * Constants::DegToRadFloat;
    const float ElRad = ElevationDegrees * Constants::DegToRadFloat;
    const float CosEl = std::cos(ElRad);
    // Right-handed rule: +X = North (Az=0), -Y = East (Az=90), -X = South (Az=180), +Y = West (Az=270), +Z = Up
    return Mathematics::NormalizeAxis({
        CosEl * std::cos(AzRad),
        -CosEl * std::sin(AzRad),
        std::sin(ElRad)
    });
}

Mathematics::LinearRGB CelestialEphemerisSolver::EvaluateKelvinToLinearRgb(float Kelvin) noexcept
{
    const float T = std::clamp(Kelvin, 1000.0f, 40000.0f) * 0.01f;
    float R = 255.0f;
    float G = 255.0f;
    float B = 255.0f;

    if (T <= 66.0f)
    {
        R = 255.0f;
        G = std::clamp(99.4708025861f * std::log(std::max(1.0f, T)) - 161.1195681661f, 0.0f, 255.0f);
        B = (T <= 19.0f)
            ? 0.0f
            : std::clamp(138.5177312231f * std::log(std::max(1.0f, T - 10.0f)) - 305.0447927307f, 0.0f, 255.0f);
    }
    else
    {
        R = std::clamp(329.698727446f * std::pow(T - 60.0f, -0.1332047592f), 0.0f, 255.0f);
        G = std::clamp(288.1221695283f * std::pow(T - 60.0f, -0.0755148492f), 0.0f, 255.0f);
        B = 255.0f;
    }

    return {
        Frontier::ColourSpace::DecodePiecewiseSrgbChannel(R / 255.0f),
        Frontier::ColourSpace::DecodePiecewiseSrgbChannel(G / 255.0f),
        Frontier::ColourSpace::DecodePiecewiseSrgbChannel(B / 255.0f)
    };
}

SolarEphemerisOutcome CelestialEphemerisSolver::SolveSolarEphemeris(
    const SolarCelestialConfiguration& Config) noexcept
{
    SolarEphemerisOutcome Outcome {};
    Outcome.EffectiveSolarTint = (Config.TintSource == SolarTintSource::KelvinTemperature)
        ? EvaluateKelvinToLinearRgb(Config.ColorTemperatureKelvin)
        : Config.ManualRgbTint;


    int32_t Y = Config.Year;
    int32_t M = static_cast<int32_t>(std::clamp(Config.Month, 1u, 12u));
    const int32_t D = static_cast<int32_t>(std::clamp(Config.DayOfMonth, 1u, 31u));
    if (M <= 2)
    {
        --Y;
        M += 12;
    }

    const int32_t A = Y / 100;
    const int32_t B = 2 - A + (A / 4);
    const double UtcHours = static_cast<double>(Config.LocalHours) - static_cast<double>(Config.UtcOffsetHours);
    const double Jd =
        std::floor(365.25 * static_cast<double>(Y + 4716)) +
        std::floor(30.6001 * static_cast<double>(M + 1)) +
        static_cast<double>(D) +
        static_cast<double>(B) - 1524.5 + (UtcHours / 24.0);
    const double T = (Jd - 2451545.0) / 36525.0;

    const double MeanLon = Frontier::Mathematics::WrapDegrees360(280.46646 + T * (36000.76983 + T * 0.0003032));
    const double MeanAnom = 357.52911 + T * (35999.05029 - 0.0001537 * T);
    const double Ecc = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
    const double Mr = MeanAnom * Frontier::Constants::DegToRad;
    const double Centre =
        std::sin(Mr) * (1.914602 - T * (0.004817 + 0.000014 * T)) +
        std::sin(2.0 * Mr) * (0.019993 - 0.000101 * T) +
        std::sin(3.0 * Mr) * 0.000289;
    const double Omega = 125.04 - 1934.136 * T;
    const double ApparentLon = MeanLon + Centre - 0.00569 - 0.00478 * std::sin(Omega * Frontier::Constants::DegToRad);
    const double MeanObliq = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
    const double Obliq = MeanObliq + 0.00256 * std::cos(Omega * Frontier::Constants::DegToRad);

    const double Declination = std::asin(std::sin(Obliq * Frontier::Constants::DegToRad) * std::sin(ApparentLon * Frontier::Constants::DegToRad)) * Frontier::Constants::RadToDeg;
    const double TanHalf = std::tan((Obliq * 0.5) * Frontier::Constants::DegToRad);
    const double Y2 = TanHalf * TanHalf;
    const double L0r = MeanLon * Frontier::Constants::DegToRad;
    const double EqTime = 4.0 * Frontier::Constants::RadToDeg * (
        Y2 * std::sin(2.0 * L0r) -
        2.0 * Ecc * std::sin(Mr) +
        4.0 * Ecc * Y2 * std::sin(Mr) * std::cos(2.0 * L0r) -
        0.5 * Y2 * Y2 * std::sin(4.0 * L0r) -
        1.25 * Ecc * Ecc * std::sin(2.0 * Mr));

    const double TrueSolarRaw = std::fmod(UtcHours * 60.0 + EqTime + 4.0 * static_cast<double>(Config.SiteLongitudeDegrees), 1440.0);
    const double TrueSolarMin = (TrueSolarRaw < 0.0) ? (TrueSolarRaw + 1440.0) : TrueSolarRaw;
    const double HourAngle = TrueSolarMin * 0.25 - 180.0;

    const double LatR = static_cast<double>(Config.SiteLatitudeDegrees) * Frontier::Constants::DegToRad;
    const double DecR = Declination * Frontier::Constants::DegToRad;
    const double HaR  = HourAngle * Frontier::Constants::DegToRad;
    const double CosAngle = std::clamp(
        std::sin(LatR) * std::sin(DecR) + std::cos(LatR) * std::cos(DecR) * std::cos(HaR),
        -1.0, 1.0);
    const double PoleAngle = std::acos(CosAngle);
    const double Elevation = 90.0 - PoleAngle * Frontier::Constants::RadToDeg;
    const double SinPole = std::sin(PoleAngle);

    double Azimuth = 0.0;
    if (std::abs(SinPole) > 1.0e-9)
    {
        const double CosAz = std::clamp(
            (std::sin(LatR) * std::cos(PoleAngle) - std::sin(DecR)) / (std::cos(LatR) * SinPole),
            -1.0, 1.0);
        Azimuth = std::acos(CosAz) * Frontier::Constants::RadToDeg;
        Azimuth = (HourAngle > 0.0) ? Frontier::Mathematics::WrapDegrees360(Azimuth + 180.0) : Frontier::Mathematics::WrapDegrees360(540.0 - Azimuth);
    }

    const double GmstDeg = Frontier::Mathematics::WrapDegrees360(280.46061837 + 360.98564736629 * (Jd - 2451545.0) + static_cast<double>(Config.SiteLongitudeDegrees));
    Outcome.SiderealHours         = static_cast<float>(GmstDeg / 15.0);
    Outcome.DeclinationDegrees    = static_cast<float>(Declination);
    Outcome.EquationOfTimeMinutes = static_cast<float>(EqTime);

    if (Config.ManualDirectionOverride)
    {
        Outcome.AzimuthDegrees   = static_cast<float>(Frontier::Mathematics::WrapDegrees360(Config.ManualAzimuthDegrees));
        Outcome.ElevationDegrees = std::clamp(Config.ManualElevationDegrees, -90.0f, 90.0f);
    }
    else
    {
        Outcome.AzimuthDegrees   = static_cast<float>(Azimuth);
        Outcome.ElevationDegrees = static_cast<float>(Elevation);
    }

    Outcome.WorldDirectionToSun = ConvertAzimuthElevationToWorldAxis(Outcome.AzimuthDegrees, Outcome.ElevationDegrees);
    return Outcome;
}

void CelestialEphemerisSolver::AdvanceDayCycleHours(
    SolarCelestialConfiguration& InOutConfig,
    double                       DeltaSeconds) noexcept
{
    if (!InOutConfig.CycleAnimationEnabled || DeltaSeconds <= 0.0)
    {
        return;
    }
    double DurationH = static_cast<double>(InOutConfig.DayDurationHours);
    switch (InOutConfig.CycleSpeedPreset)
    {
        case DayCycleSpeedPreset::Multiplier1:   DurationH = 24.0; break;
        case DayCycleSpeedPreset::Multiplier8:   DurationH = 3.0;  break;
        case DayCycleSpeedPreset::Multiplier30:  DurationH = 0.8;  break;
        case DayCycleSpeedPreset::Multiplier100: DurationH = 0.24; break;
        case DayCycleSpeedPreset::Custom:        break;
    }
    DurationH = std::max(0.01, DurationH);
    const double NextH = std::fmod(static_cast<double>(InOutConfig.LocalHours) + (24.0 / DurationH) * DeltaSeconds, 24.0);
    InOutConfig.LocalHours = static_cast<float>(NextH < 0.0 ? NextH + 24.0 : NextH);
}

} // namespace Frontier::GeometricRaster
