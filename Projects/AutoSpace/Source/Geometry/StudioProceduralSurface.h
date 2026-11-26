/*============================================================================================================================================
                                                        STUDIOPROCEDURALSURFACE.H
============================================================================================================================================*/
// 🧩 C++ port of ProjectZeroEditor's StudioProceduralSurface.js (HTML Y-up procedural builders mapped cleanly to FrontierWorldZUp)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ControlVehicleSurfaceDecoder.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/SurfaceMaterial.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace AutoSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class StudioMaterialCode : uint16_t
{
    DefaultClay = 0u, // Reserve slot 0 as neutral fallback so all authored faces carry MaterialSlot >= 1
    Cyclorama,
    TiledFloor,
    TiledFloorAlt,
    CeilingRib,
    DarkMetal,
    ChromeMetal,
    RubberMat,
    EmissiveDiffuser,
    UmbrellaSilver,
    TetherScreen,
    PelicanCase,
    ForestBackdrop,
    BlackFlag,
    FanHousing,
    FanBlade,
    CherryRedTool,
    SafetyYellow,
    ConcreteGarage,
    TrackAsphalt,
    TrackPaintWhite,
    TrackGrass,
    StadiumSkyBlue,
    StadiumSkyDome,
    TimberBench,

    // Shared vehicle parts (per-vehicle body paint slot gets registered dynamically per paint finish)
    VehicleRimAlloy,
    VehicleGlossCanopy,
    VehicleGlass,
    VehicleIntakePolymer,
    VehicleTireRubber,
    VehicleTrimMetal,
    VehicleHeadlightOptic,
    VehicleTaillightOptic,
    VehicleBrakeCaliper,

    Count
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct StudioMaterialPalette
{
    std::array<uint16_t, static_cast<std::size_t>(StudioMaterialCode::Count)> Slots {}; // [slot] - CompactMaterialRegister slot IDs

    [[nodiscard]] uint16_t Resolve(StudioMaterialCode Code) const noexcept
    {
        const std::size_t Idx = static_cast<std::size_t>(Code);
        return (Idx < Slots.size() && Slots[Idx] != 0u) ? Slots[Idx] : 1u;
    }
};

struct StudioMeshBakeTransform
{
    float UniformScale = 1.0f; // [-]   - Uniform object scale
    float YawDegrees   = 0.0f; // [deg] - HTML studio Y-up yaw around vertical axis
    float PitchDegrees = 0.0f; // [deg] - HTML local X pitch / incline
    float RollDegrees  = 0.0f; // [deg] - HTML local Z roll / bank
};

struct StudioBuiltMesh
{
    Frontier::GeometrySpace::PolygonNetwork Network               {}; // [-] - Triangulated PolygonNetwork in FrontierWorldZUp
    std::vector<float>                      AuthoredCornerNormals {}; // [-] - 9 floats per face (Nx0,Ny0,Nz0, Nx1,Ny1,Nz1, Nx2,Ny2,Nz2)
};

struct CycloramaParams
{
    float CoveWidth  = 18.0f;
    float FloorDepth = 13.5f;
    float WallHeight = 7.0f;
    float CoveRadius = 2.2f;
    bool  SideWings  = true;
};

struct TiledFloorParams
{
    float FloorWidth   = 48.0f;
    float FloorDepth   = 40.0f;
    float TilePitch    = 1.2f;
    bool  CheckerTiles = true;
};

struct CeilingTrussParams
{
    float    SpanWidth     = 34.0f;
    float    SpanDepth     = 28.0f;
    float    CeilingHeight = 7.25f;
    uint32_t RibCount      = 16u;
    bool     TrussGrid     = true;
};

struct TripodParams
{
    float    StandHeight  = 1.55f;
    float    ColumnLift   = 0.28f;
    float    LegSpreadDeg = 26.0f;
    float    TubeRadius   = 0.018f;
    uint32_t LegSegments  = 3u;
    float    HeadTiltDeg  = 0.0f;
    bool     Spreader     = true;
    bool     Casters      = false;
    bool     FloorMat     = false;
    bool     PowerCable   = true;
};

struct CinemaCameraParams
{
    std::string CameraStyle = "Cinema Matte Box";
    float       LensLength  = 0.32f;
    float       ReelRadius  = 0.13f;
    bool        MatteBox    = true;
    bool        TopHandle   = true;
    float       PitchDeg    = 0.0f;
};

struct LightFixtureParams
{
    std::string FixtureStyle   = "Overhead Softbox";
    float       ApertureWidth  = 6.2f;
    float       ApertureHeight = 3.6f;
    float       HousingDepth   = 0.28f;
    float       YokeTiltDeg    = 0.0f;
    float       BarnDoorFlare  = 34.0f;
    uint32_t    UmbrellaRibs   = 16u;
    float       SuspensionDrop = 2.88f;
};

struct WorkstationParams
{
    float DeskWidth    = 1.48f;
    float DeskDepth    = 0.72f;
    float DeskHeight   = 0.78f;
    bool  HasDesk      = true;
    bool  FlightCases  = true;
    bool  CoiledCables = true;
};

struct ForestBackdropParams
{
    float ScreenWidth     = 12.4f;
    float ScreenHeight    = 6.2f;
    float ProsceniumWidth = 20.5f;
    float WallHeight      = 7.25f;
    bool  StudioFlags     = true;
};

struct StudioFanParams
{
    float StandHeight = 1.28f;
    float FanRadius   = 0.52f;
    float TiltDeg     = -4.0f;
};

struct GarageShellParams
{
    float GarageWidth    = 22.0f;
    float GarageDepth    = 18.0f;
    float GarageHeight   = 7.15f;
    bool  ShowMezzanine  = true;
    bool  ShowLinearLeds = true;
};

struct CarLiftParams
{
    float LiftHeight = 2.12f;
    float PostHeight = 3.85f;
};

struct GaragePropParams
{
    std::string PropVariant = "tool-cart";
};

struct GarageCablesParams
{
    float CableSpan = 11.5f;
};

struct RaceTrackParams
{
    float    TrackLength     = 150.0f;
    uint32_t TrackLanes      = 3u;
    bool     ShowGrandstands = true;
    bool     ShowGantry      = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

// Coordinate mapping: HTML StudioYUp (X, Y, Z) -> FrontierWorldZUp via Engine CoordinateSpace (det = +1)
[[nodiscard]] Frontier::Mathematics::Axis3D ConvertHtmlPositionToWorldZUp(
    float Hx,
    float Hy,
    float Hz) noexcept;

[[nodiscard]] Frontier::Mathematics::Axis3D ConvertHtmlYUpToWorldZUp(
    Frontier::Mathematics::Axis3D HtmlPt,
    const StudioMeshBakeTransform& Transform = {}) noexcept;

[[nodiscard]] Frontier::Mathematics::Axis3D ConvertHtmlYUpNormalToWorldZUp(
    Frontier::Mathematics::Axis3D HtmlNormal,
    const StudioMeshBakeTransform& Transform = {}) noexcept;

[[nodiscard]] Frontier::Mathematics::LinearRGB SrgbHexToLinearRgb(uint32_t HexRgb) noexcept;
[[nodiscard]] float                            ComputeTripodMountHeight(const TripodParams& Params) noexcept;

// Register all studio materials + vehicle shared materials into CompactMaterialRegister (slots >= 1)
void RegisterStudioBaseMaterials(
    Frontier::MaterialSpace::CompactMaterialRegister& MaterialRegister,
    StudioMaterialPalette&                            OutPalette);

[[nodiscard]] uint16_t RegisterVehicleBodyPaintMaterial(
    Frontier::MaterialSpace::CompactMaterialRegister& MaterialRegister,
    std::string_view                                  MaterialTitle,
    Frontier::Mathematics::LinearRGB                  BodyColorLinear,
    float                                             Metallic = 0.25f,
    float                                             Roughness = 0.16f,
    float                                             ClearcoatWeight = 0.92f);

// Procedural surface constructors (exact geometry from StudioProceduralSurface.js in FrontierWorldZUp)
[[nodiscard]] StudioBuiltMesh BuildCycloramaSurface(
    const CycloramaParams&       Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildTiledFloorSurface(
    const TiledFloorParams&      Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildCeilingTrussSurface(
    const CeilingTrussParams&    Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildTripodSurface(
    const TripodParams&          Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildCinemaCameraSurface(
    const CinemaCameraParams&    Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildLightFixtureSurface(
    const LightFixtureParams&    Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildWorkstationSurface(
    const WorkstationParams&     Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildForestBackdropSurface(
    const ForestBackdropParams&  Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildStudioFanSurface(
    const StudioFanParams&       Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildGarageShellSurface(
    const GarageShellParams&     Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildCarLiftSurface(
    const CarLiftParams&         Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildGaragePropsSurface(
    const GaragePropParams&      Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildGarageCablesSurface(
    const GarageCablesParams&    Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

[[nodiscard]] StudioBuiltMesh BuildRaceTrackSurface(
    const RaceTrackParams&       Params,
    const StudioMaterialPalette& Palette,
    const StudioMeshBakeTransform& Transform = {});

// Convert decoded FCT1 25,068-tri vehicle into a scaled/rotated multi-material StudioBuiltMesh resting flush at Z >= 0
[[nodiscard]] StudioBuiltMesh BuildAssembledControlVehicleSurface(
    const Frontier::Codecs::AssembledControlVehicle& Vehicle,
    const StudioMaterialPalette&                     Palette,
    uint16_t                                         BodyPaintMaterialSlot,
    uint16_t                                         BrakeCaliperMaterialSlot,
    const StudioMeshBakeTransform&                   Transform = {},
    std::string_view                                 MeshTitle = "StudioVehicle");

} // namespace AutoSpace
