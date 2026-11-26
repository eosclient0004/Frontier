/*============================================================================================================================================
                                                       BOTANICALWORLDPARTITION.H
============================================================================================================================================*/
// 🧩 Spatial WorldPartition Grid for Frontier's Unified Botanical System:
//    1. 16m x 16m BotanicalPartitionCells:
//       - Remote cells without players/characters run on GPU Analytic Wind Simulation (0 CPU cost).
//       - Cells near players/characters or flagged NetworkAuthoritative smoothly promote to Deterministic 120 Hz CPU Simulation.
//       - C1-continuous velocity-matched Hermite cross-fade (0.35s promote / 0.65s demote) + 24m/32m spatial hysteresis -> ZERO SNAP!
//    2. 4m x 4m LeafLitterSubcells:
//       - 3-State Falling/Sleeping Leaf Lifecycle (Attached -> Active <= 8,192 -> Sleeping 0-ALU -> ForceField/Vehicle Wake-up).

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalSpeciesSchema.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier::GeometrySpace::Botanical
{

//------------------------------------------------------------------------------------------------------------------------
//                                              SIMULATION STATES & CONFIG
//------------------------------------------------------------------------------------------------------------------------

enum class BotanicalCellSimState : uint8_t
{
    GpuOnly            = 0u,
    TransitioningToCpu = 1u,
    DeterministicCpu   = 2u,
    TransitioningToGpu = 3u,
};

struct WorldPartitionConfig
{
    float    CellSizeMeters       = 16.0f;
    float    LeafSubcellMeters    = 4.0f;
    float    PromoteRadiusMeters  = 24.0f;
    float    DemoteRadiusMeters   = 32.0f;
    float    PromoteBlendSec      = 0.35f;
    float    DemoteBlendSec       = 0.65f;
    float    CpuFixedStepSec      = 1.0f / 120.0f;
    uint32_t MaxActiveLeaves      = 8192u;
    float    WakeWindSpeedMps     = 6.0f;
    float    LeafSettleSpeedMps   = 0.08f;
    float    LeafSettleDelaySec   = 0.45f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                      WIND / FORCEFIELD QUERY & PLAYER PROXIES
//------------------------------------------------------------------------------------------------------------------------

struct BotanicalWindFieldState
{
    std::array<float, 3> WindVelocityMps   { 4.5f, 1.2f, 0.0f };
    float                GustStrengthMps   = 2.2f;
    float                GustFrequencyHz   = 0.55f;
    // Optional localized vortex / vehicle slipstream wake impulse
    bool                 HasWakeImpulse    = false;
    std::array<float, 3> WakeCenterMeters  { 0.0f, 0.0f, 0.0f };
    float                WakeRadiusMeters  = 3.5f;
    float                WakeStrengthMps   = 0.0f;
    std::array<float, 3> WakeDirNorm       { 1.0f, 0.0f, 0.0f };
};

struct BotanicalObserverProxy
{
    std::array<float, 3> PositionMeters { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> VelocityMps    { 0.0f, 0.0f, 0.0f };
    float                RadiusMeters   = 0.75f;
    bool                 IsVehicle      = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                       REPLICATED PLANT INSTANCE & LEAF PARTICLES
//------------------------------------------------------------------------------------------------------------------------

struct BotanicalReplicatedPacket
{
    uint32_t EntityId       = 0u;
    int16_t  BendAngleXQ12  = 0;  // Fixed-point rad * 4096
    int16_t  BendAngleYQ12  = 0;
    uint16_t DetachedLeaves = 0u;
    uint8_t  CellSimState   = 0u;
    uint8_t  Reserved       = 0u;
};

struct BotanicalPlantInstance
{
    uint32_t             EntityId          = 0u;
    std::string          SpeciesId         {};
    BotanicalGrowthForm  GrowthForm        = BotanicalGrowthForm::Fern;
    std::array<float, 3> WorldPosMeters    { 0.0f, 0.0f, 0.0f };
    float                YawRad            = 0.0f;
    float                Scale             = 1.0f;
    float                HeightMeters      = 1.0f;
    float                TrunkRadiusMeters = 0.08f;
    float                StiffnessNmPerRad = 18.0f;
    float                DampingRatio      = 0.28f;
    float                WindFlexibility   = 0.75f;
    float                AbscissionThreshN = 14.0f;
    float                PhaseOffset       = 0.0f;
    bool                 ForceNetworkAuth  = false;

    // Smoothly blended output pose (always C1 continuous across GPU <-> CPU switches!)
    float                BendAngleXRad     = 0.0f;
    float                BendAngleYRad     = 0.0f;
    float                BendVelXRadPerSec = 0.0f;
    float                BendVelYRadPerSec = 0.0f;

    // Deterministic 120 Hz CPU cantilever state
    float                CpuAngleX         = 0.0f;
    float                CpuAngleY         = 0.0f;
    float                CpuVelX           = 0.0f;
    float                CpuVelY           = 0.0f;

    // GPU analytic pose snapshot (for C1 smooth cross-fade)
    float                GpuAngleX         = 0.0f;
    float                GpuAngleY         = 0.0f;

    // Telemetry for transition zero-snap verification
    float                MaxFrameDeltaRad  = 0.0f;
    uint16_t             DetachedLeafCount = 0u;
    float                ShedCooldownSec   = 0.0f;
};

enum class FallingLeafState : uint8_t
{
    Active   = 0u,
    Sleeping = 1u,
};

struct FallingLeafParticle
{
    uint32_t             Id             = 0u;
    uint32_t             SourceEntityId = 0u;
    std::array<float, 3> PositionMeters { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> VelocityMps    { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> ColorLinear    { 0.25f, 0.38f, 0.10f };
    float                HalfSpanMeters = 0.08f;
    float                YawRad         = 0.0f;
    float                PitchRad       = 0.0f;
    float                FlutterPhase   = 0.0f;
    float                RestTimerSec   = 0.0f;
    FallingLeafState     State          = FallingLeafState::Active;
    bool                 HasWokenOnce   = false;
};

struct BotanicalPartitionCell
{
    int32_t               CellX             = 0;
    int32_t               CellY             = 0;
    BotanicalCellSimState State             = BotanicalCellSimState::GpuOnly;
    float                 CpuWeight         = 0.0f; // 0 = pure GPU, 1 = pure Deterministic CPU (Hermite C1!)
    float                 TransitionTimer   = 0.0f;
    std::vector<uint32_t> PlantIndices      {};
};

struct LeafLitterSubcell
{
    int32_t               SubX              = 0;
    int32_t               SubY              = 0;
    std::vector<uint32_t> SleepingLeafIds   {};
};

struct BotanicalPartitionTelemetry
{
    uint32_t GpuCells           = 0u;
    uint32_t CpuCells           = 0u;
    uint32_t TransitioningCells = 0u;
    uint32_t ActiveLeafCount    = 0u;
    uint32_t SleepingLeafCount  = 0u;
    uint32_t WokenLeafCount     = 0u;
    float    MaxSnapDeltaRad    = 0.0f;
    uint64_t CpuFixedSubsteps   = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              BOTANICAL WORLD PARTITION ENGINE
//------------------------------------------------------------------------------------------------------------------------

class BotanicalWorldPartition
{
public:
    explicit BotanicalWorldPartition(WorldPartitionConfig cfg = {}) noexcept : Config_(cfg) {}

    uint32_t RegisterPlant(const BotanicalPlantInstance& instance);
    void     SpawnFallingLeaf(const FallingLeafParticle& leaf);

    /// Advances WorldPartition cells (GPU <-> C1 Deterministic CPU), plant physics, falling/sleeping/waking leaves.
    void Step(
        float                                     dtSec,
        const BotanicalWindFieldState&            windField,
        const std::vector<BotanicalObserverProxy>& observers);

    /// Evaluates analytic GPU wind bend & velocity at a given plant (identical to BotanicalSdfWindDeform.slang!).
    void EvaluateGpuWindPose(
        const BotanicalPlantInstance&  plant,
        float                          timeSec,
        const BotanicalWindFieldState& windField,
        float&                         outAngleX,
        float&                         outAngleY,
        float&                         outVelX,
        float&                         outVelY) const noexcept;

    [[nodiscard]] std::vector<BotanicalReplicatedPacket> BuildReplicatedPackets() const;
    [[nodiscard]] BotanicalPartitionTelemetry            GetTelemetry() const noexcept;

    [[nodiscard]] const std::vector<BotanicalPlantInstance>& Plants() const noexcept { return Plants_; }
    [[nodiscard]] std::vector<BotanicalPlantInstance>&       Plants() noexcept       { return Plants_; }
    [[nodiscard]] const std::vector<FallingLeafParticle>&    Leaves() const noexcept { return Leaves_; }
    [[nodiscard]] const std::unordered_map<uint64_t, BotanicalPartitionCell>& Cells() const noexcept { return Cells_; }
    [[nodiscard]] const WorldPartitionConfig& Config() const noexcept { return Config_; }
    [[nodiscard]] float SimTimeSec() const noexcept { return SimTimeSec_; }

private:
    [[nodiscard]] static uint64_t PackCellKey(int32_t cx, int32_t cy) noexcept;
    [[nodiscard]] std::array<float, 3> SampleWindAt(const std::array<float, 3>& pos, float timeSec, const BotanicalWindFieldState& wind) const noexcept;

    WorldPartitionConfig                                 Config_          {};
    float                                                SimTimeSec_      = 0.0f;
    float                                                CpuAccumSec_     = 0.0f;
    uint64_t                                             CpuSubsteps_     = 0u;
    uint32_t                                             NextLeafId_      = 1u;
    uint32_t                                             TotalWokenCount_ = 0u;
    std::vector<BotanicalPlantInstance>                  Plants_          {};
    std::vector<FallingLeafParticle>                     Leaves_          {};
    std::unordered_map<uint64_t, BotanicalPartitionCell> Cells_           {};
    std::unordered_map<uint64_t, LeafLitterSubcell>      LeafSubcells_    {};
};

} // namespace Frontier::GeometrySpace::Botanical
