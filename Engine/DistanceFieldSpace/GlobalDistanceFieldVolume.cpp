/*============================================================================================================================================
                                                       GLOBALDISTANCEFIELDVOLUME.CPP
============================================================================================================================================*/
// 🧩 Implementation of 4-cascade 128^3 toroidal brick scrolling and dirty-AABB scheduling for the Global Distance Field

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/GlobalDistanceFieldVolume.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace Frontier::DistanceFieldSpace
{

namespace
{

[[nodiscard]] int32_t FloorDivideInt32(int32_t Numerator, int32_t Divisor) noexcept
{
    const int32_t Quot = Numerator / Divisor;
    const int32_t Rem  = Numerator % Divisor;
    return (Rem != 0 && ((Numerator < 0) != (Divisor < 0))) ? (Quot - 1) : Quot;
}

[[nodiscard]] int32_t SnapToBrickQuantum(float WorldMeters, float VoxelPitchMeters) noexcept
{
    constexpr int32_t BrickQuantum = static_cast<int32_t>(kGlobalDistanceBrickResolution);
    constexpr int32_t HalfVoxels   = static_cast<int32_t>(kGlobalDistanceCascadeResolution / 2u);
    const int32_t CenterCell       = static_cast<int32_t>(std::llround(WorldMeters / std::max(1.0e-5f, VoxelPitchMeters)));
    const int32_t UnsnappedMin     = CenterCell - HalfVoxels;
    return FloorDivideInt32(UnsnappedMin + (BrickQuantum / 2), BrickQuantum) * BrickQuantum;
}

inline void SetBrickBit(
    std::array<uint64_t, kGlobalDistanceBricksPerCascade / 64u>& BitWords,
    uint32_t BrickX,
    uint32_t BrickY,
    uint32_t BrickZ,
    uint32_t& InOutCounter) noexcept
{
    const uint32_t FlatIndex =
        (BrickZ * kGlobalDistanceBrickGridDim + BrickY) * kGlobalDistanceBrickGridDim + BrickX;
    const uint32_t WordIdx = FlatIndex >> 6u;
    const uint64_t BitMask = 1ull << (FlatIndex & 63u);
    if ((BitWords[WordIdx] & BitMask) == 0ull)
    {
        BitWords[WordIdx] |= BitMask;
        ++InOutCounter;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             GLOBAL DISTANCE FIELD SCHEDULE
//------------------------------------------------------------------------------------------------------------------------

GlobalDistanceFieldSchedule::GlobalDistanceFieldSchedule()
{
    ResetSchedule();
}

void GlobalDistanceFieldSchedule::ResetSchedule() noexcept
{
    TrackedOccluders.clear();
    for (uint32_t CascadeIdx = 0u; CascadeIdx < kGlobalDistanceCascadeCount; ++CascadeIdx)
    {
        GlobalDistanceCascadeSlice& Slice = CascadeSlices[CascadeIdx];
        Slice.HalfSpanMeters   = kGlobalDistanceDefaultHalfSpansMeters[CascadeIdx];
        Slice.VoxelPitchMeters = (2.0f * Slice.HalfSpanMeters) / static_cast<float>(kGlobalDistanceCascadeResolution);
        Slice.TruncationMeters = Slice.VoxelPitchMeters * kGlobalDistanceTruncationVoxels;
        Slice.CenterWorld[0]   = 0.0f;
        Slice.CenterWorld[1]   = 0.0f;
        Slice.CenterWorld[2]   = 0.0f;
        Slice.MinCellWorld[0]  = -static_cast<int32_t>(kGlobalDistanceCascadeResolution / 2u);
        Slice.MinCellWorld[1]  = -static_cast<int32_t>(kGlobalDistanceCascadeResolution / 2u);
        Slice.MinCellWorld[2]  = -static_cast<int32_t>(kGlobalDistanceCascadeResolution / 2u);
        Slice.InitializedToggle = false;
    }
}

void GlobalDistanceFieldSchedule::MarkBoundsBricksDirty(
    const GlobalDistanceCascadeSlice& Slice,
    Mathematics::Axis3D               WorldMin,
    Mathematics::Axis3D               WorldMax,
    float                             DilationMeters,
    std::array<uint64_t, kGlobalDistanceBricksPerCascade / 64u>& DirtyBits,
    uint32_t&                         InOutDynamicCount) noexcept
{
    const float BrickSizeMeters = Slice.VoxelPitchMeters * static_cast<float>(kGlobalDistanceBrickResolution);
    const float InvBrickSize    = 1.0f / std::max(1.0e-5f, BrickSizeMeters);

    const float OriginX = static_cast<float>(Slice.MinCellWorld[0]) * Slice.VoxelPitchMeters;
    const float OriginY = static_cast<float>(Slice.MinCellWorld[1]) * Slice.VoxelPitchMeters;
    const float OriginZ = static_cast<float>(Slice.MinCellWorld[2]) * Slice.VoxelPitchMeters;

    const int32_t MinBX = static_cast<int32_t>(std::floor((WorldMin.X - DilationMeters - OriginX) * InvBrickSize));
    const int32_t MinBY = static_cast<int32_t>(std::floor((WorldMin.Y - DilationMeters - OriginY) * InvBrickSize));
    const int32_t MinBZ = static_cast<int32_t>(std::floor((WorldMin.Z - DilationMeters - OriginZ) * InvBrickSize));
    const int32_t MaxBX = static_cast<int32_t>(std::floor((WorldMax.X + DilationMeters - OriginX) * InvBrickSize));
    const int32_t MaxBY = static_cast<int32_t>(std::floor((WorldMax.Y + DilationMeters - OriginY) * InvBrickSize));
    const int32_t MaxBZ = static_cast<int32_t>(std::floor((WorldMax.Z + DilationMeters - OriginZ) * InvBrickSize));

    constexpr int32_t MaxGridIdx = static_cast<int32_t>(kGlobalDistanceBrickGridDim) - 1;
    if (MaxBX < 0 || MaxBY < 0 || MaxBZ < 0 ||
        MinBX > MaxGridIdx || MinBY > MaxGridIdx || MinBZ > MaxGridIdx)
    {
        return;
    }

    const uint32_t ClampedMinX = static_cast<uint32_t>(std::clamp(MinBX, 0, MaxGridIdx));
    const uint32_t ClampedMinY = static_cast<uint32_t>(std::clamp(MinBY, 0, MaxGridIdx));
    const uint32_t ClampedMinZ = static_cast<uint32_t>(std::clamp(MinBZ, 0, MaxGridIdx));
    const uint32_t ClampedMaxX = static_cast<uint32_t>(std::clamp(MaxBX, 0, MaxGridIdx));
    const uint32_t ClampedMaxY = static_cast<uint32_t>(std::clamp(MaxBY, 0, MaxGridIdx));
    const uint32_t ClampedMaxZ = static_cast<uint32_t>(std::clamp(MaxBZ, 0, MaxGridIdx));

    for (uint32_t BZ = ClampedMinZ; BZ <= ClampedMaxZ; ++BZ)
    {
        for (uint32_t BY = ClampedMinY; BY <= ClampedMaxY; ++BY)
        {
            for (uint32_t BX = ClampedMinX; BX <= ClampedMaxX; ++BX)
            {
                SetBrickBit(DirtyBits, BX, BY, BZ, InOutDynamicCount);
            }
        }
    }
}

GlobalDistanceUpdatePlan GlobalDistanceFieldSchedule::BuildUpdatePlan(
    Mathematics::Axis3D                          FocusWorld,
    std::span<const DistanceOccluderBoundsEntry> CurrentOccluders,
    bool                                         ForceFullRebuild)
{
    GlobalDistanceUpdatePlan Plan {};

    // Collect moved, added, or removed occluder world bounds to update only affected dirty bricks
    struct DirtyBoundsBox
    {
        Mathematics::Axis3D MinPos {};
        Mathematics::Axis3D MaxPos {};
    };
    std::vector<DirtyBoundsBox> ChangedBoxes;
    if (!ForceFullRebuild)
    {
        ChangedBoxes.reserve(CurrentOccluders.size() + TrackedOccluders.size());

        auto FindByIdentity = [](std::span<const DistanceOccluderBoundsEntry> Entries, uint64_t Key) noexcept
            -> const DistanceOccluderBoundsEntry*
        {
            for (const DistanceOccluderBoundsEntry& Entry : Entries)
            {
                if (Entry.IdentityKey == Key)
                {
                    return &Entry;
                }
            }
            return nullptr;
        };

        for (const DistanceOccluderBoundsEntry& Cur : CurrentOccluders)
        {
            const DistanceOccluderBoundsEntry* Prev = FindByIdentity(TrackedOccluders, Cur.IdentityKey);
            if (Prev == nullptr)
            {
                ChangedBoxes.push_back({ Cur.WorldMin, Cur.WorldMax });
            }
            else if (Prev->TransformHash != Cur.TransformHash)
            {
                ChangedBoxes.push_back({ Prev->WorldMin, Prev->WorldMax });
                ChangedBoxes.push_back({ Cur.WorldMin, Cur.WorldMax });
            }
        }
        for (const DistanceOccluderBoundsEntry& Prev : TrackedOccluders)
        {
            if (FindByIdentity(CurrentOccluders, Prev.IdentityKey) == nullptr)
            {
                ChangedBoxes.push_back({ Prev.WorldMin, Prev.WorldMax });
            }
        }
    }

    constexpr int32_t BrickQuantum = static_cast<int32_t>(kGlobalDistanceBrickResolution);
    constexpr int32_t HalfVoxels   = static_cast<int32_t>(kGlobalDistanceCascadeResolution / 2u);
    constexpr int32_t GridDim      = static_cast<int32_t>(kGlobalDistanceBrickGridDim);

    for (uint32_t CascadeIdx = 0u; CascadeIdx < kGlobalDistanceCascadeCount; ++CascadeIdx)
    {
        GlobalDistanceCascadeSlice& Slice = CascadeSlices[CascadeIdx];
        CascadeDirtyBrickBatch&     Batch = Plan.Cascades[CascadeIdx];
        Batch.CascadeIndex = CascadeIdx;

        Slice.HalfSpanMeters   = kGlobalDistanceDefaultHalfSpansMeters[CascadeIdx];
        Slice.VoxelPitchMeters = (2.0f * Slice.HalfSpanMeters) / static_cast<float>(kGlobalDistanceCascadeResolution);
        Slice.TruncationMeters = Slice.VoxelPitchMeters * kGlobalDistanceTruncationVoxels;

        const int32_t NextMinCell[3] {
            SnapToBrickQuantum(FocusWorld.X, Slice.VoxelPitchMeters),
            SnapToBrickQuantum(FocusWorld.Y, Slice.VoxelPitchMeters),
            SnapToBrickQuantum(FocusWorld.Z, Slice.VoxelPitchMeters),
        };

        const int32_t DeltaBricks[3] {
            (NextMinCell[0] - Slice.MinCellWorld[0]) / BrickQuantum,
            (NextMinCell[1] - Slice.MinCellWorld[1]) / BrickQuantum,
            (NextMinCell[2] - Slice.MinCellWorld[2]) / BrickQuantum,
        };

        const bool NeedFullReset =
            ForceFullRebuild ||
            !Slice.InitializedToggle ||
            std::abs(DeltaBricks[0]) >= GridDim ||
            std::abs(DeltaBricks[1]) >= GridDim ||
            std::abs(DeltaBricks[2]) >= GridDim;

        Slice.MinCellWorld[0]   = NextMinCell[0];
        Slice.MinCellWorld[1]   = NextMinCell[1];
        Slice.MinCellWorld[2]   = NextMinCell[2];
        Slice.CenterWorld[0]    = static_cast<float>(NextMinCell[0] + HalfVoxels) * Slice.VoxelPitchMeters;
        Slice.CenterWorld[1]    = static_cast<float>(NextMinCell[1] + HalfVoxels) * Slice.VoxelPitchMeters;
        Slice.CenterWorld[2]    = static_cast<float>(NextMinCell[2] + HalfVoxels) * Slice.VoxelPitchMeters;
        Slice.InitializedToggle = true;

        std::array<uint64_t, kGlobalDistanceBricksPerCascade / 64u> DirtyBits {};
        if (NeedFullReset)
        {
            Batch.FullResetRequired = true;
            Batch.ScrollBrickCount  = kGlobalDistanceBricksPerCascade;
            Batch.PackedBrickCoords.reserve(kGlobalDistanceBricksPerCascade);
            for (uint32_t BZ = 0u; BZ < kGlobalDistanceBrickGridDim; ++BZ)
            {
                for (uint32_t BY = 0u; BY < kGlobalDistanceBrickGridDim; ++BY)
                {
                    for (uint32_t BX = 0u; BX < kGlobalDistanceBrickGridDim; ++BX)
                    {
                        Batch.PackedBrickCoords.push_back(PackBrickCoordinate(CascadeIdx, BX, BY, BZ));
                    }
                }
            }
            Plan.TotalDirtyBricks += kGlobalDistanceBricksPerCascade;
            continue;
        }

        // 1. Mark toroidal scroll boundary slab bricks along X, Y, Z axes
        if (DeltaBricks[0] != 0)
        {
            const uint32_t XBegin = (DeltaBricks[0] > 0) ? static_cast<uint32_t>(GridDim - DeltaBricks[0]) : 0u;
            const uint32_t XEnd   = (DeltaBricks[0] > 0) ? kGlobalDistanceBrickGridDim : static_cast<uint32_t>(-DeltaBricks[0]);
            for (uint32_t BZ = 0u; BZ < kGlobalDistanceBrickGridDim; ++BZ)
            {
                for (uint32_t BY = 0u; BY < kGlobalDistanceBrickGridDim; ++BY)
                {
                    for (uint32_t BX = XBegin; BX < XEnd; ++BX)
                    {
                        SetBrickBit(DirtyBits, BX, BY, BZ, Batch.ScrollBrickCount);
                    }
                }
            }
        }
        if (DeltaBricks[1] != 0)
        {
            const uint32_t YBegin = (DeltaBricks[1] > 0) ? static_cast<uint32_t>(GridDim - DeltaBricks[1]) : 0u;
            const uint32_t YEnd   = (DeltaBricks[1] > 0) ? kGlobalDistanceBrickGridDim : static_cast<uint32_t>(-DeltaBricks[1]);
            for (uint32_t BZ = 0u; BZ < kGlobalDistanceBrickGridDim; ++BZ)
            {
                for (uint32_t BY = YBegin; BY < YEnd; ++BY)
                {
                    for (uint32_t BX = 0u; BX < kGlobalDistanceBrickGridDim; ++BX)
                    {
                        SetBrickBit(DirtyBits, BX, BY, BZ, Batch.ScrollBrickCount);
                    }
                }
            }
        }
        if (DeltaBricks[2] != 0)
        {
            const uint32_t ZBegin = (DeltaBricks[2] > 0) ? static_cast<uint32_t>(GridDim - DeltaBricks[2]) : 0u;
            const uint32_t ZEnd   = (DeltaBricks[2] > 0) ? kGlobalDistanceBrickGridDim : static_cast<uint32_t>(-DeltaBricks[2]);
            for (uint32_t BZ = ZBegin; BZ < ZEnd; ++BZ)
            {
                for (uint32_t BY = 0u; BY < kGlobalDistanceBrickGridDim; ++BY)
                {
                    for (uint32_t BX = 0u; BX < kGlobalDistanceBrickGridDim; ++BX)
                    {
                        SetBrickBit(DirtyBits, BX, BY, BZ, Batch.ScrollBrickCount);
                    }
                }
            }
        }

        // 2. Mark dirty bricks overlapping OldBounds U NewBounds of moved/modified occluders
        for (const DirtyBoundsBox& Box : ChangedBoxes)
        {
            MarkBoundsBricksDirty(Slice, Box.MinPos, Box.MaxPos, Slice.TruncationMeters, DirtyBits, Batch.DynamicDirtyBrickCount);
        }

        const uint32_t ExpectedCount = Batch.ScrollBrickCount + Batch.DynamicDirtyBrickCount;
        if (ExpectedCount > 0u)
        {
            Batch.PackedBrickCoords.reserve(ExpectedCount);
            for (uint32_t WordIdx = 0u; WordIdx < DirtyBits.size(); ++WordIdx)
            {
                uint64_t Bits = DirtyBits[WordIdx];
                while (Bits != 0ull)
                {
                    const uint32_t BitPos = static_cast<uint32_t>(std::countr_zero(Bits));
                    Bits &= (Bits - 1ull);
                    const uint32_t FlatIndex = (WordIdx << 6u) + BitPos;
                    const uint32_t BX        = FlatIndex & 15u;
                    const uint32_t BY        = (FlatIndex >> 4u) & 15u;
                    const uint32_t BZ        = (FlatIndex >> 8u) & 15u;
                    Batch.PackedBrickCoords.push_back(PackBrickCoordinate(CascadeIdx, BX, BY, BZ));
                }
            }
            Plan.TotalDirtyBricks += static_cast<uint32_t>(Batch.PackedBrickCoords.size());
        }
    }

    TrackedOccluders.assign(CurrentOccluders.begin(), CurrentOccluders.end());
    return Plan;
}

} // namespace Frontier::DistanceFieldSpace
