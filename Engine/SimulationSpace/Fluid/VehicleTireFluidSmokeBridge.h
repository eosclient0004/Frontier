/*============================================================================================================================================
                                                VEHICLETIREFLUIDSMOKEBRIDGE.H
============================================================================================================================================*/
// 🧩 World-Space 3D Eulerian Fluid Tire Smoke Bridge coupling Frontier's native VehiclePhysicsRig (JoltPhysics v5.6.0)
//    with Frontier's native FluidCpuSimulationState / FluidVulkanGpuExecutor (WorldGrid whole-cell relocation +
//    MacCormack advection + Vorticity confinement + Moving Vehicle SDF collision & aerodynamic wake entrainment).
//
//    Key Guarantees:
//    1. GLOBAL WORLD-SPACE SMOKE: Smoke advects and persists in world coordinates; turning/spinning the vehicle
//       NEVER rotates or drags previously emitted smoke with the chassis.
//    2. MOVING VEHICLE SDF COLLISION: Composite chassis + cabin + RearWing + wheel SDF enforces zero interior
//       penetration, mass-conserving tangential flow-around, and moving-body wake entrainment.
//    3. PHYSICALLY DRIVEN EMISSION: Burnout wheelspin, drift combined-slip, and lockup braking emit realistic
//       rubber smoke scaled by real tire slip power (Watts), vertical load, and tread pyrolysis temperature (K).

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Fluid/FluidGpuSolver.h"
#include "Engine/SimulationSpace/Fluid/FluidRegionTypes.h"
#include "Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h"
#include "Engine/SimulationSpace/PhysicsSpace/VehicleSpecifications.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SimulationSpace::Fluid
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class TireSmokeRegime : uint8_t
{
    None        = 0u, // [-] - Below slip/thermal pyrolysis threshold
    Burnout     = 1u, // [-] - High longitudinal wheelspin (kappa > 0.25)
    Drift       = 2u, // [-] - High lateral slip angle (|alpha| > 8 deg) / powerslide
    BrakeLockup = 3u, // [-] - Hard threshold/lockup braking (kappa < -0.22)
};

//------------------------------------------------------------------------------------------------------------------------
//                                        MOVING COMPOSITE VEHICLE SDF COLLIDER
//------------------------------------------------------------------------------------------------------------------------

/// Analytic oriented composite SDF for a live rigid-body vehicle (Chassis wedge + Cabin + RearWing + 4 Wheels)
/// with exact rigid-body surface velocity (v_body + omega x r) for moving-wall fluid collision & wake entrainment.
struct FluidVehicleSdfCollider
{
    std::array<float, 3> PositionMeters      { 0.0f, 0.0f, 0.0f }; // [m]     - Chassis CoM world position
    std::array<float, 4> OrientationQuat     { 0.0f, 0.0f, 0.0f, 1.0f }; // [-] - (x,y,z,w) chassis orientation
    std::array<float, 3> LinearVelocityMps   { 0.0f, 0.0f, 0.0f }; // [m/s]   - Chassis world linear velocity
    std::array<float, 3> AngularVelocityRadS { 0.0f, 0.0f, 0.0f }; // [rad/s] - Chassis world angular velocity

    // Local-space (+X forward, +Y up, +Z right) composite OBB bounds
    std::array<float, 3> ChassisCentreLocal  { 0.0f, 0.14f, 0.0f };
    std::array<float, 3> ChassisHalfExtents  { 2.75f, 0.30f, 0.98f };
    std::array<float, 3> CabinCentreLocal    { -0.22f, 0.62f, 0.0f };
    std::array<float, 3> CabinHalfExtents    { 1.08f, 0.28f, 0.80f };
    std::array<float, 3> WingCentreLocal     { -2.55f, 0.68f, 0.0f };
    std::array<float, 3> WingHalfExtents     { 0.20f, 0.08f, 0.96f };
    bool                 RearWingAttached    = true;
    float                FilletRadiusMeters  = 0.08f;

    [[nodiscard]] static FluidVehicleSdfCollider ConstructFromVehicleReading(
        const PhysicsSpace::VehicleReading&          reading,
        const PhysicsSpace::VehicleRigSpecification& spec) noexcept;

    /// Evaluates signed distance (meters) in world space: < 0 inside vehicle body, 0 on surface, > 0 outside.
    [[nodiscard]] float EvaluateSignedDistance(const std::array<float, 3>& worldPosMeters) const noexcept;

    /// Evaluates outward unit surface normal in world space via central differences.
    [[nodiscard]] std::array<float, 3> EvaluateOutwardNormal(const std::array<float, 3>& worldPosMeters) const noexcept;

    /// Evaluates exact rigid-body surface velocity (v_chassis + omega x (p - p_chassis)) at worldPosMeters.
    [[nodiscard]] std::array<float, 3> EvaluateSurfaceVelocity(const std::array<float, 3>& worldPosMeters) const noexcept;

    /// Exports 4 bounding spheres covering the vehicle hull for Frontier's 160-byte GPU FluidVolumeUniformBlock.
    void ExportGpuApproxSpheres(std::array<FluidColliderSphere, kMaximumFluidColliders>& outSpheres) const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                     TIRE EMISSION DIAGNOSTICS & BRIDGE CONFIG
//------------------------------------------------------------------------------------------------------------------------

struct TireSmokeCornerReport
{
    TireSmokeRegime      Regime               = TireSmokeRegime::None;
    float                EmissionIntensity    = 0.0f; // [0..1] - Normalized smoke density rate
    float                SlipPowerWatts       = 0.0f; // [W]    - Frictional rubber dissipation at contact patch
    float                CombinedSlipSpeedMps = 0.0f; // [m/s]  - |v_patch_slip|
    float                TreadTempKelvin      = 293.15f;
    std::array<float, 3> EmitterWorldMeters   { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> EjectionVelocityMps  { 0.0f, 0.0f, 0.0f };
};

struct VehicleTireFluidSmokeConfig
{
    bool                    UseWorldGridRelocation = true;   // [-]     - Whole-cell world-space sliding grid (never rotates!)
    std::array<uint32_t, 3> FixedGridCells         { 96u, 32u, 96u }; // Used when UseWorldGridRelocation == false
    uint32_t                WorldGridTileSpan      = 4u;     // [tiles] - 4x4x4 tiles (e.g. 4 * 16 = 64^3 cells)
    uint32_t                WorldGridTileCells     = 16u;    // [cells] - Cells per world tile
    float                   CellSizeMeters         = 0.25f;  // [m]     - World voxel edge length (64 * 0.25 = 16 m span)
    float                   DomainFloorYOffsetM    = 0.0f;   // [m]     - World Y floor plane (asphalt surface)

    // Realistic drift/burnout rubber smoke fluid properties
    float                   Buoyancy               = 0.85f;  // [m/s^2/T] - Gentle thermal rise (hot rubber smoke billows & hangs)
    float                   Gravity                = 0.06f;  // [m/s^2]   - Slight dense-smoke settling after cooling
    float                   Vorticity              = 2.60f;  // [-]       - Rich turbulent drift curl & vortex rings
    float                   DensityDissipation     = 0.992f; // [-]       - Slow lingering smoke cloud (per step)
    float                   VelocityDissipation    = 0.985f; // [-]       - Realistic air drag on smoke momentum
    float                   Cooling                = 0.45f;  // [1/s]     - Hot tire plume cools toward ambient track air
    uint32_t                PressureIterations     = 20u;    // [iter]    - Jacobi incompressibility projection iterations
    std::array<float, 3>    AmbientWindMps         { 0.0f, 0.0f, 0.35f }; // [m/s] - Gentle cross-track breeze

    // Tire pyrolysis & slip thresholds
    float                   MinSlipSpeedMps        = 1.65f;  // [m/s]   - Contact patch slip speed onset
    float                   MinSlipPowerWatts      = 5500.0f;// [W]     - Frictional dissipation onset
    float                   FullSmokePowerWatts    = 48000.0f;// [W]    - Full thick opaque plume power
    float                   PyrolysisBaseKelvin    = 302.0f; // [K]     - Warm rubber smoke onset
    float                   PyrolysisFullKelvin    = 355.0f; // [K]     - Peak dense pyrolysis temperature
    float                   WakeEntrainmentGain    = 0.55f;  // [-]     - Aerodynamic slipstream & rear-wake swirl coupling
};

struct TireSmokeBridgeTelemetry
{
    uint64_t                                          StepCount                = 0u;
    uint64_t                                          GridRelocationCount      = 0u;
    uint32_t                                          ActiveEmitterCount       = 0u;
    uint32_t                                          ActiveSmokeVoxels        = 0u;
    uint32_t                                          SdfCollisionVoxels       = 0u;
    uint32_t                                          SdfInteriorLeakVoxels    = 0u; // Must remain 0!
    float                                             TotalSmokeMass           = 0.0f;
    float                                             PeakSmokeDensity         = 0.0f;
    float                                             MaxFluidSpeedMps         = 0.0f;
    std::array<float, 3>                              SmokeCentroidWorldMeters { 0.0f, 0.0f, 0.0f };
    std::array<float, 3>                              GridCentreWorldMeters    { 0.0f, 0.0f, 0.0f };
    std::array<TireSmokeCornerReport, 4u>             HeroCorners              {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                         VEHICLETIREFLUIDSMOKEBRIDGE
//------------------------------------------------------------------------------------------------------------------------

class VehicleTireFluidSmokeBridge
{
public:
    VehicleTireFluidSmokeBridge() noexcept = default;
    ~VehicleTireFluidSmokeBridge() noexcept = default;

    void Initialize(
        const VehicleTireFluidSmokeConfig& config,
        const std::array<float, 3>&        initialFocusWorldMeters = { 0.0f, 0.0f, 0.0f });

    void Reset() noexcept;

    /// Steps the global world-space 3D fluid solver coupled to one or more live Jolt vehicles.
    /// The hero vehicle (index 0) guides optional WorldGrid whole-cell relocation; all vehicles emit
    /// tire smoke AND act as moving SDF obstacles with wake entrainment.
    void StepSimulation(
        std::span<const PhysicsSpace::VehicleReading>          vehicleReadings,
        std::span<const PhysicsSpace::VehicleRigSpecification> vehicleSpecs,
        float                                                  deltaTimeSeconds);

    /// Convenience overload for a single hero vehicle.
    void StepSingleVehicle(
        const PhysicsSpace::VehicleReading&          reading,
        const PhysicsSpace::VehicleRigSpecification& spec,
        float                                        deltaTimeSeconds);

    /// Samples world-space smoke density (channel 0) & temperature (channel 1) via trilinear interpolation.
    [[nodiscard]] std::array<float, 2> SampleSmokeDensityTempWorld(
        const std::array<float, 3>& worldPosMeters) const noexcept;

    /// Samples world-space fluid velocity (m/s) via trilinear interpolation.
    [[nodiscard]] std::array<float, 3> SampleVelocityWorld(
        const std::array<float, 3>& worldPosMeters) const noexcept;

    [[nodiscard]] const FluidCpuSimulationState&              InspectFluidState() const noexcept { return FluidState_; }
    [[nodiscard]] const FluidStepSettings&                    InspectStepSettings() const noexcept { return StepSettings_; }
    [[nodiscard]] const VehicleTireFluidSmokeConfig&          InspectConfig() const noexcept { return Config_; }
    [[nodiscard]] const TireSmokeBridgeTelemetry&             InspectTelemetry() const noexcept { return Telemetry_; }
    [[nodiscard]] std::span<const FluidVehicleSdfCollider>    InspectVehicleSdfs() const noexcept { return ActiveSdfs_; }
    [[nodiscard]] std::span<const FluidReleaseSourceParameters> InspectActiveEmitters() const noexcept { return ActiveEmitters_; }

private:
    void EvaluateVehicleTireEmitters(
        const PhysicsSpace::VehicleReading&          reading,
        const PhysicsSpace::VehicleRigSpecification& spec,
        bool                                         recordHeroTelemetry);

    void EnforceMovingVehicleSdfsAndGround(
        const FluidVolumeUniformBlock& volume,
        float                          deltaTimeSeconds);

    void ComputeTelemetry(const FluidVolumeUniformBlock& volume) noexcept;

    VehicleTireFluidSmokeConfig               Config_         {};
    FluidStepSettings                         StepSettings_   {};
    FluidCpuSimulationState                   FluidState_     {};
    std::vector<FluidVehicleSdfCollider>      ActiveSdfs_     {};
    std::vector<FluidReleaseSourceParameters> ActiveEmitters_ {};
    TireSmokeBridgeTelemetry                  Telemetry_      {};
    bool                                      Initialized_    = false;
};

} // namespace Frontier::SimulationSpace::Fluid
