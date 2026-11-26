/*============================================================================================================================================
                                                            LOBBYLIFECYCLE.H
============================================================================================================================================*/
// 🧩 Single-level LobbyLifecycle managing Lobby 1 (Cyclorama), Lobby 2 (Forest Stage) & Lobby 3 (Racing Garage) by LobbyPolicy inside MainWorld

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ControlVehicleSurfaceDecoder.h"
#include "Engine/Codecs/PhotometricProfile.h"
#include "Engine/ComponentClass/ControlVehicleClassBuilder.h"
#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/ParticleSpace/CarrierSolidRegistry.h"
#include "Engine/ParticleSpace/ParticleCloudTypes.h"
#include "Engine/ParticleSpace/ParticleGpuSimulation.h"
#include "Engine/ParticleSpace/ParticleTemplateRegistry.h"
#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"
#include "Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"
#include "Engine/World/CategoryInstances.h"
#include "Engine/World/WorldInstanceRegister.h"
#include "Engine/World/WorldSceneSyncBridge.h"
#include "Source/Geometry/StudioProceduralSurface.h"
#include "Source/Levels/StudioLobbyWorldBuilder.h"
#include "Source/Policies/LobbyPolicy.h"

#if FRONTIER_PROFILE_EDITOR
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Editor/WorldInstanceEditorBridge.h"
#endif

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace AutoSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LobbyGeometryAsset
{
    std::string                                   StableId {};
    Frontier::GeometrySpace::PolygonNetwork       Network  {};
    Frontier::SpatialAcceleration::ClusterPyramid Pyramid  {};
};

struct LobbyLifecycleTelemetry
{
    StudioLobbyId ActiveLobby             = StudioLobbyId::StudioLobby1Cyclorama;
    uint32_t      VehicleCount            = 0u;
    uint32_t      SurfaceInstanceCount    = 0u;
    uint32_t      AreaLightCount          = 0u;
    uint32_t      PhotometricLightCount   = 0u;
    uint32_t      CameraRigCount          = 0u;
    uint32_t      ParticleCloudCount      = 0u;
    uint32_t      SimulatedParticleCount  = 0u;
    uint32_t      TotalMeshTriangles      = 0u;
    uint32_t      OutlinerRowCount        = 0u;
    bool          VehicleDecoderLoaded    = false;
    bool          PhotometryLoaded        = false;
    bool          ParticleTemplatesLoaded = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     LOBBYLIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

class LobbyLifecycle final : private IStudioLobbySceneBuilderContext
{
public:
    LobbyLifecycle() noexcept = default;
    ~LobbyLifecycle() noexcept override;

    LobbyLifecycle(const LobbyLifecycle&)            = delete;
    LobbyLifecycle& operator=(const LobbyLifecycle&) = delete;

    [[nodiscard]] bool InitializeAndBuild(
        const std::filesystem::path& RepoRoot,
        const LobbyPolicy&           Policy,
        std::string&                 OutDiagnostic);

    void Shutdown() noexcept;

#if FRONTIER_PROFILE_EDITOR
    void AttachOutliner(Frontier::OutlinerPanel* Outliner) noexcept;
#endif

    void TickSimulation(double FixedDeltaSeconds) noexcept;

    [[nodiscard]] bool SubmitHeroVehicleDirective(
        const Frontier::SimulationSpace::PhysicsSpace::VehicleControlDirective& Directive) noexcept;
    [[nodiscard]] Frontier::SimulationSpace::PhysicsSpace::VehiclePhysicsRig::VehicleReadingLease
        AcquireHeroVehicleReading() const noexcept;
    [[nodiscard]] bool QueryHeroVehiclePhysicsActive() const noexcept { return HeroVehiclePhysicsActive; }

    [[nodiscard]] LobbyLifecycleTelemetry                                       InspectTelemetry() const noexcept;
    [[nodiscard]] const LobbyPolicy&                                            InspectPolicy() const noexcept { return ActivePolicy; }
    [[nodiscard]] Frontier::GeometricRaster::WorldSceneSpecification&           AccessWorldScene() noexcept override { return WorldScene; }
    [[nodiscard]] const Frontier::GeometricRaster::WorldSceneSpecification&     InspectWorldScene() const noexcept { return WorldScene; }
    [[nodiscard]] const Frontier::World::WorldInstanceRegister&               InspectWorldRegister() const noexcept { return WorldRegister; }
    [[nodiscard]] const std::vector<StudioCameraPresetView>&                  InspectCameraPresets() const noexcept { return CameraPresets; }
    [[nodiscard]] const std::vector<Frontier::ParticleSpace::ParticleCloudNode>& InspectParticleClouds() const noexcept { return ParticleClouds; }
    [[nodiscard]] const Frontier::ParticleSpace::ParticleGpuSimulation&         InspectParticleSimulation() const noexcept { return ParticleSimulation; }
    [[nodiscard]] const Frontier::ParticleSpace::ParticleSimulationRegion&      InspectParticleRegion() const noexcept { return ParticleRegion; }
    [[nodiscard]] double                                                        InspectSimulationClockSeconds() const noexcept { return SimulationClockSeconds; }

private:
    // IStudioLobbySceneBuilderContext overrides
    [[nodiscard]] const Frontier::Codecs::AssembledControlVehicle& InspectControlVehicle() const noexcept override { return ControlVehicleTemplate; }
    [[nodiscard]] const StudioMaterialPalette&                   InspectMaterialPalette() const noexcept override { return MaterialPalette; }
    [[nodiscard]] Frontier::World::WorldInstanceRegister&             AccessWorldRegister() noexcept override { return WorldRegister; }

    Frontier::World::InstanceToken CreateFolderEntry(
        std::string_view               Title,
        std::string_view               Subtitle,
        Frontier::World::InstanceToken EnclosingToken) override;

    Frontier::World::InstanceToken AppendSurfaceEntity(
        std::string_view              StableId,
        std::string_view              OutlinerTitle,
        std::string_view              Subtitle,
        StudioBuiltMesh               BuiltMesh,
        Frontier::Mathematics::Axis3D WorldTranslation,
        uint16_t                      FallbackMaterialSlot,
        Frontier::World::InstanceToken EnclosingToken,
        bool                          CastShadows = true) override;

    Frontier::World::InstanceToken AppendLuminaireEntity(
        std::string_view                                     Title,
        std::string_view                                     Subtitle,
        std::string_view                                     DomainTag,
        Frontier::Mathematics::Axis3D                        WorldPos,
        const Frontier::World::LuminaireInstanceParameters&  Params,
        Frontier::World::InstanceToken                       EnclosingToken) override;

    Frontier::World::InstanceToken AppendCameraEntity(
        std::string_view                                  Title,
        std::string_view                                  Subtitle,
        Frontier::Mathematics::Axis3D                     WorldPos,
        const Frontier::World::CameraInstanceParameters&  Params,
        Frontier::World::InstanceToken                    EnclosingToken,
        bool                                              RemovalProtected = false) override;

    void AppendAtmosphereEntity(
        std::string_view                                      Title,
        std::string_view                                      Subtitle,
        const Frontier::World::AtmosphereInstanceParameters&  Params,
        Frontier::World::InstanceToken                        EnclosingToken) override;

    void AppendParticleEmitter(
        std::string_view               NodeId,
        std::string_view               OutlinerTitle,
        std::string_view               Subtitle,
        std::string_view               TemplateId,
        Frontier::Mathematics::Axis3D  WorldOriginZUp,
        uint32_t                       ParticleCount,
        float                          ReleaseRate,
        Frontier::World::InstanceToken EnclosingToken) override;

    void AppendCameraPreset(StudioCameraPresetView Preset) override { CameraPresets.push_back(std::move(Preset)); }
    void IncrementVehicleCount() noexcept override { ++VehicleCount; }

    [[nodiscard]] bool EnsureSharedAssetsLoaded(const std::filesystem::path& RepoRoot, std::string& OutDiagnostic);
    void ClearLobbyObjects() noexcept;
    void FinalizeWorldSceneAcceleration();

    void SyncOutlinerEntryMetadata(
        Frontier::World::InstanceToken Token,
        std::string_view               Subtitle,
        std::string_view               DomainTag,
        Frontier::Mathematics::Axis3D  WorldPos,
        Frontier::Mathematics::Axis3D  RotationDeg      = {},
        float                          Scale            = 1.0f,
        bool                           RemovalProtected = false);

    // Shared decoded content across in-place LobbyPolicy switches
    bool                                                                 SharedAssetsReady      = false;
    bool                                                                 ParticleCatalogReady   = false;
    Frontier::Codecs::AssembledControlVehicle                            ControlVehicleTemplate {};
    std::vector<Frontier::Codecs::ResampledPhotometricProfile>           PhotometrySuite        {};
    Frontier::SimulationSpace::EffectLayers::EffectLayerSpecificationSet ParticleSpecs          {};
    Frontier::ParticleSpace::ParticleTemplateRegistry                    ParticleTemplates      {};
    Frontier::ParticleSpace::CarrierSolidRegistry                        CarrierSolids          {};

    // Live single-level world & simulation state
    LobbyPolicy                                                          ActivePolicy           {};
    StudioMaterialPalette                                                MaterialPalette        {};
    Frontier::World::WorldInstanceRegister                               WorldRegister          {};
    Frontier::World::WorldSceneSyncBridge                                SceneSyncBridge        {};
    Frontier::GeometricRaster::WorldSceneSpecification                   WorldScene             {};
    std::vector<std::unique_ptr<LobbyGeometryAsset>>                     GeometryAssets         {};
    std::vector<Frontier::World::CategoryInstanceHandle>                 ManagedCategoryHandles {};
    std::vector<Frontier::World::InstanceToken>                          FolderTokens           {};
    std::vector<StudioCameraPresetView>                                  CameraPresets          {};

    std::vector<Frontier::ParticleSpace::ParticleCloudNode>              ParticleClouds         {};
    Frontier::ParticleSpace::ParticleGpuSimulation                       ParticleSimulation     {};
    Frontier::ParticleSpace::ParticleSimulationRegion                    ParticleRegion         {};

    // Optional live Jolt VehiclePhysicsRig (3-lane fan trace + socket-free ConstraintSolver rig) bound to Hero Vehicle
    bool                                                                 ControlVehicleClassReady = false;
    Frontier::ComponentClass::ControlVehicleBuildResult                  ControlVehicleClass      {};
    bool                                                                 HeroVehiclePhysicsActive = false;
    Frontier::World::InstanceToken                                       HeroVehicleEntityToken   {};
    Frontier::Mathematics::Axis3D                                        HeroVehicleSpawnZUp      {};
    std::unique_ptr<Frontier::SimulationSpace::PhysicsSpace::PhysicsExtension>  HeroPhysics       {};
    std::unique_ptr<Frontier::SimulationSpace::PhysicsSpace::VehiclePhysicsRig> HeroVehicleRig    {};

    double                                                               SimulationClockSeconds = 0.0;
    uint32_t                                                             VehicleCount           = 0u;
    uint32_t                                                             TotalMeshTriangles     = 0u;

#if FRONTIER_PROFILE_EDITOR
    Frontier::OutlinerPanel*                                             BoundOutliner          = nullptr;
    std::unique_ptr<Frontier::Editor::WorldInstanceEditorBridge>         EditorBridge           {};
#endif
};

} // namespace AutoSpace
