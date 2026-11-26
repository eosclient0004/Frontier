/*============================================================================================================================================
                                                          UNTITLEDLIFECYCLE.H
============================================================================================================================================*/
// 🧩 Tight, zero-Editor reference project main loop for Untitled driving Frontier::Lifecycle::RuntimeLifecycle and project policies

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalSpeciesSchema.h"
#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalWorldPartition.h"
#include "Engine/Lifecycle/RuntimeLifecycle.h"
#include "Source/UntitledPolicies.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Untitled
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct UntitledLoopMetrics
{
    uint64_t TotalFixedStepsExecuted  = 0ull; // [count] - Total deterministic fixed simulation steps completed
    uint64_t PresentationTicksCount   = 0ull; // [count] - Total outer presentation ticks completed
    uint64_t AlignmentHashesComputed  = 0ull; // [count] - Number of 64-bit world alignment digests verified
    uint64_t LastWorldAlignmentHash   = 0ull; // [hash]  - Most recent deterministic 64-bit world kinematics digest
    uint64_t DirectivesAccepted       = 0ull; // [count] - Validated control directives applied by GameplayRulePolicy
    uint64_t DirectivesRejected       = 0ull; // [count] - Invalid directives blocked by GameplayRulePolicy
    double   SimulatedElapsedSeconds  = 0.0;  // [s]     - Cumulative deterministic simulated time
    double   LastInterpolationAlpha   = 0.0;  // [-]     - Last presentation blend factor [0..1)
    double   VehiclePositionMetersX   = 0.0;  // [m]     - Reference autonomous vehicle X position
    double   VehiclePositionMetersY   = 0.0;  // [m]     - Reference autonomous vehicle Y position
    float    VehicleSpeedMetersSec    = 0.0f; // [m/s]   - Reference autonomous vehicle speed
    bool     BootLoadedFromDisk       = false;// [-]     - True when Projects/Untitled/ TOML chain loaded cleanly
    bool     WorkerChannelsRunning    = false;// [-]     - True when opt-in ExecutionChannels are active
    bool     AssetStreamRunning       = false;// [-]     - True when opt-in AssetStreamThread is active
    uint32_t BotanicalSpeciesLoaded   = 0u;   // [count] - Number of .botanical species resolved into Projects/Untitled
    uint32_t BotanicalPlantsActive    = 0u;   // [count] - Total botanical plants registered in WorldPartition
    uint32_t BotanicalCpuCells        = 0u;   // [count] - 16m x 16m WorldPartition cells on Deterministic CPU simulation
    uint32_t BotanicalGpuCells        = 0u;   // [count] - 16m x 16m WorldPartition cells on GPU analytic simulation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class UntitledLifecycle
{
public:
    UntitledLifecycle() noexcept = default;
    ~UntitledLifecycle() noexcept;

    UntitledLifecycle(const UntitledLifecycle&)            = delete;
    UntitledLifecycle& operator=(const UntitledLifecycle&) = delete;

    [[nodiscard]] bool Initialize(
        const std::filesystem::path& ProjectRootPath,
        bool                         ForceHeadlessWindow,
        std::string&                 OutDiagnostic);

    uint32_t RunBoundedTicks(uint32_t MaximumOuterTicks, double WallStepSeconds = 1.0 / 60.0) noexcept;

    [[nodiscard]] bool VerifyPolicyContracts(std::string& OutDiagnostic) const;

    [[nodiscard]] bool Suspend() noexcept;
    [[nodiscard]] bool Resume() noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] UntitledLoopMetrics                       InspectMetrics() const noexcept;
    [[nodiscard]] const UntitledPolicyBundle&               InspectPolicies() const noexcept { return Policies; }
    [[nodiscard]] Frontier::Lifecycle::RuntimeLifecycle&    AccessRuntimeLoop() noexcept { return RuntimeLoop; }
    [[nodiscard]] const Frontier::GeometrySpace::Botanical::BotanicalSpeciesRegistry& InspectBotanicalRegistry() const noexcept { return BotanicalRegistry; }
    [[nodiscard]] const Frontier::GeometrySpace::Botanical::BotanicalWorldPartition&  InspectBotanicalPartition() const noexcept { return BotanicalPartition; }

private:
    static void DispatchFixedStep(double FixedDeltaSeconds, uint64_t FixedStepOrdinal, void* Payload) noexcept;
    static void DispatchPresentationStep(double InterpolationAlpha, double WallDeltaSeconds, void* Payload) noexcept;

    void OnFixedSimulationStep(double FixedDeltaSeconds, uint64_t FixedStepOrdinal) noexcept;
    void OnPresentationStep(double InterpolationAlpha, double WallDeltaSeconds) noexcept;

    Frontier::Lifecycle::RuntimeLifecycle                               RuntimeLoop       {};    // [-] - Zero-Editor tight runtime loop
    UntitledPolicyBundle                                                Policies          {};    // [-] - Project execution, gameplay & driving policies
    Frontier::SimulationSpace::Policies::WorldKinematicsDigestEntry     ReferenceVehicle  {};    // [-] - Deterministic reference actor for fixed steps
    Frontier::GeometrySpace::Botanical::BotanicalSpeciesRegistry        BotanicalRegistry {};    // [-] - Loaded .botanical species library (Ferns -> Palms -> Oak)
    Frontier::GeometrySpace::Botanical::BotanicalWorldPartition         BotanicalPartition{};    // [-] - 16m x 16m GPU<->CPU WorldPartition & 4m x 4m falling leaves
    Frontier::GeometrySpace::Botanical::BotanicalWindFieldState         BotanicalWind     {};    // [-] - ForceField wind driving Projects/Untitled foliage
    UntitledLoopMetrics                                                 Metrics           {};    // [-] - Live runtime & policy telemetry
    bool                                                                Initialized       = false;// [-] - Lifecycle active flag
};

} // namespace Untitled
