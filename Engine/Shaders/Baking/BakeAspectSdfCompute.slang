/*============================================================================================================================================
                                                        BAKEASPECTSDFCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Pure-GPU 3D Aspect-Matched Local Signed Distance Volume baker (replaces CPU thread-storm in LocalSignedDistanceVolume)

import WideTreeRayQuery;

struct AspectSdfBakeUniforms
{
    uint4  ResolutionAndCount; // x = ResX, y = ResY, z = ResZ, w = WideEntryCount
    float4 LocalMinAndMode;    // xyz = LocalMin, w = SignedMode (0 = unsigned, 1 = signed)
    float4 VoxelSpacingAndPad; // xyz = VoxelSpacing, w = PrimitiveCount
};

[[vk::binding(0, 0)]] ConstantBuffer<AspectSdfBakeUniforms>       BakeConfig;
[[vk::binding(1, 0)]] StructuredBuffer<GpuWideTreeEntry128>       WideEntries;
[[vk::binding(2, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48> Primitives;
[[vk::binding(3, 0)]] RWStructuredBuffer<float>                   OutDistances;

float BoxPointDistanceSqLocal(float3 P, float3 BMin, float3 BMax)
{
    float3 D = max(max(BMin - P, float3(0.0, 0.0, 0.0)), P - BMax);
    return dot(D, D);
}

float TrianglePointDistanceSqLocal(float3 P, float3 A, float3 AB, float3 AC)
{
    float3 B  = A + AB;
    float3 C  = A + AC;
    float3 AP = P - A;
    float  D1 = dot(AB, AP);
    float  D2 = dot(AC, AP);
    if (D1 <= 0.0 && D2 <= 0.0) return dot(AP, AP);

    float3 BP = P - B;
    float  D3 = dot(AB, BP);
    float  D4 = dot(AC, BP);
    if (D3 >= 0.0 && D4 <= D3) return dot(BP, BP);

    float VC = D1 * D4 - D3 * D2;
    if (VC <= 0.0 && D1 >= 0.0 && D3 <= 0.0)
    {
        float V = D1 / max(1.0e-12, D1 - D3);
        float3 Diff = P - (A + AB * V);
        return dot(Diff, Diff);
    }

    float3 CP = P - C;
    float  D5 = dot(AB, CP);
    float  D6 = dot(AC, CP);
    if (D6 >= 0.0 && D5 <= D6) return dot(CP, CP);

    float VB = D5 * D2 - D1 * D6;
    if (VB <= 0.0 && D2 >= 0.0 && D6 <= 0.0)
    {
        float W = D2 / max(1.0e-12, D2 - D6);
        float3 Diff = P - (A + AC * W);
        return dot(Diff, Diff);
    }

    float VA = D3 * D6 - D5 * D4;
    if (VA <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0)
    {
        float W = (D4 - D3) / max(1.0e-12, (D4 - D3) + (D5 - D6));
        float3 Diff = P - (B + (C - B) * W);
        return dot(Diff, Diff);
    }

    float Denom = 1.0 / max(1.0e-12, VA + VB + VC);
    float V = VB * Denom;
    float W = VC * Denom;
    float3 Closest = A + AB * V + AC * W;
    float3 Diff = P - Closest;
    return dot(Diff, Diff);
}

float ComputeExactBvh4Distance(float3 P)
{
    uint EntryCount = BakeConfig.ResolutionAndCount.w;
    if (EntryCount == 0u) return 0.0;

    float BestDistSq = 1.0e30;
    uint  Stack[48];
    int   StackTop = 0;
    Stack[0] = 0u;

    while (StackTop >= 0)
    {
        uint CurIdx = Stack[StackTop--];
        if (CurIdx >= EntryCount) continue;

        GpuWideTreeEntry128 Entry = WideEntries[CurIdx];
        float LaneDistSq[4];
        uint  LaneOrder[4] = { 0u, 1u, 2u, 3u };

        [unroll]
        for (int L = 0; L < 4; ++L)
        {
            uint Ref = Entry.BranchReference[L];
            if (Ref == 0xFFFFFFFFu)
            {
                LaneDistSq[L] = 1.0e30;
            }
            else
            {
                float3 BMin = float3(Entry.BranchBoundsMinX[L], Entry.BranchBoundsMinY[L], Entry.BranchBoundsMinZ[L]);
                float3 BMax = float3(Entry.BranchBoundsMaxX[L], Entry.BranchBoundsMaxY[L], Entry.BranchBoundsMaxZ[L]);
                LaneDistSq[L] = BoxPointDistanceSqLocal(P, BMin, BMax);
            }
        }

        for (int I = 0; I < 3; ++I)
        {
            for (int J = I + 1; J < 4; ++J)
            {
                if (LaneDistSq[LaneOrder[I]] < LaneDistSq[LaneOrder[J]])
                {
                    uint Tmp = LaneOrder[I];
                    LaneOrder[I] = LaneOrder[J];
                    LaneOrder[J] = Tmp;
                }
            }
        }

        for (int S = 0; S < 4; ++S)
        {
            uint L = LaneOrder[S];
            uint Ref = Entry.BranchReference[L];
            if (Ref == 0xFFFFFFFFu || LaneDistSq[L] > BestDistSq) continue;

            if ((Ref & 0x80000000u) != 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                uint Count = Entry.BranchPrimitiveCount[L];
                for (uint K = 0u; K < Count; ++K)
                {
                    GpuRayTrianglePrimitive48 Prim = Primitives[Start + K];
                    float D2 = TrianglePointDistanceSqLocal(P, Prim.Corner0, Prim.Edge01, Prim.Edge02);
                    if (D2 < BestDistSq) BestDistSq = D2;
                }
            }
            else if (StackTop + 1 < 48)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }

    return sqrt(max(0.0, BestDistSq));
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ComputeEntry(uint3 Tid : SV_DispatchThreadID)
{
    uint ResX = BakeConfig.ResolutionAndCount.x;
    uint ResY = BakeConfig.ResolutionAndCount.y;
    uint ResZ = BakeConfig.ResolutionAndCount.z;
    if (Tid.x >= ResX || Tid.y >= ResY || Tid.z >= ResZ) return;

    float3 P = BakeConfig.LocalMinAndMode.xyz + float3(Tid) * BakeConfig.VoxelSpacingAndPad.xyz;
    float Dist = ComputeExactBvh4Distance(P);

    uint LinearIdx = (Tid.z * ResY + Tid.y) * ResX + Tid.x;
    OutDistances[LinearIdx] = Dist;
}
