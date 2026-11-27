/*============================================================================================================================================
                                                              PORTALSYSTEM.CPP
============================================================================================================================================*/
// 🧩 Exact Multi-World 2D Planar Portal Subsystem Implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/World/PortalSystem.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::World
{

namespace
{

void OrthonormalizeEndpoint(PortalEndpointPose& Pose) noexcept
{
    Pose.UpAxisWorld    = Mathematics::NormalizeAxis(Pose.UpAxisWorld, { 0.0f, 0.0f, 1.0f });
    Pose.RightAxisWorld = Mathematics::NormalizeAxis(
        Pose.RightAxisWorld - Pose.UpAxisWorld * Mathematics::EvaluateDot(Pose.RightAxisWorld, Pose.UpAxisWorld),
        { 0.0f, 1.0f, 0.0f });
    Pose.NormalAxisWorld = Mathematics::NormalizeAxis(
        Mathematics::EvaluateCross(Pose.RightAxisWorld, Pose.UpAxisWorld),
        { -1.0f, 0.0f, 0.0f });
    Pose.UpAxisWorld = Mathematics::NormalizeAxis(
        Mathematics::EvaluateCross(Pose.NormalAxisWorld, Pose.RightAxisWorld),
        { 0.0f, 0.0f, 1.0f });
}

[[nodiscard]] float EvaluatePolygonSignedDistance2D(
    float                             U,
    float                             V,
    const std::vector<Mathematics::Axis2D>& Verts) noexcept
{
    const uint32_t Count = std::min(static_cast<uint32_t>(Verts.size()), MaximumPortalCutoutVertices);
    if (Count < 3u)
    {
        return 1.0e5f;
    }

    float MinDistSq = 1.0e12f;
    bool  Inside    = false;

    for (uint32_t I = 0u, J = Count - 1u; I < Count; J = I++)
    {
        const float Xi = Verts[I].X;
        const float Yi = Verts[I].Y;
        const float Xj = Verts[J].X;
        const float Yj = Verts[J].Y;

        // Exact edge segment squared distance
        const float Ex = Xj - Xi;
        const float Ey = Yj - Yi;
        const float Wx = U - Xi;
        const float Wy = V - Yi;
        const float Den = Ex * Ex + Ey * Ey;
        const float T   = (Den > 1.0e-10f) ? std::clamp((Wx * Ex + Wy * Ey) / Den, 0.0f, 1.0f) : 0.0f;
        const float Bx  = Wx - Ex * T;
        const float By  = Wy - Ey * T;
        MinDistSq       = std::min(MinDistSq, Bx * Bx + By * By);

        // Exact Jordan curve ray-casting winding parity (works for arbitrary convex AND concave 2D cutouts)
        const bool CrossesY = ((Yi > V) != (Yj > V));
        if (CrossesY)
        {
            const float IntersectX = Xi + (V - Yi) * (Xj - Xi) / (Yj - Yi);
            if (U < IntersectX)
            {
                Inside = !Inside;
            }
        }
    }

    const float Dist = std::sqrt(std::max(0.0f, MinDistSq));
    return Inside ? -Dist : Dist;
}

[[nodiscard]] Mathematics::Axis3D TransportPointBetweenFrames(
    const PortalEndpointPose& Src,
    const PortalEndpointPose& Dst,
    const Mathematics::Axis3D& P) noexcept
{
    const Mathematics::Axis3D Rel = P - Src.CenterWorld;
    const float U = Mathematics::EvaluateDot(Rel, Src.RightAxisWorld);
    const float V = Mathematics::EvaluateDot(Rel, Src.UpAxisWorld);
    const float W = Mathematics::EvaluateDot(Rel, Src.NormalAxisWorld);
    // Exact 180-degree rotation around local Up: (U, V, W) -> (-U, +V, -W), det = +1
    return Dst.CenterWorld
         + Dst.RightAxisWorld  * (-U)
         + Dst.UpAxisWorld     * ( V)
         + Dst.NormalAxisWorld * (-W);
}

[[nodiscard]] Mathematics::Axis3D TransportVectorBetweenFrames(
    const PortalEndpointPose& Src,
    const PortalEndpointPose& Dst,
    const Mathematics::Axis3D& Vec) noexcept
{
    const float U = Mathematics::EvaluateDot(Vec, Src.RightAxisWorld);
    const float V = Mathematics::EvaluateDot(Vec, Src.UpAxisWorld);
    const float W = Mathematics::EvaluateDot(Vec, Src.NormalAxisWorld);
    return Dst.RightAxisWorld  * (-U)
         + Dst.UpAxisWorld     * ( V)
         + Dst.NormalAxisWorld * (-W);
}

} // namespace

void PortalPairSpecification::NormalizeAxes() noexcept
{
    OrthonormalizeEndpoint(Entry);
    OrthonormalizeEndpoint(Exit);
}

float PortalPairSpecification::EvaluateShapeSignedDistance2D(float LocalU, float LocalV) const noexcept
{
    const float R = std::max(0.25f, HalfWidthMeters);
    const float H = std::max(0.25f, HalfHeightMeters);

    switch (Shape)
    {
        case PortalApertureShapeCategory::Rectangle:
        {
            const float Dx = std::fabs(LocalU) - R;
            const float Dy = std::fabs(LocalV) - H;
            const float Ox = std::max(Dx, 0.0f);
            const float Oy = std::max(Dy, 0.0f);
            return std::sqrt(Ox * Ox + Oy * Oy) + std::min(std::max(Dx, Dy), 0.0f);
        }
        case PortalApertureShapeCategory::Circle:
        {
            return std::sqrt(LocalU * LocalU + LocalV * LocalV) - R;
        }
        case PortalApertureShapeCategory::Hexagon:
        {
            // Exact Inigo Quilez 2D regular hexagon SDF (flat-top orientation, inradius = R * 0.8660254)
            constexpr float Kx = -0.866025404f;
            constexpr float Ky =  0.500000000f;
            constexpr float Kz =  0.577350269f;
            const float InRadius = R * 0.866025404f;
            float Px = std::fabs(LocalU);
            float Py = std::fabs(LocalV);
            const float DotK = 2.0f * std::min(Kx * Px + Ky * Py, 0.0f);
            Px -= DotK * Kx;
            Py -= DotK * Ky;
            Px -= std::clamp(Px, -Kz * InRadius, Kz * InRadius);
            Py -= InRadius;
            const float Dist = std::sqrt(Px * Px + Py * Py);
            return (Py < 0.0f) ? -Dist : Dist;
        }
        case PortalApertureShapeCategory::RegularPolygon:
        {
            const uint32_t Sides = std::clamp(PolygonSides, 3u, 16u);
            const float Sector   = Constants::TauFloat / static_cast<float>(Sides);
            const float HalfSec  = 0.5f * Sector;
            const float InRad    = R * std::cos(HalfSec);
            const float Ang      = std::atan2(LocalV, LocalU);
            const float ModAng   = std::fmod(Ang + Constants::TauFloat + HalfSec, Sector) - HalfSec;
            const float Dist2D   = std::sqrt(LocalU * LocalU + LocalV * LocalV);
            const float Px       = Dist2D * std::cos(ModAng) - InRad;
            const float Py       = std::fabs(Dist2D * std::sin(ModAng)) - R * std::sin(HalfSec);
            const float ClampedY = std::max(Py, 0.0f);
            if (Px > 0.0f)
            {
                return std::sqrt(Px * Px + ClampedY * ClampedY);
            }
            return std::max(Px, Py);
        }
        case PortalApertureShapeCategory::CustomCutout:
        {
            if (CustomPolygonUv.size() >= 3u)
            {
                return EvaluatePolygonSignedDistance2D(LocalU, LocalV, CustomPolygonUv);
            }
            // Fallback to Rectangle if custom cutout polygon is empty
            const float Dx = std::fabs(LocalU) - R;
            const float Dy = std::fabs(LocalV) - H;
            return std::sqrt(std::max(Dx, 0.0f) * std::max(Dx, 0.0f) + std::max(Dy, 0.0f) * std::max(Dy, 0.0f))
                 + std::min(std::max(Dx, Dy), 0.0f);
        }
    }
    return 1.0e5f;
}

Mathematics::Axis3D PortalPairSpecification::TransformPointEntryToExit(const Mathematics::Axis3D& WorldPoint) const noexcept
{
    return TransportPointBetweenFrames(Entry, Exit, WorldPoint);
}

Mathematics::Axis3D PortalPairSpecification::TransformVectorEntryToExit(const Mathematics::Axis3D& WorldVector) const noexcept
{
    return TransportVectorBetweenFrames(Entry, Exit, WorldVector);
}

Mathematics::Axis3D PortalPairSpecification::TransformPointExitToEntry(const Mathematics::Axis3D& WorldPoint) const noexcept
{
    return TransportPointBetweenFrames(Exit, Entry, WorldPoint);
}

Mathematics::Axis3D PortalPairSpecification::TransformVectorExitToEntry(const Mathematics::Axis3D& WorldVector) const noexcept
{
    return TransportVectorBetweenFrames(Exit, Entry, WorldVector);
}

std::vector<Mathematics::Axis2D> PortalPairSpecification::BuildPresetCustomCutout(
    uint32_t PresetOrdinal,
    float    ScaleMeters)
{
    const float S = std::max(0.5f, ScaleMeters);
    std::vector<Mathematics::Axis2D> Poly {};

    if ((PresetOrdinal % 2u) == 0u)
    {
        // Preset 0: Concave Architectural Sci-Fi Drive-Through Portal Cutout (wide flat sill + stepped shoulders + pointed arch crown)
        Poly = {
            { -1.15f * S, -0.70f * S },
            {  1.15f * S, -0.70f * S },
            {  1.15f * S,  0.02f * S },
            {  0.86f * S,  0.18f * S },
            {  0.98f * S,  0.48f * S },
            {  0.54f * S,  0.76f * S },
            {  0.00f * S,  0.92f * S },
            { -0.54f * S,  0.76f * S },
            { -0.98f * S,  0.48f * S },
            { -0.86f * S,  0.18f * S },
            { -1.15f * S,  0.02f * S },
        };
    }
    else
    {
        // Preset 1: Concave 5-Point Star Cutout (explicitly proves non-convex arbitrary 2D polygon cutout!)
        constexpr uint32_t Points = 5u;
        Poly.reserve(Points * 2u);
        for (uint32_t K = 0u; K < Points * 2u; ++K)
        {
            const float Rad = ((K & 1u) == 0u) ? (1.08f * S) : (0.58f * S);
            const float Ang = Constants::HalfPiFloat + static_cast<float>(K) * (Constants::PiFloat / static_cast<float>(Points));
            Poly.push_back({ Rad * std::cos(Ang), Rad * std::sin(Ang) });
        }
    }
    return Poly;
}

PortalVehicleOverlapReading EvaluateVehiclePortalOverlap(
    const std::vector<PortalPairSpecification>& Portals,
    const Mathematics::Axis3D&                  VehicleCenterWorld,
    float                                       VehicleYawZUpDegrees,
    float                                       HalfLengthMeters,
    float                                       HalfWidthMeters,
    float                                       HalfHeightMeters) noexcept
{
    PortalVehicleOverlapReading Out {};
    const float YawRad = VehicleYawZUpDegrees * Constants::DegToRadFloat;
    const Mathematics::Axis3D FwdWorld   { std::cos(YawRad), std::sin(YawRad), 0.0f };
    const Mathematics::Axis3D RightWorld { std::sin(YawRad), -std::cos(YawRad), 0.0f };
    const Mathematics::Axis3D UpWorld    { 0.0f, 0.0f, 1.0f };

    for (uint32_t PIdx = 0u; PIdx < static_cast<uint32_t>(Portals.size()); ++PIdx)
    {
        const auto& Portal = Portals[PIdx];
        if (!Portal.Enabled)
        {
            continue;
        }

        const uint32_t EndpointCount = Portal.Bidirectional ? 2u : 1u;
        for (uint32_t Side = 0u; Side < EndpointCount; ++Side)
        {
            const PortalEndpointPose& Src = (Side == 0u) ? Portal.Entry : Portal.Exit;
            const PortalEndpointPose& Dst = (Side == 0u) ? Portal.Exit  : Portal.Entry;

            const Mathematics::Axis3D Rel = VehicleCenterWorld - Src.CenterWorld;
            const float LocalU = Mathematics::EvaluateDot(Rel, Src.RightAxisWorld);
            const float LocalV = Mathematics::EvaluateDot(Rel, Src.UpAxisWorld);
            const float SignedW = Mathematics::EvaluateDot(Rel, Src.NormalAxisWorld);

            // Project vehicle oriented half-extent along Src.NormalAxisWorld
            const float ProjectedHalfSpan =
                std::fabs(Mathematics::EvaluateDot(FwdWorld,   Src.NormalAxisWorld)) * HalfLengthMeters +
                std::fabs(Mathematics::EvaluateDot(RightWorld, Src.NormalAxisWorld)) * HalfWidthMeters  +
                std::fabs(Mathematics::EvaluateDot(UpWorld,    Src.NormalAxisWorld)) * HalfHeightMeters;

            // Straddling check: vehicle bounding interval intersects W = 0 plane AND center projects inside 2D aperture
            if (std::fabs(SignedW) < (ProjectedHalfSpan + 0.08f))
            {
                const float ShapeSdf = Portal.EvaluateShapeSignedDistance2D(LocalU, LocalV);
                if (ShapeSdf <= (HalfWidthMeters * 0.65f))
                {
                    Out.OverlapActive       = true;
                    Out.PortalPairIndex     = PIdx;
                    Out.EnteredFromExitSide = (Side == 1u);
                    Out.SignedCenterDistM   = SignedW;

                    const float Span = std::max(0.25f, 2.0f * ProjectedHalfSpan);
                    Out.ExposedFractionSrc  = std::clamp((SignedW + ProjectedHalfSpan) / Span, 0.0f, 1.0f);
                    Out.ExposedFractionDst  = 1.0f - Out.ExposedFractionSrc;

                    // Primary instance keeps ONLY points on the front side of Src (dot(P - Src.Center, Src.Normal) >= 0)
                    Out.ClipPlanePrimary[0] = Src.NormalAxisWorld.X;
                    Out.ClipPlanePrimary[1] = Src.NormalAxisWorld.Y;
                    Out.ClipPlanePrimary[2] = Src.NormalAxisWorld.Z;
                    Out.ClipPlanePrimary[3] = -Mathematics::EvaluateDot(Src.CenterWorld, Src.NormalAxisWorld);

                    // Clone instance on Dst side keeps ONLY points emerging on the front side of Dst (dot(P - Dst.Center, Dst.Normal) >= 0)
                    Out.ClipPlaneClone[0] = Dst.NormalAxisWorld.X;
                    Out.ClipPlaneClone[1] = Dst.NormalAxisWorld.Y;
                    Out.ClipPlaneClone[2] = Dst.NormalAxisWorld.Z;
                    Out.ClipPlaneClone[3] = -Mathematics::EvaluateDot(Dst.CenterWorld, Dst.NormalAxisWorld);

                    Out.ClonePositionWorld = (Side == 0u)
                        ? Portal.TransformPointEntryToExit(VehicleCenterWorld)
                        : Portal.TransformPointExitToEntry(VehicleCenterWorld);

                    const Mathematics::Axis3D CloneFwd = (Side == 0u)
                        ? Portal.TransformVectorEntryToExit(FwdWorld)
                        : Portal.TransformVectorExitToEntry(FwdWorld);
                    Out.CloneYawZUpDegrees = std::atan2(CloneFwd.Y, CloneFwd.X) * Constants::RadToDegFloat;
                    return Out;
                }
            }
        }
    }

    return Out;
}

} // namespace Frontier::World
