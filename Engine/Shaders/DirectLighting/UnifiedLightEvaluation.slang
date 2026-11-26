/*============================================================================================================================================
                                                        UNIFIEDLIGHTEVALUATION.SLANG
============================================================================================================================================*/
// 🧩 Shared Slang module for Frontier's Unified 3-System Lighting Pipeline:
//    (1) Static 2-Cascade Toroidal Clipmap (Fine + Coarse wrapped coordinates)
//    (2) Dynamic Sparse 8x4x4 Cube-Grid Pods (-Y-pole octahedral tiles via bleed-free integer Load/texelFetch)
//    (3) Dynamic Bottom-Up Agglomerative Distance Light BVH + Error-Guided Static Promotion + Debug Channels 23..26

module UnifiedLightEvaluation;

public struct GpuLightHierarchyNode64
{
    public float4 PositionFlux;  // xyz = centroid [m], w = luminous flux
    public float4 DirectionCone; // xyz = unit cone axis, w = cos(outerCone)
    public float4 ColorRadius;   // rgb = linear color, w = cluster radius [m]
    public uint4  Topology;      // x = leftChild (~0u if leaf), y = rightChild, z = leafSourceIndex, w = (mobility<<24)|memberCount
};

public struct GpuToroidalClipmapParams
{
    public float4 FineCenterHalfRes;   // xy = snapped Fine centerXZ, z = fineHalfSpan, w = fineRes
    public float4 CoarseCenterHalfRes; // xy = Coarse centerXZ, z = coarseHalfSpan, w = coarseRes
    public float4 ErrorAndLodParams;   // x = hybridErrorGate, y = naniteErrorBudget, z = errorHeatmapGain, w = modeOrdinal
};

public float2 OctahedralEncodeMinusY(float3 UnitDir)
{
    float3 Q = float3(UnitDir.x, -UnitDir.y, UnitDir.z);
    float InvL1 = 1.0 / max(1.0e-6, abs(Q.x) + abs(Q.y) + abs(Q.z));
    float2 O = Q.xz * InvL1;
    if (Q.y < 0.0)
    {
        float2 SignO = float2(O.x >= 0.0 ? 1.0 : -1.0, O.y >= 0.0 ? 1.0 : -1.0);
        O = (1.0 - abs(O.yx)) * SignO;
    }
    return O * 0.5 + 0.5;
}

// Bleed-free bilinear tile fetch using exact integer Load() coordinates (matches CubeGridPodAtlas.cpp bit-for-bit)
public float3 SamplePodOctahedralTile(
    Texture2D<float4> PodAtlasSheet,
    uint2             TileOriginPx,
    uint              TileResPx,
    float3            UnitRayFromLight)
{
    float2 Uv = OctahedralEncodeMinusY(UnitRayFromLight);
    float ResF = float(TileResPx);
    float2 Coord = clamp(Uv * ResF - 0.5, 0.0, ResF - 1.0);
    uint2 P0 = uint2(floor(Coord));
    uint2 P1 = min(P0 + uint2(1, 1), uint2(TileResPx - 1, TileResPx - 1));
    float2 F = Coord - float2(P0);

    float3 C00 = PodAtlasSheet.Load(int3(int2(TileOriginPx + uint2(P0.x, P0.y)), 0)).rgb;
    float3 C10 = PodAtlasSheet.Load(int3(int2(TileOriginPx + uint2(P1.x, P0.y)), 0)).rgb;
    float3 C01 = PodAtlasSheet.Load(int3(int2(TileOriginPx + uint2(P0.x, P1.y)), 0)).rgb;
    float3 C11 = PodAtlasSheet.Load(int3(int2(TileOriginPx + uint2(P1.x, P1.y)), 0)).rgb;

    float3 Top = lerp(C00, C10, F.x);
    float3 Bot = lerp(C01, C11, F.x);
    return lerp(Top, Bot, F.y);
}

public float EvaluateQuarticInfluenceWindow(float DistanceMeters, float InfluenceRadiusMeters)
{
    if (InfluenceRadiusMeters <= 1.0e-4 || DistanceMeters >= InfluenceRadiusMeters)
    {
        return 0.0;
    }
    float R = DistanceMeters / InfluenceRadiusMeters;
    float R2 = R * R;
    float W = max(0.0, 1.0 - R2 * R2);
    return W * W;
}

public float ComputeContinuousPodLod(float DistanceMeters, float BoundingDiameterMeters, float LodBias)
{
    float NearThresh = max(2.2 * BoundingDiameterMeters, 1.8);
    float Ratio = max(DistanceMeters / NearThresh, 1.0);
    return clamp(1.15 * log2(Ratio) + LodBias, 0.0, 3.0);
}

public int2 WrapToroidalCoord(int2 WorldCell, int Res)
{
    int2 M = WorldCell % Res;
    if (M.x < 0) M.x += Res;
    if (M.y < 0) M.y += Res;
    return M;
}

public float3 SampleStaticToroidalClipmap(
    Texture2D<float4>       FineIrrTex,
    Texture2D<float4>       FineDirTex,
    Texture2D<float4>       CoarseIrrTex,
    Texture2D<float4>       CoarseDirTex,
    GpuToroidalClipmapParams Params,
    float3                  WorldPos,
    float3                  Normal,
    float3                  ViewDir,
    float3                  Albedo,
    float                   Roughness,
    out float               OutCellError,
    out float               OutFineWeight)
{
    float2 FineUv = (WorldPos.xz - (Params.FineCenterHalfRes.xy - Params.FineCenterHalfRes.zz)) / (2.0 * Params.FineCenterHalfRes.z);
    float EdgeDist = min(min(FineUv.x, 1.0 - FineUv.x), min(FineUv.y, 1.0 - FineUv.y));
    OutFineWeight = smoothstep(0.0, 0.14, EdgeDist);

    int CoarseRes = max(1, int(Params.CoarseCenterHalfRes.w));
    float CoarseCell = (2.0 * Params.CoarseCenterHalfRes.z) / float(CoarseRes);
    int2 CoarseCoord = WrapToroidalCoord(int2(floor(WorldPos.xz / CoarseCell)), CoarseRes);
    float4 CIrr = CoarseIrrTex.Load(int3(CoarseCoord, 0));
    float4 CDir = CoarseDirTex.Load(int3(CoarseCoord, 0));

    float3 L0 = CIrr.rgb;
    float DirR = CIrr.a;
    float3 DomDir = normalize(CDir.xyz + float3(0.0, 1.0e-5, 0.0));
    OutCellError = CDir.a;

    if (OutFineWeight > 0.001)
    {
        int FineRes = max(1, int(Params.FineCenterHalfRes.w));
        float FineCell = (2.0 * Params.FineCenterHalfRes.z) / float(FineRes);
        int2 FineCoord = WrapToroidalCoord(int2(floor(WorldPos.xz / FineCell)), FineRes);
        float4 FIrr = FineIrrTex.Load(int3(FineCoord, 0));
        float4 FDir = FineDirTex.Load(int3(FineCoord, 0));

        L0 = lerp(L0, FIrr.rgb, OutFineWeight);
        DirR = lerp(DirR, FIrr.a, OutFineWeight);
        DomDir = normalize(lerp(DomDir, normalize(FDir.xyz + float3(0.0, 1.0e-5, 0.0)), OutFineWeight));
        OutCellError = lerp(OutCellError, FDir.a, OutFineWeight);
    }

    float NdotL = max(dot(Normal, DomDir), 0.0);
    float3 H = normalize(DomDir + ViewDir);
    float NdotH = max(dot(Normal, H), 0.0);
    float NdotV = max(dot(Normal, ViewDir), 0.05);
    float A = max(0.08, Roughness * Roughness);
    float A2 = A * A;
    float Denom = NdotH * NdotH * (A2 - 1.0) + 1.0;
    float D = A2 / (3.14159265 * Denom * Denom + 1.0e-5);
    float F = 0.04 + 0.96 * pow(1.0 - max(dot(H, ViewDir), 0.0), 5.0);
    float Spec = (D * F) / (4.0 * NdotV * max(NdotL, 0.15) + 0.15);

    float NormalMod = lerp(1.0, max(0.16, NdotL / max(0.25, DomDir.y)), DirR * 0.85);
    return Albedo * L0 * NormalMod + L0 * (DirR * Spec * 0.72);
}

public float3 EvaluatePbrHierarchyNode(
    GpuLightHierarchyNode64 Node,
    float3                  WorldPos,
    float3                  Normal,
    float3                  ViewDir,
    float3                  Albedo,
    float                   Roughness,
    float                   Visibility)
{
    float3 ToL = Node.PositionFlux.xyz - WorldPos;
    float DistSq = dot(ToL, ToL) + 0.35;
    float Dist = sqrt(DistSq);
    float3 L = ToL / Dist;
    float NdotL = max(dot(Normal, L), 0.0);
    if (NdotL <= 0.0 || Visibility <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float Spot = 1.0;
    if (Node.DirectionCone.w > -0.95)
    {
        float CosAng = dot(-L, normalize(Node.DirectionCone.xyz));
        Spot = smoothstep(Node.DirectionCone.w, min(0.99, Node.DirectionCone.w + 0.30), CosAng);
        if (Spot <= 0.001)
        {
            return float3(0.0, 0.0, 0.0);
        }
    }

    float3 H = normalize(L + ViewDir);
    float NdotH = max(dot(Normal, H), 0.0);
    float NdotV = max(dot(Normal, ViewDir), 0.05);
    float A = max(0.06, Roughness * Roughness);
    float A2 = A * A;
    float Denom = NdotH * NdotH * (A2 - 1.0) + 1.0;
    float D = A2 / (3.14159265 * Denom * Denom + 1.0e-5);
    float F = 0.04 + 0.96 * pow(1.0 - max(dot(H, ViewDir), 0.0), 5.0);
    float Spec = (D * F) / (4.0 * NdotV * NdotL + 0.15);

    float Radiance = (Node.PositionFlux.w * Spot * Visibility) / (DistSq * 3.14159265);
    return Node.ColorRadius.rgb * Radiance * (Albedo * NdotL + float3(Spec * NdotL * 0.85));
}

public float3 InfernoHeatmapRamp(float T)
{
    T = clamp(T, 0.0, 1.0);
    float3 C0 = float3(0.03, 0.03, 0.07);
    float3 C1 = float3(0.36, 0.07, 0.43);
    float3 C2 = float3(0.84, 0.28, 0.15);
    float3 C3 = float3(0.99, 0.88, 0.25);
    if (T < 0.33) return lerp(C0, C1, T / 0.33);
    if (T < 0.66) return lerp(C1, C2, (T - 0.33) / 0.33);
    return lerp(C2, C3, (T - 0.66) / 0.34);
}
