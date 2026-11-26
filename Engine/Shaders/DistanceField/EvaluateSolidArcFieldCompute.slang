/*============================================================================================================================================
                                                    EVALUATESOLIDARCFIELDCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Compute entry evaluating exact CAD B-Rep SDF + outward gradient at arbitrary 3D query points or uniform 3D volume grids

import SolidArcSurfaceField;

[[vk::binding(0, 0)]] ConstantBuffer<GpuSolidArcFieldHeader>       FieldHeader;
[[vk::binding(1, 0)]] StructuredBuffer<GpuSolidArcSurface128>      Surfaces;
[[vk::binding(2, 0)]] StructuredBuffer<GpuSolidArcTrimSegment32>   TrimSegments;
[[vk::binding(3, 0)]] StructuredBuffer<GpuSolidArcBezierPatch256>  BezierPatches;
[[vk::binding(4, 0)]] StructuredBuffer<GpuSolidArcConicArc64>      ConicArcs;
[[vk::binding(5, 0)]] StructuredBuffer<GpuSolidArcEdge80>          Edges;
[[vk::binding(6, 0)]] StructuredBuffer<GpuSolidArcMacroCell16>     MacroCells;
[[vk::binding(7, 0)]] StructuredBuffer<uint>                       CellSurfaceIndices;
[[vk::binding(8, 0)]] StructuredBuffer<uint>                       CellEdgeIndices;
[[vk::binding(9, 0)]] StructuredBuffer<float4>                     QueryPositions;
[[vk::binding(10, 0)]] RWStructuredBuffer<float4>                  OutSignedDistanceGradient;

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluateSolidArcFieldEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint LinearIndex = DispatchThreadId.x;
    uint OutputMode  = FieldHeader.ExtraCounts.y;

    float3 WorldPos = float3(0.0f);
    if (OutputMode == 1u)
    {
        uint3 VolDims  = uint3(FieldHeader.BufferCounts.z, FieldHeader.BufferCounts.w, FieldHeader.ExtraCounts.x);
        uint  TotalVox = VolDims.x * VolDims.y * VolDims.z;
        if (LinearIndex >= TotalVox)
        {
            return;
        }
        uint Vx   = LinearIndex % VolDims.x;
        uint Vy   = (LinearIndex / VolDims.x) % VolDims.y;
        uint Vz   = LinearIndex / (VolDims.x * VolDims.y);
        WorldPos  = FieldHeader.VolumeOrigin.xyz + FieldHeader.VolumeOrigin.w * (float3(float(Vx), float(Vy), float(Vz)) + float3(0.5f));
    }
    else
    {
        if (LinearIndex >= FieldHeader.BufferCounts.y)
        {
            return;
        }
        WorldPos = QueryPositions[LinearIndex].xyz;
    }

    GpuSolidArcDistanceSample Evaluated = QuerySolidArcFieldGpu(
        WorldPos,
        FieldHeader,
        Surfaces,
        TrimSegments,
        BezierPatches,
        ConicArcs,
        Edges,
        MacroCells,
        CellSurfaceIndices,
        CellEdgeIndices);

    OutSignedDistanceGradient[LinearIndex] = float4(Evaluated.SignedDistance, Evaluated.OutwardGradient);
}
