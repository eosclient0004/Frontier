/*============================================================================================================================================
                                                           AUTOSPACELAUNCH.CPP
============================================================================================================================================*/
// 🧩 Standalone Game Mode executable for AutoSpace (native desktop window + Vulkan swapchain presentation, zero Editor chrome)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Gameplay/AutoSpaceLifecycle.h"
#include "Source/Cockpit/VehicleCockpitInstrumentation.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/ComponentEditor/ComponentViewportRenderer.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/Rasterization/ViewportSwapchainPresenter.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

#ifndef FRONTIER_COMPILED_SHADER_DIR
#define FRONTIER_COMPILED_SHADER_DIR "CompiledShaders"
#endif

namespace
{

struct OrbitCameraRig
{
    Frontier::Mathematics::Axis3D TargetWorld     { 0.0f, 0.0f, 0.85f };
    float                         AzimuthRadians  = -0.65f;
    float                         ElevationRadians = 0.22f;
    float                         DistanceMeters  = 6.4f;
    float                         VerticalFovRad  = 0.72f;
    float                         NearClipMeters  = 0.12f;
    float                         FarClipMeters   = 5000.0f;

    void SeedFromCamera(const Frontier::GeometricRaster::PerspectiveCameraBasis& Source) noexcept
    {
        TargetWorld    = Source.TargetPoint;
        VerticalFovRad = Source.VerticalFovRadians;
        NearClipMeters = Source.NearClipMeters;
        FarClipMeters  = Source.FarClipMeters;

        const Frontier::Mathematics::Axis3D Offset = Source.CameraOrigin - Source.TargetPoint;
        const float HorizontalDist = std::hypot(Offset.X, Offset.Y);
        DistanceMeters   = std::clamp(std::hypot(HorizontalDist, Offset.Z), 1.8f, 32.0f);
        AzimuthRadians   = std::atan2(Offset.Y, Offset.X);
        ElevationRadians = std::clamp(std::atan2(Offset.Z, std::max(0.001f, HorizontalDist)), 0.04f, 1.38f);
    }

    [[nodiscard]] Frontier::GeometricRaster::PerspectiveCameraBasis ConstructCamera(
        uint32_t WidthPx,
        uint32_t HeightPx) const noexcept
    {
        Frontier::GeometricRaster::PerspectiveCameraBasis Camera {};
        const float CosElev = std::cos(ElevationRadians);
        const float SinElev = std::sin(ElevationRadians);
        Camera.CameraOrigin = {
            TargetWorld.X + DistanceMeters * CosElev * std::cos(AzimuthRadians),
            TargetWorld.Y + DistanceMeters * CosElev * std::sin(AzimuthRadians),
            TargetWorld.Z + DistanceMeters * SinElev
        };
        Camera.TargetPoint        = TargetWorld;
        Camera.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
        Camera.VerticalFovRadians = VerticalFovRad;
        Camera.NearClipMeters     = NearClipMeters;
        Camera.FarClipMeters      = FarClipMeters;
        Camera.ViewportWidthPx    = WidthPx;
        Camera.ViewportHeightPx   = HeightPx;
        return Camera;
    }
};

[[nodiscard]] float QueryLobbyExposureGain(AutoSpace::StudioLobbyId LobbyId) noexcept
{
    switch (LobbyId)
    {
        case AutoSpace::StudioLobbyId::StudioLobby2ForestStage:    return 1.22f;
        case AutoSpace::StudioLobbyId::StudioLobby3RacingGarage:   return 1.20f;
        case AutoSpace::StudioLobbyId::StudioLobby4TrainingGround: return 1.05f;
        case AutoSpace::StudioLobbyId::StudioLobby1Cyclorama:
        default:                                                   return 1.18f;
    }
}

[[nodiscard]] std::string ConstructWindowTitle(AutoSpace::StudioLobbyId LobbyId)
{
    if (LobbyId == AutoSpace::StudioLobbyId::StudioLobby4TrainingGround)
    {
        return std::string("AutoSpace — ") +
               std::string(AutoSpace::LobbyPolicyEngine::QueryLabel(LobbyId)) +
               "  [WASD/Arrows: Drive | Space: Handbrake | B: Burnout | T: Drift TCS | E/Q: Shift | C: Cam | R: Reset | 1-4: Lobby]";
    }
    return std::string("AutoSpace — ") +
           std::string(AutoSpace::LobbyPolicyEngine::QueryLabel(LobbyId)) +
           "  [1-4: Lobby | C: Camera | Drag: Orbit | WASD: Drive | Esc: Exit]";
}

void PrintDrivingControlsLegend()
{
    std::cout
        << "\n================ AutoSpace Driving & Cockpit Controls ================\n"
        << "  1 / 2 / 3 / 4        Switch Lobby (1-3 = Studios, 4 = Training Ground + Multi-World 2D Portal)\n"
        << "  W / Up               Throttle / Accelerate\n"
        << "  S / Down             Brake (auto-Reverse when stopped)\n"
        << "  A / D  or Left/Right Steer Left / Right\n"
        << "  Space                Handbrake (initiate drifts & rear tire smoke)\n"
        << "  B                    Burnout Line-Lock (front brake + full rear throttle smoke)\n"
        << "  E / Q or Shift/Ctrl  Manual Gear Shift Up / Down\n"
        << "  T                    Cycle Assists (Sport -> Drift/Off -> Safe)\n"
        << "  C / V / Tab          Cycle Camera (Chase / Drift / Interior Cockpit / HMI / Orbit / Portal B)\n"
        << "  P / O                Cycle 2D Portal Shape (Rect/Circle/Hex/Octagon/Cutout) / Toggle World B Cam\n"
        << "  H                    Toggle HMI Touchscreen Focus View (click 3D HMI buttons with mouse)\n"
        << "  U / G                Toggle On-Screen HUD / Windshield Glass Optical HUD\n"
        << "  F5 / F6 / F7 / F8    Switch Render Engine (PBR / SDF+GI / MatCap / PathTracer) - HMI syncs live\n"
        << "  Mouse Drag / Wheel   Orbit Camera / Zoom (Left-Click interacts with HMI in Cockpit/HMI view)\n"
        << "  R                    Reset Car & Clear Smoke (or Reset Camera)\n"
        << "  Esc                  Exit\n"
        << "======================================================================\n\n";
}

[[nodiscard]] std::filesystem::path ResolveCompiledShaderDirectory(
    const std::filesystem::path& ExecutablePath,
    const std::filesystem::path& RepositoryRoot)
{
    auto ContainsRequiredShaders = [](const std::filesystem::path& Candidate) -> bool
    {
        std::error_code ExistEc {};
        return !Candidate.empty() &&
               std::filesystem::exists(Candidate / "ViewportSwapchainBlit.vert.spv", ExistEc) &&
               std::filesystem::exists(Candidate / "ViewportSwapchainBlit.frag.spv", ExistEc);
    };

    std::vector<std::filesystem::path> SearchDirs {};
    if (!ExecutablePath.empty())
    {
        const std::filesystem::path ExeDir = ExecutablePath.parent_path();
        SearchDirs.push_back(ExeDir / "CompiledShaders");
        SearchDirs.push_back(ExeDir.parent_path() / "CompiledShaders");
        SearchDirs.push_back(ExeDir.parent_path().parent_path() / "CompiledShaders");
    }
    SearchDirs.emplace_back(FRONTIER_COMPILED_SHADER_DIR);
    SearchDirs.push_back(RepositoryRoot / "Build" / "CompiledShaders");
    SearchDirs.push_back(RepositoryRoot / "Binaries" / "Win64" / "CompiledShaders");
    SearchDirs.push_back(std::filesystem::current_path() / "CompiledShaders");
    SearchDirs.push_back(std::filesystem::current_path() / "Build" / "CompiledShaders");

    for (const auto& Candidate : SearchDirs)
    {
        if (ContainsRequiredShaders(Candidate))
        {
            return Candidate;
        }
    }
    return std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
}

[[nodiscard]] std::filesystem::path ResolveProjectRootDirectory(
    const std::filesystem::path& ExplicitRoot,
    const std::filesystem::path& ExecutablePath)
{
    std::error_code ExistEc {};
    auto ContainsProjectMarker = [&](const std::filesystem::path& Dir) -> bool
    {
        return !Dir.empty() && std::filesystem::exists(Dir / "AutoSpace.projectspace", ExistEc);
    };

    if (ContainsProjectMarker(ExplicitRoot))
    {
        return ExplicitRoot;
    }
    if (ContainsProjectMarker(std::filesystem::current_path()))
    {
        return std::filesystem::current_path();
    }
    if (ContainsProjectMarker(std::filesystem::current_path() / "Projects" / "AutoSpace"))
    {
        return std::filesystem::current_path() / "Projects" / "AutoSpace";
    }
    if (!ExecutablePath.empty())
    {
        std::filesystem::path Cursor = ExecutablePath.parent_path();
        for (int Depth = 0; Depth < 6; ++Depth)
        {
            if (ContainsProjectMarker(Cursor))
            {
                return Cursor;
            }
            if (ContainsProjectMarker(Cursor / "Projects" / "AutoSpace"))
            {
                return Cursor / "Projects" / "AutoSpace";
            }
            if (!Cursor.has_parent_path() || Cursor == Cursor.parent_path())
            {
                break;
            }
            Cursor = Cursor.parent_path();
        }
    }
    return ExplicitRoot;
}

} // namespace

int main(int ArgumentCount, char** ArgumentVector)
{
    const std::filesystem::path ExecutablePath =
        (ArgumentCount > 0 && ArgumentVector != nullptr && ArgumentVector[0] != nullptr)
            ? std::filesystem::path(ArgumentVector[0])
            : std::filesystem::path{};

    std::filesystem::path ExplicitProjectRoot = AUTOSPACE_PROJECT_ROOT_DIR;
    std::filesystem::path ScreenshotPath      {};
    bool                  VerifyContracts     = false;
    bool                  DisableGpu          = false;
    uint32_t              RenderCycleLimit    = 0u;
    uint32_t              OverrideWidthPx     = 0u;
    uint32_t              OverrideHeightPx    = 0u;
    uint32_t              DebugChannel        = 0u;
    uint32_t              SdfShadowMarchSteps = 96u;
    uint32_t              InitialCameraPreset = 0u;
    double                InitialSimTimeSec   = -1.0;
    std::string           DriveTestScenario   {};
    std::string           PortalShapeArg      {};
    float                 PortalStraddleM     = -1.0f;
    AutoSpace::StudioLobbyId InitialLobby     = AutoSpace::StudioLobbyId::StudioLobby1Cyclorama;
    Frontier::GeometricRaster::WorldRenderEngine ActiveRenderEngine =
        Frontier::GeometricRaster::WorldRenderEngine::RealTimeGiGtxLowEnd;
    bool                  ExplicitRenderEngine = false;
    AutoSpace::HmiControlPanel HmiPanel   {};
    int32_t               AutoClickHmiBtnIdx  = -1;

    for (int Idx = 1; Idx < ArgumentCount; ++Idx)
    {
        const char* RawArg = ArgumentVector[Idx];
        if (RawArg == nullptr)
        {
            continue;
        }
        const std::string_view Arg(RawArg);
        if (Arg == "--verify")
        {
            VerifyContracts = true;
        }
        else if (Arg == "--no-gpu")
        {
            DisableGpu = true;
        }
        else if (Arg.starts_with("--frames="))
        {
            const long Parsed = std::strtol(RawArg + 9, nullptr, 10);
            if (Parsed > 0L)
            {
                RenderCycleLimit = static_cast<uint32_t>(Parsed);
            }
        }
        else if (Arg.starts_with("--width="))
        {
            const long Parsed = std::strtol(RawArg + 8, nullptr, 10);
            if (Parsed >= 160L && Parsed <= 3840L)
            {
                OverrideWidthPx = static_cast<uint32_t>(Parsed);
            }
        }
        else if (Arg.starts_with("--height="))
        {
            const long Parsed = std::strtol(RawArg + 9, nullptr, 10);
            if (Parsed >= 120L && Parsed <= 2160L)
            {
                OverrideHeightPx = static_cast<uint32_t>(Parsed);
            }
        }
        else if (Arg.starts_with("--screenshot="))
        {
            ScreenshotPath = std::filesystem::path(Arg.substr(13u));
        }
        else if (Arg.starts_with("--project-root="))
        {
            ExplicitProjectRoot = std::filesystem::path(Arg.substr(15u));
        }
        else if (Arg.starts_with("--lobby="))
        {
            (void)AutoSpace::LobbyPolicyEngine::ParseLobbyId(Arg.substr(8u), InitialLobby);
        }
        else if (Arg.starts_with("--debug-channel="))
        {
            const long Parsed = std::strtol(RawArg + 16, nullptr, 10);
            if (Parsed >= 0L && Parsed <= 31L)
            {
                DebugChannel = static_cast<uint32_t>(Parsed);
            }
        }
        else if (Arg.starts_with("--sdf-shadow-steps="))
        {
            const long Parsed = std::strtol(RawArg + 19, nullptr, 10);
            if (Parsed >= 16L && Parsed <= 96L)
            {
                SdfShadowMarchSteps = static_cast<uint32_t>(Parsed);
            }
        }
        else if (Arg.starts_with("--sim-time="))
        {
            const double Parsed = std::strtod(RawArg + 11, nullptr);
            if (std::isfinite(Parsed) && Parsed >= 0.0)
            {
                InitialSimTimeSec = Parsed;
            }
        }
        else if (Arg.starts_with("--camera-preset="))
        {
            const long Parsed = std::strtol(RawArg + 16, nullptr, 10);
            if (Parsed >= 0L && Parsed <= 8L)
            {
                InitialCameraPreset = static_cast<uint32_t>(Parsed);
            }
        }
        else if (Arg.starts_with("--drive-test="))
        {
            DriveTestScenario = std::string(Arg.substr(13u));
        }
        else if (Arg.starts_with("--portal-shape="))
        {
            PortalShapeArg = std::string(Arg.substr(15u));
        }
        else if (Arg.starts_with("--portal-straddle="))
        {
            const double Parsed = std::strtod(RawArg + 18, nullptr);
            if (std::isfinite(Parsed) && Parsed >= 0.0 && Parsed <= 8.0)
            {
                PortalStraddleM = static_cast<float>(Parsed);
            }
        }
        else if (Arg.starts_with("--render-engine="))
        {
            const std::string_view Mode = Arg.substr(16u);
            ExplicitRenderEngine = true;
            if (Mode == "pbr" || Mode == "openpbr")
                ActiveRenderEngine = Frontier::GeometricRaster::WorldRenderEngine::OpenPbrDirect;
            else if (Mode == "sdf" || Mode == "gi" || Mode == "sdfgi")
                ActiveRenderEngine = Frontier::GeometricRaster::WorldRenderEngine::RealTimeGiGtxLowEnd;
            else if (Mode == "matcap")
                ActiveRenderEngine = Frontier::GeometricRaster::WorldRenderEngine::MatCap;
            else if (Mode == "pathtrace" || Mode == "pt")
                ActiveRenderEngine = Frontier::GeometricRaster::WorldRenderEngine::PathTracerGameRealtime;
        }
        else if (Arg.starts_with("--hmi-cam="))
        {
            const std::string_view Cam = Arg.substr(10u);
            if (Cam == "rear")       HmiPanel.FeedMode = AutoSpace::HmiCameraFeedMode::RearBackupCam;
            else if (Cam == "chase") HmiPanel.FeedMode = AutoSpace::HmiCameraFeedMode::ChaseFollowCam;
            else if (Cam == "bumper")HmiPanel.FeedMode = AutoSpace::HmiCameraFeedMode::FrontBumperCam;
            else if (Cam == "drift") HmiPanel.FeedMode = AutoSpace::HmiCameraFeedMode::DriftWheelCam;
        }
        else if (Arg.starts_with("--hmi-engine="))
        {
            const std::string_view Eng = Arg.substr(13u);
            if (Eng == "sync")       HmiPanel.EngineMode = AutoSpace::HmiEngineSyncMode::SyncToMain;
            else if (Eng == "pbr")   HmiPanel.EngineMode = AutoSpace::HmiEngineSyncMode::ForceOpenPbr;
            else if (Eng == "sdf")   HmiPanel.EngineMode = AutoSpace::HmiEngineSyncMode::ForceSdfGi;
            else if (Eng == "matcap")HmiPanel.EngineMode = AutoSpace::HmiEngineSyncMode::ForceMatCap;
            else if (Eng == "pt")    HmiPanel.EngineMode = AutoSpace::HmiEngineSyncMode::ForcePathTracer;
        }
        else if (Arg.starts_with("--hud="))
        {
            HmiPanel.OnScreenHudEnabled = (Arg.substr(6u) != "off");
        }
        else if (Arg.starts_with("--glass-hud="))
        {
            HmiPanel.WindshieldHudEnabled = (Arg.substr(12u) != "off");
        }
        else if (Arg.starts_with("--hmi-click="))
        {
            AutoClickHmiBtnIdx = static_cast<int32_t>(std::strtol(RawArg + 12, nullptr, 10));
        }
    }

    const std::filesystem::path ProjectRoot = ResolveProjectRootDirectory(ExplicitProjectRoot, ExecutablePath);

    AutoSpace::AutoSpaceLifecycle Session {};
    std::string                   Diagnostic {};
    if (!Session.Initialize(ProjectRoot, /*HeadlessWindow=*/false, Diagnostic, InitialLobby))
    {
        std::cerr << "[AutoSpace] Initialization failed: " << Diagnostic << "\n";
        return 1;
    }
    if (InitialSimTimeSec >= 0.0)
    {
        Session.AccessLobbyLifecycle().ApplySimulationTime(InitialSimTimeSec);
    }

    if (VerifyContracts && !Session.VerifyProjectContracts(Diagnostic))
    {
        std::cerr << "[AutoSpace] Project verification failed: " << Diagnostic << "\n";
        Session.Finalize();
        return 2;
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Standalone Desktop Game Mode (native OS window + Vulkan GPU rendering + swapchain presentation)
    // -----------------------------------------------------------------------------------------------------------------
    auto& RuntimeLoop = Session.AccessRuntimeLoop();
    RuntimeLoop.AccessInput().ConfigureEditorChannelEnabled(true);

    Frontier::Windowing::DesktopPlatformWindow* DesktopWindow = nullptr;
#if !defined(__ANDROID__)
    DesktopWindow = RuntimeLoop.AccessDesktopWindow();
#endif

    const auto InitialExtent = RuntimeLoop.InspectReading().WindowExtent;
    uint32_t WindowWidthPx  = (OverrideWidthPx > 0u)
        ? OverrideWidthPx
        : std::max(320u, InitialExtent.SurfacePixelWidth > 0u ? InitialExtent.SurfacePixelWidth : 1280u);
    uint32_t WindowHeightPx = (OverrideHeightPx > 0u)
        ? OverrideHeightPx
        : std::max(240u, InitialExtent.SurfacePixelHeight > 0u ? InitialExtent.SurfacePixelHeight : 720u);

    AutoSpace::StudioLobbyId ActiveLobby = InitialLobby;
    if (DesktopWindow != nullptr)
    {
        DesktopWindow->ApplyTitle(ConstructWindowTitle(ActiveLobby));
    }

    const bool RunningOnGlfwNull =
        (DesktopWindow == nullptr) ||
        (DesktopWindow->PlatformCategory() == Frontier::Windowing::WindowPlatformCategory::GlfwNull) ||
        (DesktopWindow->PlatformCategory() == Frontier::Windowing::WindowPlatformCategory::HeadlessStub);
    if (RunningOnGlfwNull && RenderCycleLimit == 0u)
    {
        RenderCycleLimit = 3u;
    }

    const std::filesystem::path RepositoryRoot = Session.InspectRepositoryRoot().empty()
        ? std::filesystem::path(FRONTIER_REPO_ROOT_DIR)
        : Session.InspectRepositoryRoot();
    const std::filesystem::path ShaderDirectory = ResolveCompiledShaderDirectory(ExecutablePath, RepositoryRoot);

    Frontier::GeometricRaster::VulkanInterchange          VulkanDevice {};
    Frontier::GeometricRaster::VulkanWorldRenderSequence  GpuRenderer {};
    Frontier::GeometricRaster::ViewportSwapchainPresenter SwapchainPresenter {};
    Frontier::GeometricRaster::TieredWorldRenderSequence  CpuRenderer {};

    bool GpuWorldReady  = false;
    bool PresenterReady = false;
    if (!DisableGpu)
    {
        auto StartupEventPump = [&]()
        {
            if (DesktopWindow != nullptr)
            {
                DesktopWindow->PollOperatingEvents(RuntimeLoop.AccessInput());
            }
        };

        std::string VkDiag {};
        if (VulkanDevice.InitializeInterchange(RepositoryRoot, &VkDiag))
        {
            StartupEventPump();
            GpuWorldReady = GpuRenderer.InitializeSequence(VulkanDevice, ShaderDirectory, StartupEventPump);
            if (!GpuWorldReady)
            {
                std::cerr << "[AutoSpace] VulkanWorldRenderSequence unavailable; falling back to CPU renderer.\n";
            }
            StartupEventPump();

            std::string PresDiag {};
            const bool EnableVSync = RuntimeLoop.InspectBootConfiguration().RuntimeConfiguration.EnableVerticalSync;
            PresenterReady = SwapchainPresenter.InitializePresenter(
                &VulkanDevice,
                ShaderDirectory,
                DesktopWindow,
                WindowWidthPx,
                WindowHeightPx,
                EnableVSync,
                &PresDiag);
            if (!PresenterReady)
            {
                std::cerr << "[AutoSpace] ViewportSwapchainPresenter unavailable (" << PresDiag << ").\n";
            }
            StartupEventPump();
        }
        else
        {
            std::cerr << "[AutoSpace] VulkanInterchange unavailable (" << VkDiag
                      << "); falling back to CPU renderer.\n";
        }
    }

    OrbitCameraRig CameraRig {};
    uint32_t       CameraPresetIndex       = InitialCameraPreset;
    float          SmoothedFollowYawRad    = 0.0f;
    float          ChaseOrbitYawOffsetRad  = 0.0f;
    float          ChaseOrbitElevOffsetRad = 0.0f;
    float          ChaseDistanceScale      = 1.0f;
    float          SmoothedSteerInput      = 0.0f;
    uint32_t       AssistModeIndex         = 0u; // 0 = Sport, 1 = Drift (TCS/ESC Off), 2 = Safe
    double         TitleRefreshAccumSec    = 0.0;

    auto SeedCameraFromActivePreset = [&]()
    {
        const auto& Presets = Session.InspectLobbyLifecycle().InspectCameraPresets();
        if (!Presets.empty())
        {
            CameraPresetIndex %= static_cast<uint32_t>(Presets.size());
            CameraRig.SeedFromCamera(Presets[CameraPresetIndex].Camera);
        }
        ChaseOrbitYawOffsetRad  = 0.0f;
        ChaseOrbitElevOffsetRad = 0.0f;
        ChaseDistanceScale      = 1.0f;
        Frontier::Mathematics::Axis3D HeroPos {};
        float HeroYawDeg = 0.0f;
        Session.InspectLobbyLifecycle().QueryHeroVehicleWorldPose(HeroPos, HeroYawDeg);
        SmoothedFollowYawRad = HeroYawDeg * Frontier::Constants::DegToRadFloat;
    };
    SeedCameraFromActivePreset();
    PrintDrivingControlsLegend();

    // Optional deterministic driving & smoke verification scenario (for automated proof renders)
    if (!DriveTestScenario.empty() && Session.InspectLobbyLifecycle().QueryHeroVehiclePhysicsActive())
    {
        auto& Lobby = Session.AccessLobbyLifecycle();
        Frontier::SimulationSpace::PhysicsSpace::VehicleControlDirective AssistCmd {};
        AssistCmd.Category    = Frontier::SimulationSpace::PhysicsSpace::VehicleDirectiveCategory::AssignDriverAssists;
        AssistCmd.AbsEnabled  = true;
        AssistCmd.TcsCategory = Frontier::SimulationSpace::PhysicsSpace::TractionAssistCategory::Off;
        AssistCmd.EscEnabled  = false;
        (void)Lobby.SubmitHeroVehicleDirective(AssistCmd);
        AssistModeIndex = 1u;

        constexpr double TestDt = 1.0 / 60.0;
        if (DriveTestScenario == "burnout")
        {
            // Donut / line-lock burnout: short launch + hard steer + full throttle spin for dense billowing smoke
            Frontier::SimulationSpace::PhysicsSpace::VehicleControlDirective Cmd {};
            Cmd.Category           = Frontier::SimulationSpace::PhysicsSpace::VehicleDirectiveCategory::DriverInput;
            Cmd.ThrottleNormalized = 1.0f;
            for (int Step = 0; Step < 144; ++Step)
            {
                Cmd.ThrottleNormalized  = 1.0f;
                Cmd.BrakeNormalized     = (Step < 30) ? 0.0f : 0.05f;
                Cmd.SteeringNormalized  = (Step < 24) ? 0.0f : 0.92f;
                Cmd.HandbrakeNormalized = (Step >= 24 && Step < 40) ? 0.85f : 0.0f;
                (void)Lobby.SubmitHeroVehicleDirective(Cmd);
                Lobby.TickSimulation(TestDt);
            }
        }
        else if (DriveTestScenario == "drift")
        {
            Frontier::SimulationSpace::PhysicsSpace::VehicleControlDirective Cmd {};
            Cmd.Category           = Frontier::SimulationSpace::PhysicsSpace::VehicleDirectiveCategory::DriverInput;
            Cmd.ThrottleNormalized = 1.0f;
            (void)Lobby.SubmitHeroVehicleDirective(Cmd);
            for (int Step = 0; Step < 84; ++Step)
            {
                Lobby.TickSimulation(TestDt);
            }
            // Initiate drift with steering + momentary handbrake then sustained throttle oversteer
            for (int Step = 0; Step < 96; ++Step)
            {
                Cmd.ThrottleNormalized  = 1.0f;
                Cmd.BrakeNormalized     = 0.0f;
                Cmd.SteeringNormalized  = (Step < 66) ? 0.85f : -0.30f;
                Cmd.HandbrakeNormalized = (Step < 22) ? 0.90f : 0.0f;
                (void)Lobby.SubmitHeroVehicleDirective(Cmd);
                Lobby.TickSimulation(TestDt);
            }
        }
        else if (DriveTestScenario == "launch")
        {
            Frontier::SimulationSpace::PhysicsSpace::VehicleControlDirective Cmd {};
            Cmd.Category           = Frontier::SimulationSpace::PhysicsSpace::VehicleDirectiveCategory::DriverInput;
            Cmd.ThrottleNormalized = 1.0f;
            (void)Lobby.SubmitHeroVehicleDirective(Cmd);
            for (int Step = 0; Step < 120; ++Step)
            {
                Lobby.TickSimulation(TestDt);
            }
        }

        Frontier::Mathematics::Axis3D HeroPos {};
        float HeroYawDeg = 0.0f;
        Lobby.QueryHeroVehicleWorldPose(HeroPos, HeroYawDeg);
        SmoothedFollowYawRad = HeroYawDeg * Frontier::Constants::DegToRadFloat;
        const auto Reading   = Lobby.AcquireHeroVehicleReading();
        const auto SmokeTel  = Lobby.InspectTireSmokeTelemetry();
        std::cout
            << "[AutoSpace DriveTest=" << DriveTestScenario << "] "
            << "Pos=(" << HeroPos.X << ", " << HeroPos.Y << ", " << HeroPos.Z << ") m | "
            << "Yaw=" << HeroYawDeg << " deg | "
            << "Speed=" << (Reading.Valid() ? Reading->SpeedKph : 0.0f) << " km/h | "
            << "Gear=" << (Reading.Valid() ? Reading->ActiveGear : 0) << " | "
            << "RPM=" << (Reading.Valid() ? Reading->EngineRpm : 0.0f) << " | "
            << "RearSlipRL=" << (Reading.Valid() ? Reading->Corners[2u].LongitudinalSlip : 0.0f) << " | "
            << "RearTempC=" << (Reading.Valid() ? (Reading->Corners[2u].TreadTempKelvin - 273.15f) : 20.0f) << " | "
            << "SmokeVoxels=" << SmokeTel.ActiveSmokeVoxels << " | "
            << "SmokeMass=" << SmokeTel.TotalSmokeMass << " | "
            << "SdfLeaks=" << SmokeTel.SdfInteriorLeakVoxels << " | "
            << "LocalFogs=" << Lobby.InspectWorldScene().Atmosphere.Fog.LocalFogs.size()
            << "\n";
    }

    bool     SceneGpuDirty       = true;
    uint32_t PresentedCycles     = 0u;
    uint32_t FailedPresentRuns   = 0u;
    uint32_t PreHmiCameraPreset  = 2u;
    bool     AutomaticShiftUi    = true;
    float    LastThrottle01      = 0.0f;
    float    LastBrake01         = 0.0f;
    float    LastHandbrake01     = 0.0f;
    std::vector<uint8_t> LastCpuRgba8 {};
    uint32_t             LastCpuWidthPx  = 0u;
    uint32_t             LastCpuHeightPx = 0u;

    AutoSpace::VehicleCockpitInstrumentation CockpitUi {};
    const AutoSpace::VehicleCockpitAnchorLayout CockpitLayout = AutoSpace::QueryControlVehicleCockpitLayout();
    std::vector<uint8_t> HmiFeedRgba8 {};
    uint32_t             HmiFeedW = 0u;
    uint32_t             HmiFeedH = 0u;

    auto TriggerHmiButtonCommand = [&](AutoSpace::HmiButtonId Btn)
    {
        if (Btn == AutoSpace::HmiButtonId::None) return;
        HmiPanel.LastClickedButton = Btn;
        HmiPanel.ClickFlashSeconds = 0.25f;
        switch (Btn)
        {
            case AutoSpace::HmiButtonId::CycleFeedCam:
                HmiPanel.FeedMode = static_cast<AutoSpace::HmiCameraFeedMode>(
                    (static_cast<uint8_t>(HmiPanel.FeedMode) + 1u) %
                    static_cast<uint8_t>(AutoSpace::HmiCameraFeedMode::Count));
                break;
            case AutoSpace::HmiButtonId::CycleHmiEngine:
                HmiPanel.EngineMode = static_cast<AutoSpace::HmiEngineSyncMode>(
                    (static_cast<uint8_t>(HmiPanel.EngineMode) + 1u) %
                    static_cast<uint8_t>(AutoSpace::HmiEngineSyncMode::Count));
                break;
            case AutoSpace::HmiButtonId::ToggleWindshieldHud:
                HmiPanel.WindshieldHudEnabled = !HmiPanel.WindshieldHudEnabled;
                break;
            case AutoSpace::HmiButtonId::ToggleStabilityAssist:
            {
                AssistModeIndex = (AssistModeIndex == 1u) ? 0u : 1u;
                Frontier::SimulationSpace::PhysicsSpace::VehicleControlDirective AssistCmd {};
                AssistCmd.Category    = Frontier::SimulationSpace::PhysicsSpace::VehicleDirectiveCategory::AssignDriverAssists;
                AssistCmd.AbsEnabled  = true;
                AssistCmd.TcsCategory = (AssistModeIndex == 1u)
                    ? Frontier::SimulationSpace::PhysicsSpace::TractionAssistCategory::Off
                    : Frontier::SimulationSpace::PhysicsSpace::TractionAssistCategory::Sport;
                AssistCmd.EscEnabled  = (AssistModeIndex != 1u);
                (void)Session.AccessLobbyLifecycle().SubmitHeroVehicleDirective(AssistCmd);
                break;
            }
            case AutoSpace::HmiButtonId::ToggleTransmissionMode:
                AutomaticShiftUi = !AutomaticShiftUi;
                break;
            case AutoSpace::HmiButtonId::ToggleSmokePlumes:
                HmiPanel.TireSmokeEnabled = !HmiPanel.TireSmokeEnabled;
                Session.AccessLobbyLifecycle().SetTireSmokeEnabled(HmiPanel.TireSmokeEnabled);
                break;
            default:
                break;
        }
    };
    if (AutoClickHmiBtnIdx >= 1 && AutoClickHmiBtnIdx < static_cast<int32_t>(AutoSpace::HmiButtonId::Count))
    {
        TriggerHmiButtonCommand(static_cast<AutoSpace::HmiButtonId>(AutoClickHmiBtnIdx));
    }

    if (!PortalShapeArg.empty())
    {
        using Frontier::World::PortalApertureShapeCategory;
        if (PortalShapeArg == "rect" || PortalShapeArg == "rectangle" || PortalShapeArg == "quad")
        {
            Session.AccessLobbyLifecycle().SelectActivePortalShape(PortalApertureShapeCategory::Rectangle);
        }
        else if (PortalShapeArg == "circle" || PortalShapeArg == "disc")
        {
            Session.AccessLobbyLifecycle().SelectActivePortalShape(PortalApertureShapeCategory::Circle);
        }
        else if (PortalShapeArg == "hex" || PortalShapeArg == "hexagon")
        {
            Session.AccessLobbyLifecycle().SelectActivePortalShape(PortalApertureShapeCategory::Hexagon);
        }
        else if (PortalShapeArg == "poly" || PortalShapeArg == "octagon")
        {
            Session.AccessLobbyLifecycle().SelectActivePortalShape(PortalApertureShapeCategory::RegularPolygon);
        }
        else if (PortalShapeArg == "cutout" || PortalShapeArg == "custom" || PortalShapeArg == "star")
        {
            Session.AccessLobbyLifecycle().SelectActivePortalShape(PortalApertureShapeCategory::CustomCutout);
        }
    }
    if (PortalStraddleM >= 0.0f)
    {
        Session.AccessLobbyLifecycle().PlaceHeroVehicleForPortalStraddleTest(PortalStraddleM);
    }

    auto PreviousClock = std::chrono::steady_clock::now();

    while (RuntimeLoop.QueryRunning())
    {
        const auto   NowClock = std::chrono::steady_clock::now();
        const double WallDt   = std::clamp(
            std::chrono::duration<double>(NowClock - PreviousClock).count(),
            1.0 / 240.0,
            0.05);
        PreviousClock = NowClock;

        if (!Session.ExecuteInteractiveTick(WallDt))
        {
            break;
        }

        const auto& InputReading = RuntimeLoop.InspectInput().InspectEditorSnapshot();

        // Exit on Escape
        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::Escape))
        {
            RuntimeLoop.RequestTermination();
            break;
        }

        // Studio Lobby hot-switching (keys 1 / 2 / 3)
        auto TrySwitchLobby = [&](AutoSpace::StudioLobbyId RequestedLobby)
        {
            if (RequestedLobby == ActiveLobby)
            {
                return;
            }
            std::string SwitchDiag {};
            if (Session.SelectStudioLobby(RequestedLobby, SwitchDiag))
            {
                ActiveLobby       = RequestedLobby;
                CameraPresetIndex = 0u;
                SeedCameraFromActivePreset();
                SceneGpuDirty    = true;
                if (DesktopWindow != nullptr)
                {
                    DesktopWindow->ApplyTitle(ConstructWindowTitle(ActiveLobby));
                }
            }
        };

        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::Digit1))
        {
            TrySwitchLobby(AutoSpace::StudioLobbyId::StudioLobby1Cyclorama);
        }
        else if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::Digit2))
        {
            TrySwitchLobby(AutoSpace::StudioLobbyId::StudioLobby2ForestStage);
        }
        else if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::Digit3))
        {
            TrySwitchLobby(AutoSpace::StudioLobbyId::StudioLobby3RacingGarage);
        }
        else if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::Digit4))
        {
            TrySwitchLobby(AutoSpace::StudioLobbyId::StudioLobby4TrainingGround);
        }

        // Camera preset cycling (C / V / Tab), HMI Focus quick-toggle (H), HUD toggles (U / G), Render Engine (F5..F8)
        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::C) ||
            InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::V) ||
            InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::Tab))
        {
            const auto& Presets = Session.InspectLobbyLifecycle().InspectCameraPresets();
            if (!Presets.empty())
            {
                CameraPresetIndex = (CameraPresetIndex + 1u) % static_cast<uint32_t>(Presets.size());
                SeedCameraFromActivePreset();
            }
        }
        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::H) &&
            ActiveLobby == AutoSpace::StudioLobbyId::StudioLobby4TrainingGround)
        {
            if (CameraPresetIndex == 3u)
            {
                CameraPresetIndex = PreHmiCameraPreset;
            }
            else
            {
                PreHmiCameraPreset = CameraPresetIndex;
                CameraPresetIndex  = 3u;
            }
            SeedCameraFromActivePreset();
        }
        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::U))
        {
            HmiPanel.OnScreenHudEnabled = !HmiPanel.OnScreenHudEnabled;
        }
        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::G))
        {
            HmiPanel.WindshieldHudEnabled = !HmiPanel.WindshieldHudEnabled;
        }
        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::F5))
        {
            ExplicitRenderEngine = true;
            ActiveRenderEngine   = Frontier::GeometricRaster::WorldRenderEngine::OpenPbrDirect;
        }
        else if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::F6))
        {
            ExplicitRenderEngine = true;
            ActiveRenderEngine   = Frontier::GeometricRaster::WorldRenderEngine::RealTimeGiGtxLowEnd;
        }
        else if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::F7))
        {
            ExplicitRenderEngine = true;
            ActiveRenderEngine   = Frontier::GeometricRaster::WorldRenderEngine::MatCap;
        }
        else if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::F8))
        {
            ExplicitRenderEngine = true;
            ActiveRenderEngine   = Frontier::GeometricRaster::WorldRenderEngine::PathTracerGameRealtime;
        }
        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::R))
        {
            if (Session.InspectLobbyLifecycle().QueryHeroVehiclePhysicsActive())
            {
                Session.AccessLobbyLifecycle().ResetHeroVehicleToSpawn();
                SmoothedSteerInput = 0.0f;
            }
            SeedCameraFromActivePreset();
        }

        // Traction / Drift assist toggle (T key: Sport -> Drift/Off -> Safe)
        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::T) &&
            Session.InspectLobbyLifecycle().QueryHeroVehiclePhysicsActive())
        {
            AssistModeIndex = (AssistModeIndex + 1u) % 3u;
            Frontier::SimulationSpace::PhysicsSpace::VehicleControlDirective AssistCmd {};
            AssistCmd.Category = Frontier::SimulationSpace::PhysicsSpace::VehicleDirectiveCategory::AssignDriverAssists;
            if (AssistModeIndex == 0u)
            {
                AssistCmd.AbsEnabled  = true;
                AssistCmd.TcsCategory = Frontier::SimulationSpace::PhysicsSpace::TractionAssistCategory::Sport;
                AssistCmd.EscEnabled  = true;
            }
            else if (AssistModeIndex == 1u)
            {
                AssistCmd.AbsEnabled  = true;
                AssistCmd.TcsCategory = Frontier::SimulationSpace::PhysicsSpace::TractionAssistCategory::Off;
                AssistCmd.EscEnabled  = false;
            }
            else
            {
                AssistCmd.AbsEnabled  = true;
                AssistCmd.TcsCategory = Frontier::SimulationSpace::PhysicsSpace::TractionAssistCategory::Track;
                AssistCmd.EscEnabled  = true;
            }
            (void)Session.AccessLobbyLifecycle().SubmitHeroVehicleDirective(AssistCmd);
        }

        // Portal 2D shape cycling (P key: Hexagon -> Circle -> Rectangle -> Concave Cutout -> Octagon)
        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::P))
        {
            Session.AccessLobbyLifecycle().CycleActivePortalShape();
        }
        // Portal Exit World B observer camera toggle (O key)
        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::O) &&
            ActiveLobby == AutoSpace::StudioLobbyId::StudioLobby4TrainingGround)
        {
            CameraPresetIndex = (CameraPresetIndex == 5u) ? 0u : 5u;
            SeedCameraFromActivePreset();
        }

        // Smooth mouse orbit & wheel zoom (in Cockpit / HMI Focus modes, Left-Click interacts with HMI buttons; Right-Drag glances)
        const bool DrivingCamActive =
            Session.InspectLobbyLifecycle().QueryHeroVehiclePhysicsActive() &&
            (ActiveLobby == AutoSpace::StudioLobbyId::StudioLobby4TrainingGround);
        const uint32_t DrivingCamMode = DrivingCamActive ? (CameraPresetIndex % 6u) : 0u;
        const bool     InteriorViewActive = DrivingCamActive && (DrivingCamMode == 2u || DrivingCamMode == 3u);

        const bool OrbitDragDown = InteriorViewActive
            ? InputReading.QueryButtonDown(1u)
            : (InputReading.QueryButtonDown(0u) || InputReading.QueryButtonDown(1u));
        if (OrbitDragDown)
        {
            if (DrivingCamActive && DrivingCamMode < 4u)
            {
                ChaseOrbitYawOffsetRad = std::clamp(
                    ChaseOrbitYawOffsetRad - InputReading.TickDeltaX * 0.0055f,
                    InteriorViewActive ? -0.95f : -3.14f,
                    InteriorViewActive ? 0.95f : 3.14f);
                ChaseOrbitElevOffsetRad = std::clamp(
                    ChaseOrbitElevOffsetRad + InputReading.TickDeltaY * 0.0045f,
                    InteriorViewActive ? -0.35f : -0.25f,
                    InteriorViewActive ? 0.35f : 0.95f);
            }
            else
            {
                CameraRig.AzimuthRadians -= InputReading.TickDeltaX * 0.0055f;
                CameraRig.ElevationRadians =
                    std::clamp(CameraRig.ElevationRadians + InputReading.TickDeltaY * 0.0045f, 0.04f, 1.38f);
            }
        }
        if (std::abs(InputReading.ScrollDeltaY) > 0.001f)
        {
            if (DrivingCamActive && DrivingCamMode < 2u)
            {
                ChaseDistanceScale =
                    std::clamp(ChaseDistanceScale * std::pow(0.90f, InputReading.ScrollDeltaY), 0.55f, 2.5f);
            }
            else if (!InteriorViewActive)
            {
                CameraRig.DistanceMeters =
                    std::clamp(CameraRig.DistanceMeters * std::pow(0.90f, InputReading.ScrollDeltaY), 1.8f, 32.0f);
            }
        }

        // Interactive Hero vehicle driving controls (skip overwriting when deterministic --drive-test is active)
        if (DriveTestScenario.empty() && Session.InspectLobbyLifecycle().QueryHeroVehiclePhysicsActive())
        {
            Frontier::SimulationSpace::PhysicsSpace::VehicleControlDirective DriveDir {};
            const bool ForwardHeld =
                InputReading.QueryKeyHeld(Frontier::Input::InputLogicalKey::W) ||
                InputReading.QueryKeyHeld(Frontier::Input::InputLogicalKey::UpArrow);
            const bool BrakeHeld =
                InputReading.QueryKeyHeld(Frontier::Input::InputLogicalKey::S) ||
                InputReading.QueryKeyHeld(Frontier::Input::InputLogicalKey::DownArrow);
            const bool LeftHeld =
                InputReading.QueryKeyHeld(Frontier::Input::InputLogicalKey::A) ||
                InputReading.QueryKeyHeld(Frontier::Input::InputLogicalKey::LeftArrow);
            const bool RightHeld =
                InputReading.QueryKeyHeld(Frontier::Input::InputLogicalKey::D) ||
                InputReading.QueryKeyHeld(Frontier::Input::InputLogicalKey::RightArrow);
            const bool HandbrakeHeld =
                InputReading.QueryKeyHeld(Frontier::Input::InputLogicalKey::Space);
            const bool BurnoutHeld =
                InputReading.QueryKeyHeld(Frontier::Input::InputLogicalKey::B);
            const bool ShiftUpPressed =
                InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::E) ||
                InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::LeftShift);
            const bool ShiftDownPressed =
                InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::Q) ||
                InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::LeftControl);

            // Smooth progressive keyboard steering with fast center return
            const float TargetSteer = (RightHeld ? 1.0f : 0.0f) - (LeftHeld ? 1.0f : 0.0f);
            const float SteerRate   = (std::fabs(TargetSteer) > 0.01f) ? 4.8f : 6.8f;
            const float MaxStep     = SteerRate * static_cast<float>(WallDt);
            const float DeltaSteer  = TargetSteer - SmoothedSteerInput;
            if (std::fabs(DeltaSteer) <= MaxStep)
            {
                SmoothedSteerInput = TargetSteer;
            }
            else
            {
                SmoothedSteerInput += (DeltaSteer > 0.0f ? MaxStep : -MaxStep);
            }

            // Auto-reverse when holding Brake while nearly stationary
            float CurForwardMps = 0.0f;
            if (const auto Reading = Session.InspectLobbyLifecycle().AcquireHeroVehicleReading(); Reading.Valid())
            {
                CurForwardMps = Reading->ForwardSpeedMps;
            }
            const bool EngageReverse = BrakeHeld && !ForwardHeld && !BurnoutHeld && (CurForwardMps < 0.75f);

            if (BurnoutHeld)
            {
                DriveDir.ThrottleNormalized  = 1.0f;
                DriveDir.BrakeNormalized     = 0.06f; // Light front brake hold + full rear throttle smoke spin
                DriveDir.HandbrakeNormalized = 0.0f;
            }
            else if (EngageReverse)
            {
                DriveDir.ReverseRequested    = true;
                DriveDir.ThrottleNormalized  = 0.78f;
                DriveDir.BrakeNormalized     = 0.0f;
                DriveDir.HandbrakeNormalized = HandbrakeHeld ? 1.0f : 0.0f;
            }
            else
            {
                DriveDir.ThrottleNormalized  = ForwardHeld ? 1.0f : 0.0f;
                DriveDir.BrakeNormalized     = BrakeHeld ? 1.0f : 0.0f;
                DriveDir.HandbrakeNormalized = HandbrakeHeld ? 1.0f : 0.0f;
            }
            DriveDir.SteeringNormalized = SmoothedSteerInput;
            DriveDir.ShiftUpRequested   = ShiftUpPressed;
            DriveDir.ShiftDownRequested = ShiftDownPressed;
            LastThrottle01              = DriveDir.ThrottleNormalized;
            LastBrake01                 = DriveDir.BrakeNormalized;
            LastHandbrake01             = DriveDir.HandbrakeNormalized;
            (void)Session.AccessLobbyLifecycle().SubmitHeroVehicleDirective(DriveDir);
        }
        else if (!DriveTestScenario.empty())
        {
            LastThrottle01  = 1.0f;
            LastBrake01     = (DriveTestScenario == "burnout") ? 0.42f : 0.0f;
            LastHandbrake01 = 0.0f;
        }

        // Update live title-bar telemetry on Level 4 (~4 Hz) so speed, gear, RPM & tire smoke are visible immediately
        if (DrivingCamActive && DesktopWindow != nullptr)
        {
            TitleRefreshAccumSec += WallDt;
            if (TitleRefreshAccumSec >= 0.25)
            {
                TitleRefreshAccumSec = 0.0;
                const auto Reading   = Session.InspectLobbyLifecycle().AcquireHeroVehicleReading();
                const auto SmokeTel  = Session.InspectLobbyLifecycle().InspectTireSmokeTelemetry();
                static constexpr const char* kCamLabels[6]    = { "Chase", "Drift Quarter", "Interior Cockpit", "HMI Focus", "Orbit", "World B Portal Exit" };
                static constexpr const char* kAssistLabels[3] = { "Sport", "Drift/Off", "Safe" };
                char TitleBuf[256] {};
                const float Kph = Reading.Valid() ? std::fabs(Reading->SpeedKph) : 0.0f;
                const float Rpm = Reading.Valid() ? Reading->EngineRpm : 700.0f;
                const int   Gr  = Reading.Valid() ? Reading->ActiveGear : 1;
                char GearLabel[16] {};
                if (Gr < 0)      std::snprintf(GearLabel, sizeof(GearLabel), "R");
                else if (Gr == 0) std::snprintf(GearLabel, sizeof(GearLabel), "N");
                else             std::snprintf(GearLabel, sizeof(GearLabel), "G%d", Gr);
                std::snprintf(
                    TitleBuf,
                    sizeof(TitleBuf),
                    "AutoSpace · Training Ground | %3.0f km/h | %s | %4.0f RPM | Smoke: %u vox | [%s] [TCS:%s]  (V:Cam H:HMI U:HUD)",
                    Kph,
                    GearLabel,
                    Rpm,
                    SmokeTel.ActiveSmokeVoxels,
                    kCamLabels[DrivingCamMode],
                    kAssistLabels[AssistModeIndex % 3u]);
                DesktopWindow->ApplyTitle(TitleBuf);
            }
        }

        const auto LiveExtent = RuntimeLoop.InspectReading().WindowExtent;
        if (LiveExtent.Minimized && !RunningOnGlfwNull)
        {
            continue;
        }
        if (OverrideWidthPx == 0u && LiveExtent.SurfacePixelWidth >= 64u)
        {
            WindowWidthPx = LiveExtent.SurfacePixelWidth;
        }
        if (OverrideHeightPx == 0u && LiveExtent.SurfacePixelHeight >= 64u)
        {
            WindowHeightPx = LiveExtent.SurfacePixelHeight;
        }

        // Keep software Vulkan crisp & responsive when running without explicit resolution override
        uint32_t RenderWidthPx  = WindowWidthPx;
        uint32_t RenderHeightPx = WindowHeightPx;
        if (GpuWorldReady && !GpuRenderer.QueryHardwareGpuActive() &&
            OverrideWidthPx == 0u && OverrideHeightPx == 0u)
        {
            const float Aspect = static_cast<float>(WindowWidthPx) / static_cast<float>(std::max(1u, WindowHeightPx));
            RenderWidthPx      = std::min(WindowWidthPx, ScreenshotPath.empty() ? 320u : 800u);
            RenderHeightPx     = std::max(120u, static_cast<uint32_t>(std::round(static_cast<float>(RenderWidthPx) / Aspect)));
        }

        auto& Scene = Session.AccessLobbyLifecycle().AccessWorldScene();
        Frontier::Mathematics::Axis3D HeroPosWorld {};
        float                         HeroYawWorldDeg = 0.0f;
        Session.InspectLobbyLifecycle().QueryHeroVehicleWorldPose(HeroPosWorld, HeroYawWorldDeg);

        Frontier::GeometricRaster::PerspectiveCameraBasis ActiveCamera {};
        if (DrivingCamActive)
        {
            const float TargetYawRad = HeroYawWorldDeg * Frontier::Constants::DegToRadFloat;
            float YawDiff = TargetYawRad - SmoothedFollowYawRad;
            while (YawDiff >  Frontier::Constants::PiFloat) YawDiff -= Frontier::Constants::TauFloat;
            while (YawDiff < -Frontier::Constants::PiFloat) YawDiff += Frontier::Constants::TauFloat;
            const float FollowAlpha = std::clamp(static_cast<float>(WallDt) * 6.5f, 0.0f, 1.0f);
            SmoothedFollowYawRad += YawDiff * FollowAlpha;

            auto TransformVehicleLocal = [&](const Frontier::Mathematics::Axis3D& Local) -> Frontier::Mathematics::Axis3D
            {
                const float C = std::cos(TargetYawRad);
                const float S = std::sin(TargetYawRad);
                return {
                    HeroPosWorld.X + Local.X * C - Local.Y * S,
                    HeroPosWorld.Y + Local.X * S + Local.Y * C,
                    HeroPosWorld.Z + Local.Z
                };
            };

            if (DrivingCamMode == 5u)
            {
                // World B Portal Exit Observer Cam (authored static/orbit vantage facing World B's sunset exit portal!)
                ActiveCamera = CameraRig.ConstructCamera(RenderWidthPx, RenderHeightPx);
            }
            else if (DrivingCamMode == 4u)
            {
                // Orbit Showcase Cam centered around live moving vehicle
                CameraRig.TargetWorld = { HeroPosWorld.X, HeroPosWorld.Y, HeroPosWorld.Z + 0.78f };
                ActiveCamera          = CameraRig.ConstructCamera(RenderWidthPx, RenderHeightPx);
            }
            else if (DrivingCamMode == 2u || DrivingCamMode == 3u)
            {
                // Mode 2 = Interior Driver Cockpit Cam, Mode 3 = HMI Focus Cam (H)
                const bool HmiFocus = (DrivingCamMode == 3u);
                const auto NominalEyeL  = HmiFocus ? CockpitLayout.HmiFocusEyeLocal : CockpitLayout.DriverEyeLocal;
                const auto NominalLookL = HmiFocus ? CockpitLayout.HmiFocusLookLocal : CockpitLayout.DriverLookLocal;
                const auto RelLookL  = NominalLookL - NominalEyeL;
                const float DistL    = std::max(0.25f, std::sqrt(RelLookL.X * RelLookL.X + RelLookL.Y * RelLookL.Y + RelLookL.Z * RelLookL.Z));
                const float NominalYawL = std::atan2(RelLookL.Y, RelLookL.X) + ChaseOrbitYawOffsetRad;
                const float NominalPitchL = std::clamp(std::asin(std::clamp(RelLookL.Z / DistL, -0.95f, 0.95f)) - ChaseOrbitElevOffsetRad, -0.85f, 0.85f);
                const Frontier::Mathematics::Axis3D GlancedLookL {
                    NominalEyeL.X + DistL * std::cos(NominalPitchL) * std::cos(NominalYawL),
                    NominalEyeL.Y + DistL * std::cos(NominalPitchL) * std::sin(NominalYawL),
                    NominalEyeL.Z + DistL * std::sin(NominalPitchL)
                };
                ActiveCamera.CameraOrigin       = TransformVehicleLocal(NominalEyeL);
                ActiveCamera.TargetPoint        = TransformVehicleLocal(GlancedLookL);
                ActiveCamera.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
                ActiveCamera.VerticalFovRadians = (HmiFocus ? 46.0f : 64.0f) * Frontier::Constants::DegToRadFloat;
                ActiveCamera.NearClipMeters     = 0.04f;
                ActiveCamera.FarClipMeters      = 600.0f;
                ActiveCamera.ViewportWidthPx    = RenderWidthPx;
                ActiveCamera.ViewportHeightPx   = RenderHeightPx;
            }
            else
            {
                // Mode 0 = Chase Cam, Mode 1 = Drift Quarter Cam (frames rear tyres + billowing smoke wake)
                const bool  DriftQuarter = (DrivingCamMode == 1u);
                const float NominalDist     = (DriftQuarter ? 6.10f : 6.75f) * ChaseDistanceScale;
                const float NominalYawOff   = DriftQuarter ? -0.58f : -0.05f;
                const float NominalElev     = DriftQuarter ? 0.24f : 0.30f;
                const float OrbitAngle   = SmoothedFollowYawRad + Frontier::Constants::PiFloat + NominalYawOff + ChaseOrbitYawOffsetRad;
                const float ElevAngle    = std::clamp(NominalElev + ChaseOrbitElevOffsetRad, 0.06f, 1.25f);
                const float CosE         = std::cos(ElevAngle);
                const float SinE         = std::sin(ElevAngle);
                const float CosH         = std::cos(SmoothedFollowYawRad);
                const float SinH         = std::sin(SmoothedFollowYawRad);

                const Frontier::Mathematics::Axis3D LookPt = DriftQuarter
                    ? Frontier::Mathematics::Axis3D{ HeroPosWorld.X - CosH * 0.70f, HeroPosWorld.Y - SinH * 0.70f, HeroPosWorld.Z + 0.80f }
                    : Frontier::Mathematics::Axis3D{ HeroPosWorld.X + CosH * 1.65f, HeroPosWorld.Y + SinH * 1.65f, HeroPosWorld.Z + 0.92f };
                ActiveCamera.CameraOrigin = {
                    LookPt.X + NominalDist * CosE * std::cos(OrbitAngle),
                    LookPt.Y + NominalDist * CosE * std::sin(OrbitAngle),
                    std::max(0.28f, LookPt.Z + NominalDist * SinE)
                };
                ActiveCamera.TargetPoint        = LookPt;
                ActiveCamera.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
                ActiveCamera.VerticalFovRadians = (DriftQuarter ? 46.0f : 48.0f) * Frontier::Constants::DegToRadFloat;
                ActiveCamera.NearClipMeters     = 0.10f;
                ActiveCamera.FarClipMeters      = 600.0f;
                ActiveCamera.ViewportWidthPx    = RenderWidthPx;
                ActiveCamera.ViewportHeightPx   = RenderHeightPx;
            }
        }
        else
        {
            ActiveCamera = CameraRig.ConstructCamera(RenderWidthPx, RenderHeightPx);
        }

        // Interactive 3D Ray-Plane HMI Picking (Hover & Left-Click actuate live vehicle / 2nd-engine controls)
        if (HmiPanel.ClickFlashSeconds > 0.0f)
        {
            HmiPanel.ClickFlashSeconds = std::max(0.0f, HmiPanel.ClickFlashSeconds - static_cast<float>(WallDt));
        }
        HmiPanel.HoveredButton = AutoSpace::HmiButtonId::None;
        if (DrivingCamActive && InteriorViewActive && InputReading.PointerSeeded && WindowWidthPx > 0u && WindowHeightPx > 0u)
        {
            const float CursorNormX = std::clamp(InputReading.PositionX / static_cast<float>(WindowWidthPx), 0.0f, 1.0f);
            const float CursorNormY = std::clamp(InputReading.PositionY / static_cast<float>(WindowHeightPx), 0.0f, 1.0f);
            const auto Hit = CockpitUi.EvaluateHmiRayHit(ActiveCamera, CursorNormX, CursorNormY, HeroPosWorld, HeroYawWorldDeg);
            if (Hit.HitScreen)
            {
                HmiPanel.HoveredButton = Hit.HoveredButton;
                if (InputReading.QueryButtonPressed(0u) && Hit.HoveredButton != AutoSpace::HmiButtonId::None)
                {
                    TriggerHmiButtonCommand(Hit.HoveredButton);
                }
            }
        }

        Frontier::GeometricRaster::WorldRenderConfiguration RenderConfiguration {};
        RenderConfiguration.Engine = ExplicitRenderEngine
            ? ActiveRenderEngine
            : (GpuWorldReady
                   ? Frontier::GeometricRaster::WorldRenderEngine::RealTimeGiGtxLowEnd
                   : Frontier::GeometricRaster::WorldRenderEngine::OpenPbrDirect);
        RenderConfiguration.ShadowAlgorithm       = Frontier::GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery;
        RenderConfiguration.ReflectionSource      = Frontier::GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
        RenderConfiguration.LightingMode          = Frontier::GeometricRaster::UnifiedLightingMode::ExactReference;
        RenderConfiguration.AutoBakeStaticClipmap = false;
        RenderConfiguration.ReflectionsEnabled    = true;
        RenderConfiguration.RaytracingEnabled     = true;
        RenderConfiguration.SdfGiIntensity        = 1.05f;
        RenderConfiguration.SdfShadowMarchSteps   = SdfShadowMarchSteps;
        RenderConfiguration.ExposureGain          = QueryLobbyExposureGain(ActiveLobby);
        RenderConfiguration.DebugChannel          = DebugChannel;

        // -------------------------------------------------------------------------------------------------------------
        // Level 4 Cockpit Displays & Secondary Render Engine (In-Car HMI live camera feed matching active RenderEngine)
        // -------------------------------------------------------------------------------------------------------------
        bool SyncedGpuThisCycle = false;
        if (DrivingCamActive)
        {
            // Keep a soft skylight fill quad mounted inside the cabin roof so the sculpted dash, wheel & console stay crisp
            {
                const float YawRadL = HeroYawWorldDeg * Frontier::Constants::DegToRadFloat;
                const float Cl = std::cos(YawRadL), Sl = std::sin(YawRadL);
                auto XfL = [&](float Lx, float Ly, float Lz) -> Frontier::Mathematics::Axis3D
                {
                    return { HeroPosWorld.X + Lx * Cl - Ly * Sl, HeroPosWorld.Y + Lx * Sl + Ly * Cl, HeroPosWorld.Z + Lz };
                };
                Scene.AreaLights.resize(1u);
                Scene.AreaLights[0u] = AutoSpace::MakeAimedAreaQuadLight(
                    XfL(0.28f, 0.14f, 1.54f),
                    XfL(0.65f, 0.14f, 1.02f),
                    0.55f,
                    0.40f,
                    { 2.60f, 2.75f, 3.05f },
                    false);
            }

            const auto ActiveHmiEngine = AutoSpace::VehicleCockpitInstrumentation::ResolveHmiRenderEngine(
                RenderConfiguration.Engine,
                HmiPanel.EngineMode);

            // Synchronize scene once before secondary HMI render pass so both passes share the same BVH4/SDF/Clipmap
            if (GpuWorldReady)
            {
                if (SceneGpuDirty)
                {
                    if (Scene.Atmosphere.Medium.AtmosphereEnabled)
                    {
                        CpuRenderer.SynchronizeWorldAtmosphere(Scene);
                    }
                    GpuRenderer.SynchronizeSceneResources(Scene, true);
                    SceneGpuDirty = false;
                }
                else
                {
                    (void)GpuRenderer.SynchronizeMaterialEntriesIfChanged(Scene);
                    GpuRenderer.SynchronizeSceneResources(Scene, false);
                }
                SyncedGpuThisCycle = true;
            }

            // Render live secondary engine pass ONLY when inside the cockpit / HMI view (never in exterior Chase/Drift/Orbit views!)
            const bool ShouldRenderHmiFeed =
                GpuWorldReady &&
                InteriorViewActive &&
                (HmiFeedRgba8.empty() || ((PresentedCycles & 1u) == 0u));
            if (ShouldRenderHmiFeed)
            {
                for (auto& Disp : Scene.CockpitDisplays)
                {
                    Disp.Enabled = false;
                }

                Frontier::GeometricRaster::WorldRenderConfiguration HmiCfg = RenderConfiguration;
                HmiCfg.Engine              = ActiveHmiEngine;
                HmiCfg.DebugChannel        = 0u;
                HmiCfg.SdfShadowMarchSteps = std::min(SdfShadowMarchSteps, 32u);
                const auto HmiCam = AutoSpace::VehicleCockpitInstrumentation::BuildHmiFeedCamera(
                    HeroPosWorld,
                    HeroYawWorldDeg,
                    HmiPanel.FeedMode);
                std::vector<uint8_t> RenderedHmiRgba8 {};
                if (GpuRenderer.RenderWorldViewportWithEngine(HmiCfg, PresentedCycles, HmiCam, Scene, RenderedHmiRgba8) &&
                    !RenderedHmiRgba8.empty())
                {
                    HmiFeedW     = HmiCam.ViewportWidthPx;
                    HmiFeedH     = HmiCam.ViewportHeightPx;
                    HmiFeedRgba8 = std::move(RenderedHmiRgba8);
                }
            }

            // Build live automotive telemetry snapshot from Jolt VehicleReading + Eulerian Tire Smoke Bridge
            AutoSpace::CockpitTelemetryReading Tel {};
            if (const auto Reading = Session.InspectLobbyLifecycle().AcquireHeroVehicleReading(); Reading.Valid())
            {
                Tel.SpeedKph        = std::fabs(Reading->SpeedKph);
                Tel.EngineRpm       = std::max(750.0f, Reading->EngineRpm);
                Tel.MaxRpm          = 7800.0f;
                Tel.Gear            = Reading->ActiveGear;
                Tel.AbsActive       = Reading->AbsIntervening;
                float MaxRearSlip   = 0.0f;
                for (uint32_t W = 0u; W < 4u; ++W)
                {
                    const float Combined = std::hypot(
                        std::fabs(Reading->Corners[W].LongitudinalSlip),
                        std::fabs(Reading->Corners[W].SlipAngleRad));
                    Tel.WheelSlipRatios[W] = std::clamp(Combined, 0.0f, 1.0f);
                    if (W >= 2u) MaxRearSlip = std::max(MaxRearSlip, Tel.WheelSlipRatios[W]);
                }
                Tel.RearSlipRatio = MaxRearSlip;
            }
            const auto SmokeTel = Session.InspectLobbyLifecycle().InspectTireSmokeTelemetry();
            Tel.SmokePeakDensity      = SmokeTel.PeakSmokeDensity;
            Tel.ActiveSmokeVoxels     = SmokeTel.ActiveSmokeVoxels;
            Tel.Throttle01            = LastThrottle01;
            Tel.Brake01               = LastBrake01;
            Tel.Handbrake01           = LastHandbrake01;
            Tel.SteerInput            = SmoothedSteerInput;
            Tel.VehicleYawDeg         = HeroYawWorldDeg;
            Tel.StabilityAssistOn     = (AssistModeIndex != 1u);
            Tel.AutomaticTransmission = AutomaticShiftUi;
            Tel.InteriorViewActive    = InteriorViewActive;
            static constexpr const char* kCamShort[6] = { "CHASE", "DRIFT", "COCKPIT", "HMI", "ORBIT", "PORTAL B" };
            Tel.ActiveCameraLabel     = kCamShort[DrivingCamMode % 6u];
            Tel.MainRenderEngine      = RenderConfiguration.Engine;
            Tel.ActiveHmiRenderEngine = ActiveHmiEngine;

            CockpitUi.PublishCockpitDisplaysToScene(
                Scene,
                Tel,
                HmiPanel,
                HmiFeedRgba8,
                HmiFeedW,
                HmiFeedH,
                HeroPosWorld,
                HeroYawWorldDeg,
                /*IncludeInCarDisplays=*/true);
        }
        else
        {
            for (auto& Disp : Scene.CockpitDisplays)
            {
                Disp.Enabled = false;
            }
        }

        bool CyclePresented = false;
        if (GpuWorldReady)
        {
            if (!SyncedGpuThisCycle)
            {
                if (SceneGpuDirty)
                {
                    if (Scene.Atmosphere.Medium.AtmosphereEnabled)
                    {
                        CpuRenderer.SynchronizeWorldAtmosphere(Scene);
                    }
                    GpuRenderer.SynchronizeSceneResources(Scene, true);
                    SceneGpuDirty = false;
                }
                else if (Session.InspectLobbyLifecycle().QueryHeroVehiclePhysicsActive() ||
                         Session.InspectLobbyLifecycle().QueryDynamicEmissiveOrbActive())
                {
                    (void)GpuRenderer.SynchronizeMaterialEntriesIfChanged(Scene);
                    GpuRenderer.SynchronizeSceneResources(Scene, false);
                }
            }

            VkImageView WorldImageView = VK_NULL_HANDLE;
            uint32_t    TracedW        = 0u;
            uint32_t    TracedH        = 0u;
            if (GpuRenderer.RenderWorldViewportToGpuImage(
                    RenderConfiguration,
                    PresentedCycles,
                    ActiveCamera,
                    Scene,
                    WorldImageView,
                    TracedW,
                    TracedH) &&
                WorldImageView != VK_NULL_HANDLE)
            {
                if (PresenterReady)
                {
                    CyclePresented = SwapchainPresenter.PresentImageView(
                        WorldImageView,
                        WindowWidthPx,
                        WindowHeightPx);
                }
                else
                {
                    CyclePresented = true;
                }
            }
        }
        else
        {
            if (SceneGpuDirty)
            {
                CpuRenderer.PrepareWorldScene(Scene);
                SceneGpuDirty = false;
            }
            auto CpuImage  = CpuRenderer.RenderWorldFrameWithEngine(RenderConfiguration, ActiveCamera, ActiveCamera, Scene);
            LastCpuWidthPx  = CpuImage.WidthPx;
            LastCpuHeightPx = CpuImage.HeightPx;
            LastCpuRgba8    = std::move(CpuImage.DisplayRgba8);
            if (PresenterReady)
            {
                CyclePresented = SwapchainPresenter.PresentRgba8Buffer(
                    LastCpuRgba8,
                    LastCpuWidthPx,
                    LastCpuHeightPx,
                    WindowWidthPx,
                    WindowHeightPx);
            }
            else
            {
                CyclePresented = !LastCpuRgba8.empty();
            }
        }

        if (CyclePresented)
        {
            ++PresentedCycles;
            FailedPresentRuns = 0u;
        }
        else
        {
            ++FailedPresentRuns;
            std::cerr << "[AutoSpace] Viewport presentation failed: "
                      << SwapchainPresenter.QueryLastDiagnostic() << "\n";
            if (FailedPresentRuns >= 3u)
            {
                break;
            }
        }

        if (RenderCycleLimit > 0u && PresentedCycles >= RenderCycleLimit)
        {
            break;
        }
    }

    if (!ScreenshotPath.empty())
    {
        std::vector<uint8_t> CaptureRgba {};
        uint32_t             CaptureW = 0u;
        uint32_t             CaptureH = 0u;
        bool                 Captured = false;
        if (PresenterReady)
        {
            Captured = SwapchainPresenter.CapturePresentedRgba8(CaptureRgba, CaptureW, CaptureH);
        }
        else if (!LastCpuRgba8.empty() && LastCpuWidthPx > 0u && LastCpuHeightPx > 0u)
        {
            CaptureRgba = LastCpuRgba8;
            CaptureW    = LastCpuWidthPx;
            CaptureH    = LastCpuHeightPx;
            Captured    = true;
        }
        if (Captured)
        {
            if (ScreenshotPath.has_parent_path())
            {
                std::error_code DirEc {};
                std::filesystem::create_directories(ScreenshotPath.parent_path(), DirEc);
            }
            if (Frontier::GeometricRaster::ComponentEditor::ComponentViewportRenderer::WritePngFile(
                    ScreenshotPath, CaptureW, CaptureH, CaptureRgba))
            {
                std::cout << "[AutoSpace] Saved GameMode screenshot (" << CaptureW << "x" << CaptureH
                          << ") to " << ScreenshotPath.generic_string() << "\n";
            }
        }
    }

    const auto PresStats = SwapchainPresenter.QueryTelemetry();
    const auto Metrics   = Session.InspectMetrics();
    std::cout << "[AutoSpace] Mode=GameMode"
              << " Lobby=" << AutoSpace::LobbyPolicyEngine::QuerySlug(ActiveLobby)
              << " PresentedCycles=" << PresentedCycles
              << " Surface=" << WindowWidthPx << "x" << WindowHeightPx
              << " Gpu=" << (GpuWorldReady ? "YES" : "CPU")
              << " HardwareGpu=" << ((GpuWorldReady && GpuRenderer.QueryHardwareGpuActive()) ? "YES" : "Software")
              << " LiveSwapchain=" << (PresStats.LiveWindowSwapchain ? "YES" : "Offscreen")
              << " Vehicles=" << Metrics.LobbyTelemetry.VehicleCount
              << " Surfaces=" << Metrics.LobbyTelemetry.SurfaceInstanceCount
              << " AreaLights=" << Metrics.LobbyTelemetry.AreaLightCount
              << " IesLights=" << Metrics.LobbyTelemetry.PhotometricLightCount
              << "\n";

    SwapchainPresenter.TeardownPresenter();
    GpuRenderer.TeardownSequence();
    VulkanDevice.TeardownInterchange();
    Session.Finalize();
    return (PresentedCycles > 0u) ? 0 : 4;
}
