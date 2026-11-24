/*============================================================================================================================================
                                                      INSPECTORPREVIEWCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Unified Pure-GPU Compute Baker for all Inspector previews, LUTs, sky domes, clouds, stars, fogs, rainbows, UVs & SDF slices

struct InspectorPreviewUniforms
{
    uint4  DimensionsAndMode; // x = WidthPx, y = HeightPx, z = ModeIndex, w = Flags
    float4 ParamA;            // Mode-specific scalar params 0..3
    float4 ParamB;            // Mode-specific scalar params 4..7
    float4 ColourA;           // Mode-specific RGB tint A + alpha
    float4 ColourB;           // Mode-specific RGB tint B + alpha
};

[[vk::binding(0, 0)]] ConstantBuffer<InspectorPreviewUniforms> Params;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint>                 OutPixelsRgba8;

uint PackRgba8(float3 Rgb, float Alpha)
{
    uint R = uint(clamp(Rgb.r * 255.0 + 0.5, 0.0, 255.0));
    uint G = uint(clamp(Rgb.g * 255.0 + 0.5, 0.0, 255.0));
    uint B = uint(clamp(Rgb.b * 255.0 + 0.5, 0.0, 255.0));
    uint A = uint(clamp(Alpha * 255.0 + 0.5, 0.0, 255.0));
    return R | (G << 8u) | (B << 16u) | (A << 24u);
}

float Hash21(float2 P)
{
    float H = dot(P, float2(127.1, 311.7));
    return frac(sin(H) * 43758.5453123);
}

float ValueNoise2D(float2 P)
{
    float2 I = floor(P);
    float2 F = frac(P);
    float2 U = F * F * (3.0 - 2.0 * F);
    float A = Hash21(I + float2(0.0, 0.0));
    float B = Hash21(I + float2(1.0, 0.0));
    float C = Hash21(I + float2(0.0, 1.0));
    float D = Hash21(I + float2(1.0, 1.0));
    return lerp(lerp(A, B, U.x), lerp(C, D, U.x), U.y);
}

float Fbm2D(float2 P)
{
    float Sum = 0.0;
    float Amp = 0.52;
    for (int I = 0; I < 4; ++I)
    {
        Sum += Amp * ValueNoise2D(P);
        P = P * 2.04 + float2(13.7, 7.3);
        Amp *= 0.5;
    }
    return Sum;
}

float3 EvaluateSkyGradient(float Elevation01, float Rayleigh, float Mie, float Ozone, float SunElev)
{
    float H = clamp(Elevation01, 0.0, 1.0);
    float3 Zenith  = float3(0.07, 0.22, 0.52) * clamp(Rayleigh, 0.2, 2.5);
    float3 Horizon = float3(0.46, 0.64, 0.84) + float3(0.24, 0.14, 0.04) * clamp(Mie, 0.0, 2.5);
    float3 Sky = lerp(Horizon, Zenith, pow(H, 0.55));
    float Twilight = saturate(1.0 - abs(SunElev) / 28.0);
    Sky = lerp(Sky, Sky * float3(1.18, 0.84, 0.68), Twilight * (1.0 - H) * 0.55);
    Sky *= float3(1.0 - 0.08 * Ozone, 1.0 - 0.14 * Ozone, 1.0);
    return saturate(Sky);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 Tid : SV_DispatchThreadID)
{
    uint W = max(1u, Params.DimensionsAndMode.x);
    uint H = max(1u, Params.DimensionsAndMode.y);
    if (Tid.x >= W || Tid.y >= H) return;

    uint Mode = Params.DimensionsAndMode.z;
    float U = (float(Tid.x) + 0.5) / float(W);
    float V = (float(Tid.y) + 0.5) / float(H);
    float3 Col = float3(0.08, 0.08, 0.09);

    if (Mode == 0u) // Atmosphere Limb / Horizon preview
    {
        float Ray = Params.ParamA.x;
        float Mie = Params.ParamA.y;
        float Oz  = Params.ParamA.z;
        float SunE = Params.ParamA.w;
        if (V < 0.72)
        {
            float Elev = (0.72 - V) / 0.72;
            Col = EvaluateSkyGradient(Elev, Ray, Mie, Oz, SunE);
            float SunX = 0.52;
            float SunY = clamp(0.72 - (SunE / 75.0) * 0.62, 0.08, 0.70);
            float D = length(float2((U - SunX) * 1.6, V - SunY));
            Col += float3(1.0, 0.88, 0.68) * exp(-D * D / 0.018) * (0.45 + 0.25 * Mie);
            Col += float3(1.0, 0.96, 0.88) * exp(-D * D / 0.0008);
        }
        else
        {
            float T = (V - 0.72) / 0.28;
            Col = lerp(float3(0.14, 0.16, 0.18), float3(0.05, 0.06, 0.07), T);
        }
    }
    else if (Mode == 1u) // Atmosphere Outer-Space Globe
    {
        float2 P = float2((U - 0.5) * (float(W) / float(H)), V - 0.5) * 2.25;
        float R = length(P);
        Col = float3(0.02, 0.025, 0.04);
        float Halo = exp(-max(0.0, R - 0.74) * 14.0) * 0.85;
        Col += float3(0.28, 0.58, 0.98) * Halo * clamp(Params.ParamA.x, 0.2, 2.5);
        if (R < 0.74)
        {
            float Z = sqrt(max(0.0, 0.74 * 0.74 - R * R)) / 0.74;
            float3 N = normalize(float3(P.x / 0.74, -P.y / 0.74, Z));
            float3 L = normalize(float3(0.68, 0.36, 0.64));
            float NdotL = max(0.0, dot(N, L));
            float Limb = pow(1.0 - Z, 2.8);
            Col = float3(0.06, 0.16, 0.30) * (0.14 + 0.86 * NdotL) + float3(0.32, 0.66, 1.0) * Limb * (0.35 + 0.65 * NdotL);
        }
    }
    else if (Mode == 2u) // Transmittance LUT (256x64)
    {
        float Ray = max(0.1, Params.ParamA.x);
        float Mie = max(0.0, Params.ParamA.y);
        float Oz  = max(0.0, Params.ParamA.z);
        float Mu  = U * 2.0 - 1.0;
        float Alt = 1.0 - V;
        float AirMass = 1.0 / max(0.08, saturate(Mu * 0.5 + 0.5) * 0.85 + Alt * 0.45);
        float3 Tau = (float3(0.14, 0.32, 0.78) * Ray + float3(0.18, 0.18, 0.18) * Mie + float3(0.06, 0.14, 0.02) * Oz) * AirMass * (1.0 - Alt * 0.78);
        Col = exp(-Tau);
    }
    else if (Mode == 3u) // SkyView LUT (256x128)
    {
        float Elev = 1.0 - V;
        Col = EvaluateSkyGradient(Elev, Params.ParamA.x, Params.ParamA.y, Params.ParamA.z, Params.ParamA.w);
        float AzDiff = abs(U - 0.5) * 3.14159;
        float SunE = clamp(Params.ParamA.w / 90.0, 0.02, 0.95);
        float Ang = length(float2(AzDiff * 0.55, (Elev - SunE) * 1.4));
        Col += float3(1.0, 0.84, 0.62) * exp(-Ang * Ang / 0.04) * 0.65;
    }
    else if (Mode == 4u) // Solar Now / Bake LUT
    {
        float TempNorm = clamp((Params.ParamA.x - 2000.0) / 8000.0, 0.0, 1.0);
        float3 SunCol = lerp(float3(1.0, 0.54, 0.22), float3(0.78, 0.88, 1.0), TempNorm);
        float Elev = 1.0 - V;
        Col = lerp(float3(0.08, 0.10, 0.16), float3(0.16, 0.28, 0.48), Elev);
        float D = length(float2((U - 0.5) * (float(W) / float(H)), V - 0.45));
        float DiscR = clamp(Params.ParamA.y * 0.08, 0.05, 0.24);
        Col += SunCol * exp(-D * D / (DiscR * DiscR * 4.5)) * 0.75;
        if (D < DiscR) Col = lerp(SunCol * 0.9, float3(1.0, 0.98, 0.94), sqrt(max(0.0, 1.0 - (D / DiscR) * (D / DiscR))));
    }
    else if (Mode == 5u) // Volumetric Cloud Swath / Deck
    {
        float Cov = clamp(Params.ParamA.x, 0.0, 1.0);
        float Den = clamp(Params.ParamA.y, 0.1, 3.0);
        float N = Fbm2D(float2(U * 5.2 + Params.ParamA.z, V * 3.4));
        float Cloud = smoothstep(1.0 - Cov, 1.0 - Cov * 0.35 + 0.18, N);
        float3 Sky = EvaluateSkyGradient(1.0 - V, 1.0, 0.5, 0.8, 32.0);
        float3 CCol = lerp(float3(0.42, 0.46, 0.54), float3(0.95, 0.96, 0.98), saturate(1.0 - V * 0.65));
        Col = lerp(Sky, CCol, saturate(Cloud * Den * 0.85));
    }
    else if (Mode == 6u) // Lunar Phase Globe
    {
        float2 P = float2((U - 0.5) * (float(W) / float(H)), V - 0.5) * 2.3;
        float R = length(P);
        Col = float3(0.04, 0.05, 0.08);
        if (R < 0.78)
        {
            float Z = sqrt(max(0.0, 0.78 * 0.78 - R * R)) / 0.78;
            float3 N = normalize(float3(P.x / 0.78, -P.y / 0.78, Z));
            float PhaseRad = Params.ParamA.x * 6.2831853;
            float3 L = normalize(float3(sin(PhaseRad), 0.14, cos(PhaseRad)));
            float Lit = smoothstep(-0.08, 0.25, dot(N, L));
            float Craters = 0.76 + 0.24 * Fbm2D(P * 5.5 + float2(2.3, 4.1));
            Col = Params.ColourA.rgb * Craters * (0.06 + 0.94 * Lit);
        }
    }
    else if (Mode == 7u) // Starfield Chart / Star Sheet Bake
    {
        Col = float3(0.025, 0.032, 0.058) + float3(0.04, 0.05, 0.09) * exp(-pow((U - 0.5) * 2.6 + (V - 0.5) * 1.4, 2.0) * 3.5);
        float2 Cell = floor(float2(U * 48.0, V * 28.0));
        float2 Local = frac(float2(U * 48.0, V * 28.0)) - 0.5;
        float Seed = Hash21(Cell);
        if (Seed > 0.84)
        {
            float Bright = pow((Seed - 0.84) / 0.16, 1.8) * clamp(Params.ParamA.x, 0.3, 3.0);
            float D = length(Local);
            float3 StarCol = lerp(float3(1.0, 0.82, 0.64), float3(0.72, 0.86, 1.0), Hash21(Cell + 19.3));
            Col += StarCol * exp(-D * D * 42.0) * Bright;
        }
    }
    else if (Mode == 8u) // Fog Layer / Ridge / Glow
    {
        float Dens = clamp(Params.ParamA.x, 0.0, 1.0);
        float Falloff = clamp(Params.ParamA.y, 0.1, 4.0);
        float Alt = 1.0 - V;
        float Fog = saturate(exp(-Alt * Falloff * 2.2) * (0.25 + 0.75 * Dens));
        float3 Bg = lerp(float3(0.10, 0.14, 0.20), float3(0.05, 0.07, 0.12), Alt);
        Col = lerp(Bg, Params.ColourA.rgb, Fog);
    }
    else if (Mode == 9u) // Rainbow Optical Arc
    {
        float2 P = float2((U - 0.5) * 1.65, 0.94 - V);
        float R = length(P);
        Col = EvaluateSkyGradient(1.0 - V, 0.9, 0.4, 0.7, 25.0) * 0.55;
        float Band = (R - 0.54) / 0.12;
        if (Band >= 0.0 && Band <= 1.0)
        {
            float3 Spec = float3(
                smoothstep(0.50, 0.90, Band),
                sin(Band * 3.14159),
                1.0 - smoothstep(0.10, 0.55, Band));
            float Env = sin(Band * 3.14159) * clamp(Params.ParamA.x, 0.1, 2.0);
            Col += Spec * Env * 0.68;
        }
    }
    else if (Mode == 10u) // Environment Light IBL Dome
    {
        float Elev = 1.0 - V;
        Col = EvaluateSkyGradient(Elev, 1.0, 0.45, 0.8, 34.0) * clamp(Params.ParamA.x, 0.2, 2.5);
        if (V > 0.64)
        {
            Col = lerp(Col * 0.5, float3(0.08, 0.08, 0.09), saturate((V - 0.64) / 0.36));
        }
    }
    else if (Mode == 11u) // Geometry SDF Slice (cyan interior + glowing amber iso-surface)
    {
        float2 P = (float2(U, V) - 0.5) * float2(float(W) / float(H), 1.0);
        float HalfS = 0.36;
        float SD = max(abs(P.x), abs(P.y)) - HalfS;
        Col = float3(26.0, 32.0, 42.0) / 255.0;
        if (SD < 0.0)
        {
            float Depth = saturate(-SD / (HalfS * 0.55));
            Col = (float3(62.0, 182.0, 224.0) + Depth * float3(42.0, 34.0, 24.0)) / 255.0;
        }
        float Iso = exp(-pow(SD / 0.011, 2.0));
        Col = lerp(Col, float3(244.0, 174.0, 76.0) / 255.0, Iso);
    }
    else if (Mode == 12u) // Geometry UV Atlas (6-island UDIM 1001 + thumbnails)
    {
        static const float3 IslandCols[6] = {
            float3(0.34, 0.56, 0.58), float3(0.58, 0.32, 0.48),
            float3(0.44, 0.32, 0.58), float3(0.54, 0.50, 0.28),
            float3(0.56, 0.30, 0.34), float3(0.30, 0.54, 0.40)
        };
        Col = float3(16.0, 16.0, 18.0) / 255.0;
        if (U < 0.72)
        {
            float LU = U / 0.72;
            uint DarkChk = ((uint(LU * 8.0) + uint(V * 8.0)) & 1u);
            Col = (DarkChk == 0u) ? (float3(24.0, 25.0, 28.0) / 255.0) : (float3(38.0, 39.0, 43.0) / 255.0);
            if (LU < 0.666)
            {
                uint C = (LU < 0.333) ? 0u : 1u;
                uint R = min(2u, uint(V * 3.0));
                Col = lerp(Col, IslandCols[R * 2u + C], 0.58);
            }
        }
        else if (U > 0.75)
        {
            float Fv = frac(V * 3.0);
            if (Fv > 0.06 && Fv < 0.94)
            {
                uint DarkChk = (((Tid.x / 10u) + (Tid.y / 10u)) & 1u);
                Col = (DarkChk == 0u) ? (float3(22.0, 22.0, 24.0) / 255.0) : (float3(44.0, 44.0, 46.0) / 255.0);
            }
        }
    }

    OutPixelsRgba8[Tid.y * W + Tid.x] = PackRgba8(saturate(Col), 1.0);
}
