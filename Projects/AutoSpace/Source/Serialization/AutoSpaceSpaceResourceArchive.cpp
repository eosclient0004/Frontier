/*============================================================================================================================================
                                                   AUTOSPACESPACERESOURCEARCHIVE.CPP
============================================================================================================================================*/
// Native disk-backed serialization and loading of AutoSpace lobbies into Frontier .WorldSpace,
// .MaterialSpace, .GeometrySpace (with embedded signed LocalSignedDistanceVolume), and .PhotometrySpace files.

#include "Source/Serialization/AutoSpaceSpaceResourceArchive.h"

#include "Engine/Codecs/GeometryCodec.h"
#include "Engine/Codecs/MaterialCodec.h"
#include "Engine/Codecs/PhotometrySpaceCodec.h"
#include "Engine/Codecs/WorldSpaceCodec.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/DistanceFieldSpace/LocalSignedDistanceVolume.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Source/Gameplay/LobbyLifecycle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace AutoSpace
{

namespace
{

[[nodiscard]] std::string SanitizeFilenameToken(std::string_view RawText, std::string_view Fallback)
{
    std::string Clean;
    Clean.reserve(RawText.size());
    bool LastUnderscore = false;
    for (unsigned char Ch : RawText)
    {
        if (std::isalnum(Ch))
        {
            Clean.push_back(static_cast<char>(Ch));
            LastUnderscore = false;
        }
        else if (!Clean.empty() && !LastUnderscore)
        {
            Clean.push_back('_');
            LastUnderscore = true;
        }
    }
    while (!Clean.empty() && Clean.back() == '_')
    {
        Clean.pop_back();
    }
    return Clean.empty() ? std::string(Fallback) : Clean;
}

[[nodiscard]] const char* ResolveLobbyDiskStem(StudioLobbyId LobbyId) noexcept
{
    switch (LobbyId)
    {
        case StudioLobbyId::StudioLobby2ForestStage:    return "StudioLobby2ForestStage";
        case StudioLobbyId::StudioLobby3RacingGarage:   return "StudioLobby3RacingGarage";
        case StudioLobbyId::StudioLobby4TrainingGround: return "StudioLobby4TrainingGround";
        case StudioLobbyId::StudioLobby1Cyclorama:
        default:                                        return "StudioLobby1Cyclorama";
    }
}

[[nodiscard]] uint64_t HashRawBytes64(const void* Ptr, std::size_t ByteCount, uint64_t Seed = Frontier::Constants::Fnv1a64OffsetBasis) noexcept
{
    uint64_t H = Seed;
    const auto* Bytes = static_cast<const uint8_t*>(Ptr);
    for (std::size_t I = 0u; I < ByteCount; ++I)
    {
        H ^= static_cast<uint64_t>(Bytes[I]);
        H *= Frontier::Constants::Fnv1a64Prime;
    }
    return H;
}

[[nodiscard]] uint64_t ComputePyramidContentHash(const Frontier::SpatialAcceleration::ClusterPyramid& Pyramid) noexcept
{
    uint64_t H = Frontier::Constants::Fnv1a64OffsetBasis;
    const uint64_t PosCount = Pyramid.Positions.size();
    const uint64_t TriCount = Pyramid.Triangles.size();
    const uint64_t ClsCount = Pyramid.Clusters.size();
    H = HashRawBytes64(&PosCount, sizeof(PosCount), H);
    H = HashRawBytes64(&TriCount, sizeof(TriCount), H);
    H = HashRawBytes64(&ClsCount, sizeof(ClsCount), H);
    if (!Pyramid.Positions.empty())
    {
        H = HashRawBytes64(Pyramid.Positions.data(), Pyramid.Positions.size() * sizeof(Frontier::Mathematics::Axis3D), H);
    }
    if (!Pyramid.Triangles.empty())
    {
        H = HashRawBytes64(Pyramid.Triangles.data(), Pyramid.Triangles.size() * sizeof(Frontier::SpatialAcceleration::ClusterTriangleEntry), H);
    }
    return H;
}

[[nodiscard]] uint64_t ComputeMaterialContentHash(const Frontier::MaterialSpace::SurfaceMaterial& Mat) noexcept
{
    uint64_t H = Frontier::Constants::Fnv1a64OffsetBasis;
    H = HashRawBytes64(Mat.MaterialName.data(), Mat.MaterialName.size(), H);
    H = HashRawBytes64(&Mat.Diffuse, sizeof(Mat.Diffuse), H);
    H = HashRawBytes64(&Mat.Specular, sizeof(Mat.Specular), H);
    H = HashRawBytes64(&Mat.Clearcoat, sizeof(Mat.Clearcoat), H);
    H = HashRawBytes64(&Mat.ThinFilm, sizeof(Mat.ThinFilm), H);
    H = HashRawBytes64(&Mat.Transmission, sizeof(Mat.Transmission), H);
    H = HashRawBytes64(&Mat.PorosityLight, sizeof(Mat.PorosityLight), H);
    return H;
}

[[nodiscard]] std::string FormatHex8(uint64_t Val)
{
    char Buf[20]{};
    std::snprintf(Buf, sizeof(Buf), "%08x", static_cast<uint32_t>((Val ^ (Val >> 32u)) & 0xFFFFFFFFu));
    return std::string(Buf);
}

[[nodiscard]] Frontier::GeometrySpace::PolygonNetwork BuildFallbackPolygonNetworkFromPyramid(
    const Frontier::SpatialAcceleration::ClusterPyramid& Pyramid,
    const std::string&                                   Title)
{
    Frontier::GeometrySpace::PolygonNetwork Net {};
    Net.NetworkTitle = Title;
    if (Pyramid.Positions.empty() || Pyramid.Triangles.empty())
    {
        return Net;
    }

    // Keep compact valid polygonal shell from finest clusters so .GeometrySpace always has a valid PolygonNetwork section
    std::vector<uint32_t> FinestTriIndices;
    for (const auto& Cls : Pyramid.Clusters)
    {
        if (Cls.HierarchyLevel != 0u)
        {
            continue;
        }
        for (uint32_t T = 0u; T < Cls.TriangleCount; ++T)
        {
            const uint32_t Idx = Cls.TriangleStartOffset + T;
            if (Idx < Pyramid.Triangles.size())
            {
                FinestTriIndices.push_back(Idx);
            }
        }
    }
    if (FinestTriIndices.empty())
    {
        return Net;
    }

    // Add vertices and triangles (bounded to 256 proxy faces when full ClusterHierarchy is embedded)
    const uint32_t Stride = std::max<uint32_t>(1u, static_cast<uint32_t>(FinestTriIndices.size()) / 256u);
    std::unordered_map<uint32_t, uint32_t> RemapVert;
    RemapVert.reserve(std::min<std::size_t>(Pyramid.Positions.size(), 16384u));
    auto AcquireVert = [&](uint32_t SrcIdx) -> uint32_t
    {
        auto It = RemapVert.find(SrcIdx);
        if (It != RemapVert.end())
        {
            return It->second;
        }
        const uint32_t NewIdx = Net.AppendVertex(Pyramid.Positions[SrcIdx]);
        RemapVert.emplace(SrcIdx, NewIdx);
        return NewIdx;
    };

    for (std::size_t I = 0u; I < FinestTriIndices.size(); I += Stride)
    {
        const auto& Tri = Pyramid.Triangles[FinestTriIndices[I]];
        if (Tri.VertexIndex0 >= Pyramid.Positions.size() ||
            Tri.VertexIndex1 >= Pyramid.Positions.size() ||
            Tri.VertexIndex2 >= Pyramid.Positions.size())
        {
            continue;
        }
        const uint32_t A = AcquireVert(Tri.VertexIndex0);
        const uint32_t B = AcquireVert(Tri.VertexIndex1);
        const uint32_t C = AcquireVert(Tri.VertexIndex2);
        if (A != B && B != C && C != A)
        {
            const std::array<uint32_t, 3u> Loop { A, B, C };
            (void)Net.AppendFace(Loop);
        }
    }
    return Net;
}

[[nodiscard]] bool BakeSignedVolumeForPyramid(
    const Frontier::SpatialAcceleration::ClusterPyramid& Pyramid,
    Frontier::DistanceFieldSpace::LocalSignedDistanceVolume& OutVolume)
{
    using namespace Frontier::Mathematics;
    using namespace Frontier::SpatialAcceleration;

    if (Pyramid.Positions.empty() || Pyramid.Triangles.empty())
    {
        return false;
    }

    Axis3D MinPt { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    Axis3D MaxPt { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    for (const Axis3D& P : Pyramid.Positions)
    {
        MinPt.X = std::min(MinPt.X, P.X);
        MinPt.Y = std::min(MinPt.Y, P.Y);
        MinPt.Z = std::min(MinPt.Z, P.Z);
        MaxPt.X = std::max(MaxPt.X, P.X);
        MaxPt.Y = std::max(MaxPt.Y, P.Y);
        MaxPt.Z = std::max(MaxPt.Z, P.Z);
    }

    const float SpanX = std::max(0.08f, MaxPt.X - MinPt.X);
    const float SpanY = std::max(0.08f, MaxPt.Y - MinPt.Y);
    const float SpanZ = std::max(0.08f, MaxPt.Z - MinPt.Z);
    const float MaxSpan = std::max({ SpanX, SpanY, SpanZ });
    if (MaxSpan > 22.0f)
    {
        // Large room/terrain enclosures stay handled by analytic ground + Global Distance Field cascades
        return false;
    }

    std::vector<RayTrianglePrimitive> LocalPrims;
    LocalPrims.reserve(Pyramid.Triangles.size());
    for (const auto& Cls : Pyramid.Clusters)
    {
        if (Cls.HierarchyLevel != 0u)
        {
            continue;
        }
        for (uint32_t T = 0u; T < Cls.TriangleCount; ++T)
        {
            const uint32_t TriIdx = Cls.TriangleStartOffset + T;
            if (TriIdx >= Pyramid.Triangles.size())
            {
                break;
            }
            const auto& SrcTri = Pyramid.Triangles[TriIdx];
            if (SrcTri.VertexIndex0 >= Pyramid.Positions.size() ||
                SrcTri.VertexIndex1 >= Pyramid.Positions.size() ||
                SrcTri.VertexIndex2 >= Pyramid.Positions.size())
            {
                continue;
            }
            const Axis3D V0 = Pyramid.Positions[SrcTri.VertexIndex0];
            const Axis3D V1 = Pyramid.Positions[SrcTri.VertexIndex1];
            const Axis3D V2 = Pyramid.Positions[SrcTri.VertexIndex2];
            RayTrianglePrimitive Prim {};
            Prim.Corner0          = V0;
            Prim.Edge01           = V1 - V0;
            Prim.Edge02           = V2 - V0;
            Prim.SourceFaceIndex  = static_cast<uint32_t>(LocalPrims.size());
            Prim.PrimitiveOrdinal = static_cast<uint32_t>(LocalPrims.size());
            Prim.MaterialOrdinal  = SrcTri.MaterialOrdinal;
            Prim.OpaqueMask       = 1u;
            LocalPrims.push_back(Prim);
        }
    }
    if (LocalPrims.empty())
    {
        return false;
    }

    // Near-isotropic compact aspect-matched resolution (16..48 voxels/axis -> 16 KB..160 KB FP16)
    const float Pitch = std::max(0.06f, MaxSpan / 44.0f);
    const uint32_t ResX = std::clamp<uint32_t>(static_cast<uint32_t>(std::ceil((SpanX + 0.36f) / Pitch)), 16u, 48u);
    const uint32_t ResY = std::clamp<uint32_t>(static_cast<uint32_t>(std::ceil((SpanY + 0.36f) / Pitch)), 16u, 48u);
    const uint32_t ResZ = std::clamp<uint32_t>(static_cast<uint32_t>(std::ceil((SpanZ + 0.36f) / Pitch)), 16u, 48u);
    return OutVolume.BakeSignedFromTrianglesAspect(LocalPrims, ResX, ResY, ResZ, 0.18f);
}

void PopulateLocalSmoothTrianglesFromPyramid(
    const Frontier::SpatialAcceleration::ClusterPyramid&                  Pyramid,
    std::vector<Frontier::GeometricRaster::CachedLocalSmoothTriangle>&    OutSmoothTris)
{
    using namespace Frontier::Mathematics;
    using namespace Frontier::SpatialAcceleration;

    OutSmoothTris.clear();
    for (const auto& Cls : Pyramid.Clusters)
    {
        if (Cls.HierarchyLevel != 0u)
        {
            continue;
        }
        for (uint32_t TriOrd = 0u; TriOrd < Cls.TriangleCount; ++TriOrd)
        {
            const auto& Tri = Pyramid.Triangles[Cls.TriangleStartOffset + TriOrd];
            const Axis3D P0 = Pyramid.Positions[Tri.VertexIndex0];
            const Axis3D P1 = Pyramid.Positions[Tri.VertexIndex1];
            const Axis3D P2 = Pyramid.Positions[Tri.VertexIndex2];
            const Axis3D CrossN = EvaluateCross(P1 - P0, P2 - P0);
            if (EvaluateLengthSquared(CrossN) <= 1.0e-12f)
            {
                continue;
            }
            const Axis3D FlatN = NormalizeAxis(CrossN, { 0.0f, 0.0f, 1.0f });
            Frontier::GeometricRaster::CachedLocalSmoothTriangle Entry {};
            Entry.Corner0 = P0;
            Entry.Corner1 = P1;
            Entry.Corner2 = P2;
            Entry.Normal0 = (Tri.PackedNormal0 != 0u)
                ? NormalizeAxis(UnpackClusterOctahedralUnitVector(Tri.PackedNormal0), FlatN)
                : FlatN;
            Entry.Normal1 = (Tri.PackedNormal1 != 0u)
                ? NormalizeAxis(UnpackClusterOctahedralUnitVector(Tri.PackedNormal1), FlatN)
                : FlatN;
            Entry.Normal2 = (Tri.PackedNormal2 != 0u)
                ? NormalizeAxis(UnpackClusterOctahedralUnitVector(Tri.PackedNormal2), FlatN)
                : FlatN;
            Entry.MaterialSlotOverride = (Tri.MaterialOrdinal != 0u)
                ? static_cast<uint16_t>(Tri.MaterialOrdinal)
                : static_cast<uint16_t>(0xFFFFu);
            OutSmoothTris.push_back(Entry);
        }
    }
}

} // namespace

std::filesystem::path AutoSpaceSpaceResourceArchive::ResolveLobbyWorldPath(
    const std::filesystem::path& ProjectRoot,
    StudioLobbyId                LobbyId)
{
    return ProjectRoot / "Content" / "Worlds" / "Lobbies" / (std::string(ResolveLobbyDiskStem(LobbyId)) + ".WorldSpace");
}

bool AutoSpaceSpaceResourceArchive::BakeLobbyToDisk(
    const std::filesystem::path& RepoRoot,
    const std::filesystem::path& ProjectRoot,
    StudioLobbyId                LobbyId,
    LobbyArchiveSummary&         OutSummary,
    std::string&                 OutDiagnostic)
{
    using namespace Frontier::Codecs;
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::Mathematics;

    OutSummary = {};
    OutDiagnostic.clear();

    LobbyPolicy Policy = LobbyPolicyEngine::BuildDefault(LobbyId);
    Policy.EnableHeroVehiclePhysics = false;
    Policy.EnableEngineParticles    = false;

    LobbyLifecycle LobbyBuilder {};
    if (!LobbyBuilder.InitializeAndBuild(RepoRoot, Policy, OutDiagnostic))
    {
        return false;
    }

    const auto& SrcScene      = LobbyBuilder.InspectWorldScene();
    const auto& SrcGeometries = LobbyBuilder.InspectGeometryAssets();
    const auto& SrcCameras    = LobbyBuilder.InspectCameraPresets();

    const std::filesystem::path ContentRoot = ProjectRoot / "Content";
    const std::filesystem::path MatDir      = ContentRoot / "Materials";
    const std::filesystem::path GeoDir      = ContentRoot / "Geometry";
    const std::filesystem::path PhotoDir    = ContentRoot / "Photometry";
    const std::filesystem::path WorldDir    = ContentRoot / "Worlds" / "Lobbies";

    std::error_code DirEc {};
    std::filesystem::create_directories(MatDir, DirEc);
    std::filesystem::create_directories(GeoDir, DirEc);
    std::filesystem::create_directories(PhotoDir, DirEc);
    std::filesystem::create_directories(WorldDir, DirEc);

    const std::string LobbyStem(ResolveLobbyDiskStem(LobbyId));

    WorldSpaceDocumentModel WorldDoc {};
    WorldDoc.DocumentTitle           = LobbyStem;
    WorldDoc.SkyRadiance             = SrcScene.SkyRadiance;
    WorldDoc.SkyRadianceAuthored     = true;
    WorldDoc.SolarAtmosphereEnabled  = SrcScene.Atmosphere.Solar.SolarEnabled;
    WorldDoc.SkyAtmosphereEnabled    = SrcScene.Atmosphere.Medium.AtmosphereEnabled;
    WorldDoc.VolumetricCloudsEnabled = SrcScene.Atmosphere.Clouds.CloudsEnabled;

    if (!SrcCameras.empty())
    {
        WorldDoc.EditorCamera = SrcCameras.front().Camera;
        for (std::size_t CamIdx = 1u; CamIdx < SrcCameras.size(); ++CamIdx)
        {
            WorldCameraPresetEntry Preset {};
            Preset.Label  = SrcCameras[CamIdx].Label.empty()
                ? ("Camera " + std::to_string(CamIdx + 1u))
                : SrcCameras[CamIdx].Label;
            Preset.Camera = SrcCameras[CamIdx].Camera;
            WorldDoc.CameraPresets.push_back(std::move(Preset));
        }
    }

    // 1. Serialize full MaterialRegister palette into deduplicated .MaterialSpace files
    const uint32_t MatCount = SrcScene.MaterialRegister.QuerySlotCount();
    std::vector<std::string> SlotRelPaths(MatCount);
    for (uint32_t Slot = 0u; Slot < MatCount; ++Slot)
    {
        const auto& Mat = SrcScene.MaterialRegister.InspectAuthoredMaterial(static_cast<uint16_t>(Slot));
        const uint64_t MatHash = ComputeMaterialContentHash(Mat);
        const std::string FileName = SanitizeFilenameToken(Mat.MaterialName, "Material") + "_" + FormatHex8(MatHash) + ".MaterialSpace";
        const std::string RelPath  = "Content/Materials/" + FileName;
        const std::filesystem::path DiskPath = ProjectRoot / RelPath;

        if (!std::filesystem::exists(DiskPath, DirEc))
        {
            MaterialSpaceDocumentModel MatDoc {};
            MatDoc.MaterialTitle            = Mat.MaterialName.empty() ? ("MaterialSlot_" + std::to_string(Slot)) : Mat.MaterialName;
            MatDoc.IndexOfRefraction        = static_cast<double>(Mat.Specular.DielectricIOR);
            MatDoc.PhysicalMaterialIncluded = true;
            MatDoc.AuthoredMaterial         = Mat;
            if (!MaterialEncoder::EncodeToFile(DiskPath, MatDoc, OutDiagnostic))
            {
                return false;
            }
            ++OutSummary.MaterialFilesCount;
        }
        SlotRelPaths[Slot] = RelPath;
    }
    WorldDoc.MaterialPalettePaths = SlotRelPaths;

    // 2. Serialize Solar Ephemeris into .PhotometrySpace
    {
        const std::string SolarRelPath = "Content/Photometry/" + LobbyStem + "_Solar.PhotometrySpace";
        PhotometrySpaceDocumentModel SolarDoc {};
        SolarDoc.DocumentTitle     = LobbyStem + "Solar";
        SolarDoc.LuminaireCategory = PhotometryLuminaireCategory::SolarEphemeris;
        SolarDoc.SolarSource       = SrcScene.Sun;
        SolarDoc.SolarEphemeris    = SrcScene.Atmosphere.Solar;
        if (!PhotometrySpaceEncoder::EncodeToFile(ProjectRoot / SolarRelPath, SolarDoc, OutDiagnostic))
        {
            return false;
        }
        WorldDoc.SolarPhotometryPath = SolarRelPath;
        ++OutSummary.PhotometryFilesCount;
    }

    // 3. Serialize all AreaQuad lights into .PhotometrySpace + WorldDoc.AreaLuminairePlacements
    for (std::size_t I = 0u; I < SrcScene.AreaLights.size(); ++I)
    {
        const auto& Quad = SrcScene.AreaLights[I];
        const std::string AreaTitle   = (I == 0u) ? "Overhead Softbox" : ("Studio Area Softbox " + std::to_string(I + 1u));
        const std::string AreaRelPath = "Content/Photometry/" + LobbyStem + "_AreaLight_" + std::to_string(I + 1u) + ".PhotometrySpace";

        PhotometrySpaceDocumentModel AreaDoc {};
        AreaDoc.DocumentTitle     = AreaTitle;
        AreaDoc.LuminaireCategory = PhotometryLuminaireCategory::AreaQuadLuminaire;
        AreaDoc.AreaQuad          = Quad;
        if (!PhotometrySpaceEncoder::EncodeToFile(ProjectRoot / AreaRelPath, AreaDoc, OutDiagnostic))
        {
            return false;
        }
        ++OutSummary.PhotometryFilesCount;

        WorldAreaLuminairePlacement AreaPlace {};
        AreaPlace.PlacementTitle      = AreaTitle;
        AreaPlace.PhotometrySpacePath = AreaRelPath;
        AreaPlace.CenterWorld         = Quad.CenterWorld;
        AreaPlace.VisibilityEnabled   = true;
        WorldDoc.AreaLuminairePlacements.push_back(std::move(AreaPlace));
        ++OutSummary.AreaLuminairesCount;
    }

    // 4. Serialize all Measured IES PhotometricRadiators + 128x32 candela tables into .PhotometrySpace
    for (std::size_t I = 0u; I < SrcScene.PhotometricRadiators.size(); ++I)
    {
        const auto& Lamp = SrcScene.PhotometricRadiators[I];
        const std::string LampTitle   = (I == 0u) ? "Photometric Key Fresnel" :
                                        (I == 1u) ? "Photometric Rim Fresnel" :
                                                    ("Photometric Luminaire " + std::to_string(I + 1u));
        const std::string LampRelPath = "Content/Photometry/" + LobbyStem + "_IesLamp_" + std::to_string(I + 1u) + ".PhotometrySpace";

        PhotometrySpaceDocumentModel LampDoc {};
        LampDoc.DocumentTitle     = LampTitle;
        LampDoc.LuminaireCategory = PhotometryLuminaireCategory::MeasuredCandela;
        LampDoc.RadiatorPlacement = Lamp;
        if (Lamp.ProfileOrdinal < SrcScene.PhotometricProfiles.size())
        {
            LampDoc.CandelaProfile  = SrcScene.PhotometricProfiles[Lamp.ProfileOrdinal];
            LampDoc.CandelaIncluded = true;
        }
        if (!PhotometrySpaceEncoder::EncodeToFile(ProjectRoot / LampRelPath, LampDoc, OutDiagnostic))
        {
            return false;
        }
        ++OutSummary.PhotometryFilesCount;

        WorldMeasuredLuminairePlacement MeasPlace {};
        MeasPlace.PlacementTitle      = LampTitle;
        MeasPlace.PhotometrySpacePath = LampRelPath;
        MeasPlace.PositionWorld       = Lamp.PositionWorld;
        MeasPlace.BeamAxis            = Lamp.BeamAxis;
        MeasPlace.ZeroAzimuthAxis     = Lamp.ZeroAzimuthAxis;
        MeasPlace.PeakIntensityRgb    = Lamp.PeakIntensityRgb;
        MeasPlace.RangeMeters         = Lamp.RangeMeters;
        MeasPlace.RadiusMeters        = Lamp.RadiusMeters;
        WorldDoc.MeasuredLuminairePlacements.push_back(std::move(MeasPlace));
        ++OutSummary.MeasuredLuminairesCount;
    }

    // 5. Serialize deduplicated .GeometrySpace files (with ClusterPyramid + embedded signed LocalSignedDistanceVolume!)
    std::unordered_map<uint64_t, std::string> WrittenGeoByHash;
    const std::size_t InstCount = std::min(SrcGeometries.size(), SrcScene.ClusterInstances.size());
    for (std::size_t Idx = 0u; Idx < InstCount; ++Idx)
    {
        const auto& SrcGeo = SrcGeometries[Idx];
        if (!SrcGeo)
        {
            continue;
        }
        const auto& Inst = SrcScene.ClusterInstances[Idx];
        const std::string DisplayTitle = SrcGeo->DisplayTitle.empty() ? SrcGeo->StableId : SrcGeo->DisplayTitle;
        const bool ShouldEmbedSdf =
            Inst.CastShadows &&
            !Inst.TransparentGlass &&
            Inst.AnalyticalShapeCode == DistanceShapeBakedVolume;

        const uint64_t GeoHash = ComputePyramidContentHash(SrcGeo->Pyramid) ^ (ShouldEmbedSdf ? 0xA5A55A5Au : 0u);
        std::string GeoRelPath;
        auto Found = WrittenGeoByHash.find(GeoHash);
        if (Found != WrittenGeoByHash.end())
        {
            GeoRelPath = Found->second;
        }
        else
        {
            const std::string GeoFileName = SanitizeFilenameToken(DisplayTitle, "Mesh") + "_" + FormatHex8(GeoHash) + ".GeometrySpace";
            GeoRelPath = "Content/Geometry/" + GeoFileName;
            const std::filesystem::path GeoDiskPath = ProjectRoot / GeoRelPath;

            if (!std::filesystem::exists(GeoDiskPath, DirEc))
            {
                GeometrySpaceDocument GeoDoc {};
                const bool HasFullClusterHierarchy = !SrcGeo->Pyramid.Clusters.empty();
                const bool KeepFullNetwork = !SrcGeo->Network.Vertices.empty() &&
                                             !SrcGeo->Network.Faces.empty() &&
                                             (!HasFullClusterHierarchy || SrcGeo->Network.Faces.size() <= 512u);
                GeoDoc.Network = KeepFullNetwork
                    ? SrcGeo->Network
                    : BuildFallbackPolygonNetworkFromPyramid(SrcGeo->Pyramid, DisplayTitle);
                if (GeoDoc.Network.NetworkTitle.empty())
                {
                    GeoDoc.Network.NetworkTitle = DisplayTitle;
                }
                GeoDoc.ClusterHierarchy = SrcGeo->Pyramid;
                GeoDoc.ClustersIncluded = HasFullClusterHierarchy;

                if (ShouldEmbedSdf && BakeSignedVolumeForPyramid(SrcGeo->Pyramid, GeoDoc.SignedDistanceVolume))
                {
                    GeoDoc.SignedDistanceIncluded = true;
                    ++OutSummary.EmbeddedSignedVolumes;
                }

                if (!GeometryEncoder::EncodeToFile(GeoDiskPath, GeoDoc, OutDiagnostic))
                {
                    return false;
                }
                ++OutSummary.GeometryFilesCount;
            }
            WrittenGeoByHash.emplace(GeoHash, GeoRelPath);
        }

        // Compute world extents
        Axis3D MinLocal { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
        Axis3D MaxLocal { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
        for (const Axis3D& P : SrcGeo->Pyramid.Positions)
        {
            MinLocal.X = std::min(MinLocal.X, P.X);
            MinLocal.Y = std::min(MinLocal.Y, P.Y);
            MinLocal.Z = std::min(MinLocal.Z, P.Z);
            MaxLocal.X = std::max(MaxLocal.X, P.X);
            MaxLocal.Y = std::max(MaxLocal.Y, P.Y);
            MaxLocal.Z = std::max(MaxLocal.Z, P.Z);
        }
        if (SrcGeo->Pyramid.Positions.empty())
        {
            MinLocal = { -0.5f, -0.5f, -0.5f };
            MaxLocal = {  0.5f,  0.5f,  0.5f };
        }
        const Scalar3D Extents {
            std::max(0.05f, (MaxLocal.X - MinLocal.X) * Inst.UniformScale),
            std::max(0.05f, (MaxLocal.Y - MinLocal.Y) * Inst.UniformScale),
            std::max(0.05f, (MaxLocal.Z - MinLocal.Z) * Inst.UniformScale)
        };

        WorldGeometryPlacementEntry Place {};
        Place.PlacementTitle      = DisplayTitle;
        Place.StableIdentifier    = SrcGeo->StableId;
        Place.Subtitle            = SrcGeo->Subtitle.empty() ? "AutoSpace Native .GeometrySpace Asset" : SrcGeo->Subtitle;
        Place.AuthorNotes         = "Loaded from disk-backed Frontier .WorldSpace / .GeometrySpace resource.";
        Place.GeometrySpacePath   = GeoRelPath;
        Place.PrimaryMaterialPath = (Inst.MaterialSlot < SlotRelPaths.size()) ? SlotRelPaths[Inst.MaterialSlot] : std::string{};
        Place.CenterWorld         = Inst.WorldTranslation;
        Place.DimensionsMeters    = Extents;
        Place.RotationZDegrees    = Inst.RotationZDegrees;
        Place.UniformScale        = Inst.UniformScale;
        Place.NonUniformScale     = { Inst.NonUniformScale.X, Inst.NonUniformScale.Y, Inst.NonUniformScale.Z };
        Place.AnalyticalShapeCode = Inst.AnalyticalShapeCode;
        Place.SphericalPrimitive  = (Inst.AnalyticalShapeCode == DistanceShapeSphere);
        Place.GlassShell          = Inst.TransparentGlass;
        Place.VisibilityEnabled   = Inst.VisibilityEnabled;
        Place.CastShadows         = Inst.CastShadows;
        WorldDoc.GeometryPlacements.push_back(std::move(Place));
        ++OutSummary.PlacementEntriesCount;
    }

    // 6. Write the complete lobby .WorldSpace file
    const std::filesystem::path WorldPath = ResolveLobbyWorldPath(ProjectRoot, LobbyId);
    if (!WorldSpaceEncoder::EncodeToFile(WorldPath, WorldDoc, OutDiagnostic))
    {
        return false;
    }

    OutSummary.WorldSpaceFilePath = WorldPath;
    return true;
}

bool AutoSpaceSpaceResourceArchive::LoadLobbyFromDisk(
    const std::filesystem::path&                         ProjectRoot,
    StudioLobbyId                                        LobbyId,
    uint32_t                                             ViewportWidthPx,
    uint32_t                                             ViewportHeightPx,
    Frontier::GeometricRaster::ShowcaseWorldSceneBundle& OutBundle,
    std::vector<LobbyLoadedCameraPreset>&                OutExtraCameras,
    LobbyArchiveSummary&                                 OutSummary,
    std::string&                                         OutDiagnostic)
{
    using namespace Frontier::Codecs;
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::Mathematics;

    OutSummary = {};
    OutExtraCameras.clear();
    OutDiagnostic.clear();

    const std::filesystem::path WorldPath = ResolveLobbyWorldPath(ProjectRoot, LobbyId);
    std::error_code ExistEc {};
    if (!std::filesystem::exists(WorldPath, ExistEc))
    {
        OutDiagnostic = "Lobby .WorldSpace file not found on disk: " + WorldPath.string();
        return false;
    }

    WorldSpaceDocumentModel WorldDoc {};
    if (!WorldSpaceDecoder::DecodeFromFile(WorldPath, WorldDoc, OutDiagnostic))
    {
        return false;
    }

    OutBundle = {};
    OutSummary.WorldSpaceFilePath = WorldPath;

    // 1. Load MaterialPalette from .MaterialSpace files preserving exact slot ordinals 0..N-1
    //    (CompactMaterialRegister starts with slot 0 pre-populated, so slot 0 uses UpdateMaterialSlot!)
    std::unordered_map<std::string, uint16_t> SlotByRelPath;
    for (std::size_t MatIdx = 0u; MatIdx < WorldDoc.MaterialPalettePaths.size(); ++MatIdx)
    {
        const std::string& MatRelPath = WorldDoc.MaterialPalettePaths[MatIdx];
        MaterialSpaceDocumentModel MatDoc {};
        const std::filesystem::path DiskMatPath = ProjectRoot / MatRelPath;
        if (!MaterialDecoder::DecodeFromFile(DiskMatPath, MatDoc, OutDiagnostic))
        {
            return false;
        }
        Frontier::MaterialSpace::SurfaceMaterial SurfaceMat = MatDoc.AuthoredMaterial;
        if (SurfaceMat.MaterialName.empty())
        {
            SurfaceMat.MaterialName = MatDoc.MaterialTitle;
        }
        uint16_t Slot = 0u;
        if (MatIdx == 0u)
        {
            (void)OutBundle.Scene.MaterialRegister.UpdateMaterialSlot(0u, SurfaceMat);
            Slot = 0u;
        }
        else
        {
            Slot = OutBundle.Scene.MaterialRegister.RegisterMaterial(SurfaceMat);
        }
        SlotByRelPath.emplace(MatRelPath, Slot);
        ++OutSummary.MaterialFilesCount;
    }

    // 2. Load Solar Photometry from .PhotometrySpace
    if (!WorldDoc.SolarPhotometryPath.empty())
    {
        PhotometrySpaceDocumentModel SolarDoc {};
        if (!PhotometrySpaceDecoder::DecodeFromFile(ProjectRoot / WorldDoc.SolarPhotometryPath, SolarDoc, OutDiagnostic))
        {
            return false;
        }
        OutBundle.Scene.Sun              = SolarDoc.SolarSource;
        OutBundle.Scene.Atmosphere.Solar = SolarDoc.SolarEphemeris;
        ++OutSummary.PhotometryFilesCount;
    }

    if (WorldDoc.SkyRadianceAuthored)
    {
        OutBundle.Scene.SkyRadiance = WorldDoc.SkyRadiance;
    }
    OutBundle.Scene.Atmosphere.Solar.SolarEnabled        = WorldDoc.SolarAtmosphereEnabled;
    OutBundle.Scene.Atmosphere.Medium.AtmosphereEnabled  = WorldDoc.SkyAtmosphereEnabled;
    OutBundle.Scene.Atmosphere.Clouds.CloudsEnabled      = WorldDoc.VolumetricCloudsEnabled;

    // 3. Load Area Luminaires from .PhotometrySpace
    for (const auto& AreaPlace : WorldDoc.AreaLuminairePlacements)
    {
        PhotometrySpaceDocumentModel AreaDoc {};
        if (!PhotometrySpaceDecoder::DecodeFromFile(ProjectRoot / AreaPlace.PhotometrySpacePath, AreaDoc, OutDiagnostic))
        {
            return false;
        }
        auto Quad = AreaDoc.AreaQuad;
        Quad.CenterWorld = AreaPlace.CenterWorld;
        if (!AreaPlace.VisibilityEnabled)
        {
            Quad.EmittedRadiance = { 0.0f, 0.0f, 0.0f };
        }
        OutBundle.Scene.AreaLights.push_back(Quad);
        ++OutSummary.PhotometryFilesCount;
        ++OutSummary.AreaLuminairesCount;
    }

    // 4. Load Measured IES Luminaires + 128x32 Candela profiles from .PhotometrySpace
    for (const auto& MeasPlace : WorldDoc.MeasuredLuminairePlacements)
    {
        PhotometrySpaceDocumentModel LampDoc {};
        if (!PhotometrySpaceDecoder::DecodeFromFile(ProjectRoot / MeasPlace.PhotometrySpacePath, LampDoc, OutDiagnostic))
        {
            return false;
        }
        uint32_t ProfileIdx = 0u;
        if (LampDoc.CandelaIncluded)
        {
            ProfileIdx = static_cast<uint32_t>(OutBundle.Scene.PhotometricProfiles.size());
            OutBundle.Scene.PhotometricProfiles.push_back(std::move(LampDoc.CandelaProfile));
        }
        PhotometricPointRadiator Lamp = LampDoc.RadiatorPlacement;
        Lamp.PositionWorld    = MeasPlace.PositionWorld;
        Lamp.BeamAxis         = MeasPlace.BeamAxis;
        Lamp.ZeroAzimuthAxis  = MeasPlace.ZeroAzimuthAxis;
        Lamp.PeakIntensityRgb = MeasPlace.PeakIntensityRgb;
        Lamp.RangeMeters      = MeasPlace.RangeMeters;
        Lamp.RadiusMeters     = MeasPlace.RadiusMeters;
        Lamp.ProfileOrdinal   = ProfileIdx;
        OutBundle.Scene.PhotometricRadiators.push_back(Lamp);
        ++OutSummary.PhotometryFilesCount;
        ++OutSummary.MeasuredLuminairesCount;
    }

    // 5. Load Geometry Placements from .GeometrySpace (deduplicating repeated asset loads in memory!)
    std::unordered_map<std::string, std::size_t> LoadedGeoIndexByRelPath;
    OutBundle.DynamicGeometryAssets.reserve(WorldDoc.GeometryPlacements.size());
    OutBundle.Scene.ClusterInstances.reserve(WorldDoc.GeometryPlacements.size());
    OutBundle.Objects.reserve(WorldDoc.GeometryPlacements.size());

    for (const auto& Place : WorldDoc.GeometryPlacements)
    {
        std::size_t AssetIdx = 0u;
        auto GeoIt = LoadedGeoIndexByRelPath.find(Place.GeometrySpacePath);
        if (GeoIt != LoadedGeoIndexByRelPath.end())
        {
            AssetIdx = GeoIt->second;
        }
        else
        {
            GeometrySpaceDocument GeoDoc {};
            if (!GeometryDecoder::DecodeFromFile(ProjectRoot / Place.GeometrySpacePath, GeoDoc, OutDiagnostic))
            {
                return false;
            }
            auto DynAsset = std::make_unique<ShowcaseGeometryAsset>();
            DynAsset->Network  = std::move(GeoDoc.Network);
            DynAsset->Pyramid  = std::move(GeoDoc.ClusterHierarchy);
            if (GeoDoc.SignedDistanceIncluded)
            {
                DynAsset->LocalSdf = std::move(GeoDoc.SignedDistanceVolume);
                ++OutSummary.EmbeddedSignedVolumes;
            }
            AssetIdx = OutBundle.DynamicGeometryAssets.size();
            OutBundle.DynamicGeometryAssets.push_back(std::move(DynAsset));
            LoadedGeoIndexByRelPath.emplace(Place.GeometrySpacePath, AssetIdx);
            ++OutSummary.GeometryFilesCount;
        }

        const ShowcaseGeometryAsset& Asset = *OutBundle.DynamicGeometryAssets[AssetIdx];
        uint16_t MatSlot = 0u;
        if (auto MatIt = SlotByRelPath.find(Place.PrimaryMaterialPath); MatIt != SlotByRelPath.end())
        {
            MatSlot = MatIt->second;
        }

        RigidClusterInstance Inst {};
        Inst.PyramidAsset        = &Asset.Pyramid;
        Inst.WorldTranslation    = Place.CenterWorld;
        Inst.PreviousTranslation = Place.CenterWorld;
        Inst.UniformScale        = Place.UniformScale;
        Inst.NonUniformScale     = { Place.NonUniformScale.X, Place.NonUniformScale.Y, Place.NonUniformScale.Z };
        Inst.RotationZDegrees    = Place.RotationZDegrees;
        Inst.MaterialSlot        = MatSlot;
        Inst.TransparentGlass    = Place.GlassShell;
        Inst.AnalyticalShapeCode = Place.AnalyticalShapeCode;
        Inst.LocalHalfExtents    = {
            std::max(0.025f, Place.DimensionsMeters.X * 0.5f),
            std::max(0.025f, Place.DimensionsMeters.Y * 0.5f),
            std::max(0.025f, Place.DimensionsMeters.Z * 0.5f)
        };
        if (!Asset.LocalSdf.DistanceSamples.empty())
        {
            Inst.LocalSdfVolume = &Asset.LocalSdf;
        }
        Inst.CastShadows         = Place.CastShadows;
        Inst.VisibilityEnabled   = Place.VisibilityEnabled;

        const uint32_t ClusterIdx = static_cast<uint32_t>(OutBundle.Scene.ClusterInstances.size());
        OutBundle.Scene.ClusterInstances.push_back(Inst);

        ShowcaseWorldAssetEntry AssetEntry {};
        AssetEntry.Title                = Place.PlacementTitle;
        AssetEntry.StableIdentifier     = Place.StableIdentifier;
        AssetEntry.Subtitle             = Place.Subtitle;
        AssetEntry.AuthorNotes          = Place.AuthorNotes;
        AssetEntry.CenterWorld          = Place.CenterWorld;
        AssetEntry.DimensionsMeters     = Place.DimensionsMeters;
        AssetEntry.AuthoredDimensions   = Place.DimensionsMeters;
        AssetEntry.RotationZDegrees     = Place.RotationZDegrees;
        AssetEntry.MaterialSlot         = MatSlot;
        AssetEntry.ClusterInstanceIndex = ClusterIdx;
        AssetEntry.GlassShell           = Place.GlassShell;
        AssetEntry.SphericalPrimitive   = Place.SphericalPrimitive;
        AssetEntry.MaterialTitle        = OutBundle.Scene.MaterialRegister.InspectAuthoredMaterial(MatSlot).MaterialName;
        PopulateLocalSmoothTrianglesFromPyramid(Asset.Pyramid, AssetEntry.LocalSmoothTriangles);
        OutBundle.Objects.push_back(std::move(AssetEntry));
        ++OutSummary.PlacementEntriesCount;
    }

    // 6. Rebuild CPU ray/shadow scene bounds & camera presets
    RebuildShowcaseSceneAcceleration(OutBundle);

    OutBundle.EditorCamera                  = WorldDoc.EditorCamera;
    OutBundle.EditorCamera.ViewportWidthPx  = ViewportWidthPx;
    OutBundle.EditorCamera.ViewportHeightPx = ViewportHeightPx;

    for (const auto& Preset : WorldDoc.CameraPresets)
    {
        LobbyLoadedCameraPreset LoadedCam {};
        LoadedCam.Label                   = Preset.Label;
        LoadedCam.Camera                  = Preset.Camera;
        LoadedCam.Camera.ViewportWidthPx  = ViewportWidthPx;
        LoadedCam.Camera.ViewportHeightPx = ViewportHeightPx;
        OutExtraCameras.push_back(std::move(LoadedCam));
    }

    OutSummary.LoadedFromDisk = true;
    return true;
}

} // namespace AutoSpace
