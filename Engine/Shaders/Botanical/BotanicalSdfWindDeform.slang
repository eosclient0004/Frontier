//============================================================================================================================================
//                                                      BOTANICALSDFWINDDEFORM.SLANG
//============================================================================================================================================
// Unified Botanical GPU Wind Deformation + Watertight Rest-Space 3D Noisy Wood Capsules + 2.5D Thin-Leaf SDF Shadow Shader
// Mirrors Engine/GeometrySpace/Procedural/Botanical/BotanicalThinLeafSdf.cpp & BotanicalWorldPartition.cpp (Frontier Right-Hand +Z Up).

struct BotanicalWoodCapsuleGpu
{
    float3 P0;
    float  R0;
    float3 P1;
    float  R1;
    float3 RestP0;
    float  WindFlex0;
    float3 RestP1;
    float  WindFlex1;
};

struct BotanicalLeafInstanceGpu
{
    float3 CenterMeters;
    float  HalfSpanMeters;
    float3 AxisX;
    float  CamberX;
    float3 AxisY;
    float  CamberZ;
    float3 AxisZ;
    float  HalfThicknessM;
    float3 RestCenterM;
    float  WindFlex;
    float  Phase;
    uint   Archetype;
    uint   Detached;
    uint   Pad0;
};

struct CanopyBrickHeaderGpu
{
    uint  StartOffset;
    uint  InstanceCount;
    float SigmaX;
    float SigmaY;
    float SigmaZ;
    float Pad0;
};

struct BotanicalWindUniforms
{
    float3 BoundsMin;
    float  TimeSec;
    float3 BoundsMax;
    float  GustFreqHz;
    float3 WindVelocityMps;
    float  GustStrengthMps;
    float  BendAngleXRad;
    float  BendAngleYRad;
    float  RustleAmp;
    float  BarkNoiseAmp;
    float  BarkNoiseFreq;
    uint   CapsuleCount;
    uint   LeafCount;
    uint   BrickRes;
};

ConstantBuffer<BotanicalWindUniforms> gWind : register(b0);
RWStructuredBuffer<BotanicalWoodCapsuleGpu>  gWoodCapsules : register(u0);
RWStructuredBuffer<BotanicalLeafInstanceGpu> gLeaves       : register(u1);
StructuredBuffer<CanopyBrickHeaderGpu>       gBricks       : register(t0);
StructuredBuffer<uint>                       gBrickRefs    : register(t1);
Texture2DArray<float>                        gLeafSdfAtlas : register(t2);
SamplerState                                 gLinearClamp  : register(s0);

// Exact sin-free modulo-289 2-octave 3D FBM matching CPU EvalBarkFbm3d()!
float HashLattice3d(float3 i)
{
    float3 w = i - floor(i * (1.0 / 289.0)) * 289.0;
    float3 p = frac(w * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 19.19);
    return frac((p.x + p.y) * p.z);
}

float SmoothNoise3d(float3 q)
{
    float3 i = floor(q);
    float3 f = q - i;
    float3 u = f * f * (3.0 - 2.0 * f);
    float n000 = HashLattice3d(i + float3(0, 0, 0));
    float n100 = HashLattice3d(i + float3(1, 0, 0));
    float n010 = HashLattice3d(i + float3(0, 1, 0));
    float n110 = HashLattice3d(i + float3(1, 1, 0));
    float n001 = HashLattice3d(i + float3(0, 0, 1));
    float n101 = HashLattice3d(i + float3(1, 0, 1));
    float n011 = HashLattice3d(i + float3(0, 1, 1));
    float n111 = HashLattice3d(i + float3(1, 1, 1));
    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);
    return lerp(lerp(nx00, nx10, u.y), lerp(nx01, nx11, u.y), u.z);
}

float EvalBarkFbm3d(float3 p, float freq)
{
    float oct1 = SmoothNoise3d(float3(p.x * freq, p.y * freq, p.z * freq * 0.46)) * 2.0 - 1.0;
    float f2   = freq * 2.13;
    float oct2 = SmoothNoise3d(float3(p.y * f2 + 11.3, p.x * 0.62 * f2 + 7.1, p.z * f2 + 5.7)) * 2.0 - 1.0;
    return oct1 * 0.68 + oct2 * 0.32;
}

float3 ApplyCantileverWindBend(float3 restPos, float flex, float phase)
{
    float h = max(0.0, restPos.z);
    float rustle = gWind.RustleAmp * flex * sin(gWind.TimeSec * 6.5 + phase * 6.2831853) * 0.04;
    float dx = (gWind.BendAngleXRad * h + rustle) * flex;
    float dy = (gWind.BendAngleYRad * h + rustle * 0.7) * flex;
    float dz = -0.35 * (dx * dx + dy * dy) / max(h, 0.35);
    return float3(restPos.x + dx, restPos.y + dy, max(0.0, restPos.z + dz));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void DeformBotanicalSdfWind(uint3 dtid : SV_DispatchThreadID)
{
    uint idx = dtid.x;
    if (idx < gWind.CapsuleCount)
    {
        BotanicalWoodCapsuleGpu cap = gWoodCapsules[idx];
        cap.P0 = ApplyCantileverWindBend(cap.RestP0, cap.WindFlex0, 0.0);
        cap.P1 = ApplyCantileverWindBend(cap.RestP1, cap.WindFlex1, 0.0);
        gWoodCapsules[idx] = cap;
    }
    if (idx < gWind.LeafCount)
    {
        BotanicalLeafInstanceGpu lf = gLeaves[idx];
        if (lf.Detached == 0u)
        {
            lf.CenterMeters = ApplyCantileverWindBend(lf.RestCenterM, lf.WindFlex, lf.Phase);
            gLeaves[idx] = lf;
        }
    }
}

float EvaluateNoisyCapsuleSdf(BotanicalWoodCapsuleGpu cap, float3 q)
{
    float3 u = cap.P1 - cap.P0;
    float3 w = q - cap.P0;
    float  s = clamp(dot(w, u) / max(dot(u, u), 1.0e-8), 0.0, 1.0);
    float3 axisPt = cap.P0 + u * s;
    float3 diff   = q - axisPt;
    float  dist   = length(diff);
    float  rSmooth = lerp(cap.R0, cap.R1, s);
    if (gWind.BarkNoiseAmp <= 1.0e-4 || dist > rSmooth * 1.45) return dist - rSmooth;
    float3 nHat = (dist > 1.0e-6) ? (diff / dist) : float3(0.0, 0.0, 1.0);
    float3 restAxis = lerp(cap.RestP0, cap.RestP1, s);
    float3 pSkinRest = restAxis + nHat * rSmooth;
    float  rBark = rSmooth * (1.0 + 0.18 * gWind.BarkNoiseAmp * EvalBarkFbm3d(pSkinRest, gWind.BarkNoiseFreq));
    return dist - rBark;
}

float EvaluateCurvedThinLeafSdf(BotanicalLeafInstanceGpu lf, float3 q)
{
    float  invS = 1.0 / max(lf.HalfSpanMeters, 1.0e-4);
    float3 rel  = q - lf.CenterMeters;
    float  u    = dot(rel, lf.AxisX) * invS;
    float  y    = dot(rel, lf.AxisY) * invS;
    float  w    = dot(rel, lf.AxisZ) * invS;
    float2 uv   = clamp(float2(u, w) * 0.5 + 0.5, 0.0, 1.0);
    float  d2d  = lf.HalfSpanMeters * gLeafSdfAtlas.SampleLevel(gLinearClamp, float3(uv, float(lf.Archetype)), 0.0);
    float  camber = lf.CamberX * u * u + lf.CamberZ * w * w;
    float  dSheet = abs(y - camber) * lf.HalfSpanMeters - lf.HalfThicknessM;
    return (d2d <= 0.0) ? max(d2d, dSheet) : length(float2(d2d, max(0.0, dSheet)));
}
