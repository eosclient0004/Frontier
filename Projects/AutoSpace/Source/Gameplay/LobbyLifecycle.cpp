/*============================================================================================================================================
                                                           LOBBYLIFECYCLE.CPP
============================================================================================================================================*/
// 🧩 Single-level LobbyLifecycle implementation driving all 3 Studio Lobbies via LobbyPolicy inside MainWorld.WorldSpace

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Gameplay/LobbyLifecycle.h"

#include "Engine/Codecs/PhotometrySpaceCodec.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <algorithm>
#include <array>
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
    HeroVehiclePhysicsActive     = false;
    HeroVehicleEntityToken       = {};
    HeroPortalCloneEntityToken   = {};
    HeroVehicleClusterIdx        = UINT32_MAX;
    HeroPortalCloneClusterIdx    = UINT32_MAX;
    HeroVehicleBakedYawDeg       = 0.0f;
    HeroVehicleSpawnZUp          = {};
    HeroCurrentPositionZUp       = {};
    HeroCurrentYawZUpDegrees     = 0.0f;
    HeroSettledChassisHeightY    = 0.41715f;
    HeroVehicleSpec              = {};
    DynamicEmissiveOrbToken      = {};
    DynamicEmissiveOrbClusterIdx = UINT32_MAX;
    DynamicEmissiveOrbMatSlot    = 0u;
    WorldScene.Portals.clear();

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
        case StudioLobbyId::StudioLobby4TrainingGround:
            BuildLobby4TrainingGroundLevel(*this, ActivePolicy);
            break;
        case StudioLobbyId::StudioLobby1Cyclorama:
        default:
            BuildLobby1CycloramaLevel(*this, ActivePolicy);
            break;
    }

    // Spawn the moving, color-cycling dynamic emissive GI orb across Studio Lobbies 1..3 (keep Level 4 Training Ground unobstructed)
    if (ActivePolicy.SelectedLobby != StudioLobbyId::StudioLobby4TrainingGround)
    {
        DynamicEmissiveOrbMatSlot = MaterialPalette.Resolve(StudioMaterialCode::DynamicEmissiveOrb);
        const uint32_t LobbyVariant = static_cast<uint32_t>(ActivePolicy.SelectedLobby);
        Axis3D InitialPos {};
        Frontier::Mathematics::LinearRGB InitialRgb {};
        float InitialNits = 26000.0f;
        EvaluateDynamicEmissiveOrbState(LobbyVariant, 0.0, InitialPos, InitialRgb, InitialNits);
        const InstanceToken LightingFolder = FolderTokens.empty() ? InstanceToken{} : FolderTokens.front();
        DynamicEmissiveOrbToken = AppendSurfaceEntity(
            "fx-emissive-orb",
            "Dynamic Emissive Orb",
            "Live orbiting emissive GI sphere (color-cycling)",
            BuildDynamicEmissiveOrbSurface(MaterialPalette, 0.24f),
            InitialPos,
            DynamicEmissiveOrbMatSlot,
            LightingFolder,
            false);
        if (!WorldScene.ClusterInstances.empty())
        {
            DynamicEmissiveOrbClusterIdx = static_cast<uint32_t>(WorldScene.ClusterInstances.size() - 1u);
            auto& OrbInst                = WorldScene.ClusterInstances[DynamicEmissiveOrbClusterIdx];
            OrbInst.AnalyticalShapeCode  = 2u; // Exact analytical sphere SDF
            OrbInst.LocalHalfExtents     = { 0.24f, 0.24f, 0.24f };
        }
        UpdateDynamicEmissiveOrb(0.0);
    }

    (void)SceneSyncBridge.FlushRebuiltTransformsToScene(WorldRegister, WorldScene);
    FinalizeWorldSceneAcceleration();

    HeroCurrentPositionZUp      = HeroVehicleSpawnZUp;
    HeroCurrentYawZUpDegrees    = 0.0f;

    InitializeHeroVehicleSimulation();

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
    Asset->StableId     = std::string(StableId);
    Asset->DisplayTitle = std::string(OutlinerTitle);
    Asset->Subtitle     = std::string(Subtitle);
    Asset->Network      = std::move(BuiltMesh.Network);
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
    Inst.CastShadows         = CastShadows;

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
        HeroVehicleClusterIdx  = ClusterIdx;
        HeroVehicleSpawnZUp    = WorldTranslation;
    }
    else if (StableId == "vehicle-hero-portal-clone")
    {
        HeroPortalCloneEntityToken = Tok;
        HeroPortalCloneClusterIdx  = ClusterIdx;
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
                Prim.OpaqueMask       = Inst.CastShadows ? 1u : 0u;
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

void LobbyLifecycle::UpdateDynamicEmissiveOrb(double ElapsedSeconds) noexcept
{
    if (DynamicEmissiveOrbClusterIdx == UINT32_MAX ||
        DynamicEmissiveOrbClusterIdx >= WorldScene.ClusterInstances.size())
    {
        return;
    }

    const uint32_t LobbyVariant = static_cast<uint32_t>(ActivePolicy.SelectedLobby);
    Axis3D OrbPos {};
    Frontier::Mathematics::LinearRGB OrbRgb {};
    float OrbNits = 26000.0f;
    EvaluateDynamicEmissiveOrbState(LobbyVariant, ElapsedSeconds, OrbPos, OrbRgb, OrbNits);

    auto& Inst               = WorldScene.ClusterInstances[DynamicEmissiveOrbClusterIdx];
    Inst.PreviousTranslation = Inst.WorldTranslation;
    Inst.WorldTranslation    = OrbPos;

    if (DynamicEmissiveOrbToken.QueryValid())
    {
        (void)WorldRegister.AssignWorldTransform(DynamicEmissiveOrbToken, OrbPos.X, OrbPos.Y, OrbPos.Z);
    }

    if (DynamicEmissiveOrbMatSlot > 0u &&
        DynamicEmissiveOrbMatSlot < WorldScene.MaterialRegister.QuerySlotCount())
    {
        Frontier::MaterialSpace::SurfaceMaterial Mat =
            WorldScene.MaterialRegister.InspectAuthoredMaterial(DynamicEmissiveOrbMatSlot);
        Mat.Diffuse.BaseColor                        = OrbRgb;
        Mat.PorosityLight.EmissionTint               = OrbRgb;
        Mat.PorosityLight.EmissionLuminanceNits      = OrbNits;
        Mat.PorosityLight.BlackbodyTemperatureKelvin = 0.0f;
        (void)WorldScene.MaterialRegister.UpdateMaterialSlot(DynamicEmissiveOrbMatSlot, Mat);
    }
}

void LobbyLifecycle::InitializeHeroVehicleSimulation() noexcept
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

    if (!ActivePolicy.EnableHeroVehiclePhysics || !ControlVehicleClassReady || !HeroVehicleEntityToken.QueryValid())
    {
        return;
    }

    using namespace Frontier::SimulationSpace::PhysicsSpace;
    PhysicsSimulationParameters PhysParams {};
    PhysParams.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
    PhysParams.FixedStepHz = 120.0f;
    HeroPhysics = std::make_unique<PhysicsExtension>(PhysParams);
    if (!HeroPhysics->Initialize())
    {
        HeroPhysics.reset();
        return;
    }

    RigidBodyDescriptor FloorDesc {};
    FloorDesc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
    FloorDesc.Shape.HalfExtentsMeters = { 500.0f, 0.5f, 500.0f };
    FloorDesc.MotionCategory          = RigidMotionCategory::Static;
    FloorDesc.PositionMeters          = { 0.0f, -0.5f, 0.0f };
    FloorDesc.CollisionChannel        = static_cast<uint16_t>(CollisionLayerChannel::StaticEnvironment);
    (void)HeroPhysics->IssueRigidBody(FloorDesc);

    // Register World B's elevated proving deck collider in Jolt (Z_world = +18 m -> Y_jolt = +18 m, X = +160, Z_jolt = -80)
    if (!WorldScene.Portals.empty())
    {
        RigidBodyDescriptor WorldBDeck {};
        WorldBDeck.Shape.ShapeCategory     = CollisionShapeCategory::Box;
        WorldBDeck.Shape.HalfExtentsMeters = { 65.0f, 0.5f, 65.0f };
        WorldBDeck.MotionCategory          = RigidMotionCategory::Static;
        WorldBDeck.PositionMeters          = { 0.0f, -0.5f, 680.0f };
        WorldBDeck.CollisionChannel        = static_cast<uint16_t>(CollisionLayerChannel::StaticEnvironment);
        (void)HeroPhysics->IssueRigidBody(WorldBDeck);
    }

    switch (ActivePolicy.SelectedLobby)
    {
        case StudioLobbyId::StudioLobby1Cyclorama:     HeroVehicleBakedYawDeg = -16.0f;  break;
        case StudioLobbyId::StudioLobby2ForestStage:   HeroVehicleBakedYawDeg = -22.0f;  break;
        case StudioLobbyId::StudioLobby3RacingGarage:  HeroVehicleBakedYawDeg = -146.0f; break;
        case StudioLobbyId::StudioLobby4TrainingGround:
        default:                                       HeroVehicleBakedYawDeg = 0.0f;    break;
    }

    HeroVehicleSpec =
        VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(ControlVehicleClass.ClassDefinition);
    HeroVehicleSpec.WheelContactMode          = WheelContactQueryMode::HeroThreeLaneFanArc;
    HeroVehicleSpec.BindAttachmentConstraints = true;

    HeroVehicleRig = std::make_unique<VehiclePhysicsRig>();
    const Axis3D SpawnYUp = ConvertVectorBetweenBases(
        HeroVehicleSpawnZUp,
        WorldCoordinateBasis::RightHandedZUp,
        WorldCoordinateBasis::RightHandedYUp);
    constexpr float InitialChassisOffsetY = 0.41715f;
    HeroVehiclePhysicsActive = HeroVehicleRig->Initialize(
        *HeroPhysics,
        HeroVehicleSpec,
        { SpawnYUp.X, SpawnYUp.Y + InitialChassisOffsetY, SpawnYUp.Z });

    if (!HeroVehiclePhysicsActive)
    {
        return;
    }

    // Bind live multi-world portals to split-portal Jolt suspension & center-of-mass transit
    HeroVehicleRig->AssignActivePortals(WorldScene.Portals);

    // Orient Jolt chassis body to match showcase baked heading in Lobbies 1..3 so W drives forward along the hood
    if (std::fabs(HeroVehicleBakedYawDeg) > 1.0e-3f)
    {
        const float HalfRad = 0.5f * HeroVehicleBakedYawDeg * Frontier::Constants::DegToRadFloat;
        const RigidOrientationQuaternion YawQ { 0.0f, std::sin(HalfRad), 0.0f, std::cos(HalfRad) };
        HeroPhysics->TeleportBody(
            HeroVehicleRig->QueryChassisBodyToken(),
            { SpawnYUp.X, SpawnYUp.Y + InitialChassisOffsetY, SpawnYUp.Z },
            YawQ);
    }

    // Settle suspension cleanly onto deck so tyres rest flush at startup
    VehicleControlDirective SettleBrake {};
    SettleBrake.Category        = VehicleDirectiveCategory::DriverInput;
    SettleBrake.BrakeNormalized = 0.50f;
    (void)HeroVehicleRig->SubmitControlDirective(SettleBrake);
    constexpr double SettleDt = 1.0 / 120.0;
    for (int Step = 0; Step < 45; ++Step)
    {
        HeroVehicleRig->StepOnPhysicsThread(*HeroPhysics, SettleDt);
        HeroPhysics->Tick(SettleDt);
    }

    VehicleControlDirective ReleaseControls {};
    ReleaseControls.Category = VehicleDirectiveCategory::DriverInput;
    (void)HeroVehicleRig->SubmitControlDirective(ReleaseControls);

    HeroSettledChassisHeightY = InitialChassisOffsetY;
    if (const auto SettledReading = HeroVehicleRig->AcquireVehicleReading(); SettledReading.Valid())
    {
        HeroSettledChassisHeightY = std::max(0.10f, SettledReading->WorldPositionMeters.Y - SpawnYUp.Y);
    }

    HeroCurrentPositionZUp   = HeroVehicleSpawnZUp;
    HeroCurrentYawZUpDegrees = HeroVehicleBakedYawDeg;
    (void)WorldRegister.AssignWorldTransform(
        HeroVehicleEntityToken,
        HeroVehicleSpawnZUp.X,
        HeroVehicleSpawnZUp.Y,
        HeroVehicleSpawnZUp.Z,
        0.0f,
        0.0f,
        0.0f);
    SyncHeroPortalStraddlingProjection();
    (void)SceneSyncBridge.FlushRebuiltTransformsToScene(WorldRegister, WorldScene);
}

void LobbyLifecycle::ResetHeroVehicleToSpawn() noexcept
{
    InitializeHeroVehicleSimulation();
}

void LobbyLifecycle::SelectActivePortalShape(Frontier::World::PortalApertureShapeCategory Shape) noexcept
{
    using namespace Frontier::World;
    if (WorldScene.Portals.empty())
    {
        return;
    }
    auto& P = WorldScene.Portals[0];
    P.Shape = Shape;
    if (Shape == PortalApertureShapeCategory::Rectangle)
    {
        P.HalfWidthMeters       = 2.75f;
        P.HalfHeightMeters      = 1.75f;
        P.Entry.CenterWorld.Z   = 1.75f;
        P.Exit.CenterWorld.Z    = 1.75f;
    }
    else if (Shape == PortalApertureShapeCategory::Circle)
    {
        P.HalfWidthMeters       = 2.35f;
        P.HalfHeightMeters      = 2.15f;
        P.Entry.CenterWorld.Z   = 2.15f;
        P.Exit.CenterWorld.Z    = 2.15f;
    }
    else if (Shape == PortalApertureShapeCategory::Hexagon)
    {
        P.HalfWidthMeters       = 2.85f;
        P.HalfHeightMeters      = 1.80f;
        P.Entry.CenterWorld.Z   = 1.56f;
        P.Exit.CenterWorld.Z    = 1.56f;
    }
    else if (Shape == PortalApertureShapeCategory::RegularPolygon)
    {
        P.PolygonSides          = 8u;
        P.HalfWidthMeters       = 2.65f;
        P.HalfHeightMeters      = 1.95f;
        P.Entry.CenterWorld.Z   = 1.82f;
        P.Exit.CenterWorld.Z    = 1.82f;
    }
    else
    {
        // Concave asymmetric 2D shield/star-arch cutout proving arbitrary 2D polygon shapes!
        P.HalfWidthMeters       = 2.85f;
        P.HalfHeightMeters      = 2.10f;
        P.Entry.CenterWorld.Z   = 1.85f;
        P.Exit.CenterWorld.Z    = 1.85f;
        P.CustomPolygonUv       = {
            { -2.45f, -1.85f },
            {  2.45f, -1.85f },
            {  2.75f,  0.15f },
            {  1.65f,  0.65f },
            {  2.05f,  1.65f },
            {  0.00f,  2.08f },
            { -2.05f,  1.65f },
            { -1.65f,  0.65f },
            { -2.75f,  0.15f }
        };
    }
    P.NormalizeAxes();
    if (HeroVehicleRig)
    {
        HeroVehicleRig->AssignActivePortals(WorldScene.Portals);
    }
    SyncHeroPortalStraddlingProjection();
}

void LobbyLifecycle::CycleActivePortalShape() noexcept
{
    using namespace Frontier::World;
    if (WorldScene.Portals.empty())
    {
        return;
    }
    switch (WorldScene.Portals[0].Shape)
    {
        case PortalApertureShapeCategory::Hexagon:        SelectActivePortalShape(PortalApertureShapeCategory::Circle); break;
        case PortalApertureShapeCategory::Circle:         SelectActivePortalShape(PortalApertureShapeCategory::Rectangle); break;
        case PortalApertureShapeCategory::Rectangle:      SelectActivePortalShape(PortalApertureShapeCategory::CustomCutout); break;
        case PortalApertureShapeCategory::CustomCutout:   SelectActivePortalShape(PortalApertureShapeCategory::RegularPolygon); break;
        case PortalApertureShapeCategory::RegularPolygon:
        default:                                          SelectActivePortalShape(PortalApertureShapeCategory::Hexagon); break;
    }
}

void LobbyLifecycle::SyncHeroPortalStraddlingProjection() noexcept
{
    if (HeroVehicleClusterIdx >= WorldScene.ClusterInstances.size())
    {
        return;
    }
    auto& HeroInst = WorldScene.ClusterInstances[HeroVehicleClusterIdx];
    const bool HasClone = (HeroPortalCloneClusterIdx < WorldScene.ClusterInstances.size());

    if (WorldScene.Portals.empty())
    {
        HeroInst.ClipPlaneEnabled = false;
        if (HasClone)
        {
            WorldScene.ClusterInstances[HeroPortalCloneClusterIdx].VisibilityEnabled = false;
            WorldScene.ClusterInstances[HeroPortalCloneClusterIdx].ClipPlaneEnabled  = false;
        }
        return;
    }

    const auto Overlap = Frontier::World::EvaluateVehiclePortalOverlap(
        WorldScene.Portals,
        HeroCurrentPositionZUp,
        HeroCurrentYawZUpDegrees,
        2.38f,
        0.98f,
        0.72f);

    if (Overlap.OverlapActive)
    {
        HeroInst.ClipPlaneEnabled = true;
        for (int K = 0; K < 4; ++K)
        {
            HeroInst.ClipPlaneWorld[K] = Overlap.ClipPlanePrimary[K];
        }
        if (HasClone && HeroPortalCloneEntityToken.QueryValid())
        {
            auto& CloneInst = WorldScene.ClusterInstances[HeroPortalCloneClusterIdx];
            CloneInst.VisibilityEnabled = true;
            CloneInst.ClipPlaneEnabled  = true;
            for (int K = 0; K < 4; ++K)
            {
                CloneInst.ClipPlaneWorld[K] = Overlap.ClipPlaneClone[K];
            }
            const float CloneDeltaYaw = Overlap.CloneYawZUpDegrees - HeroVehicleBakedYawDeg;
            (void)WorldRegister.AssignWorldTransform(
                HeroPortalCloneEntityToken,
                Overlap.ClonePositionWorld.X,
                Overlap.ClonePositionWorld.Y,
                Overlap.ClonePositionWorld.Z,
                0.0f,
                0.0f,
                CloneDeltaYaw);
        }
    }
    else
    {
        HeroInst.ClipPlaneEnabled = false;
        if (HasClone)
        {
            WorldScene.ClusterInstances[HeroPortalCloneClusterIdx].VisibilityEnabled = false;
            WorldScene.ClusterInstances[HeroPortalCloneClusterIdx].ClipPlaneEnabled  = false;
        }
    }
}

void LobbyLifecycle::PlaceHeroVehicleForPortalStraddleTest(float PenetrationMeters) noexcept
{
    if (!HeroVehiclePhysicsActive || !HeroPhysics || !HeroVehicleRig || WorldScene.Portals.empty())
    {
        return;
    }
    const auto& P = WorldScene.Portals[0];
    // Position car center so nose protrudes PenetrationMeters past World A Entry plane (X = 11.5 - 2.38 + PenetrationMeters)
    const Axis3D TargetZUp {
        P.Entry.CenterWorld.X - 2.38f + PenetrationMeters,
        P.Entry.CenterWorld.Y,
        0.0f
    };
    const Axis3D TargetYUp = ConvertVectorBetweenBases(
        TargetZUp,
        WorldCoordinateBasis::RightHandedZUp,
        WorldCoordinateBasis::RightHandedYUp);
    const Frontier::SimulationSpace::PhysicsSpace::RigidOrientationQuaternion IdentityQ { 0.0f, 0.0f, 0.0f, 1.0f };
    HeroPhysics->TeleportBody(
        HeroVehicleRig->QueryChassisBodyToken(),
        { TargetYUp.X, TargetYUp.Y + HeroSettledChassisHeightY, TargetYUp.Z },
        IdentityQ);
    HeroPhysics->AssignBodyVelocity(HeroVehicleRig->QueryChassisBodyToken(), { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });

    // Run 6 fixed physics substeps so front wheels query World B (Y = 18 m, friction 0.48) and rear wheels query World A (Y = 0 m, friction 1.12)!
    Frontier::SimulationSpace::PhysicsSpace::VehicleControlDirective HoldBrake {};
    HoldBrake.Category        = Frontier::SimulationSpace::PhysicsSpace::VehicleDirectiveCategory::DriverInput;
    HoldBrake.BrakeNormalized = 0.65f;
    (void)HeroVehicleRig->SubmitControlDirective(HoldBrake);
    for (int S = 0; S < 6; ++S)
    {
        HeroVehicleRig->StepOnPhysicsThread(*HeroPhysics, 1.0 / 120.0);
        HeroPhysics->Tick(1.0 / 120.0);
    }

    HeroCurrentPositionZUp   = TargetZUp;
    HeroCurrentYawZUpDegrees = 0.0f;
    (void)WorldRegister.AssignWorldTransform(
        HeroVehicleEntityToken, TargetZUp.X, TargetZUp.Y, TargetZUp.Z, 0.0f, 0.0f, 0.0f);
    SyncHeroPortalStraddlingProjection();
    (void)SceneSyncBridge.FlushRebuiltTransformsToScene(WorldRegister, WorldScene);
}

uint32_t LobbyLifecycle::QueryHeroPortalTransitCount() const noexcept
{
    return HeroVehicleRig ? HeroVehicleRig->QueryPortalTransitCount() : 0u;
}

uint32_t LobbyLifecycle::QueryHeroWheelPortalSide(uint32_t CornerIdx) const noexcept
{
    return HeroVehicleRig ? HeroVehicleRig->QueryWheelPortalSide(CornerIdx) : 0u;
}

void LobbyLifecycle::QueryHeroVehicleWorldPose(
    Axis3D& OutPositionZUp,
    float&  OutYawZUpDegrees) const noexcept
{
    OutPositionZUp   = HeroCurrentPositionZUp;
    OutYawZUpDegrees = HeroCurrentYawZUpDegrees;
}

void LobbyLifecycle::ApplySimulationTime(double ElapsedSeconds) noexcept
{
    SimulationClockSeconds = std::max(0.0, ElapsedSeconds);
    WorldScene.Atmosphere.SimulationSeconds = static_cast<float>(SimulationClockSeconds);
    UpdateDynamicEmissiveOrb(SimulationClockSeconds);
    (void)SceneSyncBridge.FlushRebuiltTransformsToScene(WorldRegister, WorldScene);
}

void LobbyLifecycle::TickSimulation(double FixedDeltaSeconds) noexcept
{
    SimulationClockSeconds += FixedDeltaSeconds;
    WorldScene.Atmosphere.SimulationSeconds = static_cast<float>(SimulationClockSeconds);
    const float Dt = static_cast<float>(std::clamp(FixedDeltaSeconds, 0.001, 0.05));

    UpdateDynamicEmissiveOrb(SimulationClockSeconds);

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
            Axis3D WorldZUp = ConvertVectorBetweenBases(
                { Reading->WorldPositionMeters.X,
                  Reading->WorldPositionMeters.Y - HeroSettledChassisHeightY,
                  Reading->WorldPositionMeters.Z },
                WorldCoordinateBasis::RightHandedYUp,
                WorldCoordinateBasis::RightHandedZUp);
            WorldZUp.Z = std::max(0.0f, WorldZUp.Z);

            // Extract chassis world yaw around +Z from Jolt Y-up orientation quaternion (+X forward)
            const auto& Q = Reading->WorldOrientation;
            const Axis3D FwdYUp {
                1.0f - 2.0f * (Q.Y * Q.Y + Q.Z * Q.Z),
                2.0f * (Q.X * Q.Y + Q.W * Q.Z),
                2.0f * (Q.X * Q.Z - Q.W * Q.Y)
            };
            const Axis3D FwdZUp = ConvertVectorBetweenBases(
                FwdYUp,
                WorldCoordinateBasis::RightHandedYUp,
                WorldCoordinateBasis::RightHandedZUp);
            const float YawZUpDeg = std::atan2(FwdZUp.Y, FwdZUp.X) * Frontier::Constants::RadToDegFloat;

            HeroCurrentPositionZUp   = WorldZUp;
            HeroCurrentYawZUpDegrees = YawZUpDeg;

            const float VisualDeltaYaw = YawZUpDeg - HeroVehicleBakedYawDeg;
            (void)WorldRegister.AssignWorldTransform(
                HeroVehicleEntityToken,
                WorldZUp.X,
                WorldZUp.Y,
                WorldZUp.Z,
                0.0f,
                0.0f,
                VisualDeltaYaw);

            SyncHeroPortalStraddlingProjection();
        }
    }

    // Flush dirty world transforms (including HeroVehicle live position & yaw) BEFORE simulation clears rebuild flags!
    (void)SceneSyncBridge.FlushRebuiltTransformsToScene(WorldRegister, WorldScene);
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
