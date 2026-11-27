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
    HeroTireSmoke.reset();
    HeroVehiclePhysicsActive     = false;
    HeroTireSmokeActive          = false;
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
    TireSmokeStepAccumulator     = 0.0;
    StaticLocalFogBaselineCount  = 0u;
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
            true);
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

    StaticLocalFogBaselineCount = WorldScene.Atmosphere.Fog.LocalFogs.size();
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
    HeroTireSmoke.reset();
    HeroVehiclePhysicsActive = false;
    HeroTireSmokeActive      = false;
    TireSmokeStepAccumulator = 0.0;

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

    if (ActivePolicy.EnableTireFluidSmoke)
    {
        using namespace Frontier::SimulationSpace::Fluid;
        VehicleTireFluidSmokeConfig SmokeCfg {};
        SmokeCfg.UseWorldGridRelocation = true;
        SmokeCfg.WorldGridTileSpan      = 4u;
        SmokeCfg.WorldGridTileCells     = 8u; // 32 x 16 x 32 * 0.40 m = 12.8 m x 6.4 m x 12.8 m sliding domain
        SmokeCfg.CellSizeMeters         = 0.40f;
        SmokeCfg.PressureIterations     = 8u;
        SmokeCfg.Vorticity              = 3.4f;
        SmokeCfg.Buoyancy               = 1.45f;
        SmokeCfg.AmbientWindMps         = { -0.35f, 0.0f, 0.25f };
        HeroTireSmoke = std::make_unique<VehicleTireFluidSmokeBridge>();
        HeroTireSmoke->Initialize(SmokeCfg, { SpawnYUp.X, 0.0f, SpawnYUp.Z });
        HeroTireSmokeActive = true;
    }
}

void LobbyLifecycle::SetTireSmokeEnabled(bool Enabled) noexcept
{
    HeroTireSmokeActive = Enabled && ActivePolicy.EnableTireFluidSmoke && (HeroTireSmoke != nullptr);
    if (!HeroTireSmokeActive)
    {
        if (HeroTireSmoke)
        {
            HeroTireSmoke->Initialize(
                HeroTireSmoke->InspectConfig(),
                { HeroCurrentPositionZUp.X, 0.0f, -HeroCurrentPositionZUp.Y });
        }
        if (WorldScene.Atmosphere.Fog.LocalFogs.size() > StaticLocalFogBaselineCount)
        {
            WorldScene.Atmosphere.Fog.LocalFogs.resize(StaticLocalFogBaselineCount);
        }
    }
}

void LobbyLifecycle::ResetHeroVehicleToSpawn() noexcept
{
    if (WorldScene.Atmosphere.Fog.LocalFogs.size() > StaticLocalFogBaselineCount)
    {
        WorldScene.Atmosphere.Fog.LocalFogs.resize(StaticLocalFogBaselineCount);
    }
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

Frontier::SimulationSpace::Fluid::TireSmokeBridgeTelemetry LobbyLifecycle::InspectTireSmokeTelemetry() const noexcept
{
    if (!HeroTireSmokeActive || !HeroTireSmoke)
    {
        return {};
    }
    return HeroTireSmoke->InspectTelemetry();
}

void LobbyLifecycle::ProjectTireFluidSmokeToLocalFogs(
    const Frontier::SimulationSpace::PhysicsSpace::VehicleReading& Reading) noexcept
{
    if (WorldScene.Atmosphere.Fog.LocalFogs.size() > StaticLocalFogBaselineCount)
    {
        WorldScene.Atmosphere.Fog.LocalFogs.resize(StaticLocalFogBaselineCount);
    }
    if (!HeroTireSmokeActive || !HeroTireSmoke)
    {
        return;
    }

    const auto& FluidState = HeroTireSmoke->InspectFluidState();
    const auto& DensityGrid = FluidState.State;
    if (DensityGrid.Width == 0u || DensityGrid.Height == 0u || DensityGrid.Depth == 0u)
    {
        return;
    }

    const auto  VolU     = FluidState.TileBank.BuildVolumeUniforms(HeroTireSmoke->InspectStepSettings().Boundary, {});
    const float CellSize = HeroTireSmoke->InspectConfig().CellSizeMeters;

    // Vehicle forward (+X) & right (+Z) unit axes in Y-up physics space from unit quaternion
    const auto& Q = Reading.WorldOrientation;
    const Axis3D FwdYUp = NormalizeAxis(
        { 1.0f - 2.0f * (Q.Y * Q.Y + Q.Z * Q.Z),
          2.0f * (Q.X * Q.Y + Q.W * Q.Z),
          2.0f * (Q.X * Q.Z - Q.W * Q.Y) },
        { 1.0f, 0.0f, 0.0f });
    const Axis3D RightYUp = NormalizeAxis(
        { 2.0f * (Q.X * Q.Z + Q.W * Q.Y),
          2.0f * (Q.Y * Q.Z - Q.W * Q.X),
          1.0f - 2.0f * (Q.X * Q.X + Q.Y * Q.Y) },
        { 0.0f, 0.0f, 1.0f });

    struct PlumeAccumulator
    {
        double   WeightSum = 0.0;
        double   SumX      = 0.0;
        double   SumY      = 0.0;
        double   SumZ      = 0.0;
        double   SumXX     = 0.0;
        double   SumYY     = 0.0;
        double   SumZZ     = 0.0;
        float    PeakRho   = 0.0f;
        uint32_t CellCount = 0u;
    };
    std::array<PlumeAccumulator, 8u> Buckets {};

    const uint32_t W = DensityGrid.Width;
    const uint32_t H = DensityGrid.Height;
    const uint32_t D = DensityGrid.Depth;
    for (uint32_t Z = 0u; Z < D; ++Z)
    {
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const std::size_t Idx = DensityGrid.LinearIndex(
                    static_cast<int32_t>(X), static_cast<int32_t>(Y), static_cast<int32_t>(Z));
                const float Rho = DensityGrid.Cells[Idx][0];
                if (Rho <= 0.020f)
                {
                    continue;
                }

                const auto Wp = Frontier::SimulationSpace::Fluid::WorldFromFluidCell(VolU, {
                    static_cast<float>(X) + 0.5f,
                    static_cast<float>(Y) + 0.5f,
                    static_cast<float>(Z) + 0.5f
                });
                const float Rx = Wp[0] - Reading.WorldPositionMeters.X;
                const float Ry = Wp[1] - Reading.WorldPositionMeters.Y;
                const float Rz = Wp[2] - Reading.WorldPositionMeters.Z;
                const float LocalFwd   = Rx * FwdYUp.X + Ry * FwdYUp.Y + Rz * FwdYUp.Z;
                const float LocalRight = Rx * RightYUp.X + Ry * RightYUp.Y + Rz * RightYUp.Z;

                uint32_t BIdx = 0u;
                if (LocalFwd > -2.6f)
                {
                    const bool Rising = (Wp[1] >= 1.15f);
                    BIdx = (Rising ? 2u : 0u) + ((LocalRight > 0.0f) ? 1u : 0u);
                }
                else if (LocalFwd > -5.2f)
                {
                    BIdx = (LocalRight > 0.0f) ? 5u : 4u;
                }
                else if (LocalFwd > -8.2f)
                {
                    BIdx = 6u;
                }
                else
                {
                    BIdx = 7u;
                }

                auto& B = Buckets[BIdx];
                const double Wd = static_cast<double>(Rho);
                B.WeightSum += Wd;
                B.SumX      += Wd * static_cast<double>(Wp[0]);
                B.SumY      += Wd * static_cast<double>(Wp[1]);
                B.SumZ      += Wd * static_cast<double>(Wp[2]);
                B.SumXX     += Wd * static_cast<double>(Wp[0]) * static_cast<double>(Wp[0]);
                B.SumYY     += Wd * static_cast<double>(Wp[1]) * static_cast<double>(Wp[1]);
                B.SumZZ     += Wd * static_cast<double>(Wp[2]) * static_cast<double>(Wp[2]);
                B.PeakRho    = std::max(B.PeakRho, Rho);
                ++B.CellCount;
            }
        }
    }

    const std::size_t MaxDynamicVolumes =
        (StaticLocalFogBaselineCount < 8u) ? (8u - StaticLocalFogBaselineCount) : 0u;
    std::size_t Added = 0u;
    for (const auto& B : Buckets)
    {
        if (Added >= MaxDynamicVolumes || B.CellCount == 0u || B.WeightSum < 0.08)
        {
            continue;
        }
        const double InvW = 1.0 / B.WeightSum;
        const float MeanX = static_cast<float>(B.SumX * InvW);
        const float MeanY = static_cast<float>(B.SumY * InvW);
        const float MeanZ = static_cast<float>(B.SumZ * InvW);

        const float StdX = std::sqrt(std::max(0.0f, static_cast<float>(B.SumXX * InvW) - MeanX * MeanX));
        const float StdY = std::sqrt(std::max(0.0f, static_cast<float>(B.SumYY * InvW) - MeanY * MeanY));
        const float StdZ = std::sqrt(std::max(0.0f, static_cast<float>(B.SumZZ * InvW) - MeanZ * MeanZ));

        const float HalfYUpX = std::clamp(StdX * 2.15f + CellSize * 0.90f, 0.55f, 4.2f);
        const float HalfYUpY = std::clamp(StdY * 2.15f + CellSize * 0.85f, 0.48f, 3.4f);
        const float HalfYUpZ = std::clamp(StdZ * 2.15f + CellSize * 0.90f, 0.55f, 4.2f);

        Axis3D CenterZUp = ConvertVectorBetweenBases(
            { MeanX, MeanY, MeanZ },
            WorldCoordinateBasis::RightHandedYUp,
            WorldCoordinateBasis::RightHandedZUp);
        const Scalar3D HalfZUp { HalfYUpX, HalfYUpZ, HalfYUpY };
        CenterZUp.Z = std::max(CenterZUp.Z, HalfZUp.Z * 0.42f);

        const float MeanRho = static_cast<float>(B.WeightSum / static_cast<double>(B.CellCount));
        const float EffRho  = 0.55f * MeanRho + 0.45f * B.PeakRho;
        const float MassRamp = std::clamp(static_cast<float>(B.WeightSum / 0.65), 0.15f, 1.0f);
        const float VolDensity = std::clamp(EffRho * 13.5f * MassRamp, 0.0f, 18.0f);
        if (VolDensity <= 0.03f)
        {
            continue;
        }

        LocalFogVolume SmokeVol {};
        SmokeVol.Enabled               = true;
        SmokeVol.FollowWind            = false;
        SmokeVol.Shape                 = AtmosphericVolumeShape::Ellipsoid;
        SmokeVol.VolumeCenterMeters    = CenterZUp;
        SmokeVol.HalfExtentMeters      = HalfZUp;
        SmokeVol.YawDegrees            = 0.0f;
        SmokeVol.EdgeSoftness          = 0.62f;
        SmokeVol.Density               = VolDensity;
        SmokeVol.Coverage              = 0.78f;
        SmokeVol.FeatureScaleMeters    = 0.78f;
        SmokeVol.AnisotropyG           = 0.54f;
        SmokeVol.AbsorptionRatio       = 0.06f;
        SmokeVol.SelfShadowEnabled     = true;
        SmokeVol.DensityFalloffEnabled = true;
        SmokeVol.MarchSteps            = 32u;
        SmokeVol.FogTint               = { 0.89f, 0.91f, 0.94f };
        WorldScene.Atmosphere.Fog.LocalFogs.push_back(SmokeVol);
        ++Added;
    }
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

            if (HeroTireSmokeActive && HeroTireSmoke)
            {
                constexpr double FluidStepSec = 1.0 / 30.0;
                TireSmokeStepAccumulator += static_cast<double>(Dt);
                uint32_t FluidSteps = 0u;
                while (TireSmokeStepAccumulator >= FluidStepSec && FluidSteps < 2u)
                {
                    HeroTireSmoke->StepSingleVehicle(*Reading, HeroVehicleSpec, static_cast<float>(FluidStepSec));
                    TireSmokeStepAccumulator -= FluidStepSec;
                    ++FluidSteps;
                }
                if (FluidSteps > 0u)
                {
                    ProjectTireFluidSmokeToLocalFogs(*Reading);
                }
            }
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
