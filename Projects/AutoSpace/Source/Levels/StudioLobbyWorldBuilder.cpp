/*============================================================================================================================================
                                                    STUDIOLOBBYWORLDBUILDER.CPP
============================================================================================================================================*/
// 🧩 Shared level-population helpers for studio rig aiming, luminaire placement, and deterministic scene hashing

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Levels/StudioLobbyWorldBuilder.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace AutoSpace
{

using namespace Frontier::GeometricRaster;
using namespace Frontier::Mathematics;

HtmlAimAngles ComputeHtmlAimAngles(Axis3D FromHtml, Axis3D TargetHtml) noexcept
{
    const float Dx    = TargetHtml.X - FromHtml.X;
    const float Dy    = TargetHtml.Y - FromHtml.Y;
    const float Dz    = TargetHtml.Z - FromHtml.Z;
    const float Horiz = std::max(1.0e-5f, std::hypot(Dx, Dz));
    const float Yaw   = std::atan2(Dx, Dz) * Frontier::Constants::RadToDegFloat;
    const float Pitch = std::atan2(-Dy, Horiz) * Frontier::Constants::RadToDegFloat;
    return { Yaw, Pitch };
}

uint64_t HashStableId64(std::string_view Text) noexcept
{
    uint64_t H = 14695981039346656037ull;
    for (const char Ch : Text)
    {
        H ^= static_cast<uint64_t>(static_cast<unsigned char>(Ch));
        H *= 1099511628211ull;
    }
    return H;
}

PolygonalAreaQuadLight MakeAimedAreaQuadLight(
    Axis3D        CenterWorld,
    Axis3D        AimTargetWorld,
    float         WidthMeters,
    float         HeightMeters,
    LinearRGB     Radiance,
    bool          TwoSided,
    LightMobility Mobility,
    uint32_t      LightRigGroupId) noexcept
{
    const Axis3D AimDir = NormalizeAxis(AimTargetWorld - CenterWorld, { 0.0f, 0.0f, -1.0f });
    const Axis3D RefUp  = (std::fabs(AimDir.Z) < 0.96f) ? Axis3D{ 0.0f, 0.0f, 1.0f } : Axis3D{ 0.0f, 1.0f, 0.0f };
    Axis3D Right = NormalizeAxis(EvaluateCross(RefUp, AimDir), { 1.0f, 0.0f, 0.0f });
    if (std::fabs(AimDir.Z) >= 0.96f)
    {
        Right = { 1.0f, 0.0f, 0.0f };
    }
    const Axis3D Up = NormalizeAxis(EvaluateCross(AimDir, Right), { 0.0f, -1.0f, 0.0f });

    PolygonalAreaQuadLight Quad {};
    Quad.CenterWorld      = CenterWorld;
    Quad.RightTangent     = Right;
    Quad.UpBitangent      = Up;
    Quad.HalfWidthMeters  = WidthMeters * 0.5f;
    Quad.HalfHeightMeters = HeightMeters * 0.5f;
    Quad.EmittedRadiance  = Radiance;
    Quad.TwoSidedEmission = TwoSided;
    Quad.Mobility         = Mobility;
    Quad.LightRigGroupId  = LightRigGroupId;
    return Quad;
}

PhotometricPointRadiator MakeAimedPhotometricSpotLight(
    Axis3D        PosWorld,
    Axis3D        TargetWorld,
    LinearRGB     PeakRgb,
    float         RangeMeters,
    uint32_t      ProfileOrdinal,
    LightMobility Mobility,
    uint32_t      LightRigGroupId) noexcept
{
    const Axis3D BeamAxis = NormalizeAxis(TargetWorld - PosWorld, { 0.0f, 0.0f, -1.0f });
    const Axis3D RefUp    = (std::fabs(BeamAxis.Z) < 0.96f) ? Axis3D{ 0.0f, 0.0f, 1.0f } : Axis3D{ 1.0f, 0.0f, 0.0f };
    const Axis3D ZeroAz   = NormalizeAxis(EvaluateCross(RefUp, BeamAxis), { 1.0f, 0.0f, 0.0f });

    PhotometricPointRadiator Lamp {};
    Lamp.PositionWorld    = PosWorld;
    Lamp.BeamAxis         = BeamAxis;
    Lamp.ZeroAzimuthAxis  = ZeroAz;
    Lamp.PeakIntensityRgb = PeakRgb;
    Lamp.RangeMeters      = RangeMeters;
    Lamp.RadiusMeters     = 0.12f;
    Lamp.ProfileOrdinal   = ProfileOrdinal;
    Lamp.Mobility         = Mobility;
    Lamp.LightRigGroupId  = LightRigGroupId;
    return Lamp;
}

} // namespace AutoSpace
