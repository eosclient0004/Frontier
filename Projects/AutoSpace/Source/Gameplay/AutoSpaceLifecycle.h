/*============================================================================================================================================
                                                          AUTOSPACELIFECYCLE.H
============================================================================================================================================*/
// 🧩 Top-level project runtime lifecycle for AutoSpace orchestrating RuntimeLifecycle, WorldHost, and LobbyLifecycle inside MainWorld

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Lifecycle/RuntimeLifecycle.h"
#include "Engine/Lifecycle/RuntimeWorldHost.h"
#include "Source/Gameplay/LobbyLifecycle.h"
#include "Source/Policies/AutoSpacePolicies.h"

#if FRONTIER_PROFILE_EDITOR
#include "Engine/Components/Panes/OutlinerPanel.h"
#endif

#include <cstdint>
#include <filesystem>
#include <string>

namespace AutoSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AutoSpaceLoopMetrics
{
    uint64_t                TotalFixedStepsExecuted  = 0ull; // [count] - Total deterministic fixed simulation steps completed
    uint64_t                PresentationTicksCount   = 0ull; // [count] - Total outer presentation ticks completed
    double                  SimulatedElapsedSeconds  = 0.0;  // [s]     - Cumulative deterministic simulated time
    double                  LastInterpolationAlpha   = 0.0;  // [-]     - Last presentation blend factor [0..1)
    uint32_t                LoadedGeometryInstances  = 0u;   // [count] - Baked geometry count in MainWorld.WorldSpace (0)
    bool                    SunEnabled               = false;// [-]     - True when Solar ephemeris is active in MainWorld.WorldSpace
    bool                    SkyAtmosphereEnabled     = false;// [-]     - True when Rayleigh/Mie sky medium is active in MainWorld.WorldSpace
    bool                    BootLoadedFromDisk       = false;// [-]     - True when Projects/AutoSpace/ TOML chain loaded cleanly
    bool                    MainWorldLoaded          = false;// [-]     - True when Content/Worlds/MainWorld.WorldSpace loaded cleanly
    bool                    WorkerChannelsRunning    = false;// [-]     - True when opt-in ExecutionChannels are active
    bool                    AssetStreamRunning       = false;// [-]     - True when opt-in AssetStreamThread is active
    LobbyLifecycleTelemetry LobbyTelemetry           {};   // [-]     - Active LobbyPolicy geometry, lights, particles & Outliner counts
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    AUTOSPACELIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

class AutoSpaceLifecycle
{
public:
    AutoSpaceLifecycle() noexcept = default;
    ~AutoSpaceLifecycle() noexcept;

    AutoSpaceLifecycle(const AutoSpaceLifecycle&)            = delete;
    AutoSpaceLifecycle& operator=(const AutoSpaceLifecycle&) = delete;

    [[nodiscard]] bool Initialize(
        const std::filesystem::path& ProjectRootPath,
        bool                         ForceHeadlessWindow,
        std::string&                 OutDiagnostic,
        StudioLobbyId                InitialLobby = StudioLobbyId::StudioLobby1Cyclorama);

    // Switches active lobby inside the same MainWorld.WorldSpace level (zero level switching)
    [[nodiscard]] bool SelectStudioLobby(StudioLobbyId Lobby, std::string& OutDiagnostic);

    uint32_t RunBoundedTicks(uint32_t MaximumOuterTicks, double WallStepSeconds = 1.0 / 60.0) noexcept;

    [[nodiscard]] bool VerifyProjectContracts(std::string& OutDiagnostic) const;

    [[nodiscard]] bool Suspend() noexcept;
    [[nodiscard]] bool Resume() noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] AutoSpaceLoopMetrics                   InspectMetrics() const noexcept;
    [[nodiscard]] const AutoSpacePolicyBundle&           InspectPolicies() const noexcept { return Policies; }
    [[nodiscard]] LobbyLifecycle&                        AccessLobbyLifecycle() noexcept { return Lobby; }
    [[nodiscard]] const LobbyLifecycle&                  InspectLobbyLifecycle() const noexcept { return Lobby; }
    [[nodiscard]] Frontier::Lifecycle::RuntimeLifecycle& AccessRuntimeLoop() noexcept { return RuntimeLoop; }
    [[nodiscard]] Frontier::Lifecycle::RuntimeWorldHost& AccessWorldHost() noexcept { return WorldHost; }
#if FRONTIER_PROFILE_EDITOR
    [[nodiscard]] const Frontier::OutlinerPanel&         InspectOutliner() const noexcept { return EditorOutliner; }
#endif

private:
    static void DispatchFixedStep(double FixedDeltaSeconds, uint64_t FixedStepOrdinal, void* Payload) noexcept;
    static void DispatchPresentationStep(double InterpolationAlpha, double WallDeltaSeconds, void* Payload) noexcept;

    void OnFixedSimulationStep(double FixedDeltaSeconds, uint64_t FixedStepOrdinal) noexcept;
    void OnPresentationStep(double InterpolationAlpha, double WallDeltaSeconds) noexcept;

    Frontier::Lifecycle::RuntimeLifecycle RuntimeLoop    {};
    Frontier::Lifecycle::RuntimeWorldHost WorldHost      {};
    LobbyLifecycle                        Lobby          {};
#if FRONTIER_PROFILE_EDITOR
    Frontier::OutlinerPanel               EditorOutliner {};
#endif
    std::filesystem::path                 RepositoryRoot {};
    AutoSpacePolicyBundle                 Policies       {};
    AutoSpaceLoopMetrics                  Metrics        {};
    bool                                  Initialized    = false;
};

} // namespace AutoSpace
