/*============================================================================================================================================
                                                         COMPILEDSURFACEFIELD.H
============================================================================================================================================*/
// 🧩 GPU-ready compiled parametric B-rep signed distance field ('BSDF'), embedded 3D/2D analytic curves ('CRV1') & fp16 volume ('VSDF')

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/LocalSignedDistanceVolume.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier::DistanceFieldSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t CompiledSurfaceFieldMagic   = 0x46445342u; // [tag] - 'BSDF' exact parametric B-rep SDF section
inline constexpr uint32_t CompiledSurfaceFieldVersion = 1u;          // [ver] - Binary layout version

inline constexpr uint32_t ParametricCurvePackageMagic   = 0x31565243u; // [tag] - 'CRV1' embedded 3D/2D parametric curve section
inline constexpr uint32_t ParametricCurvePackageVersion = 1u;          // [ver] - Binary layout version

inline constexpr uint32_t BakedVolumeSectionMagic   = 0x46445356u; // [tag] - 'VSDF' pre-baked fp16 LocalSignedDistanceVolume section
inline constexpr uint32_t BakedVolumeSectionVersion = 1u;          // [ver] - Binary layout version

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class SurfacePrimitiveCategory : uint32_t
{
    Plane             = 0u, // [-] - Exact analytical plane
    Cylinder          = 1u, // [-] - Exact analytical cylinder
    Cone              = 2u, // [-] - Exact analytical cone
    Sphere            = 3u, // [-] - Exact analytical sphere
    Torus             = 4u, // [-] - Exact analytical torus / rolling-ball fillet ring
    RationalBezier4x4 = 5u, // [-] - Exact knot-split 4x4 rational cubic Bezier patch
};

enum class TrimSegmentCategory : uint32_t
{
    Line2D        = 0u, // [-] - Exact straight 2D parameter-chart segment
    CircularArc2D = 1u, // [-] - Exact 2D parameter-chart circular/isotropic arc
};

enum class EdgeProjectionCategory : uint32_t
{
    StraightSegment3D = 0u, // [-] - Exact 3D line segment between StartPoint and EndPoint
    ConicArc3D        = 1u, // [-] - Exact 3D circular/elliptical arc indexed in ConicArcs3D
};

enum class ParametricCurveCategory : uint32_t
{
    StraightLine3D       = 0u, // [-] - P(t) = Origin + t * Direction
    CircularArc3D        = 1u, // [-] - P(t) = Centre + Radius * (cos(t)*AxisX + sin(t)*AxisY)
    EllipticalArc3D      = 2u, // [-] - P(t) = Centre + Major*cos(t)*AxisX + Minor*sin(t)*AxisY
    RationalBezierSpan3D = 3u, // [-] - Exact rational Bezier curve span over Poles[FirstPole..+Count]
};

//------------------------------------------------------------------------------------------------------------------------
//                                             GPU-ALIGNED 16B STD430 STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 128-byte (8 x float4) std430 surface primitive matching SolidArcSurfaceField.slang 1:1
struct alignas(16) CompiledSurfacePrimitive
{
    std::array<float, 4u> BoundsLowCategory      {}; // [m, u32] - xyz = AABB min [m], w = bitcast<float>(SurfacePrimitiveCategory)
    std::array<float, 4u> BoundsHighPatch    {}; // [m, u32] - xyz = AABB max [m], w = bitcast<float>(BezierPatchOrdinal)
    std::array<float, 4u> OriginRadius0      {}; // [m]      - xyz = Basis origin [m], w = Radius / OriginRadius / MajorRadius [m]
    std::array<float, 4u> AxisXRadius1       {}; // [-, m]   - xyz = Basis X axis, w = MinorRadius [m] or RadialSlope [-]
    std::array<float, 4u> AxisYPeriodU       {}; // [-, rad] - xyz = Basis Y axis, w = PeriodU (0 if non-periodic)
    std::array<float, 4u> AxisZPeriodV       {}; // [-, rad] - xyz = Basis Z axis, w = PeriodV (0 if non-periodic)
    std::array<float, 4u> TrimSpanSenseBody  {}; // [idx, -] - x = FirstTrim, y = TrimCount, z = OutwardSense (+1/-1), w = BodyOrdinal
    std::array<float, 4u> ChartBounds        {}; // [uv]     - xy = ChartLow (uMin, vMin), zw = ChartHigh (uMax, vMax)
};

static_assert(sizeof(CompiledSurfacePrimitive) == 128u, "CompiledSurfacePrimitive must occupy 128 std430 bytes.");

// 📝 32-byte (2 x float4) std430 2D chart trim segment (exact line or exact 2D circular arc)
struct alignas(16) CompiledTrimSegment2D
{
    // Line2D:        Coords0 = (u0, v0, u1, v1),          Coords1 = (0, 0, 0, 0)
    // CircularArc2D: Coords0 = (centerU, centerV, R, t0), Coords1 = (sweepDelta, 1.0, 0, 0)
    std::array<float, 4u> Coords0 {}; // [uv] - Primary 2D chart parameters
    std::array<float, 4u> Coords1 {}; // [uv] - Secondary parameters & TrimSegmentCategory in y
};

static_assert(sizeof(CompiledTrimSegment2D) == 32u, "CompiledTrimSegment2D must occupy 32 std430 bytes.");

// 📝 256-byte (16 x float4) std430 rational 4x4 cubic Bezier surface patch (xyz = Euclidean pole [m], w = weight > 0)
struct alignas(16) CompiledBezierPatch4x4
{
    std::array<std::array<float, 4u>, 16u> PolesWeighted {}; // [m, -] - u-major 4x4 poles: index = uRow * 4 + vCol
};

static_assert(sizeof(CompiledBezierPatch4x4) == 256u, "CompiledBezierPatch4x4 must occupy 256 std430 bytes.");

// 📝 64-byte (4 x float4) std430 3D circular/elliptical conic arc carrier for exact 0-chord edge projection
struct alignas(16) CompiledConicArc3D
{
    std::array<float, 4u> CentreStartAngle {}; // [m, rad] - xyz = Centre [m], w = IntervalLow [rad]
    std::array<float, 4u> CosineAxisSweep  {}; // [m, rad] - xyz = Cosine semi-axis [m], w = SweepHigh - Low [rad]
    std::array<float, 4u> SineAxisMajor    {}; // [m]      - xyz = Sine semi-axis [m], w = MajorRadius [m]
    std::array<float, 4u> PlaneNormalMinor {}; // [-, m]   - xyz = Unit plane normal, w = MinorRadius [m]
};

static_assert(sizeof(CompiledConicArc3D) == 64u, "CompiledConicArc3D must occupy 64 std430 bytes.");

// 📝 80-byte (5 x float4) std430 3D trim edge with Baerentzen-Aanaes angle-weighted edge & endpoint pseudo-normals
struct alignas(16) CompiledPseudoNormalEdge
{
    std::array<float, 4u> StartPointCategory     {}; // [m, u32] - xyz = Start 3D point [m], w = bitcast<float>(EdgeProjectionCategory)
    std::array<float, 4u> EndPointArcOrdinal {}; // [m, u32] - xyz = End 3D point [m], w = bitcast<float>(ConicArcOrdinal)
    std::array<float, 4u> EdgePseudoNormal   {}; // [-, rad] - xyz = Dihedral angle-weighted unit normal, w = DihedralRadians
    std::array<float, 4u> StartVertexNormal  {}; // [-]      - xyz = Angle-weighted vertex normal at Start, w = BodyOrdinal
    std::array<float, 4u> EndVertexNormal    {}; // [-]      - xyz = Angle-weighted vertex normal at End, w = NaiveFaceNormalSign
};

static_assert(sizeof(CompiledPseudoNormalEdge) == 80u, "CompiledPseudoNormalEdge must occupy 80 std430 bytes.");

// 📝 16-byte (uint4) std430 narrow-band macro-cell accelerator entry
struct alignas(16) CompiledMacroCell
{
    uint32_t CenterDistanceBits = 0u; // [bits] - IEEE-754 float32 signed distance at cell center [m]
    uint32_t FirstSurfaceEntry  = 0u; // [idx]  - Offset into CellSurfaceIndices
    uint32_t PackedCounts       = 0u; // [cnt]  - low16 = SurfaceCount, high16 = EdgeCount (0 = outside narrow band!)
    uint32_t FirstEdgeEntry     = 0u; // [idx]  - Offset into CellEdgeIndices
};

static_assert(sizeof(CompiledMacroCell) == 16u, "CompiledMacroCell must occupy 16 std430 bytes.");

//------------------------------------------------------------------------------------------------------------------------
//                                           EMBEDDED .CURVE ('CRV1') PACKAGE
//------------------------------------------------------------------------------------------------------------------------

struct ParametricCurveEntry
{
    ParametricCurveCategory Category          = ParametricCurveCategory::StraightLine3D; // [-]   - Analytic or rational Bezier carrier
    uint32_t                BodyOrdinal       = 0u;      // [idx] - Owning body ordinal (0xFFFF for unconsumed sketch curve)
    uint32_t                FirstPoleIndex    = 0u;      // [idx] - Start in BezierPolesWeighted when RationalBezierSpan3D
    uint32_t                PoleCount         = 0u;      // [cnt] - Degree + 1 poles when RationalBezierSpan3D
    Mathematics::Axis3D     OriginOrCentre    {};        // [m]   - Line origin or conic centre
    Mathematics::Axis3D     AxisXOrDirection  { 1.0f, 0.0f, 0.0f }; // [-] - Unit line direction or conic X axis
    Mathematics::Axis3D     AxisY             { 0.0f, 1.0f, 0.0f }; // [-] - Conic Y axis
    float                   MajorRadiusMeters = 0.0f;    // [m]   - Circle radius or ellipse major semi-axis
    float                   MinorRadiusMeters = 0.0f;    // [m]   - Ellipse minor semi-axis
    float                   IntervalLow       = 0.0f;    // [m/rad] - Start parameter
    float                   IntervalHigh      = 1.0f;    // [m/rad] - End parameter
    float                   DihedralRadians   = 0.0f;    // [rad] - Sharpness across adjacent faces (0 = smooth / wire)
    uint8_t                 ClosedCondition   = 0u;      // [0/1] - 1 when curve forms a closed periodic loop
};

struct ParametricCurvePackage
{
    std::vector<ParametricCurveEntry>      Curves              {}; // [-]    - 3D B-rep edges & standalone sketch curves
    std::vector<std::array<float, 4u>>     BezierPolesWeighted {}; // [m, -] - Shared weighted poles (xyz [m], w > 0)

    void Reset() noexcept;
    [[nodiscard]] std::size_t QueryMemoryBytes() const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                         COMPILED SURFACE FIELD & QUERY API
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceFieldQuerySettings
{
    uint32_t GaussNewtonIterations = 3u;   // [cnt] - Damped 2x2 Gauss-Newton steps for RationalBezier4x4 (0..4)
    bool     EnableParameterTrim   = true; // [-]   - Rule 1: enforce 2D UV trim loops on analytical & Bezier faces
    bool     EnablePseudoNormals   = true; // [-]   - Rule 2: enforce Baerentzen-Aanaes edge/vertex pseudo-normals
    bool     EnableMacroCellSkip   = true; // [-]   - Use narrow-band macro-cell empty-space leap when outside band
    float    HollowWallMeters       = 0.0f; // [m]   - 0 = closed solid SDF; >0 = hollow sheet-metal wall |d| - 0.5*t
};

struct SurfaceDistanceSample
{
    float                    SignedDistanceMeters = 1.0e30f;                           // [m]   - Exact signed distance (<0 inside)
    Mathematics::Axis3D      OutwardGradient      { 0.0f, 1.0f, 0.0f };                // [-]   - Unit outward SDF gradient
    Mathematics::Axis3D      ClosestSurfacePoint  {};                                  // [m]   - Closest point on trimmed B-rep
    SurfacePrimitiveCategory WinningCategory      = SurfacePrimitiveCategory::Plane;   // [-]   - Carrier that won closest point
    uint32_t                 WinningBodyOrdinal   = 0u;                                // [idx] - Owning body ordinal
    uint32_t                 TestedPrimitives     = 0u;                                // [cnt] - Narrow-band primitive cost
};

class CompiledSurfaceField
{
public:
    Mathematics::Axis3D                   GridBoundsMin      {};  // [m]   - Padded macro-grid minimum corner
    Mathematics::Axis3D                   GridBoundsMax      {};  // [m]   - Padded macro-grid maximum corner
    std::array<uint32_t, 3u>              MacroDimensions    { 1u, 1u, 1u }; // [cnt] - Aspect-matched (Mx, My, Mz)
    float                                 CellRadiusMeters   = 0.0f; // [m] - Half-diagonal of one macro-cell
    float                                 EdgeTieTolerance   = 1.0e-5f; // [m] - Face-vs-edge tie-break margin
    std::vector<CompiledSurfacePrimitive> Surfaces           {};  // [-]   - Analytic + RationalBezier4x4 primitives
    std::vector<CompiledTrimSegment2D>    TrimSegments2D     {};  // [-]   - 2D parameter-space trim loop segments
    std::vector<CompiledBezierPatch4x4>   BezierPatches      {};  // [-]   - 4x4 rational cubic Bezier control nets
    std::vector<CompiledConicArc3D>       ConicArcs3D        {};  // [-]   - Exact 3D circular/elliptical edge carriers
    std::vector<CompiledPseudoNormalEdge> Edges              {};  // [-]   - 3D trim edges with pseudo-normals
    std::vector<CompiledMacroCell>        MacroCells         {};  // [-]   - Mx * My * Mz narrow-band cells
    std::vector<uint32_t>                 CellSurfaceIndices {};  // [idx] - Flattened candidate surface indices
    std::vector<uint32_t>                 CellEdgeIndices    {};  // [idx] - Flattened candidate edge indices

    void Reset() noexcept;

    [[nodiscard]] bool EmptyCondition() const noexcept { return Surfaces.empty(); }

    [[nodiscard]] std::size_t QueryGpuPayloadBytes() const noexcept;

    // Exact narrow-band (or unaccelerated) signed distance + analytical outward gradient query in SI meters
    [[nodiscard]] SurfaceDistanceSample QuerySignedDistance(
        Mathematics::Axis3D              PositionMeters,
        const SurfaceFieldQuerySettings& Settings = {}) const noexcept;

    // Exact unaccelerated full-surface reference query used when baking macro-cell centers outside narrow band
    [[nodiscard]] SurfaceDistanceSample QueryExactUnaccelerated(
        Mathematics::Axis3D              PositionMeters,
        const SurfaceFieldQuerySettings& Settings = {}) const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                            BINARY SECTION ENCODERS / DECODERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool EncodeCompiledSurfaceFieldBinary(
    const CompiledSurfaceField& SourceField,
    std::vector<std::byte>&     OutBytes,
    std::string&                OutDiagnostic);

[[nodiscard]] bool DecodeCompiledSurfaceFieldBinary(
    std::span<const std::byte> InputBytes,
    CompiledSurfaceField&      OutField,
    std::string&               OutDiagnostic);

[[nodiscard]] bool EncodeParametricCurvePackageBinary(
    const ParametricCurvePackage& SourceCurves,
    std::vector<std::byte>&       OutBytes,
    std::string&                  OutDiagnostic);

[[nodiscard]] bool DecodeParametricCurvePackageBinary(
    std::span<const std::byte> InputBytes,
    ParametricCurvePackage&    OutCurves,
    std::string&               OutDiagnostic);

[[nodiscard]] bool EncodeLocalDistanceVolumeBinary(
    const LocalSignedDistanceVolume& SourceVolume,
    std::vector<std::byte>&          OutBytes,
    std::string&                     OutDiagnostic);

[[nodiscard]] bool DecodeLocalDistanceVolumeBinary(
    std::span<const std::byte> InputBytes,
    LocalSignedDistanceVolume& OutVolume,
    std::string&               OutDiagnostic);

// IEEE-754 binary16 <-> binary32 conversion shared by SolidArc compiler & 'VSDF' codec
[[nodiscard]] uint16_t PackFloat32ToBinary16(float ScalarInput) noexcept;
[[nodiscard]] float    UnpackBinary16ToFloat32(uint16_t HalfWord) noexcept;

} // namespace Frontier::DistanceFieldSpace
