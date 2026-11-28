/*============================================================================================================================================
                                                LIVELIGHTINGSYNCHRONIZATIONVALIDATION.CPP
============================================================================================================================================*/
// Live Vulkan 1.3 verification of Phase 2 + Phase 3:
//   1. Build-time FRONTIER_GIT_COMMIT_HASH accuracy (never "unknown")
//   2. Zero phantom lights when AutoSpace Lobby 1 binds into OutlinerPanel + ShowcaseGalleryScene
//   3. Preserved authored radiance on AreaLights[0] (never overwritten with hardcoded 7.5 nits)
//   4. Full PhotometricRadiator + AreaLight + coupled diffuser visibility & Radiosity refresh synchronization
//   5. Zero idle-tick mutation / ping-pong on SynchronizeShowcaseGallerySceneLive

#include "Engine/Components/Diagnostics/ViewportTimingTelemetry.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Lifecycle/ProjectEditorSceneRegistry.h"
#include "Source/Editor/AutoSpaceEditorSceneAdapter.h"

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

struct VerificationTally
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Verify(bool Condition, const char* Label) noexcept
    {
        if (Condition)
        {
            ++Passed;
            std::printf("[LiveLightingSync][PASS] %s\n", Label);
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "[LiveLightingSync][FAIL] %s\n", Label);
        }
    }
};

[[nodiscard]] double ComputeMeanLuma(const std::vector<uint8_t>& Rgba, uint32_t Width, uint32_t Height) noexcept
{
    if (Rgba.size() < static_cast<std::size_t>(Width) * Height * 4u || Width == 0u || Height == 0u)
    {
        return 0.0;
    }
    double Sum = 0.0;
    const std::size_t Count = static_cast<std::size_t>(Width) * Height;
    for (std::size_t I = 0u; I < Count; ++I)
    {
        const std::size_t O = I * 4u;
        Sum += 0.2126 * Rgba[O + 0u] + 0.7152 * Rgba[O + 1u] + 0.0722 * Rgba[O + 2u];
    }
    return Sum / static_cast<double>(Count);
}

void BlitTile2x2(
    std::vector<uint8_t>&       Sheet,
    uint32_t                    SheetW,
    const std::vector<uint8_t>& Tile,
    uint32_t                    TileW,
    uint32_t                    TileH,
    uint32_t                    Col,
    uint32_t                    Row,
    uint32_t                    Margin) noexcept
{
    const uint32_t DstX = Margin + Col * (TileW + Margin);
    const uint32_t DstY = Margin + Row * (TileH + Margin);
    for (uint32_t Y = 0u; Y < TileH; ++Y)
    {
        std::memcpy(
            Sheet.data() + (static_cast<std::size_t>(DstY + Y) * SheetW + DstX) * 4u,
            Tile.data() + (static_cast<std::size_t>(Y) * TileW) * 4u,
            static_cast<std::size_t>(TileW) * 4u);
    }
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::Lifecycle;

    VerificationTally Tally {};

    const std::string BuildRev(FRONTIER_GIT_COMMIT_HASH);
    std::printf("[LiveLightingSync] BuildCommit=%s\n", BuildRev.c_str());
    Tally.Verify(!BuildRev.empty() && BuildRev != "unknown" && BuildRev.size() >= 7u,
                 "Build-time FRONTIER_GIT_COMMIT_HASH resolved valid Git SHA");

    const std::filesystem::path RepoRoot  = std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path OutDir    = RepoRoot / "Exhibits" / "Images" / "AutoSpace";
    std::filesystem::create_directories(OutDir);

    AutoSpace::RegisterAutoSpaceEditorSceneAdapter();
    const std::filesystem::path ProjectPath = RepoRoot / "Projects" / "AutoSpace" / "AutoSpace.projectspace";
    const ProjectEditorSceneAdapter* Adapter = ProjectEditorSceneRegistry::FindMatchingAdapter(ProjectPath);
    Tally.Verify(Adapter != nullptr && static_cast<bool>(Adapter->ConstructEditorScene),
                 "AutoSpaceEditorSceneAdapter registered cleanly");
    if (Adapter == nullptr)
    {
        return 1;
    }

    constexpr uint32_t TileW = 320u;
    constexpr uint32_t TileH = 180u;

    ProjectEditorSceneRequest Req {};
    Req.ProjectSpacePath  = ProjectPath;
    Req.RepositoryRoot    = RepoRoot;
    Req.SceneVariantIndex = 0u;
    Req.ViewportWidthPx   = TileW;
    Req.ViewportHeightPx  = TileH;

    ShowcaseWorldSceneBundle Bundle {};
    ProjectEditorSceneResult Result {};
    std::string              Diag {};
    Tally.Verify(Adapter->ConstructEditorScene(Req, Bundle, Result, Diag),
                 "AutoSpace Lobby 1 editor scene constructed cleanly");

    const uint32_t AuthoredAreaCount = static_cast<uint32_t>(Bundle.Scene.AreaLights.size());
    const uint32_t AuthoredIesCount  = static_cast<uint32_t>(Bundle.Scene.PhotometricRadiators.size());
    const float    AuthoredArea0R    = AuthoredAreaCount > 0u ? Bundle.Scene.AreaLights[0].EmittedRadiance.R : 0.0f;

    OutlinerPanel Outliner {};
    RegisterShowcaseGalleryInOutliner(Outliner, Bundle);

    bool NeedsRebuild = false;
    (void)SynchronizeShowcaseGallerySceneLive(Outliner, Bundle, NeedsRebuild, true);

    std::printf(
        "[LiveLightingSync] Authored(Area=%u, IES=%u, Area0.R=%.2f) -> Synced(Area=%zu, IES=%zu, AreaLinks=%zu, IesLinks=%zu, Area0.R=%.2f)\n",
        AuthoredAreaCount,
        AuthoredIesCount,
        AuthoredArea0R,
        Bundle.Scene.AreaLights.size(),
        Bundle.Scene.PhotometricRadiators.size(),
        Bundle.AreaLightBindings.size(),
        Bundle.PhotometricLightBindings.size(),
        Bundle.Scene.AreaLights.empty() ? 0.0f : Bundle.Scene.AreaLights[0].EmittedRadiance.R);

    Tally.Verify(Bundle.Scene.AreaLights.size() == AuthoredAreaCount && AuthoredAreaCount == 3u,
                 "Zero phantom area quad lights spawned on live Outliner sync");
    Tally.Verify(Bundle.Scene.PhotometricRadiators.size() == AuthoredIesCount && AuthoredIesCount == 2u,
                 "All authored PhotometricRadiators preserved without duplication");
    Tally.Verify(Bundle.AreaLightBindings.size() == AuthoredAreaCount &&
                 Bundle.PhotometricLightBindings.size() == AuthoredIesCount,
                 "Every AreaLight and PhotometricRadiator bound 1:1 to OutlinerPanel tokens");
    Tally.Verify(std::fabs(Bundle.Scene.AreaLights[0].EmittedRadiance.R - AuthoredArea0R) < 1.0e-3f,
                 "Overhead Softbox preserves authored 0.95-nit radiance (not overwritten with 7.5 nits)");

    uint32_t CoupledDiffusers = 0u;
    for (const auto& Link : Bundle.AreaLightBindings)
    {
        if (Link.CoupledDiffuserEntryIndex >= 0) { ++CoupledDiffusers; }
    }
    for (const auto& Link : Bundle.PhotometricLightBindings)
    {
        if (Link.CoupledDiffuserEntryIndex >= 0) { ++CoupledDiffusers; }
    }
    std::printf("[LiveLightingSync] Coupled luminaire diffuser fixtures: %u / %u\n",
                CoupledDiffusers, AuthoredAreaCount + AuthoredIesCount);
    Tally.Verify(CoupledDiffusers >= 4u, "Studio luminaire diffuser fixtures coupled to their native lights");

    // Idle re-sync must produce ZERO mutations and ZERO rebuild flags
    bool IdleRebuild = false;
    const bool IdleChanged = SynchronizeShowcaseGallerySceneLive(Outliner, Bundle, IdleRebuild, true);
    Tally.Verify(!IdleChanged && !IdleRebuild, "Idle Edit-mode sync produces zero churn and zero rebuilds");

    // Boot Vulkan 1.3 GTX GDF GI engine to visually prove zero phantom light & live Radiosity updates
    VulkanInterchange GpuDevice {};
    Tally.Verify(GpuDevice.InitializeInterchange(RepoRoot), "VulkanInterchange initialized");
    if (!GpuDevice.Ready())
    {
        return 1;
    }

    VulkanWorldRenderSequence GpuSeq {};
    Tally.Verify(GpuSeq.InitializeSequence(GpuDevice, ShaderDir), "VulkanWorldRenderSequence initialized");
    if (!GpuSeq.QueryReady())
    {
        return 1;
    }

    GpuSeq.SynchronizeSceneResources(Bundle.Scene, true);

    WorldRenderConfiguration RenderOpt {};
    RenderOpt.Engine               = WorldRenderEngine::RealTimeGiGtxLowEnd;
    RenderOpt.ShadowAlgorithm      = SolarShadowAlgorithm::ContactPcss;
    RenderOpt.LightingMode         = UnifiedLightingMode::ExactReference;
    RenderOpt.ExposureGain         = Result.PreferredExposureGain;
    RenderOpt.NaniteLodEnabled     = false;

    std::array<std::vector<uint8_t>, 4u> Tiles {};

    // Tile 0: Full Studio Lobby 1 Default Lighting
    Tally.Verify(GpuSeq.RenderWorldViewportWithEngine(RenderOpt, 0u, Bundle.EditorCamera, Bundle.Scene, Tiles[0]),
                 "Rendered Tile 0: Full AutoSpace Lobby 1 (3 Area + 2 IES + Orb)");
    const double LumaFull = ComputeMeanLuma(Tiles[0], TileW, TileH);

    // Tile 1: Hide entire Lighting folder + Sun + Dynamic Emissive Orb -> MUST go pitch dark (ZERO phantom lights!)
    if (OutlinerRecordEntry* Folder = Outliner.AccessEntry(Bundle.LightingFolderToken))
    {
        Folder->VisibilityEnabled = false;
    }
    if (OutlinerRecordEntry* SunEntry = Outliner.AccessEntry(Bundle.SunEmitterToken))
    {
        SunEntry->VisibilityEnabled = false;
    }
    OutlinerSlotToken OrbSlot {};
    for (const auto& Obj : Bundle.Objects)
    {
        if (Obj.Title == "Dynamic Emissive Orb")
        {
            OrbSlot = Obj.SlotToken;
            if (OutlinerRecordEntry* OrbEntry = Outliner.AccessEntry(OrbSlot))
            {
                OrbEntry->VisibilityEnabled = false;
            }
        }
    }
    bool DarkRebuild = false;
    (void)SynchronizeShowcaseGallerySceneLive(Outliner, Bundle, DarkRebuild, true);

    // Verify idle re-sync while dark ALSO produces ZERO ping-pong!
    bool DarkIdleRebuild = false;
    const bool DarkIdleChanged = SynchronizeShowcaseGallerySceneLive(Outliner, Bundle, DarkIdleRebuild, true);
    Tally.Verify(!DarkIdleChanged && !DarkIdleRebuild,
                 "Hidden Lighting folder stays stable with zero diffuser visibility ping-pong");

    bool AllLightsZero = true;
    for (const auto& Q : Bundle.Scene.AreaLights)
    {
        if (Q.EmittedRadiance.R > 1.0e-4f || Q.EmittedRadiance.G > 1.0e-4f || Q.EmittedRadiance.B > 1.0e-4f)
        {
            AllLightsZero = false;
        }
    }
    for (const auto& R : Bundle.Scene.PhotometricRadiators)
    {
        if (R.PeakIntensityRgb.R > 1.0e-4f || R.PeakIntensityRgb.G > 1.0e-4f || R.PeakIntensityRgb.B > 1.0e-4f)
        {
            AllLightsZero = false;
        }
    }
    Tally.Verify(AllLightsZero, "Hiding Lighting folder zeros 100% of AreaLights and PhotometricRadiators");

    // Also dim sky radiance to zero to verify zero phantom studio light leak
    const auto SavedSky = Bundle.Scene.SkyRadiance;
    Bundle.Scene.SkyRadiance = { 0.0f, 0.0f, 0.0f };
    GpuSeq.SynchronizeSceneResources(Bundle.Scene, false);
    Tally.Verify(GpuSeq.RenderWorldViewportWithEngine(RenderOpt, 0u, Bundle.EditorCamera, Bundle.Scene, Tiles[1]),
                 "Rendered Tile 1: All Lights & Lighting Folder Hidden (Darkness Proof)");
    const double LumaDark = ComputeMeanLuma(Tiles[1], TileW, TileH);

    // Tile 2: Re-enable Lighting folder but hide all AreaLights + IES #2 -> ONLY IES Key Fresnel #1 active!
    if (OutlinerRecordEntry* Folder = Outliner.AccessEntry(Bundle.LightingFolderToken))
    {
        Folder->VisibilityEnabled = true;
    }
    for (const auto& Link : Bundle.AreaLightBindings)
    {
        if (OutlinerRecordEntry* E = Outliner.AccessEntry(Link.SlotToken))
        {
            E->VisibilityEnabled = false;
        }
    }
    if (Bundle.PhotometricLightBindings.size() > 1u)
    {
        if (OutlinerRecordEntry* E = Outliner.AccessEntry(Bundle.PhotometricLightBindings[1].SlotToken))
        {
            E->VisibilityEnabled = false;
        }
    }
    bool IesRebuild = false;
    (void)SynchronizeShowcaseGallerySceneLive(Outliner, Bundle, IesRebuild, true);
    GpuSeq.SynchronizeSceneResources(Bundle.Scene, false);
    Tally.Verify(GpuSeq.RenderWorldViewportWithEngine(RenderOpt, 0u, Bundle.EditorCamera, Bundle.Scene, Tiles[2]),
                 "Rendered Tile 2: Single IES Key Fresnel Lamp Live (Live Photometric + Radiosity Refresh)");
    const double LumaSingleIes = ComputeMeanLuma(Tiles[2], TileW, TileH);

    // Tile 3: Restore ALL lights + sky + orb -> returns cleanly to full Studio Lobby 1 lighting
    Bundle.Scene.SkyRadiance = SavedSky;
    for (const auto& Link : Bundle.AreaLightBindings)
    {
        if (OutlinerRecordEntry* E = Outliner.AccessEntry(Link.SlotToken))
        {
            E->VisibilityEnabled = true;
        }
    }
    for (const auto& Link : Bundle.PhotometricLightBindings)
    {
        if (OutlinerRecordEntry* E = Outliner.AccessEntry(Link.SlotToken))
        {
            E->VisibilityEnabled = true;
        }
    }
    if (OutlinerRecordEntry* OrbEntry = Outliner.AccessEntry(OrbSlot))
    {
        OrbEntry->VisibilityEnabled = true;
    }
    bool RestoreRebuild = false;
    (void)SynchronizeShowcaseGallerySceneLive(Outliner, Bundle, RestoreRebuild, true);
    GpuSeq.SynchronizeSceneResources(Bundle.Scene, false);
    Tally.Verify(GpuSeq.RenderWorldViewportWithEngine(RenderOpt, 0u, Bundle.EditorCamera, Bundle.Scene, Tiles[3]),
                 "Rendered Tile 3: All Lights & Diffusers Restored");
    const double LumaRestored = ComputeMeanLuma(Tiles[3], TileW, TileH);

    std::printf(
        "[LiveLightingSync] Mean Luma [0..255]: Full=%.2f | AllHidden=%.2f | SingleIES=%.2f | Restored=%.2f\n",
        LumaFull, LumaDark, LumaSingleIes, LumaRestored);

    Tally.Verify(LumaFull > 45.0, "Full Lobby 1 studio scene renders brightly");
    Tally.Verify(LumaDark < 15.0 && LumaDark < LumaFull * 0.25,
                 "Hiding Lighting folder + Sun + Orb eliminates phantom light and stale Radiosity bounce");
    Tally.Verify(LumaSingleIes > LumaDark + 8.0,
                 "Enabling single Photometric IES lamp restores illumination + live Radiosity bounce");
    Tally.Verify(std::fabs(LumaRestored - LumaFull) < 6.0,
                 "Restoring Lighting folder recovers exact original Lobby 1 illumination");

    // Export 2x2 visual proof sheet
    constexpr uint32_t Margin = 8u;
    const uint32_t SheetW = TileW * 2u + Margin * 3u;
    const uint32_t SheetH = TileH * 2u + Margin * 3u;
    std::vector<uint8_t> Sheet(static_cast<std::size_t>(SheetW) * SheetH * 4u, 22u);
    for (std::size_t I = 3u; I < Sheet.size(); I += 4u) { Sheet[I] = 255u; }
    BlitTile2x2(Sheet, SheetW, Tiles[0], TileW, TileH, 0u, 0u, Margin);
    BlitTile2x2(Sheet, SheetW, Tiles[1], TileW, TileH, 1u, 0u, Margin);
    BlitTile2x2(Sheet, SheetW, Tiles[2], TileW, TileH, 0u, 1u, Margin);
    BlitTile2x2(Sheet, SheetW, Tiles[3], TileW, TileH, 1u, 1u, Margin);

    const std::filesystem::path SheetPath = OutDir / "LiveLightingSynchronizationProofSheet.png";
    const int Saved = stbi_write_png(
        SheetPath.string().c_str(),
        static_cast<int>(SheetW),
        static_cast<int>(SheetH),
        4,
        Sheet.data(),
        static_cast<int>(SheetW * 4u));
    Tally.Verify(Saved != 0, "Exported Exhibits/Images/AutoSpace/LiveLightingSynchronizationProofSheet.png");

    GpuSeq.TeardownSequence();
    GpuDevice.TeardownInterchange();

    std::printf("[LiveLightingSync] Summary: Passed=%u Failed=%u -> %s\n",
                Tally.Passed, Tally.Failed, SheetPath.string().c_str());
    return (Tally.Failed == 0u) ? 0 : 1;
}
