/*============================================================================================================================================
                                                   GLOBALDISTANCEBRICKCULLCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 GPU Pass 1: Sparse 8x8x8 brick culling of local signed distance volumes & bone capsules + indirect dispatch argument builder

#include "GlobalDistanceBrickShared.slang"

[[vk::binding(0, 0)]] ConstantBuffer<GpuGlobalDistanceBrickUniforms>  BrickUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<GpuDynamicLocalSdfInstance144> DynamicInstances;
[[vk::binding(2, 0)]] StructuredBuffer<GpuBoneCapsule32>              BoneCapsules;
[[vk::binding(3, 0)]] StructuredBuffer<uint>                          DirtyBrickCoords;
[[vk::binding(4, 0)]] RWStructuredBuffer<GpuBrickCullBucket144>       OutBrickBuckets;
[[vk::binding(5, 0)]] RWStructuredBuffer<uint>                        OutIndirectArgs;

[shader("compute")]
[numthreads(64, 1, 1)]
void BrickCullEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint DirtyIdx   = DispatchThreadID.x;
    uint DirtyCount = BrickUniforms.CountsTruncation.x;
    if (DirtyIdx >= DirtyCount)
    {
        return;
    }

    uint PackedCoord = DirtyBrickCoords[DirtyIdx];
    uint LogicalBX   = PackedCoord & 0xFFu;
    uint LogicalBY   = (PackedCoord >> 8u) & 0xFFu;
    uint LogicalBZ   = (PackedCoord >> 16u) & 0xFFu;

    float  HalfSpan   = max(0.25, BrickUniforms.CascadeCenterHalfSpan.w);
    float  VoxSize    = (2.0 * HalfSpan) / float(kGdfCascadeResolution);
    float  BrickSpan  = VoxSize * float(kGdfBrickResolution);
    float  TruncDist  = asfloat(BrickUniforms.CountsTruncation.w);
    int3   BaseCell   = BrickUniforms.CascadeMinCellIndex.xyz + int3(LogicalBX, LogicalBY, LogicalBZ) * int(kGdfBrickResolution);
    float3 BrickMinW  = float3(BaseCell) * VoxSize;
    float3 BrickMaxW  = BrickMinW + float3(BrickSpan);
    float3 BrickCtrW  = 0.5 * (BrickMinW + BrickMaxW);
    float  BrickRadW  = 0.8660254 * BrickSpan; // half-diagonal of 8x8x8 brick

    uint InstCount = BrickUniforms.CountsTruncation.y;
    uint CapCount  = min(64u, BrickUniforms.CountsTruncation.z);

    uint  PickedInstIdx[16];
    float PickedInstDist[16];
    uint  PickedCapIdx[16];
    for (uint InitIdx = 0u; InitIdx < kGdfMaxInstancesPerBrick; ++InitIdx)
    {
        PickedInstIdx[InitIdx]  = 0u;
        PickedInstDist[InitIdx] = 1.0e9;
        PickedCapIdx[InitIdx]   = 0u;
    }
    uint  CulledInstCnt = 0u;

    for (uint InstIdx = 0u; InstIdx < InstCount; ++InstIdx)
    {
        GpuDynamicLocalSdfInstance144 Inst = DynamicInstances[InstIdx];
        uint PackedBits = Inst.ResolutionOffset.w;
        if ((PackedBits >> kInstanceActiveBitShift) == 0u || (PackedBits & kInstanceGroundSlabBit) != 0u)
        {
            continue;
        }

        // Conservative brick-sphere vs oriented instance local AABB lower bound
        float3 LocalCtr = float3(
            dot(Inst.WorldToLocalRow0.xyz, BrickCtrW) + Inst.WorldToLocalRow0.w,
            dot(Inst.WorldToLocalRow1.xyz, BrickCtrW) + Inst.WorldToLocalRow1.w,
            dot(Inst.WorldToLocalRow2.xyz, BrickCtrW) + Inst.WorldToLocalRow2.w);
        float  MinWorldScale = max(kMinHalfExtentMeters, Inst.LocalToWorldNormRow0.w);
        float3 ClampedLocal  = clamp(LocalCtr, Inst.LocalMinPad.xyz, Inst.LocalMaxPad.xyz);
        float  CenterBoxD    = length(LocalCtr - ClampedLocal) * MinWorldScale;
        float  MinBrickDist  = max(0.0, CenterBoxD - BrickRadW);
        if (MinBrickDist > TruncDist + VoxSize)
        {
            continue;
        }

        if (CulledInstCnt < kGdfMaxInstancesPerBrick)
        {
            PickedInstIdx[CulledInstCnt]  = InstIdx;
            PickedInstDist[CulledInstCnt] = MinBrickDist;
            ++CulledInstCnt;
        }
        else
        {
            // Keep the 16 closest overlapping instances to this brick
            uint  WorstSlot = 0u;
            float WorstDist = PickedInstDist[0];
            for (uint K = 1u; K < kGdfMaxInstancesPerBrick; ++K)
            {
                if (PickedInstDist[K] > WorstDist)
                {
                    WorstDist = PickedInstDist[K];
                    WorstSlot = K;
                }
            }
            if (MinBrickDist < WorstDist)
            {
                PickedInstIdx[WorstSlot]  = InstIdx;
                PickedInstDist[WorstSlot] = MinBrickDist;
            }
        }
    }

    uint CulledCapCnt = 0u;
    float3 DilatedMin = BrickMinW - float3(TruncDist + VoxSize);
    float3 DilatedMax = BrickMaxW + float3(TruncDist + VoxSize);

    for (uint CapIdx = 0u; CapIdx < CapCount && CulledCapCnt < kGdfMaxCapsulesPerBrick; ++CapIdx)
    {
        GpuBoneCapsule32 Cap = BoneCapsules[CapIdx];
        float  MaxR  = max(Cap.JointStartRadius.w, Cap.JointEndRadius.w);
        float3 CMin  = min(Cap.JointStartRadius.xyz, Cap.JointEndRadius.xyz) - float3(MaxR);
        float3 CMax  = max(Cap.JointStartRadius.xyz, Cap.JointEndRadius.xyz) + float3(MaxR);
        if (all(CMax >= DilatedMin) && all(CMin <= DilatedMax))
        {
            PickedCapIdx[CulledCapCnt++] = CapIdx;
        }
    }

    uint BucketSlot = 0u;
    InterlockedAdd(OutIndirectArgs[0], 1u, BucketSlot);
    if (BucketSlot == 0u)
    {
        OutIndirectArgs[1] = 1u;
        OutIndirectArgs[2] = 1u;
    }

    GpuBrickCullBucket144 Bucket;
    Bucket.PackedHeader = uint4(PackedCoord, CulledInstCnt, CulledCapCnt, 0u);
    for (uint I = 0u; I < kGdfMaxInstancesPerBrick; ++I)
    {
        Bucket.InstanceIndices[I] = (I < CulledInstCnt) ? PickedInstIdx[I] : 0u;
        Bucket.CapsuleIndices[I]  = (I < CulledCapCnt)  ? PickedCapIdx[I]  : 0u;
    }
    OutBrickBuckets[BucketSlot] = Bucket;
}
