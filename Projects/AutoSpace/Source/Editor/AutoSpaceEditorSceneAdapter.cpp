/*============================================================================================================================================
                                                  AUTOSPACEEDITORSCENEADAPTER.CPP
============================================================================================================================================*/
// 🧩 Frontier Editor & ProjectBrowser integration adapter populating AutoSpace's 3 Studio Lobbies inside Frontier Editor

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Editor/AutoSpaceEditorSceneAdapter.h"

#include "Engine/Configuration/BuildConfiguration.h"

#if FRONTIER_PROFILE_EDITOR

#include "Engine/Lifecycle/ProjectEditorSceneRegistry.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Source/Gameplay/LobbyLifecycle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace AutoSpace
{

namespace
{

[[nodiscard]] std::string LowercaseAscii(std::string Text)
{
    for (char& Ch : Text)
    {
        Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
    }
    return Text;
}

[[nodiscard]] bool MatchesAutoSpaceProjectFile(const std::filesystem::path& ProjectSpacePath)
{
    if (ProjectSpacePath.empty())
    {
        return false;
    }
    const std::string SuffixLower = LowercaseAscii(ProjectSpacePath.extension().string());
    const std::string StemLower   = LowercaseAscii(ProjectSpacePath.stem().string());
    return SuffixLower == ".projectspace" && StemLower == "autospace";
}

[[nodiscard]] StudioLobbyId ResolveLobbyFromVariant(uint32_t VariantIndex) noexcept
{
    switch (VariantIndex % 3u)
    {
        case 1u:  return StudioLobbyId::StudioLobby2ForestStage;
        case 2u:  return StudioLobbyId::StudioLobby3RacingGarage;
        case 0u:
        default:  return StudioLobbyId::StudioLobby1Cyclorama;
    }
}

[[nodiscard]] float ResolveLobbyExposureGain(StudioLobbyId LobbyId) noexcept
{
    switch (LobbyId)
    {
        case StudioLobbyId::StudioLobby2ForestStage:  return 1.22f;
        case StudioLobbyId::StudioLobby3RacingGarage: return 1.20f;
        case StudioLobbyId::StudioLobby1Cyclorama:
        default:                                      return 1.18f;
    }
}

[[nodiscard]] bool QueryAutoSpaceBrowserDescriptor(
    const std::filesystem::path&                  /*ProjectSpacePath*/,
    const std::filesystem::path&                  RepositoryRoot,
    Frontier::Lifecycle::ProjectBrowserDisplayDescriptor& OutDescriptor)
{
    OutDescriptor.SummaryCaption = "Automotive Showcase · 3 Lobbies · Game & Editor";
    OutDescriptor.Description =
        "AutoSpace interactive automotive project with 3 selectable lobbies (Cyclorama, Forest Soundstage, "
        "Racing Garage), Jolt vehicle physics, studio softbox & measured IES lighting, and standalone Game Mode.";
    OutDescriptor.ViewTitles = {
        "Lobby 1 · Cyclorama",
        "Lobby 2 · Forest Soundstage",
        "Lobby 3 · Racing Garage",
    };

    const std::filesystem::path ExhibitsDir = RepositoryRoot / "Exhibits" / "Images" / "AutoSpace";
    const std::filesystem::path Lobby1Thumb = ExhibitsDir / "studio-lobby-Hero34-VulkanGi.png";
    const std::filesystem::path Lobby2Thumb = ExhibitsDir / "studio-lobby-2-Hero34-VulkanGi.png";
    const std::filesystem::path Lobby3Thumb = ExhibitsDir / "studio-lobby-3-Hero34-VulkanGi.png";

    std::error_code ExistEc {};
    if (std::filesystem::exists(Lobby1Thumb, ExistEc) ||
        std::filesystem::exists(Lobby2Thumb, ExistEc) ||
        std::filesystem::exists(Lobby3Thumb, ExistEc))
    {
        OutDescriptor.ViewThumbnailPaths = { Lobby1Thumb, Lobby2Thumb, Lobby3Thumb };
    }
    return true;
}

[[nodiscard]] bool ConstructAutoSpaceEditorScene(
    const Frontier::Lifecycle::ProjectEditorSceneRequest& Request,
    Frontier::GeometricRaster::ShowcaseWorldSceneBundle&  OutBundle,
    Frontier::Lifecycle::ProjectEditorSceneResult&        OutResult,
    std::string&                                          OutDiagnostic)
{
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::Mathematics;
    using namespace Frontier::SpatialAcceleration;

    const StudioLobbyId SelectedLobby = ResolveLobbyFromVariant(Request.SceneVariantIndex);
    LobbyPolicy         Policy        = LobbyPolicyEngine::BuildDefault(SelectedLobby);
    Policy.EnableHeroVehiclePhysics   = false;
    Policy.EnableEngineParticles      = false;

    LobbyLifecycle LobbyBuilder {};
    if (!LobbyBuilder.InitializeAndBuild(Request.RepositoryRoot, Policy, OutDiagnostic))
    {
        return false;
    }

    OutBundle = {};

    const auto& SrcScene      = LobbyBuilder.InspectWorldScene();
    const auto& SrcGeometries = LobbyBuilder.InspectGeometryAssets();

    OutBundle.Scene.MaterialRegister     = SrcScene.MaterialRegister;
    OutBundle.Scene.AreaLights           = SrcScene.AreaLights;
    OutBundle.Scene.PhotometricRadiators = SrcScene.PhotometricRadiators;
    OutBundle.Scene.PhotometricProfiles  = SrcScene.PhotometricProfiles;
    OutBundle.Scene.Atmosphere           = SrcScene.Atmosphere;
    OutBundle.Scene.Sun                  = SrcScene.Sun;
    OutBundle.Scene.SkyRadiance          = SrcScene.SkyRadiance;

    const std::size_t InstanceCount = std::min(SrcGeometries.size(), SrcScene.ClusterInstances.size());
    OutBundle.DynamicGeometryAssets.reserve(InstanceCount);
    OutBundle.Scene.ClusterInstances.reserve(InstanceCount);
    OutBundle.Objects.reserve(InstanceCount);

    for (std::size_t Idx = 0u; Idx < InstanceCount; ++Idx)
    {
        const auto& SrcGeometry = SrcGeometries[Idx];
        if (!SrcGeometry)
        {
            continue;
        }

        auto DynGeometry     = std::make_unique<ShowcaseGeometryAsset>();
        DynGeometry->Network = SrcGeometry->Network;
        DynGeometry->Pyramid = SrcGeometry->Pyramid;

        Axis3D MinLocal {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()
        };
        Axis3D MaxLocal {
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()
        };
        for (const Axis3D& Pos : DynGeometry->Pyramid.Positions)
        {
            MinLocal.X = std::min(MinLocal.X, Pos.X);
            MinLocal.Y = std::min(MinLocal.Y, Pos.Y);
            MinLocal.Z = std::min(MinLocal.Z, Pos.Z);
            MaxLocal.X = std::max(MaxLocal.X, Pos.X);
            MaxLocal.Y = std::max(MaxLocal.Y, Pos.Y);
            MaxLocal.Z = std::max(MaxLocal.Z, Pos.Z);
        }
        if (DynGeometry->Pyramid.Positions.empty())
        {
            MinLocal = { -0.5f, -0.5f, -0.5f };
            MaxLocal = {  0.5f,  0.5f,  0.5f };
        }

        RigidClusterInstance Inst = SrcScene.ClusterInstances[Idx];
        Inst.PyramidAsset         = &DynGeometry->Pyramid;
        const Scalar3D Extents {
            std::max(0.05f, (MaxLocal.X - MinLocal.X) * Inst.UniformScale),
            std::max(0.05f, (MaxLocal.Y - MinLocal.Y) * Inst.UniformScale),
            std::max(0.05f, (MaxLocal.Z - MinLocal.Z) * Inst.UniformScale)
        };
        Inst.LocalHalfExtents = { Extents.X * 0.5f, Extents.Y * 0.5f, Extents.Z * 0.5f };

        const uint32_t ClusterIdx = static_cast<uint32_t>(OutBundle.Scene.ClusterInstances.size());
        OutBundle.Scene.ClusterInstances.push_back(Inst);

        ShowcaseWorldAssetEntry ObjEntry {};
        ObjEntry.Title                = SrcGeometry->DisplayTitle.empty() ? SrcGeometry->StableId : SrcGeometry->DisplayTitle;
        ObjEntry.StableIdentifier     = SrcGeometry->StableId;
        ObjEntry.Subtitle             = SrcGeometry->Subtitle.empty() ? "AutoSpace Lobby Geometry" : SrcGeometry->Subtitle;
        ObjEntry.AuthorNotes          = "Constructed by AutoSpace LobbyLifecycle inside MainWorld.WorldSpace.";
        ObjEntry.CenterWorld          = Inst.WorldTranslation;
        ObjEntry.DimensionsMeters     = Extents;
        ObjEntry.AuthoredDimensions   = Extents;
        ObjEntry.RotationZDegrees     = Inst.RotationZDegrees;
        ObjEntry.MaterialSlot         = Inst.MaterialSlot;
        ObjEntry.ClusterInstanceIndex = ClusterIdx;
        ObjEntry.MaterialTitle        =
            OutBundle.Scene.MaterialRegister.InspectAuthoredMaterial(Inst.MaterialSlot).MaterialName;

        for (const auto& Cluster : DynGeometry->Pyramid.Clusters)
        {
            if (Cluster.HierarchyLevel != 0u)
            {
                continue;
            }
            for (uint32_t TriOrd = 0u; TriOrd < Cluster.TriangleCount; ++TriOrd)
            {
                const auto& Tri = DynGeometry->Pyramid.Triangles[Cluster.TriangleStartOffset + TriOrd];
                const Axis3D P0 = DynGeometry->Pyramid.Positions[Tri.VertexIndex0];
                const Axis3D P1 = DynGeometry->Pyramid.Positions[Tri.VertexIndex1];
                const Axis3D P2 = DynGeometry->Pyramid.Positions[Tri.VertexIndex2];
                const Axis3D CrossN = EvaluateCross(P1 - P0, P2 - P0);
                if (EvaluateLengthSquared(CrossN) <= 1.0e-12f)
                {
                    continue;
                }
                const Axis3D FlatN = NormalizeAxis(CrossN, { 0.0f, 0.0f, 1.0f });
                CachedLocalSmoothTriangle Cached {};
                Cached.Corner0 = P0;
                Cached.Corner1 = P1;
                Cached.Corner2 = P2;
                Cached.Normal0 = (Tri.PackedNormal0 != 0u)
                    ? NormalizeAxis(UnpackClusterOctahedralUnitVector(Tri.PackedNormal0), FlatN)
                    : FlatN;
                Cached.Normal1 = (Tri.PackedNormal1 != 0u)
                    ? NormalizeAxis(UnpackClusterOctahedralUnitVector(Tri.PackedNormal1), FlatN)
                    : FlatN;
                Cached.Normal2 = (Tri.PackedNormal2 != 0u)
                    ? NormalizeAxis(UnpackClusterOctahedralUnitVector(Tri.PackedNormal2), FlatN)
                    : FlatN;
                Cached.MaterialSlotOverride = (Tri.MaterialOrdinal != 0u)
                    ? static_cast<uint16_t>(Tri.MaterialOrdinal)
                    : static_cast<uint16_t>(0xFFFFu);
                ObjEntry.LocalSmoothTriangles.push_back(Cached);
            }
        }

        OutBundle.Objects.push_back(std::move(ObjEntry));
        OutBundle.DynamicGeometryAssets.push_back(std::move(DynGeometry));
    }

    OutBundle.Scene.SceneTree = SrcScene.SceneTree;

    const auto& Presets = LobbyBuilder.InspectCameraPresets();
    if (!Presets.empty())
    {
        OutBundle.EditorCamera                  = Presets.front().Camera;
        OutBundle.EditorCamera.ViewportWidthPx  = Request.ViewportWidthPx;
        OutBundle.EditorCamera.ViewportHeightPx = Request.ViewportHeightPx;
    }

    const std::string LobbyLabel(LobbyPolicyEngine::QueryLabel(SelectedLobby));
    OutResult.SceneFolderTitle         = "AutoSpace · " + LobbyLabel;
    OutResult.SceneFolderSubtitle      = "MainWorld · Single-level Lobby Scene";
    OutResult.ActiveVariantTitle       = LobbyLabel;
    OutResult.PreferredLightingMode    = UnifiedLightingMode::ExactReference;
    OutResult.PreferredExposureGain    = ResolveLobbyExposureGain(SelectedLobby);
    OutResult.DisableStaticClipmapBake = true;

    for (std::size_t CamIdx = 1u; CamIdx < Presets.size(); ++CamIdx)
    {
        Frontier::Lifecycle::ProjectSceneCameraEntry ExtraCam {};
        ExtraCam.Title    = Presets[CamIdx].Label.empty()
            ? ("Camera Preset " + std::to_string(CamIdx + 1u))
            : Presets[CamIdx].Label;
        ExtraCam.Subtitle = "AutoSpace Camera Preset";
        ExtraCam.Camera   = Presets[CamIdx].Camera;
        OutResult.ExtraCameras.push_back(std::move(ExtraCam));
    }

    LobbyBuilder.Shutdown();
    OutDiagnostic.clear();
    return true;
}

} // namespace

void RegisterAutoSpaceEditorSceneAdapter()
{
    Frontier::Lifecycle::ProjectEditorSceneAdapter Adapter {};
    Adapter.ProjectIdentifier      = "AutoSpace";
    Adapter.MatchesProject         = &MatchesAutoSpaceProjectFile;
    Adapter.QueryBrowserDescriptor = &QueryAutoSpaceBrowserDescriptor;
    Adapter.ConstructEditorScene   = &ConstructAutoSpaceEditorScene;
    Frontier::Lifecycle::ProjectEditorSceneRegistry::RegisterAdapter(std::move(Adapter));
}

} // namespace AutoSpace

#else

namespace AutoSpace
{
void RegisterAutoSpaceEditorSceneAdapter() {}
} // namespace AutoSpace

#endif // FRONTIER_PROFILE_EDITOR
