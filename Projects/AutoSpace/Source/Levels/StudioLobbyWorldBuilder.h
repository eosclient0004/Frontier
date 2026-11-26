/*============================================================================================================================================
                                                      STUDIOLOBBYWORLDBUILDER.H
============================================================================================================================================*/
// 🧩 Shared scene population interface implemented by Lobby 1 (Cyclorama), Lobby 2 (Forest Stage) & Lobby 3 (Racing Garage) inside MainWorld

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ControlVehicleSurfaceDecoder.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/World/CategoryInstances.h"
#include "Engine/World/WorldInstanceRegister.h"
#include "Source/Geometry/StudioProceduralSurface.h"
#include "Source/Policies/LobbyPolicy.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace AutoSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct HtmlAimAngles
{
    float YawDeg   = 0.0f; // [deg] - HTML Y-up azimuth yaw
    float PitchDeg = 0.0f; // [deg] - HTML Y-up elevation pitch
};

struct StudioCameraPresetView
{
    std::string                                       Id     {};
    std::string                                       Label  {};
    Frontier::GeometricRaster::PerspectiveCameraBasis Camera {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                               SCENE PLACEMENT HELPERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] HtmlAimAngles ComputeHtmlAimAngles(
    Frontier::Mathematics::Axis3D FromHtml,
    Frontier::Mathematics::Axis3D TargetHtml) noexcept;

[[nodiscard]] uint64_t HashStableId64(std::string_view Text) noexcept;

[[nodiscard]] Frontier::GeometricRaster::PolygonalAreaQuadLight MakeAimedAreaQuadLight(
    Frontier::Mathematics::Axis3D            CenterWorld,
    Frontier::Mathematics::Axis3D            AimTargetWorld,
    float                                    WidthMeters,
    float                                    HeightMeters,
    Frontier::Mathematics::LinearRGB         Radiance,
    bool                                     TwoSided        = false,
    Frontier::GeometricRaster::LightMobility Mobility        = Frontier::GeometricRaster::LightMobility::Static,
    uint32_t                                 LightRigGroupId = 0u) noexcept;

[[nodiscard]] Frontier::GeometricRaster::PhotometricPointRadiator MakeAimedPhotometricSpotLight(
    Frontier::Mathematics::Axis3D            PosWorld,
    Frontier::Mathematics::Axis3D            TargetWorld,
    Frontier::Mathematics::LinearRGB         PeakRgb,
    float                                    RangeMeters,
    uint32_t                                 ProfileOrdinal  = 2u,
    Frontier::GeometricRaster::LightMobility Mobility        = Frontier::GeometricRaster::LightMobility::Static,
    uint32_t                                 LightRigGroupId = 0u) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERFACE
//------------------------------------------------------------------------------------------------------------------------

class IStudioLobbySceneBuilderContext
{
public:
    virtual ~IStudioLobbySceneBuilderContext() noexcept = default;

    [[nodiscard]] virtual const Frontier::Codecs::AssembledControlVehicle& InspectControlVehicle() const noexcept = 0;
    [[nodiscard]] virtual const StudioMaterialPalette&                   InspectMaterialPalette() const noexcept = 0;
    [[nodiscard]] virtual Frontier::GeometricRaster::WorldSceneSpecification& AccessWorldScene() noexcept = 0;
    [[nodiscard]] virtual Frontier::World::WorldInstanceRegister&             AccessWorldRegister() noexcept = 0;

    [[nodiscard]] virtual Frontier::World::InstanceToken CreateFolderEntry(
        std::string_view               Title,
        std::string_view               Subtitle,
        Frontier::World::InstanceToken EnclosingToken) = 0;

    virtual Frontier::World::InstanceToken AppendSurfaceEntity(
        std::string_view                     StableId,
        std::string_view                     OutlinerTitle,
        std::string_view                     Subtitle,
        StudioBuiltMesh                      BuiltMesh,
        Frontier::Mathematics::Axis3D        WorldTranslation,
        uint16_t                             FallbackMaterialSlot,
        Frontier::World::InstanceToken       EnclosingToken,
        bool                                 CastShadows = true) = 0;

    virtual Frontier::World::InstanceToken AppendLuminaireEntity(
        std::string_view                                     Title,
        std::string_view                                     Subtitle,
        std::string_view                                     DomainTag,
        Frontier::Mathematics::Axis3D                        WorldPos,
        const Frontier::World::LuminaireInstanceParameters&  Params,
        Frontier::World::InstanceToken                       EnclosingToken) = 0;

    virtual Frontier::World::InstanceToken AppendCameraEntity(
        std::string_view                                  Title,
        std::string_view                                  Subtitle,
        Frontier::Mathematics::Axis3D                     WorldPos,
        const Frontier::World::CameraInstanceParameters&  Params,
        Frontier::World::InstanceToken                    EnclosingToken,
        bool                                              RemovalProtected = false) = 0;

    virtual void AppendAtmosphereEntity(
        std::string_view                                      Title,
        std::string_view                                      Subtitle,
        const Frontier::World::AtmosphereInstanceParameters&  Params,
        Frontier::World::InstanceToken                        EnclosingToken) = 0;

    virtual void AppendParticleEmitter(
        std::string_view               NodeId,
        std::string_view               OutlinerTitle,
        std::string_view               Subtitle,
        std::string_view               TemplateId,
        Frontier::Mathematics::Axis3D  WorldOriginZUp,
        uint32_t                       ParticleCount,
        float                          ReleaseRate,
        Frontier::World::InstanceToken EnclosingToken) = 0;

    virtual void AppendCameraPreset(StudioCameraPresetView Preset) = 0;
    virtual void IncrementVehicleCount() noexcept = 0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  LEVEL POPULATION ROUTINES
//------------------------------------------------------------------------------------------------------------------------

void BuildLobby1CycloramaLevel(IStudioLobbySceneBuilderContext& Ctx, const LobbyPolicy& Policy);
void BuildLobby2ForestStageLevel(IStudioLobbySceneBuilderContext& Ctx, const LobbyPolicy& Policy);
void BuildLobby3RacingGarageLevel(IStudioLobbySceneBuilderContext& Ctx, const LobbyPolicy& Policy);

} // namespace AutoSpace
