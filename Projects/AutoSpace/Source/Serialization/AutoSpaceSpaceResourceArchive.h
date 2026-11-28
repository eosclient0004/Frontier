/*============================================================================================================================================
                                                    AUTOSPACESPACERESOURCEARCHIVE.H
============================================================================================================================================*/
// Native disk-backed serialization and loading of AutoSpace lobbies into Frontier .WorldSpace,
// .MaterialSpace, .GeometrySpace (with embedded signed LocalSignedDistanceVolume), and .PhotometrySpace files.

#pragma once

#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Source/Policies/LobbyPolicy.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace AutoSpace
{

struct LobbyArchiveSummary
{
    std::filesystem::path WorldSpaceFilePath       {};
    uint32_t              MaterialFilesCount       = 0u;
    uint32_t              GeometryFilesCount       = 0u;
    uint32_t              EmbeddedSignedVolumes    = 0u;
    uint32_t              PhotometryFilesCount     = 0u;
    uint32_t              PlacementEntriesCount    = 0u;
    uint32_t              AreaLuminairesCount      = 0u;
    uint32_t              MeasuredLuminairesCount  = 0u;
    bool                  LoadedFromDisk           = false;
};

struct LobbyLoadedCameraPreset
{
    std::string                                        Label  {};
    Frontier::GeometricRaster::PerspectiveCameraBasis  Camera {};
};

class AutoSpaceSpaceResourceArchive
{
public:
    [[nodiscard]] static std::filesystem::path ResolveLobbyWorldPath(
        const std::filesystem::path& ProjectRoot,
        StudioLobbyId                LobbyId);

    [[nodiscard]] static bool BakeLobbyToDisk(
        const std::filesystem::path& RepoRoot,
        const std::filesystem::path& ProjectRoot,
        StudioLobbyId                LobbyId,
        LobbyArchiveSummary&         OutSummary,
        std::string&                 OutDiagnostic);

    [[nodiscard]] static bool LoadLobbyFromDisk(
        const std::filesystem::path&                        ProjectRoot,
        StudioLobbyId                                       LobbyId,
        uint32_t                                            ViewportWidthPx,
        uint32_t                                            ViewportHeightPx,
        Frontier::GeometricRaster::ShowcaseWorldSceneBundle& OutBundle,
        std::vector<LobbyLoadedCameraPreset>&               OutExtraCameras,
        LobbyArchiveSummary&                                OutSummary,
        std::string&                                        OutDiagnostic);
};

} // namespace AutoSpace
