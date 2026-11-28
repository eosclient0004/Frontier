/*============================================================================================================================================
                                                        THUMBNAILSPECIMENCOMPUTE.SLANG
============================================================================================================================================*/
// Pure-GPU Vulkan compute baker for 128x128 OpenPBR material shader-balls, 3D geometry silhouettes,
// luminaire/camera specimens, and Outliner per-entry baked icons.
// Eliminates startup & UI-thread CPU raytracing stalls inside ThumbnailSheetRepository.

struct ThumbnailSpecimenUniform
{
    uint4  DimCategoryMode; // x=Width, y=Height, z=CategoryCode, w=ShapeOrPresetSubMode (0..9)
    float4 BaseTintAlpha;   // rgb=Linear/sRGB base tint [0..1], w=Metallic [0..1]
    float4 SurfaceParams;   // x=Roughness, y=Clearcoat, z=Transmission, w=Emission
};

[[vk::binding(0, 0)]] ConstantBuffer<ThumbnailSpecimenUniform> Params;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint>                 OutPixelsRgba8;

uint PackRgba8(float3 Col, float Alpha)
{
    uint R = uint(clamp(Col.r * 255.0 + 0.5, 0.0, 255.0));
    uint G = uint(clamp(Col.g * 255.0 + 0.5, 0.0, 255.0));
    uint B = uint(clamp(Col.b * 255.0 + 0.5, 0.0, 255.0));
    uint A = uint(clamp(Alpha * 255.0 + 0.5, 0.0, 255.0));
    return R | (G << 8u) | (B << 16u) | (A << 24u);
}

float3 LinearToSrgb(float3 C)
{
    return pow(max(C, float3(0.0, 0.0, 0.0)), float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
}

float EvaluateStudioEnvironment(float3 Dir)
{
    float Sky = saturate(Dir.y * 0.5 + 0.5);
    float StripA = exp(-pow((Dir.x - 0.52) * 3.8, 2.0)) * exp(-pow((Dir.y - 0.62) * 3.2, 2.0));
    float StripB = exp(-pow((Dir.x + 0.64) * 3.4, 2.0)) * exp(-pow((Dir.y - 0.25) * 4.2, 2.0));
    return 0.14 + 0.28 * Sky + 1.85 * StripA + 0.85 * StripB;
}

float SdRoundedBox(float3 P, float3 HalfExt, float Rad)
{
    float3 Q = abs(P) - HalfExt;
    return length(max(Q, 0.0)) + min(max(Q.x, max(Q.y, Q.z)), 0.0) - Rad;
}

float SdCylinderZ(float3 P, float Radius, float HalfLen)
{
    float2 D = float2(length(P.xy) - Radius, abs(P.z) - HalfLen);
    return min(max(D.x, D.y), 0.0) + length(max(D, 0.0));
}

float SdCylinderX(float3 P, float Radius, float HalfLen)
{
    float2 D = float2(length(P.yz) - Radius, abs(P.x) - HalfLen);
    return min(max(D.x, D.y), 0.0) + length(max(D, 0.0));
}

float SmoothMin(float A, float B, float K)
{
    float H = saturate(0.5 + 0.5 * (B - A) / K);
    return lerp(B, A, H) - K * H * (1.0 - H);
}

float3 RotateYawPitch(float3 P, float YawRad, float PitchRad)
{
    float Cy = cos(YawRad), Sy = sin(YawRad);
    float3 Q = float3(Cy * P.x + Sy * P.z, P.y, -Sy * P.x + Cy * P.z);
    float Cp = cos(PitchRad), Sp = sin(PitchRad);
    return float3(Q.x, Cp * Q.y - Sp * Q.z, Sp * Q.y + Cp * Q.z);
}

// Returns float2(signedDistance, partTag: 0=primaryBody, 1=accentDarkHousing/Wheels, 2=emissiveLens/Diffuser)
float2 EvaluateSpecimenField(float3 P, uint ShapeKind)
{
    if (ShapeKind == 1u) // Chamfered Box / Workbench / Cabinet
    {
        float3 Q = RotateYawPitch(P, 0.58, 0.22);
        return float2(SdRoundedBox(Q, float3(0.46, 0.42, 0.46), 0.07), 0.0);
    }
    if (ShapeKind == 2u) // Torus / Tire / Parabolic Ring
    {
        float3 Q = RotateYawPitch(P, 0.45, 0.35);
        float2 T = float2(length(Q.xy) - 0.44, Q.z);
        return float2(length(T) - 0.17, 0.0);
    }
    if (ShapeKind == 3u) // Cone / Tripod Stand
    {
        float H = 0.60;
        float2 Q = float2(length(P.xz), P.y + 0.32);
        float Slope = (Q.x - (1.0 - saturate(Q.y / (2.0 * H))) * 0.44);
        return float2(max(Slope * 0.85, max(-Q.y, Q.y - 2.0 * H)), 0.0);
    }
    if (ShapeKind == 4u) // Cylinder / Hydraulic Column / Pillar
    {
        float3 Q = RotateYawPitch(P, 0.30, 0.18);
        float2 D = float2(length(Q.xz) - 0.36, abs(Q.y) - 0.48);
        return float2(length(max(D, 0.0)) + min(max(D.x, D.y), 0.0) - 0.05, 0.0);
    }
    if (ShapeKind == 5u) // Automotive Coupe Silhouette (3/4 Hero Angle)
    {
        float3 Q = RotateYawPitch(P + float3(0.0, 0.08, 0.0), -0.62, 0.16);
        float Body   = SdRoundedBox(Q - float3(0.0, 0.01, 0.0), float3(0.64, 0.135, 0.30), 0.055);
        float Cabin  = SdRoundedBox(Q - float3(-0.07, 0.19, 0.0), float3(0.27, 0.105, 0.22), 0.065);
        float Hood   = SdRoundedBox(Q - float3(0.28, 0.055, 0.0), float3(0.32, 0.095, 0.27), 0.055);
        float Shell  = SmoothMin(SmoothMin(Body, Cabin, 0.085), Hood, 0.075);

        float Glass  = SdRoundedBox(Q - float3(-0.04, 0.195, 0.0), float3(0.245, 0.075, 0.232), 0.035);
        float3 WSym  = float3(abs(Q.x) - 0.39, Q.y + 0.11, abs(Q.z) - 0.285);
        float Wheels = SdCylinderZ(WSym, 0.145, 0.065) - 0.015;
        float HeadL  = SdRoundedBox(Q - float3(0.66, 0.04, 0.0), float3(0.025, 0.032, 0.23), 0.014);

        float D = Shell;
        float Tag = 0.0;
        if (Glass  < D + 0.008) { D = min(D, Glass); Tag = 1.0; }
        if (Wheels < D) { D = Wheels; Tag = 1.0; }
        if (HeadL  < D) { D = HeadL;  Tag = 2.0; }
        return float2(D, Tag);
    }
    if (ShapeKind == 6u) // Studio Area Softbox Luminaire
    {
        float3 Q = RotateYawPitch(P, 0.42, 0.18);
        float Bezel    = SdRoundedBox(Q - float3(0.0, 0.0, -0.06), float3(0.56, 0.38, 0.12), 0.04);
        float Diffuser = SdRoundedBox(Q - float3(0.0, 0.0,  0.07), float3(0.49, 0.31, 0.03), 0.015);
        float Stand    = SdCylinderX(float3(Q.y + 0.52, Q.x, Q.z + 0.06), 0.045, 0.18);
        float Housing  = min(Bezel, Stand);
        return (Diffuser < Housing) ? float2(Diffuser, 2.0) : float2(Housing, 1.0);
    }
    if (ShapeKind == 7u) // Studio Fresnel / IES Barn-Door Lamp
    {
        float3 Q = RotateYawPitch(P, 0.55, 0.14);
        float Barrel = SdCylinderZ(Q - float3(0.0, 0.05, -0.10), 0.26, 0.28) - 0.03;
        float Hood   = SdRoundedBox(Q - float3(0.0, 0.05,  0.20), float3(0.30, 0.30, 0.10), 0.03);
        float Lens   = length(Q - float3(0.0, 0.05, 0.22)) - 0.21;
        float Stand  = SdCylinderX(float3(Q.y + 0.42, Q.x, Q.z + 0.10), 0.045, 0.24);
        float Body   = min(min(Barrel, Hood), Stand);
        return (Lens < Body) ? float2(Lens, 2.0) : float2(Body, 1.0);
    }
    if (ShapeKind == 8u) // Cinema Camera / Rig
    {
        float3 Q = RotateYawPitch(P + float3(0.0, 0.03, 0.0), -0.58, 0.14);
        float CamBody  = SdRoundedBox(Q - float3(-0.12, -0.02, 0.0), float3(0.34, 0.23, 0.21), 0.04);
        float Handle   = SdRoundedBox(Q - float3(-0.12,  0.29, 0.0), float3(0.22, 0.04, 0.055), 0.02);
        float PostF    = SdRoundedBox(Q - float3( 0.04,  0.22, 0.0), float3(0.04, 0.08, 0.05), 0.015);
        float PostB    = SdRoundedBox(Q - float3(-0.28,  0.22, 0.0), float3(0.04, 0.08, 0.05), 0.015);
        float Lens     = SdCylinderX(Q - float3(0.34, -0.02, 0.0), 0.165, 0.20) - 0.02;
        float MatteBox = SdRoundedBox(Q - float3(0.54, -0.02, 0.0), float3(0.07, 0.22, 0.25), 0.02);
        float Glass    = length(Q - float3(0.57, -0.02, 0.0)) - 0.145;
        float Shell    = min(min(min(CamBody, min(Handle, min(PostF, PostB))), Lens), MatteBox);
        return (Glass < Shell) ? float2(Glass, 2.0) : float2(Shell, 0.0);
    }
    if (ShapeKind == 9u) // Studio Cyclorama / Floor Stage
    {
        float3 Q = RotateYawPitch(P + float3(0.0, 0.10, 0.0), -0.50, 0.22);
        float FloorSlab = SdRoundedBox(Q - float3(0.0, -0.26, 0.0), float3(0.58, 0.06, 0.50), 0.03);
        float BackWall  = SdRoundedBox(Q - float3(0.0,  0.14, -0.44), float3(0.58, 0.42, 0.06), 0.03);
        float Cove      = SmoothMin(FloorSlab, BackWall, 0.22);
        return float2(Cove, 0.0);
    }
    return float2(length(P) - 0.62, 0.0); // Default studio shader-ball / emissive orb sphere
}

float3 EstimateSpecimenNormal(float3 P, uint ShapeKind)
{
    float E = 0.0025;
    return normalize(float3(
        EvaluateSpecimenField(P + float3(E, 0, 0), ShapeKind).x - EvaluateSpecimenField(P - float3(E, 0, 0), ShapeKind).x,
        EvaluateSpecimenField(P + float3(0, E, 0), ShapeKind).x - EvaluateSpecimenField(P - float3(0, E, 0), ShapeKind).x,
        EvaluateSpecimenField(P + float3(0, 0, E), ShapeKind).x - EvaluateSpecimenField(P - float3(0, 0, E), ShapeKind).x));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 Tid : SV_DispatchThreadID)
{
    uint W = Params.DimCategoryMode.x;
    uint H = Params.DimCategoryMode.y;
    if (Tid.x >= W || Tid.y >= H)
    {
        return;
    }

    uint Category  = Params.DimCategoryMode.z;
    uint SubMode   = Params.DimCategoryMode.w;
    float3 BaseCol = clamp(Params.BaseTintAlpha.rgb, 0.02, 1.0);
    float Metallic = saturate(Params.BaseTintAlpha.w);
    float Rough    = clamp(Params.SurfaceParams.x, 0.04, 0.98);
    float Coat     = saturate(Params.SurfaceParams.y);
    float Trans    = saturate(Params.SurfaceParams.z);
    float Emiss    = max(0.0, Params.SurfaceParams.w);

    float2 Uv = (float2(Tid.xy) + 0.5) / float2(float(W), float(H));
    float2 Ndc = float2(Uv.x * 2.0 - 1.0, 1.0 - Uv.y * 2.0);
    float Vign = 1.0 - 0.28 * length(Ndc);
    float3 BgCol = float3(30.0, 32.0, 36.0) / 255.0 * Vign;
    if (Emiss > 0.15)
    {
        // Soft radial studio aura behind emissive orbs and luminaires
        float Halo = exp(-dot(Ndc, Ndc) * 2.6) * min(0.55, Emiss * 0.22);
        BgCol += BaseCol * Halo;
    }

    // 2x2 super-sampled GPU studio shader-ball / specimen raymarch
    float3 Ro    = float3(0.0, 0.14, 2.25);
    float3 KeyL  = normalize(float3(0.58, 0.72, 0.56));
    float3 RimL  = normalize(float3(-0.72, 0.34, -0.45));
    float3 FillL = normalize(float3(-0.35, 0.22, 0.88));

    // Geometry (2) and Photometry (11) honor full specimen SubMode (0..9); Materials (4) uses shader-ball (0)
    uint ShapeKind = (Category == 2u || Category == 11u) ? SubMode : 0u;
    float3 AccumCol = float3(0.0, 0.0, 0.0);
    float  AccumAlpha = 0.0;

    static const float2 Jitter[4] = {
        float2(-0.25, -0.25), float2(0.25, -0.25),
        float2(-0.25,  0.25), float2(0.25,  0.25)
    };

    for (int S = 0; S < 4; ++S)
    {
        float2 Sp = ((float2(Tid.xy) + 0.5 + Jitter[S]) / float2(float(W), float(H))) * 2.0 - 1.0;
        Sp.y = -Sp.y;
        float3 Rd = normalize(float3(Sp.x * 0.45, Sp.y * 0.45 - 0.04, -1.0));

        float T = 0.0;
        bool Hit = false;
        float HitPart = 0.0;
        for (int Step = 0; Step < 48; ++Step)
        {
            float3 P = Ro + Rd * T;
            float2 Field = EvaluateSpecimenField(P, ShapeKind);
            if (Field.x < 0.0018)
            {
                Hit = true;
                HitPart = Field.y;
                break;
            }
            T += max(0.002, Field.x * 0.90);
            if (T > 4.5)
            {
                break;
            }
        }

        if (Hit)
        {
            float3 P = Ro + Rd * T;
            float3 N = EstimateSpecimenNormal(P, ShapeKind);
            float3 V = -Rd;
            float3 R = reflect(Rd, N);
            float NdotV = max(0.001, dot(N, V));
            float Fresnel = pow(1.0 - NdotV, 5.0);

            float3 PartAlbedo = BaseCol;
            float  PartMetal  = Metallic;
            float  PartRough  = Rough;
            float  PartCoat   = Coat;
            float  PartEmiss  = Emiss;

            if (HitPart > 0.5 && HitPart < 1.5)
            {
                // Matte anodized dark housing / rubber tires
                PartAlbedo = float3(0.07, 0.075, 0.085);
                PartMetal  = 0.15;
                PartRough  = 0.52;
                PartCoat   = 0.0;
                PartEmiss  = 0.0;
            }
            else if (HitPart >= 1.5)
            {
                // Glowing diffuser, Fresnel lens, headlight bar, or camera optical glass
                if (ShapeKind == 8u)
                {
                    PartAlbedo = float3(0.22, 0.52, 0.85);
                    PartMetal  = 0.10;
                    PartRough  = 0.08;
                    PartCoat   = 1.0;
                    PartEmiss  = 0.35;
                }
                else
                {
                    PartAlbedo = BaseCol;
                    PartMetal  = 0.0;
                    PartRough  = 0.20;
                    PartCoat   = 0.0;
                    PartEmiss  = max(1.65, Emiss);
                }
            }

            float3 F0 = lerp(float3(0.04, 0.04, 0.04), PartAlbedo, PartMetal);
            float3 F  = F0 + (1.0 - F0) * Fresnel;

            float KeyDiff  = max(0.0, dot(N, KeyL));
            float FillDiff = max(0.0, dot(N, FillL)) * 0.38;
            float RimDiff  = pow(1.0 - NdotV, 2.3) * max(0.0, dot(N, RimL) * 0.5 + 0.5);

            float3 Hk = normalize(KeyL + V);
            float SpecExp  = lerp(180.0, 8.0, PartRough * PartRough);
            float KeySpec  = pow(max(0.0, dot(N, Hk)), SpecExp) * lerp(1.65, 0.16, PartRough);
            float EnvSpec  = EvaluateStudioEnvironment(R) * lerp(0.95, 0.22, PartRough);
            float CoatSpec = pow(max(0.0, dot(N, Hk)), 140.0) * PartCoat * 0.85;

            float3 DiffCol  = PartAlbedo * (1.0 - PartMetal) * (1.0 - Trans * 0.72) * (0.16 + 0.92 * KeyDiff + FillDiff);
            float3 SpecCol  = F * (KeySpec + EnvSpec) + float3(1.0, 0.98, 0.95) * CoatSpec;
            float3 RimCol   = float3(0.55, 0.72, 0.95) * RimDiff * 0.45;
            float3 GlassCol = lerp(float3(0.0, 0.0, 0.0), PartAlbedo * (0.35 + 0.65 * Fresnel), Trans);
            float3 Shaded   = DiffCol + SpecCol + RimCol + GlassCol + PartAlbedo * PartEmiss;

            // ACES-like soft shoulder tone map
            Shaded = Shaded / (Shaded + float3(1.0, 1.0, 1.0));
            AccumCol += LinearToSrgb(Shaded);
            AccumAlpha += 1.0;
        }
        else
        {
            AccumCol += BgCol;
            AccumAlpha += 1.0;
        }
    }

    float3 FinalCol = AccumCol * 0.25;
    OutPixelsRgba8[Tid.y * W + Tid.x] = PackRgba8(FinalCol, 1.0);
}
