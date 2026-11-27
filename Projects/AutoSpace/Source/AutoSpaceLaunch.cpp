/*============================================================================================================================================
                                                           AUTOSPACELAUNCH.CPP
============================================================================================================================================*/
// 🧩 Standalone Game Mode executable for AutoSpace (native desktop window + Vulkan swapchain presentation, zero Editor chrome)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Gameplay/AutoSpaceLifecycle.h"

#include "Engine/GeometricRaster/ComponentEditor/ComponentViewportRenderer.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/Rasterization/ViewportSwapchainPresenter.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
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
        case AutoSpace::StudioLobbyId::StudioLobby2ForestStage:  return 1.22f;
        case AutoSpace::StudioLobbyId::StudioLobby3RacingGarage: return 1.20f;
        case AutoSpace::StudioLobbyId::StudioLobby1Cyclorama:
        default:                                                 return 1.18f;
    }
}

[[nodiscard]] std::string ConstructWindowTitle(AutoSpace::StudioLobbyId LobbyId)
{
    return std::string("AutoSpace — ") +
           std::string(AutoSpace::LobbyPolicyEngine::QueryLabel(LobbyId)) +
           "  [1-3: Lobby | C: Camera | Drag: Orbit | WASD: Drive | Esc: Exit]";
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
    uint32_t              SdfShadowMarchSteps = 32u;
    double                InitialSimTimeSec   = -1.0;
    AutoSpace::StudioLobbyId InitialLobby     = AutoSpace::StudioLobbyId::StudioLobby1Cyclorama;

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
        std::string VkDiag {};
        if (VulkanDevice.InitializeInterchange(RepositoryRoot, &VkDiag))
        {
            GpuWorldReady = GpuRenderer.InitializeSequence(VulkanDevice, ShaderDirectory);
            if (!GpuWorldReady)
            {
                std::cerr << "[AutoSpace] VulkanWorldRenderSequence unavailable; falling back to CPU renderer.\n";
            }

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
        }
        else
        {
            std::cerr << "[AutoSpace] VulkanInterchange unavailable (" << VkDiag
                      << "); falling back to CPU renderer.\n";
        }
    }

    OrbitCameraRig CameraRig {};
    uint32_t       CameraPresetIndex = 0u;
    auto SeedCameraFromActivePreset = [&]()
    {
        const auto& Presets = Session.InspectLobbyLifecycle().InspectCameraPresets();
        if (!Presets.empty())
        {
            CameraPresetIndex %= static_cast<uint32_t>(Presets.size());
            CameraRig.SeedFromCamera(Presets[CameraPresetIndex].Camera);
        }
    };
    SeedCameraFromActivePreset();

    bool     SceneGpuDirty     = true;
    uint32_t PresentedCycles   = 0u;
    uint32_t FailedPresentRuns = 0u;
    std::vector<uint8_t> LastCpuRgba8 {};
    uint32_t             LastCpuWidthPx  = 0u;
    uint32_t             LastCpuHeightPx = 0u;

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
                SceneGpuDirty = true;
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

        // Camera preset cycling (C / Tab) and reset (R)
        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::C) ||
            InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::Tab))
        {
            const auto& Presets = Session.InspectLobbyLifecycle().InspectCameraPresets();
            if (!Presets.empty())
            {
                CameraPresetIndex = (CameraPresetIndex + 1u) % static_cast<uint32_t>(Presets.size());
                CameraRig.SeedFromCamera(Presets[CameraPresetIndex].Camera);
            }
        }
        if (InputReading.QueryKeyPressed(Frontier::Input::InputLogicalKey::R))
        {
            SeedCameraFromActivePreset();
        }

        // Smooth mouse orbit & wheel zoom
        if (InputReading.QueryButtonDown(0u) || InputReading.QueryButtonDown(1u))
        {
            CameraRig.AzimuthRadians -= InputReading.TickDeltaX * 0.0055f;
            CameraRig.ElevationRadians =
                std::clamp(CameraRig.ElevationRadians + InputReading.TickDeltaY * 0.0045f, 0.04f, 1.38f);
        }
        if (std::abs(InputReading.ScrollDeltaY) > 0.001f)
        {
            CameraRig.DistanceMeters =
                std::clamp(CameraRig.DistanceMeters * std::pow(0.90f, InputReading.ScrollDeltaY), 1.8f, 32.0f);
        }

        // Hero vehicle driving controls (WASD / Arrow keys + Space handbrake)
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

            DriveDir.ThrottleNormalized  = ForwardHeld ? 1.0f : 0.0f;
            DriveDir.BrakeNormalized     = BrakeHeld ? 1.0f : 0.0f;
            DriveDir.SteeringNormalized  = (RightHeld ? 1.0f : 0.0f) - (LeftHeld ? 1.0f : 0.0f);
            DriveDir.HandbrakeNormalized = HandbrakeHeld ? 1.0f : 0.0f;
            (void)Session.AccessLobbyLifecycle().SubmitHeroVehicleDirective(DriveDir);
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
        const auto ActiveCamera = CameraRig.ConstructCamera(RenderWidthPx, RenderHeightPx);

        Frontier::GeometricRaster::WorldRenderConfiguration RenderConfiguration {};
        RenderConfiguration.Engine                = GpuWorldReady
            ? Frontier::GeometricRaster::WorldRenderEngine::RealTimeGiGtxLowEnd
            : Frontier::GeometricRaster::WorldRenderEngine::OpenPbrDirect;
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

        bool CyclePresented = false;
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
            else if (Session.InspectLobbyLifecycle().QueryHeroVehiclePhysicsActive() ||
                     Session.InspectLobbyLifecycle().QueryDynamicEmissiveOrbActive())
            {
                (void)GpuRenderer.SynchronizeMaterialEntriesIfChanged(Scene);
                GpuRenderer.SynchronizeSceneResources(Scene, false);
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
