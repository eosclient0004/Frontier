/*============================================================================================================================================
                                                      BOTANICALWORLDPARTITION.CPP
============================================================================================================================================*/
// 🧩 Implementation of Spatial WorldPartition (16m x 16m C1 GPU <-> Deterministic 120 Hz CPU Cells + 4m x 4m 3-State Falling/Sleeping Leaves).

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalWorldPartition.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometrySpace::Botanical
{

namespace
{

constexpr float kTau = 6.28318530717958647692f;

[[nodiscard]] inline float HermiteSmooth(float x) noexcept
{
    const float t = std::clamp(x, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] inline float Hypot2(float x, float y) noexcept
{
    return std::sqrt(x * x + y * y);
}

} // namespace

uint64_t BotanicalWorldPartition::PackCellKey(const int32_t cx, const int32_t cy) noexcept
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32u) | static_cast<uint64_t>(static_cast<uint32_t>(cy));
}

uint32_t BotanicalWorldPartition::RegisterPlant(const BotanicalPlantInstance& instance)
{
    const uint32_t idx = static_cast<uint32_t>(Plants_.size());
    BotanicalPlantInstance copy = instance;
    if (copy.EntityId == 0u) copy.EntityId = idx + 1u;
    Plants_.push_back(copy);

    const int32_t cx = static_cast<int32_t>(std::floor(copy.WorldPosMeters[0] / Config_.CellSizeMeters));
    const int32_t cy = static_cast<int32_t>(std::floor(copy.WorldPosMeters[1] / Config_.CellSizeMeters));
    const uint64_t key = PackCellKey(cx, cy);
    auto& cell = Cells_[key];
    cell.CellX = cx;
    cell.CellY = cy;
    cell.PlantIndices.push_back(idx);
    return idx;
}

void BotanicalWorldPartition::SpawnFallingLeaf(const FallingLeafParticle& leaf)
{
    // Enforce hard Active particle budget cap (<= 8,192): recycle oldest active leaf if saturated!
    uint32_t activeCount = 0u;
    int oldestActiveIdx = -1;
    for (std::size_t i = 0u; i < Leaves_.size(); ++i)
    {
        if (Leaves_[i].State == FallingLeafState::Active)
        {
            ++activeCount;
            if (oldestActiveIdx < 0) oldestActiveIdx = static_cast<int>(i);
        }
    }

    FallingLeafParticle p = leaf;
    p.Id = NextLeafId_++;
    p.State = FallingLeafState::Active;
    p.RestTimerSec = 0.0f;

    if (activeCount >= Config_.MaxActiveLeaves && oldestActiveIdx >= 0)
    {
        Leaves_[static_cast<std::size_t>(oldestActiveIdx)] = p;
    }
    else
    {
        Leaves_.push_back(p);
    }
}

std::array<float, 3> BotanicalWorldPartition::SampleWindAt(
    const std::array<float, 3>&    pos,
    const float                    timeSec,
    const BotanicalWindFieldState& wind) const noexcept
{
    // Travelling coherent spatial gust wave (matches BotanicalSdfWindDeform.slang!)
    const float phase = pos[0] * 0.14f + pos[1] * 0.11f - timeSec * wind.GustFrequencyHz * kTau;
    const float gust  = 0.5f + 0.5f * std::sin(phase) + 0.25f * std::sin(phase * 1.93f + 1.1f);
    const float wLen  = std::max(0.001f, Hypot2(wind.WindVelocityMps[0], wind.WindVelocityMps[1]));
    const float dirX  = wind.WindVelocityMps[0] / wLen;
    const float dirY  = wind.WindVelocityMps[1] / wLen;

    std::array<float, 3> out {
        wind.WindVelocityMps[0] + dirX * wind.GustStrengthMps * gust,
        wind.WindVelocityMps[1] + dirY * wind.GustStrengthMps * gust,
        wind.WindVelocityMps[2] + 0.15f * wind.GustStrengthMps * std::sin(phase * 1.3f)
    };

    if (wind.HasWakeImpulse && wind.WakeStrengthMps > 1.0e-3f)
    {
        const float dx = pos[0] - wind.WakeCenterMeters[0];
        const float dy = pos[1] - wind.WakeCenterMeters[1];
        const float dist = Hypot2(dx, dy);
        if (dist < wind.WakeRadiusMeters)
        {
            const float atten = 1.0f - (dist / std::max(wind.WakeRadiusMeters, 0.1f));
            const float boost = wind.WakeStrengthMps * atten * atten;
            // Swirl + upward vortex lift so sleeping leaves visibly lift off the ground!
            const float invD = 1.0f / std::max(dist, 0.25f);
            out[0] += boost * (wind.WakeDirNorm[0] * 0.75f - (dy * invD) * 0.55f);
            out[1] += boost * (wind.WakeDirNorm[1] * 0.75f + (dx * invD) * 0.55f);
            out[2] += boost * 0.62f;
        }
    }
    return out;
}

void BotanicalWorldPartition::EvaluateGpuWindPose(
    const BotanicalPlantInstance&  plant,
    const float                    timeSec,
    const BotanicalWindFieldState& windField,
    float&                         outAngleX,
    float&                         outAngleY,
    float&                         outVelX,
    float&                         outVelY) const noexcept
{
    auto evalAt = [&](float t) -> std::array<float, 2>
    {
        const auto w = SampleWindAt(plant.WorldPosMeters, t, windField);
        const float speed = Hypot2(w[0], w[1]);
        const float dragCoeff = 0.011f * plant.WindFlexibility / std::max(0.25f, std::sqrt(plant.StiffnessNmPerRad * 0.08f));
        const float osc = 1.0f + 0.22f * std::sin(t * 2.4f + plant.PhaseOffset * kTau);
        const float ax = std::clamp(w[0] * speed * dragCoeff * osc * 0.12f, -0.42f, 0.42f);
        const float ay = std::clamp(w[1] * speed * dragCoeff * osc * 0.12f, -0.42f, 0.42f);
        return { ax, ay };
    };

    const auto cur = evalAt(timeSec);
    constexpr float kEps = 0.01f;
    const auto nxt = evalAt(timeSec + kEps);
    outAngleX = cur[0];
    outAngleY = cur[1];
    outVelX   = (nxt[0] - cur[0]) / kEps;
    outVelY   = (nxt[1] - cur[1]) / kEps;
}

void BotanicalWorldPartition::Step(
    const float                                dtSec,
    const BotanicalWindFieldState&             windField,
    const std::vector<BotanicalObserverProxy>& observers)
{
    if (dtSec <= 0.0f) return;
    const float prevTime = SimTimeSec_;
    SimTimeSec_ += dtSec;

    // 1. Update 16m x 16m Cell States with spatial hysteresis & C1 Hermite transition ramps!
    for (auto& [key, cell] : Cells_)
    {
        const float cellCenterX = (static_cast<float>(cell.CellX) + 0.5f) * Config_.CellSizeMeters;
        const float cellCenterY = (static_cast<float>(cell.CellY) + 0.5f) * Config_.CellSizeMeters;

        bool forceAuth = false;
        for (const uint32_t idx : cell.PlantIndices)
        {
            if (Plants_[idx].ForceNetworkAuth) { forceAuth = true; break; }
        }

        float minObsDist = 1.0e6f;
        for (const auto& obs : observers)
        {
            const float d = Hypot2(obs.PositionMeters[0] - cellCenterX, obs.PositionMeters[1] - cellCenterY);
            minObsDist = std::min(minObsDist, d);
        }

        const bool wantCpu = forceAuth || (minObsDist <= Config_.PromoteRadiusMeters);
        const bool wantGpu = !forceAuth && (minObsDist >= Config_.DemoteRadiusMeters);

        if (cell.State == BotanicalCellSimState::GpuOnly && wantCpu)
        {
            // Seed deterministic CPU cantilever state with exact GPU pose + velocity -> ZERO SNAP!
            for (const uint32_t idx : cell.PlantIndices)
            {
                auto& p = Plants_[idx];
                float gx = 0.0f, gy = 0.0f, gvx = 0.0f, gvy = 0.0f;
                EvaluateGpuWindPose(p, prevTime, windField, gx, gy, gvx, gvy);
                p.CpuAngleX = gx;  p.CpuAngleY = gy;
                p.CpuVelX   = gvx; p.CpuVelY   = gvy;
            }
            cell.State = BotanicalCellSimState::TransitioningToCpu;
            cell.TransitionTimer = 0.0f;
        }
        else if (cell.State == BotanicalCellSimState::DeterministicCpu && wantGpu)
        {
            cell.State = BotanicalCellSimState::TransitioningToGpu;
            cell.TransitionTimer = 0.0f;
        }

        if (cell.State == BotanicalCellSimState::TransitioningToCpu)
        {
            cell.TransitionTimer += dtSec;
            const float u = cell.TransitionTimer / std::max(0.05f, Config_.PromoteBlendSec);
            cell.CpuWeight = HermiteSmooth(u);
            if (u >= 1.0f)
            {
                cell.CpuWeight = 1.0f;
                cell.State = BotanicalCellSimState::DeterministicCpu;
            }
        }
        else if (cell.State == BotanicalCellSimState::TransitioningToGpu)
        {
            if (wantCpu)
            {
                // Reverse smoothly without discontinuity!
                cell.State = BotanicalCellSimState::TransitioningToCpu;
                cell.TransitionTimer = cell.CpuWeight * Config_.PromoteBlendSec;
            }
            else
            {
                cell.TransitionTimer += dtSec;
                const float u = cell.TransitionTimer / std::max(0.05f, Config_.DemoteBlendSec);
                cell.CpuWeight = 1.0f - HermiteSmooth(u);
                if (u >= 1.0f)
                {
                    cell.CpuWeight = 0.0f;
                    cell.State = BotanicalCellSimState::GpuOnly;
                }
            }
        }
        else if (cell.State == BotanicalCellSimState::GpuOnly)
        {
            cell.CpuWeight = 0.0f;
        }
        else
        {
            cell.CpuWeight = 1.0f;
        }
    }

    // 2. Deterministic 120 Hz Fixed-Step CPU Cantilever Solver for Active & Transitioning Cells
    CpuAccumSec_ += dtSec;
    const float h = Config_.CpuFixedStepSec;
    uint32_t substepsThisFrame = 0u;
    while (CpuAccumSec_ >= h && substepsThisFrame < 16u)
    {
        CpuAccumSec_ -= h;
        ++substepsThisFrame;
        ++CpuSubsteps_;
        const float subTime = prevTime + static_cast<float>(substepsThisFrame) * h;

        for (const auto& [key, cell] : Cells_)
        {
            if (cell.State == BotanicalCellSimState::GpuOnly) continue;

            for (const uint32_t idx : cell.PlantIndices)
            {
                auto& p = Plants_[idx];
                // Compute target wind equilibrium + observer body contact brush deflection!
                float gx = 0.0f, gy = 0.0f, gvx = 0.0f, gvy = 0.0f;
                EvaluateGpuWindPose(p, subTime, windField, gx, gy, gvx, gvy);

                float contactTorqueX = 0.0f;
                float contactTorqueY = 0.0f;
                for (const auto& obs : observers)
                {
                    const float dx = p.WorldPosMeters[0] - obs.PositionMeters[0];
                    const float dy = p.WorldPosMeters[1] - obs.PositionMeters[1];
                    const float dist = Hypot2(dx, dy);
                    const float reach = obs.RadiusMeters + std::max(0.45f, p.TrunkRadiusMeters * 2.5f + p.HeightMeters * 0.18f);
                    if (dist < reach && dist > 1.0e-4f)
                    {
                        const float pen = (reach - dist) / reach;
                        const float push = pen * (obs.IsVehicle ? 14.0f : 7.5f) * p.WindFlexibility;
                        contactTorqueX += (dx / dist) * push;
                        contactTorqueY += (dy / dist) * push;
                    }
                }

                // Exact critically-damped cantilever spring ODE: d2theta/dt2 = omega^2*(target - theta) - 2*zeta*omega*dtheta/dt + contact
                const float omega = std::clamp(std::sqrt(p.StiffnessNmPerRad) * 0.95f, 1.8f, 9.5f);
                const float accX  = omega * omega * (gx - p.CpuAngleX) - 2.0f * p.DampingRatio * omega * p.CpuVelX + contactTorqueX;
                const float accY  = omega * omega * (gy - p.CpuAngleY) - 2.0f * p.DampingRatio * omega * p.CpuVelY + contactTorqueY;
                p.CpuVelX   += accX * h;
                p.CpuVelY   += accY * h;
                p.CpuAngleX  = std::clamp(p.CpuAngleX + p.CpuVelX * h, -0.55f, 0.55f);
                p.CpuAngleY  = std::clamp(p.CpuAngleY + p.CpuVelY * h, -0.55f, 0.55f);
            }
        }
    }

    // 3. Smooth C1 Blend Output Pose + Peak Gust Leaf Shedding
    for (const auto& [key, cell] : Cells_)
    {
        for (const uint32_t idx : cell.PlantIndices)
        {
            auto& p = Plants_[idx];
            const float prevAX = p.BendAngleXRad;
            const float prevAY = p.BendAngleYRad;

            float gx = 0.0f, gy = 0.0f, gvx = 0.0f, gvy = 0.0f;
            EvaluateGpuWindPose(p, SimTimeSec_, windField, gx, gy, gvx, gvy);
            p.GpuAngleX = gx;
            p.GpuAngleY = gy;

            const float w = cell.CpuWeight;
            p.BendAngleXRad     = gx * (1.0f - w) + p.CpuAngleX * w;
            p.BendAngleYRad     = gy * (1.0f - w) + p.CpuAngleY * w;
            p.BendVelXRadPerSec = gvx * (1.0f - w) + p.CpuVelX * w;
            p.BendVelYRadPerSec = gvy * (1.0f - w) + p.CpuVelY * w;

            const float delta = Hypot2(p.BendAngleXRad - prevAX, p.BendAngleYRad - prevAY);
            if (prevTime > 0.01f)
            {
                p.MaxFrameDeltaRad = std::max(p.MaxFrameDeltaRad, delta);
            }

            // Abscission: shed leaves during strong gusts (especially Oak & Palm canopies)
            p.ShedCooldownSec = std::max(0.0f, p.ShedCooldownSec - dtSec);
            const auto localWind = SampleWindAt(p.WorldPosMeters, SimTimeSec_, windField);
            const float windSpd  = Hypot2(localWind[0], localWind[1]);
            const float aeroForceN = windSpd * windSpd * 0.24f;
            if (aeroForceN > p.AbscissionThreshN && p.ShedCooldownSec <= 0.0f && p.GrowthForm != BotanicalGrowthForm::Fern)
            {
                p.ShedCooldownSec = 0.35f;
                ++p.DetachedLeafCount;
                const float ang = static_cast<float>(p.DetachedLeafCount) * 2.39996f;
                const float rOff = p.HeightMeters * 0.24f;
                FallingLeafParticle lf {};
                lf.SourceEntityId = p.EntityId;
                lf.PositionMeters = {
                    p.WorldPosMeters[0] + std::cos(ang) * rOff,
                    p.WorldPosMeters[1] + std::sin(ang) * rOff,
                    p.WorldPosMeters[2] + p.HeightMeters * (0.62f + 0.22f * std::sin(ang))
                };
                lf.VelocityMps = { localWind[0] * 0.35f, localWind[1] * 0.35f, -0.35f };
                lf.ColorLinear = (p.GrowthForm == BotanicalGrowthForm::Tree)
                    ? std::array<float, 3>{ 0.28f, 0.34f, 0.08f }
                    : std::array<float, 3>{ 0.22f, 0.36f, 0.09f };
                lf.HalfSpanMeters = (p.GrowthForm == BotanicalGrowthForm::Tree) ? 0.085f : 0.14f;
                lf.FlutterPhase = ang;
                SpawnFallingLeaf(lf);
            }
        }
    }

    // 4. 3-State Falling / Sleeping / Waking Leaf Particle Simulation (4m x 4m Subcells!)
    // First check ForceField / observer wake-ups on occupied 4m x 4m sleeping subcells (0 ALU on undisturbed leaves!)
    for (auto& [subKey, sub] : LeafSubcells_)
    {
        if (sub.SleepingLeafIds.empty()) continue;
        const std::array<float, 3> subPos {
            (static_cast<float>(sub.SubX) + 0.5f) * Config_.LeafSubcellMeters,
            (static_cast<float>(sub.SubY) + 0.5f) * Config_.LeafSubcellMeters,
            0.05f
        };

        const auto wAt = SampleWindAt(subPos, SimTimeSec_, windField);
        const float wSpd = Hypot2(wAt[0], wAt[1]);
        bool wakeTriggered = (wSpd >= Config_.WakeWindSpeedMps && windField.HasWakeImpulse);

        if (!wakeTriggered)
        {
            for (const auto& obs : observers)
            {
                const float d = Hypot2(obs.PositionMeters[0] - subPos[0], obs.PositionMeters[1] - subPos[1]);
                const float obsSpd = Hypot2(obs.VelocityMps[0], obs.VelocityMps[1]);
                if (d < (Config_.LeafSubcellMeters * 0.85f + obs.RadiusMeters) && obsSpd > 2.2f)
                {
                    wakeTriggered = true;
                    break;
                }
            }
        }

        if (wakeTriggered)
        {
            for (const uint32_t leafIdx : sub.SleepingLeafIds)
            {
                if (leafIdx >= Leaves_.size()) continue;
                auto& lf = Leaves_[leafIdx];
                if (lf.State != FallingLeafState::Sleeping) continue;
                const auto lw = SampleWindAt(lf.PositionMeters, SimTimeSec_, windField);
                lf.State = FallingLeafState::Active;
                lf.RestTimerSec = 0.0f;
                lf.PositionMeters[2] = std::max(0.06f, lf.PositionMeters[2] + 0.08f);
                lf.VelocityMps = { lw[0] * 0.65f, lw[1] * 0.65f, std::max(1.25f, std::fabs(lw[2]) + 1.15f) };
                if (!lf.HasWokenOnce)
                {
                    lf.HasWokenOnce = true;
                    ++TotalWokenCount_;
                }
            }
            sub.SleepingLeafIds.clear();
        }
    }

    // Advance ONLY Active leaves (Sleeping leaves cost strictly 0 ALU!)
    for (uint32_t i = 0u; i < static_cast<uint32_t>(Leaves_.size()); ++i)
    {
        auto& lf = Leaves_[i];
        if (lf.State == FallingLeafState::Sleeping) continue;

        const auto lw = SampleWindAt(lf.PositionMeters, SimTimeSec_, windField);
        const bool onGround = (lf.PositionMeters[2] <= 0.025f && lf.VelocityMps[2] <= 0.05f);

        if (!onGround)
        {
            // Airborne aerodynamic tumbling plate flutter + drag + gravity
            const float flutter = std::sin(SimTimeSec_ * 5.2f + lf.FlutterPhase);
            const float drag = 2.4f;
            const float ax = (lw[0] - lf.VelocityMps[0]) * drag + flutter * 0.85f;
            const float ay = (lw[1] - lf.VelocityMps[1]) * drag + std::cos(SimTimeSec_ * 4.7f + lf.FlutterPhase) * 0.85f;
            const float az = -2.6f + (lw[2] - lf.VelocityMps[2]) * 1.85f + std::fabs(flutter) * 0.45f;

            lf.VelocityMps[0] += ax * dtSec;
            lf.VelocityMps[1] += ay * dtSec;
            lf.VelocityMps[2] += az * dtSec;
            lf.PositionMeters[0] += lf.VelocityMps[0] * dtSec;
            lf.PositionMeters[1] += lf.VelocityMps[1] * dtSec;
            lf.PositionMeters[2] += lf.VelocityMps[2] * dtSec;
            lf.YawRad   += (1.8f + flutter * 1.4f) * dtSec;
            lf.PitchRad  = 0.35f * flutter;
        }
        else
        {
            // Physical Coulomb ground static friction: leaf slides to rest unless gust exceeds wake threshold!
            lf.PositionMeters[2] = 0.02f;
            lf.VelocityMps[2]    = 0.0f;
            lf.PitchRad          = 0.0f;
            const float windSpd = Hypot2(lw[0], lw[1]);
            if (windSpd > Config_.WakeWindSpeedMps && lw[2] > 0.35f)
            {
                lf.VelocityMps = { lw[0] * 0.55f, lw[1] * 0.55f, std::max(0.85f, lw[2]) };
                lf.PositionMeters[2] = 0.05f;
                lf.RestTimerSec = 0.0f;
            }
            else
            {
                const float damp = std::exp(-9.5f * dtSec);
                lf.VelocityMps[0] *= damp;
                lf.VelocityMps[1] *= damp;
                lf.PositionMeters[0] += lf.VelocityMps[0] * dtSec;
                lf.PositionMeters[1] += lf.VelocityMps[1] * dtSec;
                const float groundSpd = Hypot2(lf.VelocityMps[0], lf.VelocityMps[1]);
                if (groundSpd <= Config_.LeafSettleSpeedMps)
                {
                    lf.RestTimerSec += dtSec;
                    if (lf.RestTimerSec >= Config_.LeafSettleDelaySec)
                    {
                        lf.State = FallingLeafState::Sleeping;
                        lf.VelocityMps = { 0.0f, 0.0f, 0.0f };
                        const int32_t sx = static_cast<int32_t>(std::floor(lf.PositionMeters[0] / Config_.LeafSubcellMeters));
                        const int32_t sy = static_cast<int32_t>(std::floor(lf.PositionMeters[1] / Config_.LeafSubcellMeters));
                        auto& sub = LeafSubcells_[PackCellKey(sx, sy)];
                        sub.SubX = sx;
                        sub.SubY = sy;
                        sub.SleepingLeafIds.push_back(i);
                    }
                }
            }
        }

        if (lf.PositionMeters[2] < 0.02f)
        {
            lf.PositionMeters[2] = 0.02f;
            lf.VelocityMps[2] = 0.0f;
        }
    }
}

std::vector<BotanicalReplicatedPacket> BotanicalWorldPartition::BuildReplicatedPackets() const
{
    std::vector<BotanicalReplicatedPacket> out {};
    for (const auto& [key, cell] : Cells_)
    {
        if (cell.State == BotanicalCellSimState::GpuOnly) continue;
        for (const uint32_t idx : cell.PlantIndices)
        {
            const auto& p = Plants_[idx];
            BotanicalReplicatedPacket pkt {};
            pkt.EntityId       = p.EntityId;
            pkt.BendAngleXQ12  = static_cast<int16_t>(std::clamp(std::lround(p.BendAngleXRad * 4096.0f), -32767L, 32767L));
            pkt.BendAngleYQ12  = static_cast<int16_t>(std::clamp(std::lround(p.BendAngleYRad * 4096.0f), -32767L, 32767L));
            pkt.DetachedLeaves = p.DetachedLeafCount;
            pkt.CellSimState   = static_cast<uint8_t>(cell.State);
            out.push_back(pkt);
        }
    }
    return out;
}

BotanicalPartitionTelemetry BotanicalWorldPartition::GetTelemetry() const noexcept
{
    BotanicalPartitionTelemetry tel {};
    for (const auto& [key, cell] : Cells_)
    {
        if (cell.State == BotanicalCellSimState::GpuOnly) ++tel.GpuCells;
        else if (cell.State == BotanicalCellSimState::DeterministicCpu) ++tel.CpuCells;
        else ++tel.TransitioningCells;
    }
    for (const auto& p : Plants_)
    {
        tel.MaxSnapDeltaRad = std::max(tel.MaxSnapDeltaRad, p.MaxFrameDeltaRad);
    }
    for (const auto& lf : Leaves_)
    {
        if (lf.State == FallingLeafState::Active) ++tel.ActiveLeafCount;
        else ++tel.SleepingLeafCount;
    }
    tel.WokenLeafCount   = TotalWokenCount_;
    tel.CpuFixedSubsteps = CpuSubsteps_;
    return tel;
}

} // namespace Frontier::GeometrySpace::Botanical
