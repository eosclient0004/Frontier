/*============================================================================================================================================
                                                      GLOBALDISTANCEBRICKSHARED.SLANG
============================================================================================================================================*/
// 🧩 Shared constants, GPU layouts, and local signed distance evaluators for the 4-cascade 128^3 sparse-brick Global Distance Field

static const uint  kGdfCascadeResolution      = 128u;
static const uint  kGdfCascadeWrapMask        = 127u;
static const uint  kGdfBrickResolution        = 8u;
static const uint  kGdfBrickGridDim           = 16u;
static const uint  kGdfBrickGridWrapMask      = 15u;
static const uint  kRadiosityGridResolution   = 64u;
static const uint  kGdfMaxInstancesPerBrick   = 16u;
static const uint  kGdfMaxCapsulesPerBrick    = 16u;
static const uint  kInstanceSlotMask16        = 0xFFFFu;
static const uint  kInstanceActiveBitShift    = 16u;
static const uint  kInstanceGroundSlabBit     = 1u << 17u;
static const float kMinHalfExtentMeters       = 1.0e-4;
static const float kSurfaceBandVoxelScale     = 1.45;
static const float kRadiometricBandVoxelScale = 0.92;

struct GpuGlobalDistanceBrickUniforms
{
    float4 CascadeCenterHalfSpan; // xyz = CenterWorld, w = HalfSpanMeters
    int4   CascadeMinCellIndex;   // xyz = MinCellWorld (8-voxel brick aligned), w = CascadeIndex
    uint4  CountsTruncation;      // x = DirtyBrickCount, y = InstanceCount, z = BoneCapsuleCount, w = asuint(TruncationMeters)
    uint4  ShellOptions;          // x = WriteSurfaceShellToggle, y = Reserved, z = Reserved, w = Reserved
};

struct GpuDynamicLocalSdfInstance144
{
    float4 WorldToLocalRow0;
    float4 WorldToLocalRow1;
    float4 WorldToLocalRow2;
    float4 LocalToWorldNormRow0; // w = MinWorldScale
    float4 LocalToWorldNormRow1; // w = VoxelSpacing.z
    float4 LocalToWorldNormRow2;
    float4 LocalMinPad;          // xyz = LocalMin, w = VoxelSpacing.x
    float4 LocalMaxPad;          // xyz = LocalMax, w = VoxelSpacing.y
    uint4  ResolutionOffset;     // x = ResX|(ResY<<16), y = ResZ|(ShapeCode<<16), z = VoxelFloatOffset, w = MaterialSlot|(Active<<16)
};

struct GpuBoneCapsule32
{
    float4 JointStartRadius;
    float4 JointEndRadius;
};

struct GpuBrickCullBucket144
{
    uint4 PackedHeader; // x = PackedBrickCoord, y = CulledInstanceCount, z = CulledCapsuleCount, w = Reserved
    uint  InstanceIndices[16];
    uint  CapsuleIndices[16];
};

uint3 WrapCascadeCell(int3 WorldCell)
{
    return uint3(WorldCell & int3(kGdfCascadeWrapMask));
}

uint3 WrapBrickCell(int3 WorldBrick)
{
    return uint3(WorldBrick & int3(kGdfBrickGridWrapMask));
}

float SmoothMinBrickQuadratic(float A, float B, float BlendK)
{
    if (BlendK <= 1.0e-5)
    {
        return min(A, B);
    }
    float H = saturate(0.5 + 0.5 * (B - A) / BlendK);
    return lerp(B, A, H) - BlendK * H * (1.0 - H);
}

float EvaluateTaperedCapsuleDistance(float3 WorldP, GpuBoneCapsule32 Capsule)
{
    float3 A     = Capsule.JointStartRadius.xyz;
    float3 B     = Capsule.JointEndRadius.xyz;
    float3 Pa    = WorldP - A;
    float3 Ba    = B - A;
    float  LenSq = max(1.0e-6, dot(Ba, Ba));
    float  T     = saturate(dot(Pa, Ba) / LenSq);
    float  Rad   = lerp(Capsule.JointStartRadius.w, Capsule.JointEndRadius.w, T);
    return length(Pa - Ba * T) - Rad;
}

float SampleLocalVolumeSdf(
    StructuredBuffer<float> LocalVoxelBuffer,
    uint                    VoxelFloatOffset,
    uint3                   Resolution,
    float3                  LocalMin,
    float3                  LocalMax,
    float3                  LocalP)
{
    float3 Span     = max(float3(1.0e-5), LocalMax - LocalMin);
    float3 ClampedP = clamp(LocalP, LocalMin, LocalMax);
    float  OutDist  = length(LocalP - ClampedP);

    uint3  SafeRes  = max(uint3(1u), Resolution);
    float3 MaxIdxF  = float3(SafeRes - 1u);
    float3 GridF    = saturate((ClampedP - LocalMin) / Span) * MaxIdxF;
    uint3  Idx0     = min(uint3(floor(GridF)), SafeRes - 1u);
    uint3  Idx1     = min(Idx0 + 1u, SafeRes - 1u);
    float3 Frac     = GridF - float3(Idx0);

    uint StrideY   = SafeRes.x;
    uint StrideZ   = SafeRes.x * SafeRes.y;
    uint Origin000 = VoxelFloatOffset + Idx0.z * StrideZ + Idx0.y * StrideY + Idx0.x;

    // Far exterior fast path: 1 boundary voxel read + exact Euclidean distance (C0 continuous, 8x fewer loads!)
    if (OutDist > 0.25)
    {
        float EdgeD = max(0.0, LocalVoxelBuffer[Origin000]);
        return sqrt(EdgeD * EdgeD + OutDist * OutDist);
    }

    uint OffX      = Idx1.x - Idx0.x;
    uint OffY      = (Idx1.y - Idx0.y) * StrideY;
    uint OffZ      = (Idx1.z - Idx0.z) * StrideZ;

    float C000 = LocalVoxelBuffer[Origin000];
    float C100 = LocalVoxelBuffer[Origin000 + OffX];
    float C010 = LocalVoxelBuffer[Origin000 + OffY];
    float C110 = LocalVoxelBuffer[Origin000 + OffX + OffY];
    float C001 = LocalVoxelBuffer[Origin000 + OffZ];
    float C101 = LocalVoxelBuffer[Origin000 + OffX + OffZ];
    float C011 = LocalVoxelBuffer[Origin000 + OffY + OffZ];
    float C111 = LocalVoxelBuffer[Origin000 + OffX + OffY + OffZ];

    float C00      = lerp(C000, C100, Frac.x);
    float C10      = lerp(C010, C110, Frac.x);
    float C01      = lerp(C001, C101, Frac.x);
    float C11      = lerp(C011, C111, Frac.x);
    float C0       = lerp(C00, C10, Frac.y);
    float C1       = lerp(C01, C11, Frac.y);
    float GridDist = lerp(C0, C1, Frac.z);
    return (OutDist > 1.0e-5)
        ? sqrt(max(0.0, GridDist) * max(0.0, GridDist) + OutDist * OutDist)
        : GridDist;
}

float EvaluatePrimitiveLocalSdf(uint ShapeCode, float3 HalfExt, float3 LocalP)
{
    if (ShapeCode == 2u) // Sphere
    {
        float Rad = max(kMinHalfExtentMeters, max(HalfExt.x, max(HalfExt.y, HalfExt.z)));
        return length(LocalP) - Rad;
    }
    if (ShapeCode == 3u) // Cylinder (Z-axis)
    {
        float  Rad = max(kMinHalfExtentMeters, max(HalfExt.x, HalfExt.y));
        float  Hz  = max(kMinHalfExtentMeters, HalfExt.z);
        float2 D   = float2(length(LocalP.xy) - Rad, abs(LocalP.z) - Hz);
        return min(max(D.x, D.y), 0.0) + length(max(D, float2(0.0)));
    }
    if (ShapeCode == 4u) // Cone (Z-axis)
    {
        float  Rad = max(kMinHalfExtentMeters, max(HalfExt.x, HalfExt.y));
        float  Hz  = max(kMinHalfExtentMeters, HalfExt.z);
        float  H2  = 2.0 * Hz;
        float2 Q   = float2(length(LocalP.xy), LocalP.z - Hz);
        float2 K2  = float2(-Rad, H2);
        float2 Ca  = float2(Q.x - min(Q.x, (Q.y < 0.0) ? 0.0 : Rad), abs(Q.y + Hz) - Hz);
        float2 Cb  = Q + K2 * saturate(dot(-Q, K2) / max(1.0e-8, dot(K2, K2)));
        float  Sgn = (Cb.x < 0.0 && Ca.y < 0.0) ? -1.0 : 1.0;
        return Sgn * sqrt(max(0.0, min(dot(Ca, Ca), dot(Cb, Cb))));
    }
    if (ShapeCode == 5u) // Torus (XY-plane)
    {
        float  MinorR = max(kMinHalfExtentMeters, HalfExt.z);
        float  MajorR = max(MinorR, max(HalfExt.x, HalfExt.y) - MinorR);
        float2 Q      = float2(length(LocalP.xy) - MajorR, LocalP.z);
        return length(Q) - MinorR;
    }
    float3 Q = abs(LocalP) - HalfExt;
    return length(max(Q, float3(0.0))) + min(max(Q.x, max(Q.y, Q.z)), 0.0);
}

float EvaluateDynamicInstanceLocalSdf(
    StructuredBuffer<float>        LocalVoxelBuffer,
    GpuDynamicLocalSdfInstance144  Inst,
    uint                           ShapeCode,
    uint3                          Resolution,
    float3                         LocalP)
{
    if (ShapeCode == 0u)
    {
        return SampleLocalVolumeSdf(
            LocalVoxelBuffer,
            Inst.ResolutionOffset.z,
            Resolution,
            Inst.LocalMinPad.xyz,
            Inst.LocalMaxPad.xyz,
            LocalP);
    }
    float3 BoxCenter = 0.5 * (Inst.LocalMinPad.xyz + Inst.LocalMaxPad.xyz);
    float3 BoxHalf   = max(float3(kMinHalfExtentMeters), 0.5 * (Inst.LocalMaxPad.xyz - Inst.LocalMinPad.xyz));
    return EvaluatePrimitiveLocalSdf(ShapeCode, BoxHalf, LocalP - BoxCenter);
}

uint PackBrickOctNormal32(float3 N)
{
    float  InvL1 = 1.0 / max(1.0e-6, abs(N.x) + abs(N.y) + abs(N.z));
    float2 Oct   = N.xy * InvL1;
    if (N.z < 0.0)
    {
        float2 SignNotZero = float2((Oct.x >= 0.0) ? 1.0 : -1.0, (Oct.y >= 0.0) ? 1.0 : -1.0);
        Oct = (1.0 - abs(Oct.yx)) * SignNotZero;
    }
    uint UX = uint(clamp((Oct.x * 0.5 + 0.5) * 65535.0 + 0.5, 0.0, 65535.0));
    uint UY = uint(clamp((Oct.y * 0.5 + 0.5) * 65535.0 + 0.5, 0.0, 65535.0));
    return (UX & 0xFFFFu) | ((UY & 0xFFFFu) << 16u);
}
