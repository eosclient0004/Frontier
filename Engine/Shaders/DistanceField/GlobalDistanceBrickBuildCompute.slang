/*============================================================================================================================================
                                                   GLOBALDISTANCEBRICKBUILDCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 GPU Pass 2 (vkCmdDispatchIndirect): Evaluates ONLY dirty 8x8x8 bricks using their culled local SDF & bone capsule buckets

#include "GlobalDistanceBrickShared.slang"

[[vk::binding(0, 0)]] ConstantBuffer<GpuGlobalDistanceBrickUniforms>  BrickUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<float>                         LocalAssetVoxels;
[[vk::binding(2, 0)]] StructuredBuffer<GpuDynamicLocalSdfInstance144> DynamicInstances;
[[vk::binding(3, 0)]] StructuredBuffer<GpuBoneCapsule32>              BoneCapsules;
[[vk::binding(4, 0)]] StructuredBuffer<GpuBrickCullBucket144>         BrickBuckets;
[[vk::image_format("r16f")]] [[vk::binding(5, 0)]] RWTexture3D<float> OutCascadeImg3D;
[[vk::image_format("r16f")]] [[vk::binding(6, 0)]] RWTexture3D<float> OutBrickMinImg3D;
[[vk::binding(7, 0)]] RWStructuredBuffer<uint2>                       GlobalSurfaceShellBuffer;

groupshared float SharedThreadMinDist[64];

[shader("compute")]
[numthreads(4, 4, 4)]
void BrickBuildEntry(
    uint3 GroupID       : SV_GroupID,
    uint3 GroupThreadID : SV_GroupThreadID,
    uint  GroupIndex    : SV_GroupIndex)
{
    GpuBrickCullBucket144 Bucket = BrickBuckets[GroupID.x];
    uint PackedCoord = Bucket.PackedHeader.x;
    uint CulledInst  = min(kGdfMaxInstancesPerBrick, Bucket.PackedHeader.y);
    uint CulledCaps  = min(kGdfMaxCapsulesPerBrick,  Bucket.PackedHeader.z);

    uint LogicalBX   = PackedCoord & 0xFFu;
    uint LogicalBY   = (PackedCoord >> 8u) & 0xFFu;
    uint LogicalBZ   = (PackedCoord >> 16u) & 0xFFu;

    float HalfSpan   = max(0.25, BrickUniforms.CascadeCenterHalfSpan.w);
    float VoxSize    = (2.0 * HalfSpan) / float(kGdfCascadeResolution);
    float TruncDist  = asfloat(BrickUniforms.CountsTruncation.w);
    bool  WriteShell = (BrickUniforms.ShellOptions.x != 0u);

    int3  MinCellWorld = BrickUniforms.CascadeMinCellIndex.xyz;
    int3  BrickCell0   = MinCellWorld + int3(LogicalBX, LogicalBY, LogicalBZ) * int(kGdfBrickResolution);
    uint3 PhysBrick    = WrapBrickCell(BrickCell0 >> int3(3));

    // Fast path for empty dirty bricks (clears vacated positions after object movement in 8 writes, zero SDF math!)
    if (CulledInst == 0u && CulledCaps == 0u)
    {
        for (uint DZ = 0u; DZ < 2u; ++DZ)
        {
            for (uint DY = 0u; DY < 2u; ++DY)
            {
                for (uint DX = 0u; DX < 2u; ++DX)
                {
                    int3  WorldCell = BrickCell0 + int3(GroupThreadID * 2u + uint3(DX, DY, DZ));
                    uint3 PhysCoord = WrapCascadeCell(WorldCell);
                    OutCascadeImg3D[int3(PhysCoord)] = TruncDist;
                    if (WriteShell && ((DX | DY | DZ) == 0u))
                    {
                        uint3 RadPhys   = PhysCoord >> 1u;
                        uint  LinearIdx = RadPhys.z * (kRadiosityGridResolution * kRadiosityGridResolution)
                                        + RadPhys.y * kRadiosityGridResolution + RadPhys.x;
                        GlobalSurfaceShellBuffer[LinearIdx] = uint2(0u, 0u);
                    }
                }
            }
        }
        if (GroupIndex == 0u)
        {
            OutBrickMinImg3D[int3(PhysBrick)] = TruncDist;
        }
        return;
    }

    float ThreadMinDist   = TruncDist;
    float SurfaceBandDist = VoxSize * 2.0 * kSurfaceBandVoxelScale;
    float RadioBandDist   = VoxSize * 2.0 * kRadiometricBandVoxelScale;

    for (uint DZ = 0u; DZ < 2u; ++DZ)
    {
        for (uint DY = 0u; DY < 2u; ++DY)
        {
            for (uint DX = 0u; DX < 2u; ++DX)
            {
                int3   WorldCell            = BrickCell0 + int3(GroupThreadID * 2u + uint3(DX, DY, DZ));
                uint3  PhysCoord            = WrapCascadeCell(WorldCell);
                float3 VoxelPos             = (float3(WorldCell) + 0.5) * VoxSize;
                bool   RadiosityLatticeSite = WriteShell && ((DX | DY | DZ) == 0u);

                float  BestSignedD  = TruncDist;
                float  BestAbsD     = TruncDist;
                float3 BestNormal   = float3(0.0, 0.0, 1.0);
                uint   BestMat      = 0u;
                uint   BestInstSlot = 0xFFFFFFFFu;
                bool   FoundSurface = false;

                for (uint K = 0u; K < CulledInst; ++K)
                {
                    uint InstIdx = Bucket.InstanceIndices[K];
                    GpuDynamicLocalSdfInstance144 Inst = DynamicInstances[InstIdx];
                    uint PackedBits = Inst.ResolutionOffset.w;

                    float3 LocalP = float3(
                        dot(Inst.WorldToLocalRow0.xyz, VoxelPos) + Inst.WorldToLocalRow0.w,
                        dot(Inst.WorldToLocalRow1.xyz, VoxelPos) + Inst.WorldToLocalRow1.w,
                        dot(Inst.WorldToLocalRow2.xyz, VoxelPos) + Inst.WorldToLocalRow2.w);
                    float  MinWorldScale = max(kMinHalfExtentMeters, Inst.LocalToWorldNormRow0.w);
                    float3 ClampedLocal  = clamp(LocalP, Inst.LocalMinPad.xyz, Inst.LocalMaxPad.xyz);
                    float  BoxLowerW     = length(LocalP - ClampedLocal) * MinWorldScale;
                    float  SafeVolBound  = max(BestSignedD, 0.0);
                    float  CullBound     = RadiosityLatticeSite ? max(SafeVolBound, min(BestAbsD, SurfaceBandDist)) : SafeVolBound;
                    if (BoxLowerW > CullBound + kMinHalfExtentMeters)
                    {
                        continue;
                    }

                    uint  ShapeCode = (Inst.ResolutionOffset.y >> kInstanceActiveBitShift) & kInstanceSlotMask16;
                    uint3 Res       = uint3(
                        Inst.ResolutionOffset.x & kInstanceSlotMask16,
                        (Inst.ResolutionOffset.x >> kInstanceActiveBitShift) & kInstanceSlotMask16,
                        Inst.ResolutionOffset.y & kInstanceSlotMask16);
                    float LocalD = EvaluateDynamicInstanceLocalSdf(LocalAssetVoxels, Inst, ShapeCode, Res, LocalP);
                    float WorldD = LocalD * MinWorldScale;
                    float AbsD   = abs(WorldD);

                    BestSignedD = min(BestSignedD, WorldD);

                    if (RadiosityLatticeSite && (!FoundSurface || AbsD < BestAbsD))
                    {
                        BestAbsD     = AbsD;
                        BestMat      = PackedBits & kInstanceSlotMask16;
                        BestInstSlot = InstIdx;
                        FoundSurface = true;
                    }
                }

                // Compute 6-tap surface gradient AT MOST ONCE per winning closest instance on the 64^3 radiosity lattice
                if (RadiosityLatticeSite && FoundSurface && BestInstSlot != 0xFFFFFFFFu && BestAbsD <= SurfaceBandDist)
                {
                    GpuDynamicLocalSdfInstance144 WinInst = DynamicInstances[BestInstSlot];
                    float3 WinLocalP = float3(
                        dot(WinInst.WorldToLocalRow0.xyz, VoxelPos) + WinInst.WorldToLocalRow0.w,
                        dot(WinInst.WorldToLocalRow1.xyz, VoxelPos) + WinInst.WorldToLocalRow1.w,
                        dot(WinInst.WorldToLocalRow2.xyz, VoxelPos) + WinInst.WorldToLocalRow2.w);
                    uint  WinShape = (WinInst.ResolutionOffset.y >> kInstanceActiveBitShift) & kInstanceSlotMask16;
                    uint3 WinRes   = uint3(
                        WinInst.ResolutionOffset.x & kInstanceSlotMask16,
                        (WinInst.ResolutionOffset.x >> kInstanceActiveBitShift) & kInstanceSlotMask16,
                        WinInst.ResolutionOffset.y & kInstanceSlotMask16);
                    float StepEps = (WinShape == 0u)
                        ? max(1.0e-3, max(WinInst.LocalMinPad.w, max(WinInst.LocalMaxPad.w, WinInst.LocalToWorldNormRow1.w)) * 0.85)
                        : 0.008;
                    float Dx = EvaluateDynamicInstanceLocalSdf(LocalAssetVoxels, WinInst, WinShape, WinRes, WinLocalP + float3(StepEps, 0.0, 0.0))
                             - EvaluateDynamicInstanceLocalSdf(LocalAssetVoxels, WinInst, WinShape, WinRes, WinLocalP - float3(StepEps, 0.0, 0.0));
                    float Dy = EvaluateDynamicInstanceLocalSdf(LocalAssetVoxels, WinInst, WinShape, WinRes, WinLocalP + float3(0.0, StepEps, 0.0))
                             - EvaluateDynamicInstanceLocalSdf(LocalAssetVoxels, WinInst, WinShape, WinRes, WinLocalP - float3(0.0, StepEps, 0.0));
                    float Dz = EvaluateDynamicInstanceLocalSdf(LocalAssetVoxels, WinInst, WinShape, WinRes, WinLocalP + float3(0.0, 0.0, StepEps))
                             - EvaluateDynamicInstanceLocalSdf(LocalAssetVoxels, WinInst, WinShape, WinRes, WinLocalP - float3(0.0, 0.0, StepEps));
                    float3 LocalGrad = float3(Dx, Dy, Dz);
                    float3 WorldGrad = float3(
                        dot(WinInst.LocalToWorldNormRow0.xyz, LocalGrad),
                        dot(WinInst.LocalToWorldNormRow1.xyz, LocalGrad),
                        dot(WinInst.LocalToWorldNormRow2.xyz, LocalGrad));
                    float GradLenSq = dot(WorldGrad, WorldGrad);
                    BestNormal = (GradLenSq > 1.0e-12) ? (WorldGrad * rsqrt(GradLenSq)) : float3(0.0, 0.0, 1.0);
                }

                // Fuse live deforming bone capsules directly into the single Global Distance Field (ZERO runtime capsule loops!)
                if (CulledCaps > 0u)
                {
                    float CapUnion = TruncDist;
                    for (uint C = 0u; C < CulledCaps; ++C)
                    {
                        float CapD = EvaluateTaperedCapsuleDistance(VoxelPos, BoneCapsules[Bucket.CapsuleIndices[C]]);
                        CapUnion   = (C == 0u) ? CapD : SmoothMinBrickQuadratic(CapUnion, CapD, 0.06);
                    }
                    BestSignedD = SmoothMinBrickQuadratic(BestSignedD, CapUnion, 0.08);
                    if (RadiosityLatticeSite && abs(CapUnion) < BestAbsD)
                    {
                        BestAbsD     = abs(CapUnion);
                        FoundSurface = true;
                    }
                }

                float ClampedSignedD = clamp(BestSignedD, -TruncDist, TruncDist);
                OutCascadeImg3D[int3(PhysCoord)] = ClampedSignedD;
                ThreadMinDist = min(ThreadMinDist, ClampedSignedD);

                if (RadiosityLatticeSite)
                {
                    uint3 RadPhys   = PhysCoord >> 1u;
                    uint  LinearIdx = RadPhys.z * (kRadiosityGridResolution * kRadiosityGridResolution)
                                    + RadPhys.y * kRadiosityGridResolution + RadPhys.x;
                    float Opacity = FoundSurface ? saturate(1.0 - BestAbsD / RadioBandDist) : 0.0;
                    if (Opacity > 0.01)
                    {
                        uint PackedNrm = PackBrickOctNormal32(BestNormal);
                        uint OpacU16   = uint(clamp(Opacity * 65535.0 + 0.5, 0.0, 65535.0));
                        uint PackedMat = (BestMat & 0xFFFFu) | (OpacU16 << 16u);
                        GlobalSurfaceShellBuffer[LinearIdx] = uint2(PackedNrm, PackedMat);
                    }
                    else
                    {
                        GlobalSurfaceShellBuffer[LinearIdx] = uint2(0u, 0u);
                    }
                }
            }
        }
    }

    // Parallel reduction across 64 threads to store brick minimum signed distance into the 16^3 summary volume
    SharedThreadMinDist[GroupIndex] = ThreadMinDist;
    GroupMemoryBarrierWithGroupSync();
    for (uint Stride = 32u; Stride >= 1u; Stride >>= 1u)
    {
        if (GroupIndex < Stride)
        {
            SharedThreadMinDist[GroupIndex] = min(SharedThreadMinDist[GroupIndex], SharedThreadMinDist[GroupIndex + Stride]);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (GroupIndex == 0u)
    {
        OutBrickMinImg3D[int3(PhysBrick)] = SharedThreadMinDist[0];
    }
}
