/*============================================================================================================================================
                                               VEHICLETIREFLUIDSMOKEBRIDGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of World-Space 3D Eulerian Fluid Tire Smoke Bridge (Jolt VehiclePhysicsRig <-> Frontier FluidGpuSolver)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Fluid/VehicleTireFluidSmokeBridge.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::SimulationSpace::Fluid
{

namespace
{

using Vec3 = std::array<float, 3>;

[[nodiscard]] inline Vec3 Add3(const Vec3& a, const Vec3& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
[[nodiscard]] inline Vec3 Sub3(const Vec3& a, const Vec3& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
[[nodiscard]] inline Vec3 Scale3(const Vec3& a, float s) noexcept { return { a[0] * s, a[1] * s, a[2] * s }; }
[[nodiscard]] inline float Dot3(const Vec3& a, const Vec3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
[[nodiscard]] inline Vec3 Cross3(const Vec3& a, const Vec3& b) noexcept
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
[[nodiscard]] inline float Len3(const Vec3& a) noexcept { return std::sqrt(Dot3(a, a)); }
[[nodiscard]] inline Vec3 Norm3(const Vec3& a, const Vec3& fallback = { 0.0f, 1.0f, 0.0f }) noexcept
{
    const float l = Len3(a);
    return (l > 1.0e-6f) ? Scale3(a, 1.0f / l) : fallback;
}

[[nodiscard]] inline Vec3 RotateQuat(const std::array<float, 4>& q, const Vec3& v) noexcept
{
    const Vec3 u { q[0], q[1], q[2] };
    const Vec3 t = Scale3(Cross3(u, v), 2.0f);
    return Add3(v, Add3(Scale3(t, q[3]), Cross3(u, t)));
}

[[nodiscard]] inline Vec3 RotateInvQuat(const std::array<float, 4>& q, const Vec3& v) noexcept
{
    return RotateQuat({ -q[0], -q[1], -q[2], q[3] }, v);
}

[[nodiscard]] inline float SmoothStep(float edge0, float edge1, float x) noexcept
{
    const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1.0e-5f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] inline float SignedDistRoundedBox(const Vec3& p, const Vec3& centre, const Vec3& halfExt, float r) noexcept
{
    const float qx = std::fabs(p[0] - centre[0]) - std::max(halfExt[0] - r, 0.01f);
    const float qy = std::fabs(p[1] - centre[1]) - std::max(halfExt[1] - r, 0.01f);
    const float qz = std::fabs(p[2] - centre[2]) - std::max(halfExt[2] - r, 0.01f);
    const float ox = std::max(qx, 0.0f);
    const float oy = std::max(qy, 0.0f);
    const float oz = std::max(qz, 0.0f);
    const float outDist = std::sqrt(ox * ox + oy * oy + oz * oz);
    const float inDist  = std::min(std::max({ qx, qy, qz }), 0.0f);
    return outDist + inDist - r;
}

[[nodiscard]] inline float SmoothMin(float a, float b, float k) noexcept
{
    const float h = std::max(k - std::fabs(a - b), 0.0f) / std::max(k, 1.0e-5f);
    return std::min(a, b) - h * h * k * 0.25f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           FLUIDVEHICLESDFCOLLIDER
//------------------------------------------------------------------------------------------------------------------------

FluidVehicleSdfCollider FluidVehicleSdfCollider::ConstructFromVehicleReading(
    const PhysicsSpace::VehicleReading&          reading,
    const PhysicsSpace::VehicleRigSpecification& spec) noexcept
{
    FluidVehicleSdfCollider sdf {};
    sdf.PositionMeters      = { reading.WorldPositionMeters.X, reading.WorldPositionMeters.Y, reading.WorldPositionMeters.Z };
    sdf.OrientationQuat     = { reading.WorldOrientation.X, reading.WorldOrientation.Y, reading.WorldOrientation.Z, reading.WorldOrientation.W };
    sdf.LinearVelocityMps   = { reading.LinearVelocityWorldMps.X, reading.LinearVelocityWorldMps.Y, reading.LinearVelocityWorldMps.Z };
    sdf.AngularVelocityRadS = { reading.AngularVelocityWorldRadS.X, reading.AngularVelocityWorldRadS.Y, reading.AngularVelocityWorldRadS.Z };

    // Match authored ControlVehicle dimensions cleanly (lower wedge body + upper cabin + optional RearWing)
    sdf.ChassisHalfExtents = {
        std::max(spec.ChassisHalfExtentsM.X * 0.96f, 2.2f),
        std::max(spec.ChassisHalfExtentsM.Y * 0.85f, 0.28f),
        std::max(spec.ChassisHalfExtentsM.Z * 0.96f, 0.85f)
    };
    sdf.ChassisCentreLocal = { 0.0f, 0.12f, 0.0f };
    sdf.CabinCentreLocal   = { -0.22f, sdf.ChassisHalfExtents[1] + 0.30f, 0.0f };
    sdf.CabinHalfExtents   = { sdf.ChassisHalfExtents[0] * 0.40f, 0.27f, sdf.ChassisHalfExtents[2] * 0.80f };

    sdf.RearWingAttached   = reading.RearWingAttached;
    sdf.WingCentreLocal    = {
        spec.Aerodynamics.RearWingLocalMountMeters.X,
        spec.Aerodynamics.RearWingLocalMountMeters.Y,
        spec.Aerodynamics.RearWingLocalMountMeters.Z
    };
    sdf.WingHalfExtents    = { 0.20f, 0.08f, std::max(spec.ChassisHalfExtentsM.Z * 0.92f, 0.85f) };
    sdf.FilletRadiusMeters = 0.08f;
    return sdf;
}

float FluidVehicleSdfCollider::EvaluateSignedDistance(const std::array<float, 3>& worldPosMeters) const noexcept
{
    const Vec3 local = RotateInvQuat(OrientationQuat, Sub3(worldPosMeters, PositionMeters));
    const float dChassis = SignedDistRoundedBox(local, ChassisCentreLocal, ChassisHalfExtents, FilletRadiusMeters);
    const float dCabin   = SignedDistRoundedBox(local, CabinCentreLocal,   CabinHalfExtents,   FilletRadiusMeters + 0.02f);
    float d = SmoothMin(dChassis, dCabin, 0.14f);
    if (RearWingAttached)
    {
        const float dWing = SignedDistRoundedBox(local, WingCentreLocal, WingHalfExtents, 0.03f);
        d = std::min(d, dWing);
    }
    return d;
}

std::array<float, 3> FluidVehicleSdfCollider::EvaluateOutwardNormal(const std::array<float, 3>& worldPosMeters) const noexcept
{
    constexpr float eps = 0.03f;
    const float dx = EvaluateSignedDistance({ worldPosMeters[0] + eps, worldPosMeters[1], worldPosMeters[2] })
                   - EvaluateSignedDistance({ worldPosMeters[0] - eps, worldPosMeters[1], worldPosMeters[2] });
    const float dy = EvaluateSignedDistance({ worldPosMeters[0], worldPosMeters[1] + eps, worldPosMeters[2] })
                   - EvaluateSignedDistance({ worldPosMeters[0], worldPosMeters[1] - eps, worldPosMeters[2] });
    const float dz = EvaluateSignedDistance({ worldPosMeters[0], worldPosMeters[1], worldPosMeters[2] + eps })
                   - EvaluateSignedDistance({ worldPosMeters[0], worldPosMeters[1], worldPosMeters[2] - eps });
    return Norm3({ dx, dy, dz }, { 0.0f, 1.0f, 0.0f });
}

std::array<float, 3> FluidVehicleSdfCollider::EvaluateSurfaceVelocity(const std::array<float, 3>& worldPosMeters) const noexcept
{
    const Vec3 r = Sub3(worldPosMeters, PositionMeters);
    return Add3(LinearVelocityMps, Cross3(AngularVelocityRadS, r));
}

void FluidVehicleSdfCollider::ExportGpuApproxSpheres(
    std::array<FluidColliderSphere, kMaximumFluidColliders>& outSpheres) const noexcept
{
    // Pack 4 world-space bounding spheres along the coupe spine (+X nose -> cabin -> tail) for Frontier's 160B GPU UBO
    const float spanX = ChassisHalfExtents[0] * 0.68f;
    const std::array<Vec3, 4u> localOffsets {{
        {  spanX,        0.16f, 0.0f },
        {  spanX * 0.25f, 0.44f, 0.0f },
        { -spanX * 0.35f, 0.44f, 0.0f },
        { -spanX,        0.24f, 0.0f },
    }};
    const std::array<float, 4u> radii { 0.78f, 0.90f, 0.90f, 0.78f };
    for (size_t i = 0u; i < kMaximumFluidColliders; ++i)
    {
        outSpheres[i].CentreMeters = Add3(PositionMeters, RotateQuat(OrientationQuat, localOffsets[i]));
        outSpheres[i].RadiusMeters = radii[i];
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                         VEHICLETIREFLUIDSMOKEBRIDGE
//------------------------------------------------------------------------------------------------------------------------

void VehicleTireFluidSmokeBridge::Initialize(
    const VehicleTireFluidSmokeConfig& config,
    const std::array<float, 3>&        initialFocusWorldMeters)
{
    Config_ = config;
    Config_.CellSizeMeters = std::clamp(Config_.CellSizeMeters, 0.08f, 0.75f);

    StepSettings_ = {};
    StepSettings_.Boundary            = FluidBoundaryOrdinal::OpenAll;
    StepSettings_.Advection           = FluidAdvectionOrdinal::MacCormack;
    StepSettings_.VelocityDissipation = Config_.VelocityDissipation;
    StepSettings_.Dissipation         = Config_.DensityDissipation;
    StepSettings_.Cooling             = Config_.Cooling;
    StepSettings_.Combustion          = 0.0f; // Pure rubber pyrolysis smoke (zero flame ignition)
    StepSettings_.AmbientTemperature  = 0.0f;
    StepSettings_.Buoyancy            = Config_.Buoyancy;
    StepSettings_.Gravity             = Config_.Gravity;
    StepSettings_.ForceResponse       = 1.0f;
    StepSettings_.Vorticity           = Config_.Vorticity;
    StepSettings_.Expansion           = 0.0f;
    StepSettings_.PressureIterations  = Config_.PressureIterations;

    if (Config_.UseWorldGridRelocation)
    {
        const uint32_t span  = std::clamp(Config_.WorldGridTileSpan, 1u, kMaximumGridTiles);
        const uint32_t tc    = std::clamp(Config_.WorldGridTileCells, 4u, 24u);
        const uint32_t dimXZ = std::min(span * tc, kMaximumFixedCells);
        // Exact dimY = 2 * tc so halfH == tileWorld: grid bottom rests cleanly on asphalt (Y = DomainFloorYOffsetM)
        // AND snappedCentre.Y is an exact integer multiple of tileWorld so RelocateFluidSimulationCpu never shifts Y!
        const uint32_t dimY  = std::min(2u * tc, kMaximumFixedCells);
        const float tileW    = static_cast<float>(tc) * Config_.CellSizeMeters;
        const Vec3 snappedCentre {
            std::round(initialFocusWorldMeters[0] / std::max(tileW, 1.0e-5f)) * tileW,
            std::round((Config_.DomainFloorYOffsetM + tileW) / std::max(tileW, 1.0e-5f)) * tileW,
            std::round(initialFocusWorldMeters[2] / std::max(tileW, 1.0e-5f)) * tileW
        };
        FluidState_.InitializeFixed({ dimXZ, dimY, dimXZ }, Config_.CellSizeMeters, snappedCentre);
        FluidState_.WorldTileCells = tc;
    }
    else
    {
        const float halfH = 0.5f * static_cast<float>(Config_.FixedGridCells[1]) * Config_.CellSizeMeters;
        const Vec3 centre {
            initialFocusWorldMeters[0],
            Config_.DomainFloorYOffsetM + halfH - Config_.CellSizeMeters,
            initialFocusWorldMeters[2]
        };
        FluidState_.InitializeFixed(Config_.FixedGridCells, Config_.CellSizeMeters, centre);
    }

    ActiveSdfs_.clear();
    ActiveEmitters_.clear();
    Telemetry_ = {};
    Telemetry_.GridCentreWorldMeters = FluidState_.TileBank.QueryGridCentre();
    Initialized_ = true;
}

void VehicleTireFluidSmokeBridge::Reset() noexcept
{
    if (!Initialized_) return;
    FluidState_.Velocity.Clear();
    FluidState_.State.Clear();
    FluidState_.Curl.Clear();
    FluidState_.Divergence.Clear();
    FluidState_.Pressure.Clear();
    FluidState_.SimulationTime = 0.0;
    FluidState_.StepCount = 0u;
    ActiveSdfs_.clear();
    ActiveEmitters_.clear();
    Telemetry_ = {};
    Telemetry_.GridCentreWorldMeters = FluidState_.TileBank.QueryGridCentre();
}

void VehicleTireFluidSmokeBridge::StepSingleVehicle(
    const PhysicsSpace::VehicleReading&          reading,
    const PhysicsSpace::VehicleRigSpecification& spec,
    const float                                  deltaTimeSeconds)
{
    const std::array<PhysicsSpace::VehicleReading, 1u>          readings { reading };
    const std::array<PhysicsSpace::VehicleRigSpecification, 1u> specs    { spec };
    StepSimulation(readings, specs, deltaTimeSeconds);
}

void VehicleTireFluidSmokeBridge::StepSimulation(
    const std::span<const PhysicsSpace::VehicleReading>          vehicleReadings,
    const std::span<const PhysicsSpace::VehicleRigSpecification> vehicleSpecs,
    const float                                                  deltaTimeSeconds)
{
    if (!Initialized_ || vehicleReadings.empty() || vehicleReadings.size() != vehicleSpecs.size())
    {
        return;
    }

    const float dt = std::clamp(deltaTimeSeconds, 1.0e-4f, 0.05f);

    // 1. Build exact moving composite SDFs for all live vehicles
    ActiveSdfs_.clear();
    ActiveSdfs_.reserve(vehicleReadings.size());
    for (size_t v = 0u; v < vehicleReadings.size(); ++v)
    {
        ActiveSdfs_.push_back(FluidVehicleSdfCollider::ConstructFromVehicleReading(vehicleReadings[v], vehicleSpecs[v]));
    }

    // 2. Global World-Space whole-cell grid relocation (tracks hero XZ in world-axis-aligned whole cells;
    //    NEVER rotates or drags fluid with the vehicle!).
    if (Config_.UseWorldGridRelocation && FluidState_.WorldTileCells > 0u)
    {
        const auto curCentre = FluidState_.TileBank.QueryGridCentre();
        const Vec3 heroTarget {
            vehicleReadings[0u].WorldPositionMeters.X,
            curCentre[1], // Lock vertical centre to asphalt floor plane
            vehicleReadings[0u].WorldPositionMeters.Z
        };
        if (RelocateFluidSimulationCpu(FluidState_, heroTarget, StepSettings_.Boundary, {}))
        {
            ++Telemetry_.GridRelocationCount;
        }
    }

    // 3. Evaluate physically driven tire smoke emitters (Burnout / Drift / Lockup Braking)
    ActiveEmitters_.clear();
    Telemetry_.HeroCorners = {};
    for (size_t v = 0u; v < vehicleReadings.size(); ++v)
    {
        EvaluateVehicleTireEmitters(vehicleReadings[v], vehicleSpecs[v], v == 0u);
    }

    // Keep up to kMaximumReleaseSources (6) strongest emitters matching Frontier's GPU FluidInjectCompute ABI
    if (ActiveEmitters_.size() > kMaximumReleaseSources)
    {
        std::sort(ActiveEmitters_.begin(), ActiveEmitters_.end(),
            [](const FluidReleaseSourceParameters& a, const FluidReleaseSourceParameters& b)
            {
                return a.Density > b.Density;
            });
        ActiveEmitters_.resize(kMaximumReleaseSources);
    }

    // 4. Add gentle cross-track ambient wind as a soft background velocity bias before Frontier's native fluid step
    const float windBlend = std::clamp(dt * 0.45f, 0.0f, 0.15f);
    if (Len3(Config_.AmbientWindMps) > 1.0e-4f)
    {
        for (auto& vel : FluidState_.Velocity.Cells)
        {
            vel[0] += (Config_.AmbientWindMps[0] - vel[0]) * windBlend * 0.15f;
            vel[2] += (Config_.AmbientWindMps[2] - vel[2]) * windBlend * 0.15f;
        }
    }

    // 5. Execute Frontier's native 3D Eulerian Navier-Stokes fluid solver step
    //    (Inject -> MacCormack Advect -> Thermodynamics -> Buoyancy/Gravity -> Curl/Vorticity -> Jacobi Projection)
    //    Pass empty sphere colliders here because step 6 applies our exact composite moving-vehicle SDF + wake!
    StepFluidSimulationCpu(
        FluidState_,
        StepSettings_,
        ActiveEmitters_,
        {},
        {},
        dt);

    // 6. Enforce Moving Composite Vehicle SDF no-penetration, mass-conserving expulsion, wake entrainment & ground floor
    const auto volU = FluidState_.TileBank.BuildVolumeUniforms(StepSettings_.Boundary, {});
    EnforceMovingVehicleSdfsAndGround(volU, dt);

    // 7. Publish diagnostics & world-space smoke telemetry
    ComputeTelemetry(volU);
}

void VehicleTireFluidSmokeBridge::EvaluateVehicleTireEmitters(
    const PhysicsSpace::VehicleReading&          reading,
    const PhysicsSpace::VehicleRigSpecification& spec,
    const bool                                   recordHeroTelemetry)
{
    const std::array<float, 4> q {
        reading.WorldOrientation.X, reading.WorldOrientation.Y, reading.WorldOrientation.Z, reading.WorldOrientation.W
    };
    const Vec3 chassisPos { reading.WorldPositionMeters.X, reading.WorldPositionMeters.Y, reading.WorldPositionMeters.Z };
    const Vec3 chassisVel { reading.LinearVelocityWorldMps.X, reading.LinearVelocityWorldMps.Y, reading.LinearVelocityWorldMps.Z };
    const Vec3 chassisAng { reading.AngularVelocityWorldRadS.X, reading.AngularVelocityWorldRadS.Y, reading.AngularVelocityWorldRadS.Z };
    const Vec3 bodyFwd    = RotateQuat(q, { 1.0f, 0.0f, 0.0f });
    const Vec3 bodyUp     = RotateQuat(q, { 0.0f, 1.0f, 0.0f });
    const Vec3 bodyRight  = RotateQuat(q, { 0.0f, 0.0f, 1.0f });

    for (uint32_t i = 0u; i < PhysicsSpace::MaxVehicleWheelCorners; ++i)
    {
        const auto& w = reading.Corners[i];
        const auto& cSpec = spec.Corners[i];
        TireSmokeCornerReport rep {};
        rep.TreadTempKelvin = w.TreadTempKelvin;

        if (!w.InContact || w.VerticalLoadNewtons < 150.0f)
        {
            if (recordHeroTelemetry) Telemetry_.HeroCorners[i] = rep;
            continue;
        }

        const Vec3 contactPt { w.ContactPointWorldM.X, w.ContactPointWorldM.Y, w.ContactPointWorldM.Z };
        const Vec3 contactN  = Norm3(bodyUp);

        // Steered wheel basis projected onto contact plane
        const float cs = std::cos(w.SteerAngleRad);
        const float sn = std::sin(w.SteerAngleRad);
        const Vec3 rawFwd = Add3(Scale3(bodyFwd, cs), Scale3(bodyRight, sn));
        const Vec3 wFwd   = Norm3(Sub3(rawFwd, Scale3(contactN, Dot3(rawFwd, contactN))), bodyFwd);
        const Vec3 wRight = Norm3(Cross3(wFwd, contactN), bodyRight);

        // Contact-patch ground velocity in world space (v_body + omega x r)
        const Vec3 rArm       = Sub3(contactPt, chassisPos);
        const Vec3 patchVel   = Add3(chassisVel, Cross3(chassisAng, rArm));
        const float vxGround  = Dot3(patchVel, wFwd);
        const float vyGround  = Dot3(patchVel, wRight);
        const float wheelSpd  = w.AngularVelocityRadS * cSpec.Tire.OuterRadiusMeters;

        // True slip velocities & frictional rubber dissipation power (Watts)
        const float vSlipLong = wheelSpd - vxGround; // >0 = spinning forward (burnout), <0 = locked braking
        const float vSlipLat  = vyGround;            // lateral tire scrub (drift)
        const float vSlipMag  = std::sqrt(vSlipLong * vSlipLong + vSlipLat * vSlipLat);
        const float slipPower = std::fabs(w.LongitudinalForceN * vSlipLong)
                              + std::fabs(w.LateralForceN * vSlipLat);

        rep.CombinedSlipSpeedMps = vSlipMag;
        rep.SlipPowerWatts       = slipPower;

        // Classify physical tire smoke regime (a throttle powerslide with high lateral slip angle IS a Drift!)
        const float absKappa = std::fabs(w.LongitudinalSlip);
        const float absAlpha = std::fabs(w.SlipAngleRad);
        const bool isDrift   = (absAlpha > 0.14f && std::fabs(vSlipLat) > Config_.MinSlipSpeedMps * 0.90f);
        const bool isBurnout = (w.LongitudinalSlip > 0.22f && vSlipLong > Config_.MinSlipSpeedMps);
        const bool isLockup  = (w.LongitudinalSlip < -0.20f && w.BrakeTorqueNm > 700.0f && std::fabs(vSlipLong) > Config_.MinSlipSpeedMps);

        if (isDrift)
        {
            rep.Regime = TireSmokeRegime::Drift;
        }
        else if (isBurnout)
        {
            rep.Regime = TireSmokeRegime::Burnout;
        }
        else if (isLockup)
        {
            rep.Regime = TireSmokeRegime::BrakeLockup;
        }
        else if (vSlipMag > Config_.MinSlipSpeedMps * 1.25f && slipPower > Config_.MinSlipPowerWatts)
        {
            rep.Regime = (absKappa >= absAlpha) ? TireSmokeRegime::Burnout : TireSmokeRegime::Drift;
        }

        if (rep.Regime == TireSmokeRegime::None)
        {
            if (recordHeroTelemetry) Telemetry_.HeroCorners[i] = rep;
            continue;
        }

        // Smooth physical intensity combining slip speed, friction power, load, and rubber pyrolysis temperature
        const float slipFactor  = SmoothStep(Config_.MinSlipSpeedMps * 0.7f, Config_.MinSlipSpeedMps * 4.5f, vSlipMag);
        const float powerFactor = SmoothStep(Config_.MinSlipPowerWatts, Config_.FullSmokePowerWatts, slipPower);
        const float loadFactor  = SmoothStep(200.0f, 2200.0f, w.VerticalLoadNewtons);
        const float thermalRamp = SmoothStep(Config_.PyrolysisBaseKelvin, Config_.PyrolysisFullKelvin, w.TreadTempKelvin);
        // High slip power produces immediate pyrolysis smoke even on cooler tires, thickening dramatically as rubber heats
        const float thermalFactor = std::clamp(0.42f + 0.58f * thermalRamp, 0.35f, 1.0f);
        const float intensity = std::clamp(
            (0.45f * slipFactor + 0.55f * powerFactor) * loadFactor * thermalFactor,
            0.0f,
            1.0f);

        rep.EmissionIntensity = intensity;
        if (intensity < 0.04f)
        {
            rep.Regime = TireSmokeRegime::None;
            if (recordHeroTelemetry) Telemetry_.HeroCorners[i] = rep;
            continue;
        }

        // Emit just behind/outside the contact patch so smoke detaches cleanly into world space (NOT inside wheel SDF)
        const float sideSign = cSpec.LeftSide ? -1.0f : 1.0f;
        const Vec3 emitPos = Add3(
            contactPt,
            Add3(
                Scale3(contactN, 0.24f),
                Add3(Scale3(wFwd, -0.26f), Scale3(wRight, sideSign * 0.14f))));

        // Realistic world-space smoke ejection velocity:
        // Spinning tire kicks rubber plume opposite to tread direction; sliding tire shears outward into world air
        // with gentle upward thermal lift (NOT rigidly inheriting chassis velocity!).
        const Vec3 treadKick = Scale3(wFwd, -std::clamp(vSlipLong * 0.32f, -4.5f, 6.5f));
        const Vec3 latKick   = Scale3(wRight, std::clamp(-vSlipLat * 0.24f + sideSign * (0.85f + 1.1f * intensity), -5.0f, 5.0f));
        const Vec3 upLift    = Scale3(contactN, 1.25f + 1.85f * intensity);
        const Vec3 ejectVel  = Add3(Add3(treadKick, latKick), upLift);

        rep.EmitterWorldMeters  = emitPos;
        rep.EjectionVelocityMps = ejectVel;
        if (recordHeroTelemetry) Telemetry_.HeroCorners[i] = rep;

        FluidReleaseSourceParameters src {};
        src.SourceId       = (i == 0u) ? "tire_fl" : ((i == 1u) ? "tire_fr" : ((i == 2u) ? "tire_rl" : "tire_rr"));
        src.Enabled        = true;
        src.Shape          = ReleaseSourceShapeOrdinal::Disc;
        src.PositionMeters = emitPos;
        src.RadiusMeters   = std::max(Config_.CellSizeMeters * 1.55f, 0.42f + 0.18f * intensity);
        src.VelocityMps    = ejectVel;
        src.Orientation    = contactN;
        // Inject dense, billowing rubber smoke (density ~ 2.2..6.2, buoyant pyrolysis warmth ~ 1.1..2.6, zero fuel/fire)
        src.Density        = 2.2f + 4.0f * intensity;
        src.Temperature    = 1.10f + 1.65f * intensity * thermalFactor;
        src.Fuel           = 0.0f;
        src.Jitter         = 0.32f;
        src.EnvelopeWeight = 1.0f;
        ActiveEmitters_.push_back(src);
    }
}

void VehicleTireFluidSmokeBridge::EnforceMovingVehicleSdfsAndGround(
    const FluidVolumeUniformBlock& volume,
    const float                    deltaTimeSeconds)
{
    const uint32_t W = FluidState_.State.Width;
    const uint32_t H = FluidState_.State.Height;
    const uint32_t D = FluidState_.State.Depth;
    const float cellSize = std::max(volume.GridCentreCellSize[3], 1.0e-4f);
    const float shellThickness = cellSize * 1.85f;

    uint32_t sdfCollisionCount = 0u;

    // 1. Solid asphalt floor boundary (only bottom 2 Y layers need checking!)
    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < std::min(H, 3u); ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const int32_t ix = static_cast<int32_t>(x);
                const int32_t iy = static_cast<int32_t>(y);
                const int32_t iz = static_cast<int32_t>(z);
                const size_t idx = FluidState_.State.LinearIndex(ix, iy, iz);
                const Vec3 worldPos = WorldFromFluidCell(volume, {
                    static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f,
                    static_cast<float>(z) + 0.5f
                });
                if (worldPos[1] <= Config_.DomainFloorYOffsetM + 0.25f * cellSize)
                {
                    if (worldPos[1] < Config_.DomainFloorYOffsetM - 0.10f * cellSize)
                    {
                        if (iy + 1 < static_cast<int32_t>(H) && FluidState_.State.Cells[idx][0] > 1.0e-4f)
                        {
                            const size_t upIdx = FluidState_.State.LinearIndex(ix, iy + 1, iz);
                            FluidState_.State.Cells[upIdx][0] += FluidState_.State.Cells[idx][0] * 0.85f;
                            FluidState_.State.Cells[upIdx][1] = std::max(FluidState_.State.Cells[upIdx][1], FluidState_.State.Cells[idx][1]);
                        }
                        FluidState_.State.Cells[idx]    = { 0.0f, 0.0f, 0.0f, 0.0f };
                        FluidState_.Velocity.Cells[idx] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    }
                    else
                    {
                        auto& vel = FluidState_.Velocity.Cells[idx];
                        if (vel[1] < 0.0f) vel[1] = 0.0f;
                    }
                }
            }
        }
    }

    // 2. Moving composite vehicle SDF collision & wake entrainment (tight AABB around each live vehicle!)
    const float boundPadMeters = 5.4f;
    for (const auto& sdf : ActiveSdfs_)
    {
        const auto cMin = FluidCellFromWorld(volume, { sdf.PositionMeters[0] - boundPadMeters, sdf.PositionMeters[1] - 1.5f, sdf.PositionMeters[2] - boundPadMeters });
        const auto cMax = FluidCellFromWorld(volume, { sdf.PositionMeters[0] + boundPadMeters, sdf.PositionMeters[1] + 2.6f, sdf.PositionMeters[2] + boundPadMeters });
        const int32_t x0 = std::clamp(static_cast<int32_t>(std::floor(cMin[0])), 0, static_cast<int32_t>(W) - 1);
        const int32_t y0 = std::clamp(static_cast<int32_t>(std::floor(cMin[1])), 0, static_cast<int32_t>(H) - 1);
        const int32_t z0 = std::clamp(static_cast<int32_t>(std::floor(cMin[2])), 0, static_cast<int32_t>(D) - 1);
        const int32_t x1 = std::clamp(static_cast<int32_t>(std::ceil(cMax[0])),  0, static_cast<int32_t>(W) - 1);
        const int32_t y1 = std::clamp(static_cast<int32_t>(std::ceil(cMax[1])),  0, static_cast<int32_t>(H) - 1);
        const int32_t z1 = std::clamp(static_cast<int32_t>(std::ceil(cMax[2])),  0, static_cast<int32_t>(D) - 1);

        // Pass A: Expel interior smoke outward onto guaranteed exterior cells & deflect boundary shell/wake velocities
        for (int32_t iz = z0; iz <= z1; ++iz)
        {
            for (int32_t iy = y0; iy <= y1; ++iy)
            {
                for (int32_t ix = x0; ix <= x1; ++ix)
                {
                    const size_t idx = FluidState_.State.LinearIndex(ix, iy, iz);
                    const Vec3 worldPos = WorldFromFluidCell(volume, {
                        static_cast<float>(ix) + 0.5f,
                        static_cast<float>(iy) + 0.5f,
                        static_cast<float>(iz) + 0.5f
                    });

                    const float d = sdf.EvaluateSignedDistance(worldPos);
                    if (d < 0.0f)
                    {
                        ++sdfCollisionCount;
                        const Vec3 n    = sdf.EvaluateOutwardNormal(worldPos);
                        const Vec3 vSol = sdf.EvaluateSurfaceVelocity(worldPos);

                        const float rho  = FluidState_.State.Cells[idx][0];
                        const float temp = FluidState_.State.Cells[idx][1];
                        if (rho > 1.0e-4f)
                        {
                            const float pushDist = (-d) + cellSize * 1.45f;
                            const Vec3 outWorld  = Add3(worldPos, Scale3(n, pushDist));
                            if (sdf.EvaluateSignedDistance(outWorld) >= 0.02f)
                            {
                                const auto outCell   = FluidCellFromWorld(volume, outWorld);
                                const int32_t ox = std::clamp(static_cast<int32_t>(std::floor(outCell[0])), 0, static_cast<int32_t>(W) - 1);
                                const int32_t oy = std::clamp(static_cast<int32_t>(std::floor(outCell[1])), 0, static_cast<int32_t>(H) - 1);
                                const int32_t oz = std::clamp(static_cast<int32_t>(std::floor(outCell[2])), 0, static_cast<int32_t>(D) - 1);
                                if (ox != ix || oy != iy || oz != iz)
                                {
                                    const size_t oIdx = FluidState_.State.LinearIndex(ox, oy, oz);
                                    FluidState_.State.Cells[oIdx][0] = std::min(FluidState_.State.Cells[oIdx][0] + rho * 0.92f, 8.0f);
                                    FluidState_.State.Cells[oIdx][1] = std::max(FluidState_.State.Cells[oIdx][1], temp);
                                    auto& oVel = FluidState_.Velocity.Cells[oIdx];
                                    const float pushSpd = std::max(1.2f, Dot3(vSol, n));
                                    oVel[0] += n[0] * pushSpd * 0.55f + vSol[0] * 0.25f;
                                    oVel[1] += n[1] * pushSpd * 0.55f + vSol[1] * 0.25f + 0.35f;
                                    oVel[2] += n[2] * pushSpd * 0.55f + vSol[2] * 0.25f;
                                }
                            }
                        }

                        FluidState_.State.Cells[idx]    = { 0.0f, 0.0f, 0.0f, 0.0f };
                        FluidState_.Velocity.Cells[idx] = { vSol[0], vSol[1], vSol[2], 0.0f };
                    }
                    else if (d < shellThickness)
                    {
                        ++sdfCollisionCount;
                        const Vec3 n    = sdf.EvaluateOutwardNormal(worldPos);
                        const Vec3 vSol = sdf.EvaluateSurfaceVelocity(worldPos);
                        auto& vel = FluidState_.Velocity.Cells[idx];
                        const Vec3 vFlu { vel[0], vel[1], vel[2] };
                        const Vec3 vRel = Sub3(vFlu, vSol);
                        const float vn  = Dot3(vRel, n);

                        Vec3 vNew = vFlu;
                        if (vn < 0.0f)
                        {
                            vNew = Sub3(vNew, Scale3(n, vn * 1.15f));
                        }
                        const float proximity = 1.0f - (d / shellThickness);
                        const float entrain   = std::clamp(proximity * Config_.WakeEntrainmentGain * deltaTimeSeconds * 18.0f, 0.0f, 0.45f);
                        vNew = Add3(Scale3(vNew, 1.0f - entrain), Scale3(vSol, entrain));
                        vel[0] = vNew[0];
                        vel[1] = vNew[1];
                        vel[2] = vNew[2];
                    }
                    else
                    {
                        const float carSpeed = Len3(sdf.LinearVelocityMps);
                        if (carSpeed > 2.5f)
                        {
                            const Vec3 rel   = Sub3(worldPos, sdf.PositionMeters);
                            const Vec3 local = RotateInvQuat(sdf.OrientationQuat, rel);
                            if (local[0] < -sdf.ChassisHalfExtents[0] + 0.2f && local[0] > -sdf.ChassisHalfExtents[0] - 2.6f
                                && std::fabs(local[2]) < sdf.ChassisHalfExtents[2] * 1.35f
                                && local[1] > -0.15f && local[1] < 1.55f)
                            {
                                const float wakeFalloff = SmoothStep(-sdf.ChassisHalfExtents[0] - 2.6f, -sdf.ChassisHalfExtents[0], local[0]);
                                const float swirlMag = std::min(carSpeed * 0.16f, 3.2f) * wakeFalloff * Config_.WakeEntrainmentGain;
                                const float sideSign = (local[2] >= 0.0f) ? 1.0f : -1.0f;
                                const Vec3 localWakeVel {
                                    0.35f * swirlMag,
                                    (0.55f - 0.35f * std::fabs(local[2])) * swirlMag,
                                    -sideSign * (local[1] - 0.55f) * swirlMag * 0.65f
                                };
                                const Vec3 worldWake = RotateQuat(sdf.OrientationQuat, localWakeVel);
                                auto& vel = FluidState_.Velocity.Cells[idx];
                                const float blend = std::clamp(deltaTimeSeconds * 5.0f, 0.0f, 0.25f);
                                vel[0] += worldWake[0] * blend;
                                vel[1] += worldWake[1] * blend;
                                vel[2] += worldWake[2] * blend;
                            }
                        }
                    }
                }
            }
        }
    }

    // Pass B (after ALL vehicles complete Pass A): watertight multi-vehicle interior guarantee (0 leaks mathematically!)
    for (const auto& sdf : ActiveSdfs_)
    {
        const auto cMin = FluidCellFromWorld(volume, { sdf.PositionMeters[0] - boundPadMeters, sdf.PositionMeters[1] - 1.5f, sdf.PositionMeters[2] - boundPadMeters });
        const auto cMax = FluidCellFromWorld(volume, { sdf.PositionMeters[0] + boundPadMeters, sdf.PositionMeters[1] + 2.6f, sdf.PositionMeters[2] + boundPadMeters });
        const int32_t x0 = std::clamp(static_cast<int32_t>(std::floor(cMin[0])), 0, static_cast<int32_t>(W) - 1);
        const int32_t y0 = std::clamp(static_cast<int32_t>(std::floor(cMin[1])), 0, static_cast<int32_t>(H) - 1);
        const int32_t z0 = std::clamp(static_cast<int32_t>(std::floor(cMin[2])), 0, static_cast<int32_t>(D) - 1);
        const int32_t x1 = std::clamp(static_cast<int32_t>(std::ceil(cMax[0])),  0, static_cast<int32_t>(W) - 1);
        const int32_t y1 = std::clamp(static_cast<int32_t>(std::ceil(cMax[1])),  0, static_cast<int32_t>(H) - 1);
        const int32_t z1 = std::clamp(static_cast<int32_t>(std::ceil(cMax[2])),  0, static_cast<int32_t>(D) - 1);
        for (int32_t iz = z0; iz <= z1; ++iz)
            for (int32_t iy = y0; iy <= y1; ++iy)
                for (int32_t ix = x0; ix <= x1; ++ix)
                {
                    const size_t idx = FluidState_.State.LinearIndex(ix, iy, iz);
                    if (FluidState_.State.Cells[idx][0] <= 1.0e-5f) continue;
                    const Vec3 wp = WorldFromFluidCell(volume, {
                        static_cast<float>(ix) + 0.5f, static_cast<float>(iy) + 0.5f, static_cast<float>(iz) + 0.5f
                    });
                    if (sdf.EvaluateSignedDistance(wp) < 0.0f)
                    {
                        FluidState_.State.Cells[idx] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    }
                }
    }

    Telemetry_.SdfCollisionVoxels = sdfCollisionCount;
}

void VehicleTireFluidSmokeBridge::ComputeTelemetry(const FluidVolumeUniformBlock& volume) noexcept
{
    const uint32_t W = FluidState_.State.Width;
    const uint32_t H = FluidState_.State.Height;
    const uint32_t D = FluidState_.State.Depth;

    uint32_t activeVoxels = 0u;
    uint32_t leakVoxels   = 0u;
    double   massSum      = 0.0;
    double   cx = 0.0, cy = 0.0, cz = 0.0;
    float    peakRho      = 0.0f;
    float    maxSpeed     = 0.0f;

    for (uint32_t z = 0u; z < D; ++z)
    {
        for (uint32_t y = 0u; y < H; ++y)
        {
            for (uint32_t x = 0u; x < W; ++x)
            {
                const size_t idx = FluidState_.State.LinearIndex(
                    static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z));
                const float rho = FluidState_.State.Cells[idx][0];
                const auto& vel = FluidState_.Velocity.Cells[idx];
                const float spd = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
                maxSpeed = std::max(maxSpeed, spd);

                if (rho > 0.02f)
                {
                    ++activeVoxels;
                    peakRho = std::max(peakRho, rho);
                    const Vec3 wp = WorldFromFluidCell(volume, {
                        static_cast<float>(x) + 0.5f,
                        static_cast<float>(y) + 0.5f,
                        static_cast<float>(z) + 0.5f
                    });
                    massSum += static_cast<double>(rho);
                    cx += static_cast<double>(rho) * static_cast<double>(wp[0]);
                    cy += static_cast<double>(rho) * static_cast<double>(wp[1]);
                    cz += static_cast<double>(rho) * static_cast<double>(wp[2]);

                    for (const auto& sdf : ActiveSdfs_)
                    {
                        if (sdf.EvaluateSignedDistance(wp) < -0.02f)
                        {
                            ++leakVoxels;
                            break;
                        }
                    }
                }
            }
        }
    }

    Telemetry_.StepCount             = FluidState_.StepCount;
    Telemetry_.ActiveEmitterCount    = static_cast<uint32_t>(ActiveEmitters_.size());
    Telemetry_.ActiveSmokeVoxels     = activeVoxels;
    Telemetry_.SdfInteriorLeakVoxels = leakVoxels;
    Telemetry_.TotalSmokeMass        = static_cast<float>(massSum);
    Telemetry_.PeakSmokeDensity      = peakRho;
    Telemetry_.MaxFluidSpeedMps      = maxSpeed;
    Telemetry_.GridCentreWorldMeters = FluidState_.TileBank.QueryGridCentre();
    if (massSum > 1.0e-5)
    {
        Telemetry_.SmokeCentroidWorldMeters = {
            static_cast<float>(cx / massSum),
            static_cast<float>(cy / massSum),
            static_cast<float>(cz / massSum)
        };
    }
}

std::array<float, 2> VehicleTireFluidSmokeBridge::SampleSmokeDensityTempWorld(
    const std::array<float, 3>& worldPosMeters) const noexcept
{
    if (!Initialized_) return { 0.0f, 0.0f };
    const auto volU = FluidState_.TileBank.BuildVolumeUniforms(StepSettings_.Boundary, {});
    const auto cell = FluidCellFromWorld(volU, worldPosMeters);
    if (cell[0] < 0.0f || cell[1] < 0.0f || cell[2] < 0.0f
        || cell[0] >= static_cast<float>(FluidState_.State.Width)
        || cell[1] >= static_cast<float>(FluidState_.State.Height)
        || cell[2] >= static_cast<float>(FluidState_.State.Depth))
    {
        return { 0.0f, 0.0f };
    }
    const auto st = FluidState_.State.SampleTrilinear(cell[0], cell[1], cell[2]);
    return { std::max(0.0f, st[0]), st[1] };
}

std::array<float, 3> VehicleTireFluidSmokeBridge::SampleVelocityWorld(
    const std::array<float, 3>& worldPosMeters) const noexcept
{
    if (!Initialized_) return { 0.0f, 0.0f, 0.0f };
    const auto volU = FluidState_.TileBank.BuildVolumeUniforms(StepSettings_.Boundary, {});
    const auto cell = FluidCellFromWorld(volU, worldPosMeters);
    if (cell[0] < 0.0f || cell[1] < 0.0f || cell[2] < 0.0f
        || cell[0] >= static_cast<float>(FluidState_.Velocity.Width)
        || cell[1] >= static_cast<float>(FluidState_.Velocity.Height)
        || cell[2] >= static_cast<float>(FluidState_.Velocity.Depth))
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const auto v = FluidState_.Velocity.SampleTrilinear(cell[0], cell[1], cell[2]);
    return { v[0], v[1], v[2] };
}

} // namespace Frontier::SimulationSpace::Fluid
