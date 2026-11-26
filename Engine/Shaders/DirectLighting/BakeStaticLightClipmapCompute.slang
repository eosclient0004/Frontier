/*============================================================================================================================================
                                                    BAKESTATICLIGHTCLIPMAPCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Automatic GPU Compute Baker for Static 2-Cascade Toroidal Light Clipmap (Fine + Coarse dirty slabs)
//    - Evaluates ONLY LightMobility::Static emitters into wrapped toroidal cell coordinates
//    - Outputs L0 Irradiance + Directionality (RGBA16F/32F) and L1 Dominant Direction + Sub-Cell Error (RGBA16F/32F)

import DirectLighting.UnifiedLightEvaluation;

struct GpuStaticClipmapBakeUniforms
{
    int4   OriginCellAndRes; // x = originCellX, y = originCellZ, z = resolution, w = staticLightCount
    float4 CellAndElevation; // x = cellSizeMeters, y = receiverElevationY, z = subCornerFraction (0.32), w = dirtyOnlyFlag
    int4   DirtyMinMaxCell;  // x = dirtyMinX, y = dirtyMinZ, z = dirtyMaxX, w = dirtyMaxZ
};

[[vk::binding(0, 0)]] StructuredBuffer<GpuLightHierarchyNode64>    StaticLights;
[[vk::binding(1, 0)]] ConstantBuffer<GpuStaticClipmapBakeUniforms> BakeUniforms;
[[vk::binding(2, 0)]] RWTexture2D<float4>                          OutClipmapIrradiance; // rgb = L0, a = directionality R
[[vk::binding(3, 0)]] RWTexture2D<float4>                          OutClipmapDirection;  // rgb = unit L1, a = cellError

void AccumulateStaticFloorPoint(
    float3     Pos,
    uint       LightCount,
    out float3 OutIrr,
    out float3 OutDirSum,
    out float  OutLumaSum)
{
    OutIrr = float3(0.0, 0.0, 0.0);
    OutDirSum = float3(0.0, 0.0, 0.0);
    OutLumaSum = 0.0;

    for (uint I = 0u; I < LightCount; ++I)
    {
        GpuLightHierarchyNode64 L = StaticLights[I];
        float3 ToL = L.PositionFlux.xyz - Pos;
        float DistSq = dot(ToL, ToL) + 0.35;
        float Dist = sqrt(DistSq);
        float3 W = ToL / Dist;
        if (W.y <= 0.0)
        {
            continue;
        }

        float Spot = 1.0;
        if (L.DirectionCone.w > -0.95)
        {
            float CosAng = dot(-W, normalize(L.DirectionCone.xyz));
            Spot = smoothstep(L.DirectionCone.w, min(0.99, L.DirectionCone.w + 0.30), CosAng);
            if (Spot <= 0.001)
            {
                continue;
            }
        }

        float Atten = (L.PositionFlux.w * W.y * Spot) / (DistSq * 3.14159265);
        float3 Rad = L.ColorRadius.rgb * Atten;
        float Lum = dot(Rad, float3(0.2126, 0.7152, 0.0722));
        OutIrr += Rad;
        OutDirSum += W * Lum;
        OutLumaSum += Lum;
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    int Res = BakeUniforms.OriginCellAndRes.z;
    if (int(DispatchThreadId.x) >= Res || int(DispatchThreadId.y) >= Res)
    {
        return;
    }

    int HalfRes = Res / 2;
    int WorldCellX = BakeUniforms.OriginCellAndRes.x + int(DispatchThreadId.x) - HalfRes;
    int WorldCellZ = BakeUniforms.OriginCellAndRes.y + int(DispatchThreadId.y) - HalfRes;

    if (BakeUniforms.CellAndElevation.w > 0.5)
    {
        bool InDirty = (WorldCellX >= BakeUniforms.DirtyMinMaxCell.x && WorldCellX <= BakeUniforms.DirtyMinMaxCell.z &&
                        WorldCellZ >= BakeUniforms.DirtyMinMaxCell.y && WorldCellZ <= BakeUniforms.DirtyMinMaxCell.w);
        if (!InDirty)
        {
            return;
        }
    }

    float CellSize = BakeUniforms.CellAndElevation.x;
    float ElevY    = BakeUniforms.CellAndElevation.y;
    float SubOff   = CellSize * BakeUniforms.CellAndElevation.z;
    uint  LightCnt = uint(max(0, BakeUniforms.OriginCellAndRes.w));

    float Cx = (float(WorldCellX) + 0.5) * CellSize;
    float Cz = (float(WorldCellZ) + 0.5) * CellSize;

    float3 CenterIrr, CenterDirSum;
    float CenterLuma;
    AccumulateStaticFloorPoint(float3(Cx, ElevY, Cz), LightCnt, CenterIrr, CenterDirSum, CenterLuma);

    // Sub-corner variance captures shadow/falloff discontinuities for Unified All-3 error-guided promotion
    float2 Offsets[4] = { float2(-SubOff, -SubOff), float2(SubOff, -SubOff), float2(-SubOff, SubOff), float2(SubOff, SubOff) };
    float VarianceSum = 0.0;
    for (int K = 0; K < 4; ++K)
    {
        float3 SubIrr, SubDir;
        float SubLuma;
        AccumulateStaticFloorPoint(float3(Cx + Offsets[K].x, ElevY, Cz + Offsets[K].y), LightCnt, SubIrr, SubDir, SubLuma);
        VarianceSum += length(SubIrr - CenterIrr);
    }

    float VecLen = length(CenterDirSum);
    float DirR   = clamp(VecLen / max(1.0e-5, CenterLuma), 0.0, 1.0);
    float3 DomDir = (VecLen > 1.0e-5) ? (CenterDirSum / VecLen) : float3(0.0, 1.0, 0.0);

    int2 ToroidalPx = WrapToroidalCoord(int2(WorldCellX, WorldCellZ), Res);
    OutClipmapIrradiance[ToroidalPx] = float4(CenterIrr, DirR);
    OutClipmapDirection[ToroidalPx]  = float4(DomDir, VarianceSum * 0.25);
}
