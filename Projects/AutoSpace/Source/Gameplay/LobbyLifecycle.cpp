/*============================================================================================================================================
                                                           LOBBYLIFECYCLE.CPP
============================================================================================================================================*/
// 🧩 Single-level LobbyLifecycle implementation driving all 3 Studio Lobbies via LobbyPolicy inside MainWorld.WorldSpace

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Gameplay/LobbyLifecycle.h"

#include "Engine/Codecs/PhotometrySpaceCodec.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace AutoSpace
{

using namespace Frontier;
using namespace Frontier::Codecs;
using namespace Frontier::GeometricRaster;
using namespace Frontier::Mathematics;
using namespace Frontier::ParticleSpace;
using namespace Frontier::World;

LobbyLifecycle::~LobbyLifecycle() noexcept
{
    Shutdown();
}

void LobbyLifecycle::Shutdown() noexcept
{
    ClearLobbyObjects();
#if FRONTIER_PROFILE_EDITOR
    if (EditorBridge != nullptr)
    {
        EditorBridge->Unbind();
        EditorBridge.reset();
    }
    BoundOutliner = nullptr;
#endif
}

void LobbyLifecycle::ClearLobbyObjects() noexcept
{
    if (HeroVehicleRig && HeroPhysics)
    {
        HeroVehicleRig->Finalize(*HeroPhysics);
    }
    HeroVehicleRig.reset();
    if (HeroPhysics)
    {
        HeroPhysics->Finalize();
    }
    HeroPhysics.reset();
    HeroVehiclePhysicsActive = false;
    HeroVehicleEntityToken   = {};
    HeroVehicleSpawnZUp      = {};

    // Destroy RAII CategoryInstanceHandles before clearing WorldRegister or unbinding Outliner
    ManagedCategoryHandles.clear();
    FolderTokens.clear();
    SceneSyncBridge.ClearBindings();
#if FRONTIER_PROFILE_EDITOR
    if (EditorBridge != nullptr)
    {
        EditorBridge->Unbind();
    }
    if (BoundOutliner != nullptr)
    {
        BoundOutliner->ClearAll();
    }
#endif
    WorldRegister.ClearAll();
    WorldScene = {};
    GeometryAssets.clear();
    CameraPresets.clear();
    ParticleClouds.clear();
    ParticleSimulation = {};
    SimulationClockSeconds = 0.0;
    VehicleCount = 0u;
    TotalMeshTriangles = 0u;
}

#if FRONTIER_PROFILE_EDITOR
void LobbyLifecycle::AttachOutliner(Frontier::OutlinerPanel* Outliner) noexcept
{
    if (BoundOutliner == Outliner && EditorBridge != nullptr)
    {
        return;
    }
    ManagedCategoryHandles.clear();
    FolderTokens.clear();
    if (EditorBridge != nullptr)
    {
        EditorBridge->Unbind();
        EditorBridge.reset();
    }
    BoundOutliner = Outliner;
    if (BoundOutliner != nullptr)
    {
        EditorBridge = std::make_unique<Frontier::Editor::WorldInstanceEditorBridge>();
        EditorBridge->BindOutliner(WorldRegister, *BoundOutliner);
    }
}
#endif

bool LobbyLifecycle::EnsureSharedAssetsLoaded(
    const std::filesystem::path& RepoRoot,
    std::string&                 OutDiagnostic)
{
    if (SharedAssetsReady)
    {
        return true;
    }

    if (!ControlVehicleSurfaceDecoder::DecodeAssembledVehicle(
            RepoRoot,
            VehicleCoordinateConvention::FrontierWorldZUp,
            ControlVehicleTemplate,
            OutDiagnostic))
    {
        OutDiagnostic = "LobbyLifecycle failed to decode ControlVehicle.GeometrySpace: " + OutDiagnostic;
        return false;
    }

    if (!PhotometricProfileCodec::LoadCanonicalPhotometrySuite(RepoRoot, PhotometrySuite, OutDiagnostic))
    {
        OutDiagnostic = "LobbyLifecycle failed to load canonical photometry profiles: " + OutDiagnostic;
        return false;
    }

    const std::filesystem::path SpecsPath    = RepoRoot / "EngineContent/Simulation/LayerSpecifications.VolumetricSpace";
    const std::filesystem::path TemplatesDir = RepoRoot / "EngineContent/Simulation/ParticleTemplates";
    std::string ParticleErr {};
    if (std::filesystem::exists(SpecsPath) && std::filesystem::exists(TemplatesDir))
    {
        if (const auto LoadedSpecs = Frontier::SimulationSpace::EffectLayers::LoadLayerSpecificationsFromFile(SpecsPath.string(), &ParticleErr))
        {
            ParticleSpecs        = *LoadedSpecs;
            ParticleCatalogReady = ParticleTemplates.LoadFromDirectory(ParticleSpecs, TemplatesDir.string(), &ParticleErr);
        }
    }

    SharedAssetsReady = true;
    std::string ClassDiag {};
    ControlVehicleClassReady = Frontier::ComponentClass::ControlVehicleClassBuilder::ConstructControlVehicle(
        RepoRoot,
        ControlVehicleClass,
        ClassDiag);
    return true;
}

bool LobbyLifecycle::InitializeAndBuild(
    const std::filesystem::path& RepoRoot,
    const LobbyPolicy&           Policy,
    std::string&                 OutDiagnostic)
{
    ActivePolicy = Policy;
    LobbyPolicyEngine::ClampToSafeBounds(ActivePolicy);

    if (!EnsureSharedAssetsLoaded(RepoRoot, OutDiagnostic))
    {
        return false;
    }

    ClearLobbyObjects();

#if FRONTIER_PROFILE_EDITOR
    if (BoundOutliner != nullptr)
    {
        if (EditorBridge == nullptr)
        {
            EditorBridge = std::make_unique<Frontier::Editor::WorldInstanceEditorBridge>();
        }
        EditorBridge->BindOutliner(WorldRegister, *BoundOutliner);
    }
#endif

    WorldRegister.ReserveInstanceCapacity(128u);
    RegisterStudioBaseMaterials(WorldScene.MaterialRegister, MaterialPalette);
    WorldScene.PhotometricProfiles = PhotometrySuite;

    switch (ActivePolicy.SelectedLobby)
    {
        case StudioLobbyId::StudioLobby2ForestStage:
            BuildLobby2ForestStageLevel(*this, ActivePolicy);
            break;
        case StudioLobbyId::StudioLobby3RacingGarage:
            BuildLobby3RacingGarageLevel(*this, ActivePolicy);
            break;
        case StudioLobbyId::StudioLobby1Cyclorama:
        default:
            BuildLobby1CycloramaLevel(*this, ActivePolicy);
            break;
    }

    (void)SceneSyncBridge.FlushRebuiltTransformsToScene(WorldRegister, WorldScene);
    FinalizeWorldSceneAcceleration();

    if (ActivePolicy.EnableHeroVehiclePhysics && ControlVehicleClassReady && HeroVehicleEntityToken.QueryValid())
    {
        using namespace Frontier::SimulationSpace::PhysicsSpace;
        PhysicsSimulationParameters PhysParams {};
        PhysParams.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
        PhysParams.FixedStepHz = 120.0f;
        HeroPhysics = std::make_unique<PhysicsExtension>(PhysParams);
        if (HeroPhysics->Initialize())
        {
            RigidBodyDescriptor FloorDesc {};
            FloorDesc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
            FloorDesc.Shape.HalfExtentsMeters = { 500.0f, 0.5f, 500.0f };
            FloorDesc.MotionCategory          = RigidMotionCategory::Static;
            FloorDesc.PositionMeters          = { 0.0f, -0.5f, 0.0f };
            FloorDesc.CollisionChannel        = static_cast<uint16_t>(CollisionLayerChannel::StaticEnvironment);
            (void)HeroPhysics->IssueRigidBody(FloorDesc);

            VehicleRigSpecification Spec =
                VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(ControlVehicleClass.ClassDefinition);
            Spec.WheelContactMode          = WheelContactQueryMode::HeroThreeLaneFanArc;
            Spec.BindAttachmentConstraints = true;

            HeroVehicleRig = std::make_unique<VehiclePhysicsRig>();
            const Axis3D SpawnYUp = ConvertVectorBetweenBases(
                HeroVehicleSpawnZUp,
                WorldCoordinateBasis::RightHandedZUp,
                WorldCoordinateBasis::RightHandedYUp);
            constexpr float ChassisGroundOffsetY = 0.41715f;
            HeroVehiclePhysicsActive = HeroVehicleRig->Initialize(
                *HeroPhysics,
                Spec,
                { SpawnYUp.X, SpawnYUp.Y + ChassisGroundOffsetY, SpawnYUp.Z });
        }
    }

    if (ActivePolicy.EnableEngineParticles && !ParticleClouds.empty())
    {
        for (int Step = 0; Step < 20; ++Step)
        {
            TickSimulation(1.0 / 60.0);
        }
    }

    return true;
}

InstanceToken LobbyLifecycle::CreateFolderEntry(
    std::string_view Title,
    std::string_view Subtitle,
    InstanceToken    EnclosingToken)
{
    const InstanceToken FolderTok = WorldRegister.IssueInstanceSlot(InstanceCategory::ContainmentFolder, 0u);
    if (!FolderTok.QueryValid())
    {
        return {};
    }
    if (EnclosingToken.QueryValid())
    {
        (void)WorldRegister.AttachNestedSlot(EnclosingToken, FolderTok);
    }
    FolderTokens.push_back(FolderTok);

#if FRONTIER_PROFILE_EDITOR
    EditorInstanceRegistrationDescriptor Desc {};
    Desc.Token              = FolderTok;
    Desc.EnclosingToken     = EnclosingToken;
    Desc.DisplayTitle       = Title;
    Desc.CategoryLabel      = "Folder";
    Desc.Subtitle           = Subtitle;
    Desc.CategoryTag        = "env:folder";
    Desc.VisibilityEnabled  = true;
    Desc.RemovalProtected   = false;
    Desc.DuplicateProtected = false;
    WorldRegister.NotifyEditorInstanceRegistered(Desc);
#else
    (void)Title;
    (void)Subtitle;
#endif
    return FolderTok;
}

void LobbyLifecycle::SyncOutlinerEntryMetadata(
    InstanceToken    Token,
    std::string_view Subtitle,
    std::string_view DomainTag,
    Axis3D           WorldPos,
    Axis3D           RotationDeg,
    float            Scale,
    bool             RemovalProtected)
{
#if FRONTIER_PROFILE_EDITOR
    if (BoundOutliner == nullptr || EditorBridge == nullptr || !Token.QueryValid())
    {
        return;
    }
    const OutlinerSlotToken Slot = EditorBridge->QueryOutlinerSlot(Token);
    if (OutlinerRecordEntry* Entry = BoundOutliner->AccessEntry(Slot))
    {
        Entry->Subtitle         = std::string(Subtitle);
        Entry->DomainTag        = std::string(DomainTag);
        Entry->PositionX        = WorldPos.X;
        Entry->PositionY        = WorldPos.Y;
        Entry->PositionZ        = WorldPos.Z;
        Entry->RotationX        = RotationDeg.X;
        Entry->RotationY        = RotationDeg.Y;
        Entry->RotationZ        = RotationDeg.Z;
        Entry->ScaleX           = Scale;
        Entry->ScaleY           = Scale;
        Entry->ScaleZ           = Scale;
        Entry->RemovalProtected = RemovalProtected;
        Entry->Expanded         = true;
    }
#else
    (void)Token;
    (void)Subtitle;
    (void)DomainTag;
    (void)WorldPos;
    (void)RotationDeg;
    (void)Scale;
    (void)RemovalProtected;
#endif
}

InstanceToken LobbyLifecycle::AppendSurfaceEntity(
    std::string_view StableId,
    std::string_view OutlinerTitle,
    std::string_view Subtitle,
    StudioBuiltMesh  BuiltMesh,
    Axis3D           WorldTranslation,
    uint16_t         FallbackMaterialSlot,
    InstanceToken    EnclosingToken,
    bool             CastShadows)
{
    auto Asset = std::make_unique<LobbyGeometryAsset>();
    Asset->StableId = std::string(StableId);
    Asset->Network  = std::move(BuiltMesh.Network);
    TotalMeshTriangles += static_cast<uint32_t>(Asset->Network.Faces.size());

    Frontier::SpatialAcceleration::ClusterLodBakeConfiguration BakeCfg {};
    BakeCfg.MaximumLevels             = 4u;
    BakeCfg.LockMaterialBoundaries    = true;
    BakeCfg.AuthoredFaceCornerNormals = std::move(BuiltMesh.AuthoredCornerNormals);
    (void)Asset->Pyramid.BuildFromPolygonNetwork(Asset->Network, BakeCfg);

    RigidClusterInstance Inst {};
    Inst.PyramidAsset        = &Asset->Pyramid;
    Inst.WorldTranslation    = WorldTranslation;
    Inst.PreviousTranslation = WorldTranslation;
    Inst.UniformScale        = 1.0f;
    Inst.NonUniformScale     = { 1.0f, 1.0f, 1.0f };
    Inst.RotationZDegrees    = 0.0f;
    Inst.MaterialSlot        = FallbackMaterialSlot;

    const uint32_t ClusterIdx = static_cast<uint32_t>(WorldScene.ClusterInstances.size());
    WorldScene.ClusterInstances.push_back(Inst);
    GeometryAssets.push_back(std::move(Asset));

    SurfaceInstanceParameters Params {};
    Params.GeometryDocumentHash64 = HashStableId64(StableId);
    Params.MaterialDocumentHash64 = static_cast<uint64_t>(FallbackMaterialSlot);
    Params.CastShadows            = CastShadows;
    Params.VisibilityEnabled      = true;

#if FRONTIER_PROFILE_EDITOR
    SurfaceInstance Handle(WorldRegister, OutlinerTitle, Params, EnclosingToken);
#else
    (void)OutlinerTitle;
    SurfaceInstance Handle(WorldRegister, Params);
#endif
    const InstanceToken Tok = Handle.QueryToken();
    if (EnclosingToken.QueryValid())
    {
        (void)WorldRegister.AttachNestedSlot(EnclosingToken, Tok);
    }
    (void)WorldRegister.AssignWorldTransform(Tok, WorldTranslation.X, WorldTranslation.Y, WorldTranslation.Z);
    SceneSyncBridge.BindSurfaceToClusterInstance(Tok, ClusterIdx);
    SyncOutlinerEntryMetadata(Tok, Subtitle, "geom:studio", WorldTranslation);
    if (StableId == "vehicle-hero")
    {
        HeroVehicleEntityToken = Tok;
        HeroVehicleSpawnZUp    = WorldTranslation;
    }
    ManagedCategoryHandles.push_back(std::move(Handle));
    return Tok;
}

InstanceToken LobbyLifecycle::AppendLuminaireEntity(
    std::string_view                   Title,
    std::string_view                   Subtitle,
    std::string_view                   DomainTag,
    Axis3D                             WorldPos,
    const LuminaireInstanceParameters& Params,
    InstanceToken                      EnclosingToken)
{
#if FRONTIER_PROFILE_EDITOR
    LuminaireInstance Handle(WorldRegister, Title, Params, EnclosingToken);
#else
    (void)Title;
    LuminaireInstance Handle(WorldRegister, Params);
#endif
    const InstanceToken Tok = Handle.QueryToken();
    if (EnclosingToken.QueryValid())
    {
        (void)WorldRegister.AttachNestedSlot(EnclosingToken, Tok);
    }
    (void)WorldRegister.AssignWorldTransform(Tok, WorldPos.X, WorldPos.Y, WorldPos.Z);
    SyncOutlinerEntryMetadata(Tok, Subtitle, DomainTag, WorldPos);
    ManagedCategoryHandles.push_back(std::move(Handle));
    return Tok;
}

InstanceToken LobbyLifecycle::AppendCameraEntity(
    std::string_view                Title,
    std::string_view                Subtitle,
    Axis3D                          WorldPos,
    const CameraInstanceParameters& Params,
    InstanceToken                   EnclosingToken,
    bool                            RemovalProtected)
{
#if FRONTIER_PROFILE_EDITOR
    CameraInstance Handle(WorldRegister, Title, Params, EnclosingToken);
#else
    (void)Title;
    CameraInstance Handle(WorldRegister, Params);
#endif
    const InstanceToken Tok = Handle.QueryToken();
    if (EnclosingToken.QueryValid())
    {
        (void)WorldRegister.AttachNestedSlot(EnclosingToken, Tok);
    }
    (void)WorldRegister.AssignWorldTransform(Tok, WorldPos.X, WorldPos.Y, WorldPos.Z);
    SyncOutlinerEntryMetadata(Tok, Subtitle, "camera:perspective", WorldPos, {}, 1.0f, RemovalProtected);
    ManagedCategoryHandles.push_back(std::move(Handle));
    return Tok;
}

void LobbyLifecycle::AppendAtmosphereEntity(
    std::string_view                    Title,
    std::string_view                    Subtitle,
    const AtmosphereInstanceParameters& Params,
    InstanceToken                       EnclosingToken)
{
#if FRONTIER_PROFILE_EDITOR
    AtmosphereInstance Handle(WorldRegister, Title, Params, EnclosingToken);
#else
    (void)Title;
    AtmosphereInstance Handle(WorldRegister, Params);
#endif
    const InstanceToken Tok = Handle.QueryToken();
    if (EnclosingToken.QueryValid())
    {
        (void)WorldRegister.AttachNestedSlot(EnclosingToken, Tok);
    }
    SyncOutlinerEntryMetadata(Tok, Subtitle, "env:atmosphere", {});
    ManagedCategoryHandles.push_back(std::move(Handle));
}

void LobbyLifecycle::AppendParticleEmitter(
    std::string_view NodeId,
    std::string_view OutlinerTitle,
    std::string_view Subtitle,
    std::string_view TemplateId,
    Axis3D           WorldOriginZUp,
    uint32_t         ParticleCount,
    float            ReleaseRate,
    InstanceToken    EnclosingToken)
{
    ParticleInstanceParameters Params {};
    Params.ParticleDocumentHash64 = HashStableId64(TemplateId);
    Params.MaximumParticleCount   = ParticleCount;
    Params.ReleaseRatePerSecond   = ReleaseRate;
    Params.SimulationActive       = true;

#if FRONTIER_PROFILE_EDITOR
    ParticleInstance Handle(WorldRegister, OutlinerTitle, Params, EnclosingToken);
#else
    (void)OutlinerTitle;
    ParticleInstance Handle(WorldRegister, Params);
#endif
    const InstanceToken Tok = Handle.QueryToken();
    if (EnclosingToken.QueryValid())
    {
        (void)WorldRegister.AttachNestedSlot(EnclosingToken, Tok);
    }
    (void)WorldRegister.AssignWorldTransform(Tok, WorldOriginZUp.X, WorldOriginZUp.Y, WorldOriginZUp.Z);
    SyncOutlinerEntryMetadata(Tok, Subtitle, "env:particle", WorldOriginZUp);
    ManagedCategoryHandles.push_back(std::move(Handle));

    if (ActivePolicy.EnableEngineParticles && ParticleCatalogReady)
    {
        if (auto CloudOpt = ParticleTemplates.ConstructParticleCloud(ParticleSpecs, TemplateId, NodeId))
        {
            CloudOpt->Evaluated.Count = std::max(16u, std::min(ParticleCount, 1024u));
            const Axis3D ParticleOriginYUp = ConvertVectorBetweenBases(
                WorldOriginZUp,
                WorldCoordinateBasis::RightHandedZUp,
                WorldCoordinateBasis::RightHandedYUp);
            CloudOpt->Evaluated.Position = { ParticleOriginYUp.X, ParticleOriginYUp.Y, ParticleOriginYUp.Z };
            ParticleClouds.push_back(std::move(*CloudOpt));
        }
    }
}

void LobbyLifecycle::FinalizeWorldSceneAcceleration()
{
    std::vector<SpatialAcceleration::RayTrianglePrimitive> WorldPrims;
    std::vector<SpatialAcceleration::TriangleSmoothNormals> WorldNormals;

    for (const auto& Inst : WorldScene.ClusterInstances)
    {
        const auto* Pyr = Inst.PyramidAsset;
        if (Pyr == nullptr)
        {
            continue;
        }
        for (const auto& Cluster : Pyr->Clusters)
        {
            if (Cluster.HierarchyLevel != 0u)
            {
                continue;
            }
            for (uint32_t T = 0u; T < Cluster.TriangleCount; ++T)
            {
                const auto& Tri = Pyr->Triangles[Cluster.TriangleStartOffset + T];
                const Axis3D P0 = Inst.WorldTranslation + Pyr->Positions[Tri.VertexIndex0] * Inst.UniformScale;
                const Axis3D P1 = Inst.WorldTranslation + Pyr->Positions[Tri.VertexIndex1] * Inst.UniformScale;
                const Axis3D P2 = Inst.WorldTranslation + Pyr->Positions[Tri.VertexIndex2] * Inst.UniformScale;
                const Axis3D CrossN = EvaluateCross(P1 - P0, P2 - P0);
                if (EvaluateLengthSquared(CrossN) <= 1.0e-12f)
                {
                    continue;
                }
                const Axis3D FlatN = NormalizeAxis(CrossN, { 0.0f, 0.0f, 1.0f });
                SpatialAcceleration::RayTrianglePrimitive Prim {};
                Prim.Corner0          = P0;
                Prim.Edge01           = P1 - P0;
                Prim.Edge02           = P2 - P0;
                Prim.PrimitiveOrdinal = static_cast<uint32_t>(WorldPrims.size());
                Prim.MaterialOrdinal  = (Tri.MaterialOrdinal != 0u) ? Tri.MaterialOrdinal : Inst.MaterialSlot;
                Prim.OpaqueMask       = 1u;
                WorldPrims.push_back(Prim);

                SpatialAcceleration::TriangleSmoothNormals SN {};
                SN.Normal0 = (Tri.PackedNormal0 != 0u) ? NormalizeAxis(SpatialAcceleration::UnpackClusterOctahedralUnitVector(Tri.PackedNormal0), FlatN) : FlatN;
                SN.Normal1 = (Tri.PackedNormal1 != 0u) ? NormalizeAxis(SpatialAcceleration::UnpackClusterOctahedralUnitVector(Tri.PackedNormal1), FlatN) : FlatN;
                SN.Normal2 = (Tri.PackedNormal2 != 0u) ? NormalizeAxis(SpatialAcceleration::UnpackClusterOctahedralUnitVector(Tri.PackedNormal2), FlatN) : FlatN;
                WorldNormals.push_back(SN);
            }
        }
    }

    (void)WorldScene.SceneTree.BuildFromTrianglePrimitivesWithNormals(WorldPrims, WorldNormals);
}

void LobbyLifecycle::TickSimulation(double FixedDeltaSeconds) noexcept
{
    SimulationClockSeconds += FixedDeltaSeconds;
    const float Dt = static_cast<float>(std::clamp(FixedDeltaSeconds, 0.001, 0.05));

    if (HeroVehiclePhysicsActive && HeroPhysics && HeroVehicleRig && HeroVehicleEntityToken.QueryValid())
    {
        constexpr double SubDt = 1.0 / 120.0;
        const uint32_t SubSteps = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(Dt) / SubDt)));
        for (uint32_t S = 0u; S < std::min(SubSteps, 6u); ++S)
        {
            HeroVehicleRig->StepOnPhysicsThread(*HeroPhysics, SubDt);
            HeroPhysics->Tick(SubDt);
        }
        const auto Reading = HeroVehicleRig->AcquireVehicleReading();
        if (Reading.Valid())
        {
            constexpr float ChassisGroundOffsetY = 0.41715f;
            const Axis3D WorldZUp = ConvertVectorBetweenBases(
                { Reading->WorldPositionMeters.X,
                  Reading->WorldPositionMeters.Y - ChassisGroundOffsetY,
                  Reading->WorldPositionMeters.Z },
                WorldCoordinateBasis::RightHandedYUp,
                WorldCoordinateBasis::RightHandedZUp);
            (void)WorldRegister.AssignWorldTransform(HeroVehicleEntityToken, WorldZUp.X, WorldZUp.Y, WorldZUp.Z);
        }
    }

    (void)World::ExecuteWorldSimulationSequence(WorldRegister, Dt, true);
    (void)SceneSyncBridge.FlushRebuiltTransformsToScene(WorldRegister, WorldScene);

    if (ActivePolicy.EnableEngineParticles && !ParticleClouds.empty())
    {
        ParticleSimulation.StepCloudsCpu(
            ParticleClouds,
            CarrierSolids,
            ParticleRegion,
            static_cast<float>(SimulationClockSeconds),
            Dt);
    }
}

bool LobbyLifecycle::SubmitHeroVehicleDirective(
    const Frontier::SimulationSpace::PhysicsSpace::VehicleControlDirective& Directive) noexcept
{
    if (!HeroVehiclePhysicsActive || !HeroVehicleRig)
    {
        return false;
    }
    return HeroVehicleRig->SubmitControlDirective(Directive);
}

Frontier::SimulationSpace::PhysicsSpace::VehiclePhysicsRig::VehicleReadingLease
LobbyLifecycle::AcquireHeroVehicleReading() const noexcept
{
    if (!HeroVehicleRig)
    {
        return {};
    }
    return HeroVehicleRig->AcquireVehicleReading();
}

LobbyLifecycleTelemetry LobbyLifecycle::InspectTelemetry() const noexcept
{
    LobbyLifecycleTelemetry T {};
    T.ActiveLobby             = ActivePolicy.SelectedLobby;
    T.VehicleCount            = VehicleCount;
    T.SurfaceInstanceCount    = WorldRegister.InspectSurfaceRegister().QueryActiveCount();
    T.AreaLightCount          = static_cast<uint32_t>(WorldScene.AreaLights.size());
    T.PhotometricLightCount   = static_cast<uint32_t>(WorldScene.PhotometricRadiators.size());
    T.CameraRigCount          = WorldRegister.InspectCameraRegister().QueryActiveCount();
    T.ParticleCloudCount      = static_cast<uint32_t>(ParticleClouds.size());
    T.SimulatedParticleCount  = ParticleSimulation.QueryStatistics().ActiveParticleCount;
    T.TotalMeshTriangles      = TotalMeshTriangles;
    T.VehicleDecoderLoaded    = !ControlVehicleTemplate.Triangles.empty();
    T.PhotometryLoaded        = !PhotometrySuite.empty();
    T.ParticleTemplatesLoaded = ParticleCatalogReady;
#if FRONTIER_PROFILE_EDITOR
    T.OutlinerRowCount        = (BoundOutliner != nullptr) ? BoundOutliner->QueryTotalCount() : 0u;
#endif
    return T;
}

} // namespace AutoSpace
