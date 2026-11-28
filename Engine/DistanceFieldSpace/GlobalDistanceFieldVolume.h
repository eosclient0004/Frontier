/*============================================================================================================================================
                                                        GLOBALDISTANCEFIELDVOLUME.H
============================================================================================================================================*/
// 🧩 4-cascade 128^3 toroidal Global Distance Field with sparse 8x8x8 brick scrolling & dirty-bounds scheduling (zero per-ray loops)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::DistanceFieldSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kGlobalDistanceCascadeCount      = 4u;
inline constexpr uint32_t kGlobalDistanceCascadeResolution = 128u;
inline constexpr uint32_t kGlobalDistanceBrickResolution   = 8u;
inline constexpr uint32_t kGlobalDistanceBrickGridDim      = kGlobalDistanceCascadeResolution / kGlobalDistanceBrickResolution; // 16
inline constexpr uint32_t kGlobalDistanceBricksPerCascade  =
    kGlobalDistanceBrickGridDim * kGlobalDistanceBrickGridDim * kGlobalDistanceBrickGridDim; // 4,096
inline constexpr float    kGlobalDistanceTruncationVoxels  = 12.0f;
inline constexpr uint32_t kGlobalDistanceMaxBrickInstances = 16u;
inline constexpr uint32_t kGlobalDistanceMaxBrickCapsules  = 16u;

inline constexpr std::array<float, kGlobalDistanceCascadeCount> kGlobalDistanceDefaultHalfSpansMeters {{
    4.0f,   // Cascade 0:  6.25 cm voxels, 0.50 m bricks
    12.0f,  // Cascade 1: 18.75 cm voxels, 1.50 m bricks
    36.0f,  // Cascade 2: 56.25 cm voxels, 4.50 m bricks
    128.0f, // Cascade 3:  2.00 m  voxels, 16.0 m bricks
}};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DistanceOccluderBoundsEntry
{
    uint64_t            IdentityKey   = 0ull; // [id]   - Stable instance or capsule group identifier
    uint64_t            TransformHash = 0ull; // [hash] - Transform, shape, and visibility fingerprint
    Mathematics::Axis3D WorldMin      {};     // [m]    - Tight world-space AABB minimum
    Mathematics::Axis3D WorldMax      {};     // [m]    - Tight world-space AABB maximum
};

struct GlobalDistanceCascadeSlice
{
    float    CenterWorld[3]      {};
    float    HalfSpanMeters      = 4.0f;
    float    VoxelPitchMeters    = 0.0625f;
    float    TruncationMeters    = 1.50f;
    int32_t  MinCellWorld[3]     { -64, -64, -64 };
    bool     InitializedToggle   = false;
};

struct CascadeDirtyBrickBatch
{
    uint32_t              CascadeIndex           = 0u;
    bool                  FullResetRequired      = false;
    uint32_t              ScrollBrickCount       = 0u;
    uint32_t              DynamicDirtyBrickCount = 0u;
    std::vector<uint32_t> PackedBrickCoords      {}; // bx | (by << 8) | (bz << 16) | (cascade << 24)
};

struct GlobalDistanceUpdatePlan
{
    std::array<CascadeDirtyBrickBatch, kGlobalDistanceCascadeCount> Cascades       {};
    uint32_t                                                        TotalDirtyBricks = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                             GLOBAL DISTANCE FIELD SCHEDULE
//------------------------------------------------------------------------------------------------------------------------

class GlobalDistanceFieldSchedule
{
public:
    GlobalDistanceFieldSchedule();

    void ResetSchedule() noexcept;

    [[nodiscard]] GlobalDistanceUpdatePlan BuildUpdatePlan(
        Mathematics::Axis3D                             FocusWorld,
        std::span<const DistanceOccluderBoundsEntry>    CurrentOccluders,
        bool                                            ForceFullRebuild);

    [[nodiscard]] const std::array<GlobalDistanceCascadeSlice, kGlobalDistanceCascadeCount>& QueryCascades() const noexcept
    {
        return CascadeSlices;
    }

    [[nodiscard]] static constexpr uint32_t PackBrickCoordinate(
        uint32_t CascadeIndex,
        uint32_t LogicalBrickX,
        uint32_t LogicalBrickY,
        uint32_t LogicalBrickZ) noexcept
    {
        return (LogicalBrickX & 0xFFu) |
               ((LogicalBrickY & 0xFFu) << 8u) |
               ((LogicalBrickZ & 0xFFu) << 16u) |
               ((CascadeIndex & 0xFFu) << 24u);
    }

private:
    static void MarkBoundsBricksDirty(
        const GlobalDistanceCascadeSlice& Slice,
        Mathematics::Axis3D               WorldMin,
        Mathematics::Axis3D               WorldMax,
        float                             DilationMeters,
        std::array<uint64_t, kGlobalDistanceBricksPerCascade / 64u>& DirtyBits,
        uint32_t&                         InOutDynamicCount) noexcept;

    std::array<GlobalDistanceCascadeSlice, kGlobalDistanceCascadeCount> CascadeSlices {};
    std::vector<DistanceOccluderBoundsEntry>                            TrackedOccluders {};
};

} // namespace Frontier::DistanceFieldSpace
