/*============================================================================================================================================
                                                               PORTALSYSTEM.H
============================================================================================================================================*/
// 🧩 Exact Multi-World 2D Planar Portal Subsystem:
//    - Arbitrary 2D Aperture Shapes: Rectangle (Plane / Cube-face), Circle (Disc / Sphere-slice),
//      Regular 2D Hexagon, Regular N-gon, and Custom 2D Cutout Polygon (convex or concave, up to 32 vertices)
//    - Exact Proper-Rotation Rigid Frame Transport (det = +1, zero mirroring) between World A and World B
//    - Exact Split-Object Half-Space Clipping & Split-Physics Exposure Fractions for Vehicles & Rigid Bodies

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::World
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 PORTAL SHAPE & ENDPOINTS
//------------------------------------------------------------------------------------------------------------------------

enum class PortalApertureShapeCategory : uint8_t
{
    Rectangle      = 0u, // 2D Plane / Square / Cube-face cutout (HalfWidth x HalfHeight)
    Circle         = 1u, // 2D Circular Disc / Sphere-slice cutout (Radius = HalfWidth)
    Hexagon        = 2u, // 2D Regular Hexagon cutout (Circumradius = HalfWidth)
    RegularPolygon = 3u, // 2D Regular N-gon cutout (Sides in [3..16], Circumradius = HalfWidth)
    CustomCutout   = 4u, // Arbitrary 2D Polygon cutout (convex or concave, up to 32 vertices in local (u, v) meters)
};

inline constexpr uint32_t MaximumPortalCutoutVertices = 32u;
inline constexpr uint32_t MaximumGpuPortalPairs       = 8u;

struct PortalEndpointPose
{
    Mathematics::Axis3D    CenterWorld               { 0.0f, 0.0f, 0.0f };    // [m]   - Portal center in world Z-up coordinates
    Mathematics::Axis3D    RightAxisWorld            { 0.0f, 1.0f, 0.0f };    // [-]   - Unit local +U axis on portal plane
    Mathematics::Axis3D    UpAxisWorld               { 0.0f, 0.0f, 1.0f };    // [-]   - Unit local +V axis on portal plane
    Mathematics::Axis3D    NormalAxisWorld           { -1.0f, 0.0f, 0.0f };   // [-]   - Unit front outward normal (cross(Right, Up))
    uint32_t               WorldRegionOrdinal        = 0u;                    // [idx] - Logical world identifier (0=Primary, 1=Destination)
    Mathematics::LinearRGB SkyUpperRgb              { 0.14f, 0.34f, 0.72f }; // [lin] - Per-world sky zenith radiance for portal view
    Mathematics::LinearRGB SkyLowerRgb             { 0.56f, 0.72f, 0.90f }; // [lin] - Per-world sky horizon radiance for portal view
    Mathematics::Axis3D    SunDirectionWorld         { 0.45f, -0.35f, 0.82f };// [-]   - Per-world sun unit direction
    Mathematics::LinearRGB SunRadianceRgb            { 5.5f, 5.1f, 4.5f };    // [lin] - Per-world solar radiance tint
    float                  SurfaceFrictionMultiplier = 1.0f;                  // [x]   - Tyre/contact friction multiplier on this world side
    float                  GravityScale              = 1.0f;                  // [x]   - Relative gravity scale acting on exposed mass on this side
};

struct PortalPairSpecification
{
    bool                        Enabled            = false;
    bool                        Bidirectional      = true;
    PortalApertureShapeCategory Shape              = PortalApertureShapeCategory::Rectangle;
    float                       HalfWidthMeters    = 2.60f;                 // [m]   - Rectangle half-width or Circle/Hex/N-gon radius
    float                       HalfHeightMeters   = 1.65f;                 // [m]   - Rectangle half-height (ignored for Circle/Hex/N-gon)
    uint32_t                    PolygonSides       = 6u;                    // [cnt] - Side count for RegularPolygon [3..16]
    float                       RimThicknessMeters = 0.12f;                 // [m]   - Illuminated border rim around exact 2D cutout
    Mathematics::LinearRGB      RimColorEntryRgb   { 0.12f, 0.86f, 1.00f }; // [lin] - Entry portal glowing rim color
    Mathematics::LinearRGB      RimColorExitRgb    { 1.00f, 0.44f, 0.10f }; // [lin] - Exit portal glowing rim color
    std::vector<Mathematics::Axis2D> CustomPolygonUv {};                    // [m]   - Ordered 2D cutout polygon vertices (max 32)
    PortalEndpointPose         Entry              {};
    PortalEndpointPose         Exit               {};

    // Orthonormalizes Entry and Exit frames so Normal = cross(Right, Up) is exact unit length
    void NormalizeAxes() noexcept;

    // Exact 2D signed distance to the portal aperture contour in portal-local (u, v) meters:
    //   <= 0.0 : strictly inside the 2D portal opening
    //   >  0.0 : outside the 2D portal opening (distance to boundary edge)
    [[nodiscard]] float EvaluateShapeSignedDistance2D(float LocalU, float LocalV) const noexcept;

    // Exact rigid-body coordinate transport Entry -> Exit (det = +1, zero mirroring)
    [[nodiscard]] Mathematics::Axis3D TransformPointEntryToExit(const Mathematics::Axis3D& WorldPoint) const noexcept;
    [[nodiscard]] Mathematics::Axis3D TransformVectorEntryToExit(const Mathematics::Axis3D& WorldVector) const noexcept;

    // Exact rigid-body coordinate transport Exit -> Entry (exact involution)
    [[nodiscard]] Mathematics::Axis3D TransformPointExitToEntry(const Mathematics::Axis3D& WorldPoint) const noexcept;
    [[nodiscard]] Mathematics::Axis3D TransformVectorExitToEntry(const Mathematics::Axis3D& WorldVector) const noexcept;

    // Builds an expressive custom concave cutout polygon (e.g. arched sci-fi gateway, 5-point star, or keyhole)
    [[nodiscard]] static std::vector<Mathematics::Axis2D> BuildPresetCustomCutout(
        uint32_t PresetOrdinal,
        float    ScaleMeters = 2.40f);
};

//------------------------------------------------------------------------------------------------------------------------
//                                       SPLIT-OBJECT RENDER & PHYSICS OVERLAP READING
//------------------------------------------------------------------------------------------------------------------------

struct PortalVehicleOverlapReading
{
    bool                OverlapActive       = false; // True when object straddles an active portal plane inside its 2D aperture
    uint32_t            PortalPairIndex     = UINT32_MAX;
    bool                EnteredFromExitSide = false; // False => crossing Entry->Exit; True => crossing Exit->Entry
    float               SignedCenterDistM   = 0.0f;  // Signed distance of object center from source portal front normal
    float               ExposedFractionSrc  = 1.0f;  // [0..1] Fraction of object exposed on source world side
    float               ExposedFractionDst  = 0.0f;  // [0..1] Fraction of object emerged into destination world side
    float               ClipPlanePrimary[4] { 0.0f, 0.0f, 1.0f, 1.0e6f }; // Keeps only source-side half of primary object
    float               ClipPlaneClone[4]   { 0.0f, 0.0f, 1.0f, 1.0e6f }; // Keeps only destination-side half of exit clone
    Mathematics::Axis3D ClonePositionWorld  {};                           // Exact transported pose for emerging destination-side clone
    float               CloneYawZUpDegrees  = 0.0f;                       // Exact transported Z-up yaw for exit clone
};

// Evaluates exact split-portal overlap for an oriented vehicle/rigid object (Right-Handed Z-Up, +X nose)
[[nodiscard]] PortalVehicleOverlapReading EvaluateVehiclePortalOverlap(
    const std::vector<PortalPairSpecification>& Portals,
    const Mathematics::Axis3D&                  VehicleCenterWorld,
    float                                       VehicleYawZUpDegrees,
    float                                       HalfLengthMeters,
    float                                       HalfWidthMeters,
    float                                       HalfHeightMeters) noexcept;

} // namespace Frontier::World
