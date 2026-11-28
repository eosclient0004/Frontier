/*============================================================================================================================================
                                                NATIVESPACERESOURCEROUNDTRIPVALIDATION.CPP
============================================================================================================================================*/
// Phase 4 Verification:
//   1. Bakes all 4 AutoSpace Lobbies into native disk-backed .WorldSpace, .MaterialSpace,
//      .GeometrySpace (with embedded true-signed LocalSignedDistanceVolume), and .PhotometrySpace files.
//   2. Loads all 4 Lobbies directly from disk via AutoSpaceSpaceResourceArchive::LoadLobbyFromDisk.
//   3. Verifies embedded signed SDF volumes (negative interior samples + direct LocalSdfVolume adoption).
//   4. Renders Procedural vs Disk-Loaded Lobby 1 side-by-side in Vulkan 1.3 GTX GDF GI proving visual parity.

#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Source/Gameplay/LobbyLifecycle.h"
#include "Source/Serialization/AutoSpaceSpaceResourceArchive.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

struct RoundTripTally
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Verify(bool Condition, const char* Label) noexcept
    {
        if (Condition)
        {
            ++Passed;
            std::printf("[NativeSpaceRoundTrip][PASS] %s\n", Label);
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "[NativeSpaceRoundTrip][FAIL] %s\n", Label);
        }
    }
};

[[nodiscard]] double ComputeRgbaRmse(
    const std::vector<uint8_t>& A,
    const std::vector<uint8_t>& B,
    uint32_t                    Width,
    uint32_t                    Height) noexcept
{
    const std::size_t Pixels = static_cast<std::size_t>(Width) * Height;
    if (Pixels == 0u || A.size() < Pixels * 4u || B.size() < Pixels * 4u)
    {
        return 1.0;
    }
    double SumSq = 0.0;
    for (std::size_t I = 0u; I < Pixels; ++I)
    {
        for (std::size_t C = 0u; C < 3u; ++C)
        {
            const double Diff = (static_cast<double>(A[I * 4u + C]) - static_cast<double>(B[I * 4u + C])) / 255.0;
            SumSq += Diff * Diff;
        }
    }
    return std::sqrt(SumSq / static_cast<double>(Pixels * 3u));
}

} // namespace

int main()
{
    using namespace AutoSpace;
    using namespace Frontier::GeometricRaster;

    RoundTripTally Tally {};

    const std::filesystem::path RepoRoot    = std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path ProjectRoot = RepoRoot / "Projects" / "AutoSpace";
    const std::filesystem::path ShaderDir   = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path OutDir      = RepoRoot / "Exhibits" / "Images" / "AutoSpace";
    std::filesystem::create_directories(OutDir);

    const std::array<StudioLobbyId, 4u> AllLobbies {{
        StudioLobbyId::StudioLobby1Cyclorama,
        StudioLobbyId::StudioLobby2ForestStage,
        StudioLobbyId::StudioLobby3RacingGarage,
        StudioLobbyId::StudioLobby4TrainingGround,
    }};

    uint32_t TotalGeoFiles   = 0u;
    uint32_t TotalMatFiles   = 0u;
    uint32_t TotalPhotoFiles = 0u;
    uint32_t TotalSignedVols = 0u;

    for (StudioLobbyId Lobby : AllLobbies)
    {
        LobbyArchiveSummary BakeSummary {};
        std::string         BakeDiag {};
        const bool BakeOk = AutoSpaceSpaceResourceArchive::BakeLobbyToDisk(
            RepoRoot, ProjectRoot, Lobby, BakeSummary, BakeDiag);
        if (!BakeOk)
        {
            std::fprintf(stderr, "[NativeSpaceRoundTrip] Bake failed (%s): %s\n",
                         LobbyPolicyEngine::QueryLabel(Lobby).data(), BakeDiag.c_str());
        }
        Tally.Verify(BakeOk, ("Baked native Space resources for " + std::string(LobbyPolicyEngine::QueryLabel(Lobby))).c_str());

        TotalGeoFiles   += BakeSummary.GeometryFilesCount;
        TotalMatFiles   += BakeSummary.MaterialFilesCount;
        TotalPhotoFiles += BakeSummary.PhotometryFilesCount;
        TotalSignedVols += BakeSummary.EmbeddedSignedVolumes;

        ShowcaseWorldSceneBundle             DiskBundle {};
        std::vector<LobbyLoadedCameraPreset> DiskCameras {};
        LobbyArchiveSummary                  LoadSummary {};
        std::string                          LoadDiag {};
        const bool LoadOk = AutoSpaceSpaceResourceArchive::LoadLobbyFromDisk(
            ProjectRoot, Lobby, 320u, 180u, DiskBundle, DiskCameras, LoadSummary, LoadDiag);
        if (!LoadOk)
        {
            std::fprintf(stderr, "[NativeSpaceRoundTrip] Load failed (%s): %s\n",
                         LobbyPolicyEngine::QueryLabel(Lobby).data(), LoadDiag.c_str());
        }
        Tally.Verify(LoadOk && LoadSummary.LoadedFromDisk,
                     ("Decoded .WorldSpace + resources from disk for " + std::string(LobbyPolicyEngine::QueryLabel(Lobby))).c_str());
        Tally.Verify(LoadSummary.PlacementEntriesCount == BakeSummary.PlacementEntriesCount &&
                     LoadSummary.AreaLuminairesCount == BakeSummary.AreaLuminairesCount &&
                     LoadSummary.MeasuredLuminairesCount == BakeSummary.MeasuredLuminairesCount,
                     ("Exact placement & luminaire parity on disk load for " + std::string(LobbyPolicyEngine::QueryLabel(Lobby))).c_str());
    }

    std::printf(
        "[NativeSpaceRoundTrip] Total disk assets baked: Materials=%u Geometry=%u EmbeddedSignedSDFs=%u Photometry=%u Worlds=4\n",
        TotalMatFiles, TotalGeoFiles, TotalSignedVols, TotalPhotoFiles);

    // Measure total disk footprint under Projects/AutoSpace/Content
    uint64_t TotalBytes = 0ull;
    uint32_t TotalFiles = 0u;
    for (const auto& Entry : std::filesystem::recursive_directory_iterator(ProjectRoot / "Content"))
    {
        if (Entry.is_regular_file())
        {
            TotalBytes += Entry.file_size();
            ++TotalFiles;
        }
    }
    std::printf("[NativeSpaceRoundTrip] Projects/AutoSpace/Content footprint: %u files, %.2f MB\n",
                TotalFiles, static_cast<double>(TotalBytes) / (1024.0 * 1024.0));
    Tally.Verify(TotalFiles >= 20u && TotalBytes < 64ull * 1024ull * 1024ull,
                 "Native disk assets stay compact (< 64 MB total across all 4 lobbies)");

    // Deep Lobby 1 verification: embedded signed SDFs + side-by-side Vulkan render parity
    constexpr uint32_t RenderW = 320u;
    constexpr uint32_t RenderH = 180u;

    ShowcaseWorldSceneBundle             DiskLobby1 {};
    std::vector<LobbyLoadedCameraPreset> DiskLobby1Cams {};
    LobbyArchiveSummary                  DiskLobby1Summary {};
    std::string                          Diag {};
    Tally.Verify(AutoSpaceSpaceResourceArchive::LoadLobbyFromDisk(
                     ProjectRoot, StudioLobbyId::StudioLobby1Cyclorama, RenderW, RenderH,
                     DiskLobby1, DiskLobby1Cams, DiskLobby1Summary, Diag),
                 "Loaded Lobby 1 Cyclorama directly from disk");

    uint32_t AdoptedSignedCount = 0u;
    uint32_t NegativeInteriorVolumes = 0u;
    for (const auto& Inst : DiskLobby1.Scene.ClusterInstances)
    {
        if (Inst.LocalSdfVolume != nullptr && !Inst.LocalSdfVolume->DistanceSamples.empty())
        {
            ++AdoptedSignedCount;
            float MinSample = 1.0e6f;
            for (float S : Inst.LocalSdfVolume->DistanceSamples)
            {
                MinSample = std::min(MinSample, S);
            }
            if (MinSample < -0.005f)
            {
                ++NegativeInteriorVolumes;
            }
        }
    }
    std::printf("[NativeSpaceRoundTrip] Lobby 1 adopted embedded SDF instances=%u (with true negative interiors=%u)\n",
                AdoptedSignedCount, NegativeInteriorVolumes);
    Tally.Verify(AdoptedSignedCount >= 5u && NegativeInteriorVolumes >= 3u,
                 "Disk-loaded .GeometrySpace assets adopt embedded true-signed LocalSignedDistanceVolume directly");

    // Build Procedural Lobby 1 for direct pixel-by-pixel Vulkan 1.3 parity comparison
    LobbyPolicy ProcPolicy = LobbyPolicyEngine::BuildDefault(StudioLobbyId::StudioLobby1Cyclorama);
    ProcPolicy.EnableHeroVehiclePhysics = false;
    ProcPolicy.EnableEngineParticles    = false;
    LobbyLifecycle ProcBuilder {};
    Tally.Verify(ProcBuilder.InitializeAndBuild(RepoRoot, ProcPolicy, Diag), "Built procedural Lobby 1 baseline");

    PerspectiveCameraBasis ProcCam = ProcBuilder.InspectCameraPresets().front().Camera;
    ProcCam.ViewportWidthPx        = RenderW;
    ProcCam.ViewportHeightPx       = RenderH;

    VulkanInterchange GpuDevice {};
    Tally.Verify(GpuDevice.InitializeInterchange(RepoRoot), "VulkanInterchange initialized");
    VulkanWorldRenderSequence GpuSeq {};
    Tally.Verify(GpuSeq.InitializeSequence(GpuDevice, ShaderDir), "VulkanWorldRenderSequence initialized");
    if (!GpuSeq.QueryReady())
    {
        return 1;
    }

    WorldRenderConfiguration RenderOpt {};
    RenderOpt.Engine           = WorldRenderEngine::RealTimeGiGtxLowEnd;
    RenderOpt.ShadowAlgorithm  = SolarShadowAlgorithm::ContactPcss;
    RenderOpt.LightingMode     = UnifiedLightingMode::ExactReference;
    RenderOpt.ExposureGain     = 1.18f;
    RenderOpt.NaniteLodEnabled = false;

    std::vector<uint8_t> ProcRgba;
    GpuSeq.SynchronizeSceneResources(ProcBuilder.InspectWorldScene(), true);
    Tally.Verify(GpuSeq.RenderWorldViewportWithEngine(RenderOpt, 0u, ProcCam, ProcBuilder.InspectWorldScene(), ProcRgba),
                 "Rendered Procedural Lobby 1 in Vulkan GTX GDF GI");

    std::vector<uint8_t> DiskRgba;
    GpuSeq.SynchronizeSceneResources(DiskLobby1.Scene, true);
    Tally.Verify(GpuSeq.RenderWorldViewportWithEngine(RenderOpt, 0u, DiskLobby1.EditorCamera, DiskLobby1.Scene, DiskRgba),
                 "Rendered Disk-Loaded .WorldSpace Lobby 1 in Vulkan GTX GDF GI");

    const double Rmse = ComputeRgbaRmse(ProcRgba, DiskRgba, RenderW, RenderH);
    std::printf("[NativeSpaceRoundTrip] Procedural vs Disk-Loaded Lobby 1 RMSE = %.5f (<= 0.035)\n", Rmse);
    Tally.Verify(Rmse <= 0.035, "Disk-loaded .WorldSpace render visually matches procedural Lobby 1");

    // Export side-by-side comparison PNG (Left: Procedural | Right: Disk-Loaded .WorldSpace)
    constexpr uint32_t Margin = 8u;
    const uint32_t SheetW = RenderW * 2u + Margin * 3u;
    const uint32_t SheetH = RenderH + Margin * 2u;
    std::vector<uint8_t> Sheet(static_cast<std::size_t>(SheetW) * SheetH * 4u, 24u);
    for (std::size_t I = 3u; I < Sheet.size(); I += 4u) { Sheet[I] = 255u; }
    for (uint32_t Y = 0u; Y < RenderH; ++Y)
    {
        std::memcpy(Sheet.data() + (static_cast<std::size_t>(Margin + Y) * SheetW + Margin) * 4u,
                    ProcRgba.data() + static_cast<std::size_t>(Y) * RenderW * 4u,
                    static_cast<std::size_t>(RenderW) * 4u);
        std::memcpy(Sheet.data() + (static_cast<std::size_t>(Margin + Y) * SheetW + Margin * 2u + RenderW) * 4u,
                    DiskRgba.data() + static_cast<std::size_t>(Y) * RenderW * 4u,
                    static_cast<std::size_t>(RenderW) * 4u);
    }

    const std::filesystem::path ProofPath = OutDir / "NativeSpaceResourceRoundTripProof.png";
    const int Saved = stbi_write_png(ProofPath.string().c_str(), static_cast<int>(SheetW), static_cast<int>(SheetH), 4, Sheet.data(), static_cast<int>(SheetW * 4u));
    Tally.Verify(Saved != 0, "Saved Exhibits/Images/AutoSpace/NativeSpaceResourceRoundTripProof.png");

    ProcBuilder.Shutdown();
    GpuSeq.TeardownSequence();
    GpuDevice.TeardownInterchange();

    std::printf("[NativeSpaceRoundTrip] Summary: Passed=%u Failed=%u -> %s\n",
                Tally.Passed, Tally.Failed, ProofPath.string().c_str());
    return (Tally.Failed == 0u) ? 0 : 1;
}
