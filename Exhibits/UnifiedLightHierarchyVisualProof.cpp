/*============================================================================================================================================
                                                    UNIFIEDLIGHTHIERARCHYVISUALPROOF.CPP
============================================================================================================================================*/
// 🧩 Real C++20 PBR Raytraced Visual Proof Renderer executing Frontier's Unified 3-System Lighting C++ Engine across:
//    - All 5 Lighting Modes: ExactReference, NaivePointMerge, CubeGridPodsOnly (8x4x4), ToroidalClipmap, UnifiedAll3
//    - All 4 Project Zero Debug Views: Debug 23 (Error x8), Debug 24 (Toroidal Fine/Coarse), Debug 25 (Static vs Dynamic), Debug 26 (Pods & LOD)
//    - Close-Up Headlamp Pod Proof: Ground Truth vs Naive Center-Collapse vs Sparse 8x4x4 Residual-Optimized Cube-Grid Pod

#include "Engine/GeometricRaster/DirectLighting/CubeGridPodAtlas.h"
#include "Engine/GeometricRaster/DirectLighting/StaticToroidalLightClipmap.h"
#include "Engine/GeometricRaster/DirectLighting/UnifiedLightHierarchy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

using namespace Frontier::GeometricRaster;

inline constexpr float PiF = 3.14159265358979323846f;
inline constexpr float WorldHalfExtent = 12.0f;

struct SceneBox
{
    std::array<float, 3> Min;
    std::array<float, 3> Max;
    std::array<float, 3> Albedo;
    float                Roughness;
};

// Same physical layout as LightHierarchyStudio (Car body + twin shadow-test bollards + lantern pedestal + neon divider wall)
const std::vector<SceneBox> kSceneBoxes = {
    // Glossy car body behind dual headlights
    { { -1.15f, 0.0f, -4.35f }, {  1.15f, 1.32f, -2.15f }, { 0.24f, 0.28f, 0.36f }, 0.22f },
    // Front shadow-test bollards directly in front of Left & Right headlights!
    { { -0.82f, 0.0f,  0.55f }, { -0.44f, 1.48f,  0.95f }, { 0.64f, 0.66f, 0.70f }, 0.34f },
    { {  0.44f, 0.0f,  0.55f }, {  0.82f, 1.48f,  0.95f }, { 0.64f, 0.66f, 0.70f }, 0.34f },
    // Warm lantern cluster pedestal & vertical pillar
    { { -5.80f, 0.0f, -2.55f }, { -4.60f, 0.90f, -1.35f }, { 0.42f, 0.38f, 0.34f }, 0.62f },
    { { -5.36f, 0.9f, -2.11f }, { -5.04f, 2.25f, -1.79f }, { 0.30f, 0.30f, 0.32f }, 0.50f },
    // Neon opposing-strip divider wall (East cyan vs West red)
    { {  5.12f, 0.0f, -3.70f }, {  5.48f, 2.75f,  0.10f }, { 0.50f, 0.52f, 0.56f }, 0.28f },
    // Far distance-LOD test monolith (tests far headlight merge)
    { { -1.20f, 0.0f,  7.40f }, {  1.20f, 2.20f,  8.20f }, { 0.46f, 0.48f, 0.52f }, 0.40f },
    // Courtyard columns
    { { -4.40f, 0.0f,  4.40f }, { -3.50f, 2.80f,  5.30f }, { 0.40f, 0.43f, 0.48f }, 0.46f },
    { {  3.50f, 0.0f,  4.40f }, {  4.40f, 2.80f,  5.30f }, { 0.40f, 0.43f, 0.48f }, 0.46f },
};

[[nodiscard]] float Dot3(const std::array<float, 3>& A, const std::array<float, 3>& B) noexcept
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

[[nodiscard]] std::array<float, 3> Normalize3(const std::array<float, 3>& V, const std::array<float, 3>& Fallback = { 0.0f, 1.0f, 0.0f }) noexcept
{
    const float L = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
    if (L <= 1.0e-6f) return Fallback;
    return { V[0] / L, V[1] / L, V[2] / L };
}

[[nodiscard]] std::array<float, 3> Cross3(const std::array<float, 3>& A, const std::array<float, 3>& B) noexcept
{
    return {
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0]
    };
}

[[nodiscard]] float SmoothStep(float E0, float E1, float X) noexcept
{
    const float T = std::clamp((X - E0) / std::max(1.0e-5f, E1 - E0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] float IntersectBox(
    const std::array<float, 3>& Ro,
    const std::array<float, 3>& Rd,
    const SceneBox&             Box,
    std::array<float, 3>&       OutN) noexcept
{
    float TMin = -1.0e9f;
    float TMax =  1.0e9f;
    int HitAxis = -1;
    float HitSign = 1.0f;

    for (int A = 0; A < 3; ++A)
    {
        if (std::fabs(Rd[A]) < 1.0e-6f)
        {
            if (Ro[A] < Box.Min[A] || Ro[A] > Box.Max[A]) return -1.0f;
            continue;
        }
        const float InvD = 1.0f / Rd[A];
        float T0 = (Box.Min[A] - Ro[A]) * InvD;
        float T1 = (Box.Max[A] - Ro[A]) * InvD;
        float Sign = -1.0f;
        if (T0 > T1)
        {
            std::swap(T0, T1);
            Sign = 1.0f;
        }
        if (T0 > TMin)
        {
            TMin = T0;
            HitAxis = A;
            HitSign = Sign;
        }
        TMax = std::min(TMax, T1);
        if (TMin > TMax) return -1.0f;
    }

    if (TMin <= 0.0f || HitAxis < 0) return -1.0f;
    OutN = { 0.0f, 0.0f, 0.0f };
    OutN[HitAxis] = HitSign;
    return TMin;
}

[[nodiscard]] float TraceShadowRay(const std::array<float, 3>& P, const std::array<float, 3>& LightPos) noexcept
{
    const std::array<float, 3> ToL { LightPos[0] - P[0], LightPos[1] - P[1], LightPos[2] - P[2] };
    const float Dist = std::sqrt(Dot3(ToL, ToL));
    if (Dist < 0.03f) return 1.0f;
    const std::array<float, 3> Rd { ToL[0] / Dist, ToL[1] / Dist, ToL[2] / Dist };
    const std::array<float, 3> Ro { P[0] + Rd[0] * 0.02f, P[1] + Rd[1] * 0.02f, P[2] + Rd[2] * 0.02f };
    std::array<float, 3> TmpN {};
    for (const SceneBox& B : kSceneBoxes)
    {
        const float Hit = IntersectBox(Ro, Rd, B, TmpN);
        if (Hit > 0.0f && Hit < Dist - 0.05f) return 0.0f;
    }
    return 1.0f;
}

[[nodiscard]] std::array<float, 3> ShadePbrLight(
    const std::array<float, 3>& P,
    const std::array<float, 3>& N,
    const std::array<float, 3>& V,
    const std::array<float, 3>& Albedo,
    float                       Rough,
    const std::array<float, 3>& LightPos,
    const std::array<float, 3>& LightDir,
    const std::array<float, 3>& Color,
    float                       Flux,
    float                       CosCone,
    float                       InfluenceRadius,
    bool                        EceCutoff = false) noexcept
{
    const std::array<float, 3> ToL { LightPos[0] - P[0], LightPos[1] - P[1], LightPos[2] - P[2] };
    const float DistSq = Dot3(ToL, ToL) + 0.35f;
    const float Dist = std::sqrt(DistSq);
    const std::array<float, 3> L { ToL[0] / Dist, ToL[1] / Dist, ToL[2] / Dist };
    const float NdotL = std::max(Dot3(N, L), 0.0f);
    if (NdotL <= 0.0f) return { 0.0f, 0.0f, 0.0f };

    const float Window = EvaluateQuarticInfluenceWindow(Dist, InfluenceRadius);
    if (Window <= 1.0e-4f) return { 0.0f, 0.0f, 0.0f };

    float Spot = 1.0f;
    if (CosCone > -0.95f)
    {
        const float CosAng = Dot3({ -L[0], -L[1], -L[2] }, LightDir);
        Spot = SmoothStep(CosCone, std::min(0.99f, CosCone + 0.30f), CosAng);
        if (Spot <= 1.0e-3f) return { 0.0f, 0.0f, 0.0f };
    }
    if (EceCutoff)
    {
        const auto Right = Normalize3(Cross3(LightDir, { 0.0f, 1.0f, 0.0f }), { 1.0f, 0.0f, 0.0f });
        const auto Up    = Normalize3(Cross3(Right, LightDir), { 0.0f, 1.0f, 0.0f });
        const float Lat  = Dot3({ -L[0], -L[1], -L[2] }, Right);
        const float Elev = Dot3({ -L[0], -L[1], -L[2] }, Up);
        const float Cut  = (Lat <= 0.0f) ? -0.018f : (-0.018f + Lat * 0.268f);
        Spot *= 1.0f - SmoothStep(Cut, Cut + 0.06f, Elev);
    }

    const float Vis = TraceShadowRay({ P[0] + N[0] * 0.015f, P[1] + N[1] * 0.015f, P[2] + N[2] * 0.015f }, LightPos);
    if (Vis <= 0.0f) return { 0.0f, 0.0f, 0.0f };

    const auto H = Normalize3({ L[0] + V[0], L[1] + V[1], L[2] + V[2] }, N);
    const float NdotH = std::max(Dot3(N, H), 0.0f);
    const float NdotV = std::max(Dot3(N, V), 0.05f);
    const float VdotH = std::max(Dot3(V, H), 0.0f);
    const float A = std::max(0.06f, Rough * Rough);
    const float A2 = A * A;
    const float Denom = NdotH * NdotH * (A2 - 1.0f) + 1.0f;
    const float D = A2 / (PiF * Denom * Denom + 1.0e-5f);
    const float F = 0.04f + 0.96f * std::pow(1.0f - VdotH, 5.0f);
    const float Spec = (D * F) / (4.0f * NdotV * NdotL + 0.15f);

    const float Rad = (Flux * Spot * Vis * Window) / (DistSq * PiF);
    return {
        Color[0] * Rad * (Albedo[0] * NdotL + Spec * NdotL * 0.85f),
        Color[1] * Rad * (Albedo[1] * NdotL + Spec * NdotL * 0.85f),
        Color[2] * Rad * (Albedo[2] * NdotL + Spec * NdotL * 0.85f)
    };
}

[[nodiscard]] std::array<float, 3> ToneMapSrgb(const std::array<float, 3>& Hdr) noexcept
{
    std::array<float, 3> Out {};
    for (int C = 0; C < 3; ++C)
    {
        const float V = std::max(0.0f, Hdr[C]);
        const float T = V / (1.0f + V);
        Out[C] = std::pow(T, 1.0f / 2.2f);
    }
    return Out;
}

[[nodiscard]] std::array<float, 3> InfernoPalette(float T) noexcept
{
    T = std::clamp(T, 0.0f, 1.0f);
    constexpr float C0[3] = { 0.03f, 0.03f, 0.07f };
    constexpr float C1[3] = { 0.36f, 0.07f, 0.43f };
    constexpr float C2[3] = { 0.84f, 0.28f, 0.15f };
    constexpr float C3[3] = { 0.99f, 0.88f, 0.25f };
    const auto Lerp3 = [](const float A[3], const float B[3], float K) -> std::array<float, 3> {
        return { A[0] + (B[0] - A[0]) * K, A[1] + (B[1] - A[1]) * K, A[2] + (B[2] - A[2]) * K };
    };
    if (T < 0.33f) return Lerp3(C0, C1, T / 0.33f);
    if (T < 0.66f) return Lerp3(C1, C2, (T - 0.33f) / 0.33f);
    return Lerp3(C2, C3, (T - 0.66f) / 0.34f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  BUILD 32-EMITTER UNIFIED SHOWROOM & COURTYARD SCENE
//------------------------------------------------------------------------------------------------------------------------

std::vector<UnifiedLightSource> BuildFull32EmitterScene()
{
    std::vector<UnifiedLightSource> Lights;
    uint32_t Id = 0u;

    // 1) Dynamic Dual ECE Low-Beam Headlamps (5 LEDs Left + 5 LEDs Right = 10 dynamic emitters, RigGroupId=1)
    for (int I = 0; I < 5; ++I)
    {
        const float OffX = (static_cast<float>(I) - 2.0f) * 0.055f;
        UnifiedLightSource L {};
        L.SourceId = Id++; L.RigGroupId = 1u; L.SubFixtureId = 10u;
        L.Mobility = LightMobility::Dynamic;
        L.PositionWorld = { -0.82f + OffX, 0.72f, -2.06f };
        L.DirectionWorld = Normalize3({ -0.02f, -0.14f, 0.99f });
        L.ColorLinear = { 1.0f, 0.93f, 0.78f };
        L.LuminousFlux = 11.5f; L.CosOuterCone = 0.62f; L.ApertureRadiusM = 0.05f; L.InfluenceRadiusM = 24.0f;
        L.EceLowBeamCutoff = true;
        Lights.push_back(L);
    }
    for (int I = 0; I < 5; ++I)
    {
        const float OffX = (static_cast<float>(I) - 2.0f) * 0.055f;
        UnifiedLightSource R {};
        R.SourceId = Id++; R.RigGroupId = 1u; R.SubFixtureId = 20u;
        R.Mobility = LightMobility::Dynamic;
        R.PositionWorld = { +0.82f + OffX, 0.72f, -2.06f };
        R.DirectionWorld = Normalize3({ +0.02f, -0.14f, 0.99f });
        R.ColorLinear = { 1.0f, 0.93f, 0.78f };
        R.LuminousFlux = 11.5f; R.CosOuterCone = 0.62f; R.ApertureRadiusM = 0.05f; R.InfluenceRadiusM = 24.0f;
        R.EceLowBeamCutoff = true;
        Lights.push_back(R);
    }

    // 2) Static Warm Lantern Cluster (5 emitters, RigGroupId=2)
    for (int I = 0; I < 5; ++I)
    {
        const float A = (2.0f * PiF * static_cast<float>(I)) / 5.0f;
        UnifiedLightSource L {};
        L.SourceId = Id++; L.RigGroupId = 2u; L.SubFixtureId = 30u;
        L.Mobility = LightMobility::Static;
        L.PositionWorld = { -5.20f + std::cos(A) * 0.34f, 2.55f, -1.95f + std::sin(A) * 0.34f };
        L.DirectionWorld = { 0.0f, -1.0f, 0.0f };
        L.ColorLinear = { 1.0f, 0.68f, 0.30f };
        L.LuminousFlux = 10.0f; L.CosOuterCone = -1.0f; L.ApertureRadiusM = 0.07f; L.InfluenceRadiusM = 20.0f;
        Lights.push_back(L);
    }

    // 3) Static Opposing Neon Strips around wall at x=5.3 (4 Red West + 4 Cyan East, RigGroupId=3 & 4)
    for (int I = 0; I < 4; ++I)
    {
        const float Z = -3.15f + static_cast<float>(I) * 0.90f;
        UnifiedLightSource W {};
        W.SourceId = Id++; W.RigGroupId = 3u; W.SubFixtureId = 40u;
        W.Mobility = LightMobility::Static;
        W.PositionWorld = { 4.92f, 1.55f, Z };
        W.DirectionWorld = Normalize3({ -0.95f, -0.32f, 0.0f });
        W.ColorLinear = { 1.0f, 0.22f, 0.16f };
        W.LuminousFlux = 11.0f; W.CosOuterCone = 0.25f; W.ApertureRadiusM = 0.08f; W.InfluenceRadiusM = 18.0f;
        Lights.push_back(W);

        UnifiedLightSource E {};
        E.SourceId = Id++; E.RigGroupId = 4u; E.SubFixtureId = 41u;
        E.Mobility = LightMobility::Static;
        E.PositionWorld = { 5.68f, 1.55f, Z };
        E.DirectionWorld = Normalize3({ 0.95f, -0.32f, 0.0f });
        E.ColorLinear = { 0.14f, 0.84f, 1.0f };
        E.LuminousFlux = 11.0f; E.CosOuterCone = 0.25f; E.ApertureRadiusM = 0.08f; E.InfluenceRadiusM = 18.0f;
        Lights.push_back(E);
    }

    // 4) Static Architectural Plaza IES Downlights (6 emitters, RigGroupId=5)
    constexpr float PlazaPts[6][2] = {
        { -4.6f, 4.2f }, { 0.0f, 4.6f }, { 4.6f, 4.2f },
        { -4.6f, 8.6f }, { 0.0f, 9.0f }, { 4.6f, 8.6f }
    };
    for (int I = 0; I < 6; ++I)
    {
        UnifiedLightSource S {};
        S.SourceId = Id++; S.RigGroupId = 5u; S.SubFixtureId = 50u + static_cast<uint32_t>(I);
        S.Mobility = LightMobility::Static;
        S.PositionWorld = { PlazaPts[I][0], 5.1f, PlazaPts[I][1] };
        S.DirectionWorld = { 0.0f, -1.0f, 0.0f };
        S.ColorLinear = { 0.88f, 0.94f, 1.0f };
        S.LuminousFlux = 14.5f; S.CosOuterCone = 0.34f; S.ApertureRadiusM = 0.14f; S.InfluenceRadiusM = 24.0f;
        Lights.push_back(S);
    }

    // 5) Dynamic Hover Rover Drone Spotlights (3 emitters, RigGroupId=6)
    for (int I = 0; I < 3; ++I)
    {
        const float A = (2.0f * PiF * static_cast<float>(I)) / 3.0f;
        UnifiedLightSource D {};
        D.SourceId = Id++; D.RigGroupId = 6u; D.SubFixtureId = 60u;
        D.Mobility = LightMobility::Dynamic;
        D.PositionWorld = { -2.2f + std::cos(A) * 0.28f, 2.35f, 3.8f + std::sin(A) * 0.28f };
        D.DirectionWorld = Normalize3({ 0.20f, -0.94f, 0.28f });
        D.ColorLinear = { 0.32f, 1.0f, 0.56f };
        D.LuminousFlux = 12.0f; D.CosOuterCone = 0.45f; D.ApertureRadiusM = 0.07f; D.InfluenceRadiusM = 18.0f;
        Lights.push_back(D);
    }

    return Lights;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  CAMERA, MINIMAL 5x7 BITMAP HUD FONT & PNG WRITER
//------------------------------------------------------------------------------------------------------------------------

struct ProofCamera
{
    std::array<float, 3> Pos;
    std::array<float, 3> Fwd;
    std::array<float, 3> Right;
    std::array<float, 3> Up;
    float                TanHalfFov;
};

ProofCamera MakeCameraLookAt(const std::array<float, 3>& Pos, const std::array<float, 3>& Target, float FovDeg = 44.0f) noexcept
{
    const auto Fwd   = Normalize3({ Target[0] - Pos[0], Target[1] - Pos[1], Target[2] - Pos[2] }, { 0.0f, 0.0f, 1.0f });
    const auto UpRef = (std::fabs(Fwd[1]) > 0.985f) ? std::array<float, 3>{ 0.0f, 0.0f, -1.0f } : std::array<float, 3>{ 0.0f, 1.0f, 0.0f };
    const auto Right = Normalize3(Cross3(Fwd, UpRef), { 1.0f, 0.0f, 0.0f });
    const auto Up    = Normalize3(Cross3(Right, Fwd), { 0.0f, 1.0f, 0.0f });
    return { Pos, Fwd, Right, Up, std::tan(FovDeg * PiF / 360.0f) };
}

struct ImageRgba8
{
    uint32_t             W = 0u;
    uint32_t             H = 0u;
    std::vector<uint8_t> Pixels;

    ImageRgba8(uint32_t InW, uint32_t InH) : W(InW), H(InH), Pixels(static_cast<std::size_t>(InW) * InH * 4u, 255u) {}

    void SetPixel(uint32_t X, uint32_t Y, uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255u) noexcept
    {
        if (X >= W || Y >= H) return;
        const std::size_t I = (static_cast<std::size_t>(Y) * W + X) * 4u;
        if (A == 255u)
        {
            Pixels[I + 0] = R; Pixels[I + 1] = G; Pixels[I + 2] = B; Pixels[I + 3] = 255u;
        }
        else
        {
            const float Alpha = static_cast<float>(A) / 255.0f;
            Pixels[I + 0] = static_cast<uint8_t>(Pixels[I + 0] * (1.0f - Alpha) + R * Alpha);
            Pixels[I + 1] = static_cast<uint8_t>(Pixels[I + 1] * (1.0f - Alpha) + G * Alpha);
            Pixels[I + 2] = static_cast<uint8_t>(Pixels[I + 2] * (1.0f - Alpha) + B * Alpha);
            Pixels[I + 3] = 255u;
        }
    }

    void FillRect(uint32_t X0, uint32_t Y0, uint32_t Rw, uint32_t Rh, uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255u) noexcept
    {
        for (uint32_t Y = Y0; Y < std::min(H, Y0 + Rh); ++Y)
            for (uint32_t X = X0; X < std::min(W, X0 + Rw); ++X)
                SetPixel(X, Y, R, G, B, A);
    }

    void Blit(const ImageRgba8& Src, uint32_t DstX, uint32_t DstY) noexcept
    {
        for (uint32_t Y = 0u; Y < Src.H && (DstY + Y) < H; ++Y)
            for (uint32_t X = 0u; X < Src.W && (DstX + X) < W; ++X)
            {
                const std::size_t Si = (static_cast<std::size_t>(Y) * Src.W + X) * 4u;
                SetPixel(DstX + X, DstY + Y, Src.Pixels[Si], Src.Pixels[Si + 1], Src.Pixels[Si + 2], 255u);
            }
    }
};

// Minimal crisp 5x7 uppercase/digit/punctuation glyph table for clean HUD headers
std::array<uint8_t, 7> LookupGlyph5x7(char Ch) noexcept
{
    if (Ch >= 'a' && Ch <= 'z') Ch = static_cast<char>(Ch - 'a' + 'A');
    switch (Ch)
    {
        case 'A': return { 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11 };
        case 'B': return { 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E };
        case 'C': return { 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E };
        case 'D': return { 0x1E,0x11,0x11,0x11,0x11,0x11,0x1E };
        case 'E': return { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F };
        case 'F': return { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10 };
        case 'G': return { 0x0E,0x11,0x10,0x17,0x11,0x11,0x0E };
        case 'H': return { 0x11,0x11,0x11,0x1F,0x11,0x11,0x11 };
        case 'I': return { 0x0E,0x04,0x04,0x04,0x04,0x04,0x0E };
        case 'J': return { 0x07,0x02,0x02,0x02,0x12,0x12,0x0C };
        case 'K': return { 0x11,0x12,0x14,0x18,0x14,0x12,0x11 };
        case 'L': return { 0x10,0x10,0x10,0x10,0x10,0x10,0x1F };
        case 'M': return { 0x11,0x1B,0x15,0x15,0x11,0x11,0x11 };
        case 'N': return { 0x11,0x19,0x15,0x13,0x11,0x11,0x11 };
        case 'O': return { 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E };
        case 'P': return { 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10 };
        case 'Q': return { 0x0E,0x11,0x11,0x11,0x15,0x12,0x0D };
        case 'R': return { 0x1E,0x11,0x11,0x1E,0x14,0x12,0x11 };
        case 'S': return { 0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E };
        case 'T': return { 0x1F,0x04,0x04,0x04,0x04,0x04,0x04 };
        case 'U': return { 0x11,0x11,0x11,0x11,0x11,0x11,0x0E };
        case 'V': return { 0x11,0x11,0x11,0x11,0x11,0x0A,0x04 };
        case 'W': return { 0x11,0x11,0x11,0x15,0x15,0x1B,0x11 };
        case 'X': return { 0x11,0x11,0x0A,0x04,0x0A,0x11,0x11 };
        case 'Y': return { 0x11,0x11,0x0A,0x04,0x04,0x04,0x04 };
        case 'Z': return { 0x1F,0x01,0x02,0x04,0x08,0x10,0x1F };
        case '0': return { 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E };
        case '1': return { 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E };
        case '2': return { 0x0E,0x11,0x01,0x06,0x08,0x10,0x1F };
        case '3': return { 0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E };
        case '4': return { 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02 };
        case '5': return { 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E };
        case '6': return { 0x0E,0x10,0x1E,0x11,0x11,0x11,0x0E };
        case '7': return { 0x1F,0x01,0x02,0x04,0x08,0x08,0x08 };
        case '8': return { 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E };
        case '9': return { 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C };
        case ':': return { 0x00,0x04,0x04,0x00,0x04,0x04,0x00 };
        case '.': return { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C };
        case ',': return { 0x00,0x00,0x00,0x00,0x04,0x04,0x08 };
        case '-': return { 0x00,0x00,0x00,0x1F,0x00,0x00,0x00 };
        case '+': return { 0x00,0x04,0x04,0x1F,0x04,0x04,0x00 };
        case '/': return { 0x01,0x02,0x02,0x04,0x08,0x08,0x10 };
        case '%': return { 0x18,0x19,0x02,0x04,0x08,0x13,0x03 };
        case '(': return { 0x02,0x04,0x08,0x08,0x08,0x04,0x02 };
        case ')': return { 0x08,0x04,0x02,0x02,0x02,0x04,0x08 };
        case '|': return { 0x04,0x04,0x04,0x04,0x04,0x04,0x04 };
        case '=': return { 0x00,0x00,0x1F,0x00,0x1F,0x00,0x00 };
        case '&': return { 0x0C,0x12,0x14,0x08,0x15,0x12,0x0D };
        case '@': return { 0x0E,0x11,0x17,0x15,0x17,0x10,0x0E };
        case '[': return { 0x0E,0x08,0x08,0x08,0x08,0x08,0x0E };
        case ']': return { 0x0E,0x02,0x02,0x02,0x02,0x02,0x0E };
        default:  return { 0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
    }
}

void DrawText(ImageRgba8& Img, uint32_t X, uint32_t Y, const std::string& Text, uint32_t Scale, uint8_t R, uint8_t G, uint8_t B) noexcept
{
    uint32_t Cx = X;
    for (char Ch : Text)
    {
        const auto Rows = LookupGlyph5x7(Ch);
        for (uint32_t Ry = 0u; Ry < 7u; ++Ry)
        {
            for (uint32_t Rx = 0u; Rx < 5u; ++Rx)
            {
                if ((Rows[Ry] & (1u << (4u - Rx))) != 0u)
                {
                    Img.FillRect(Cx + Rx * Scale, Y + Ry * Scale, Scale, Scale, R, G, B, 255u);
                }
            }
        }
        Cx += 6u * Scale;
    }
}

bool WritePngRgba8(const std::filesystem::path& FilePath, const ImageRgba8& Img)
{
    std::filesystem::create_directories(FilePath.parent_path());
    const std::size_t RowBytes = 1u + static_cast<std::size_t>(Img.W) * 4u;
    std::vector<uint8_t> Raw;
    Raw.reserve(RowBytes * Img.H);
    for (uint32_t Y = 0u; Y < Img.H; ++Y)
    {
        Raw.push_back(0u);
        const uint8_t* P = Img.Pixels.data() + static_cast<std::size_t>(Y) * Img.W * 4u;
        Raw.insert(Raw.end(), P, P + static_cast<std::size_t>(Img.W) * 4u);
    }

    // Stored zlib stream (RFC 1950)
    std::vector<uint8_t> Zlib;
    Zlib.push_back(0x78u);
    Zlib.push_back(0x01u);
    uint32_t A = 1u, B = 0u;
    for (uint8_t Byte : Raw) { A = (A + Byte) % 65521u; B = (B + A) % 65521u; }
    std::size_t Pos = 0u;
    while (Pos < Raw.size())
    {
        const uint16_t Len = static_cast<uint16_t>(std::min<std::size_t>(65535u, Raw.size() - Pos));
        const uint16_t NLen = static_cast<uint16_t>(~Len);
        Zlib.push_back((Pos + Len >= Raw.size()) ? 0x01u : 0x00u);
        Zlib.push_back(static_cast<uint8_t>(Len & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>(Len >> 8u));
        Zlib.push_back(static_cast<uint8_t>(NLen & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>(NLen >> 8u));
        Zlib.insert(Zlib.end(), Raw.begin() + static_cast<std::ptrdiff_t>(Pos), Raw.begin() + static_cast<std::ptrdiff_t>(Pos + Len));
        Pos += Len;
    }
    const uint32_t Adler = (B << 16u) | A;
    for (int S = 24; S >= 0; S -= 8) Zlib.push_back(static_cast<uint8_t>((Adler >> S) & 0xFFu));

    auto Crc32 = [](const uint8_t* Data, std::size_t Len) -> uint32_t {
        uint32_t Crc = 0xFFFFFFFFu;
        for (std::size_t I = 0u; I < Len; ++I)
        {
            Crc ^= Data[I];
            for (int K = 0; K < 8; ++K) Crc = (Crc & 1u) ? (0xEDB88320u ^ (Crc >> 1u)) : (Crc >> 1u);
        }
        return Crc ^ 0xFFFFFFFFu;
    };

    std::vector<uint8_t> Out = { 137,80,78,71,13,10,26,10 };
    auto PushU32 = [&](uint32_t V) {
        Out.push_back(static_cast<uint8_t>((V >> 24u) & 0xFFu));
        Out.push_back(static_cast<uint8_t>((V >> 16u) & 0xFFu));
        Out.push_back(static_cast<uint8_t>((V >>  8u) & 0xFFu));
        Out.push_back(static_cast<uint8_t>( V         & 0xFFu));
    };
    auto PushChunk = [&](const char Type[4], const std::vector<uint8_t>& Data) {
        PushU32(static_cast<uint32_t>(Data.size()));
        const std::size_t Start = Out.size();
        Out.insert(Out.end(), Type, Type + 4);
        Out.insert(Out.end(), Data.begin(), Data.end());
        PushU32(Crc32(Out.data() + Start, 4u + Data.size()));
    };

    std::vector<uint8_t> Ihdr(13u, 0u);
    Ihdr[0] = static_cast<uint8_t>((Img.W >> 24u) & 0xFFu); Ihdr[1] = static_cast<uint8_t>((Img.W >> 16u) & 0xFFu);
    Ihdr[2] = static_cast<uint8_t>((Img.W >>  8u) & 0xFFu); Ihdr[3] = static_cast<uint8_t>( Img.W         & 0xFFu);
    Ihdr[4] = static_cast<uint8_t>((Img.H >> 24u) & 0xFFu); Ihdr[5] = static_cast<uint8_t>((Img.H >> 16u) & 0xFFu);
    Ihdr[6] = static_cast<uint8_t>((Img.H >>  8u) & 0xFFu); Ihdr[7] = static_cast<uint8_t>( Img.H         & 0xFFu);
    Ihdr[8] = 8u; Ihdr[9] = 6u; // 8-bit RGBA
    PushChunk("IHDR", Ihdr);
    PushChunk("IDAT", Zlib);
    PushChunk("IEND", {});

    std::ofstream File(FilePath, std::ios::binary);
    File.write(reinterpret_cast<const char*>(Out.data()), static_cast<std::streamsize>(Out.size()));
    return File.good();
}

//------------------------------------------------------------------------------------------------------------------------
//                                  CORE RAYTRACED VIEWPORT RENDERER FOR ANY MODE / DEBUG VIEW
//------------------------------------------------------------------------------------------------------------------------

enum class VisualMode
{
    ExactLit,
    NaiveLit,
    PodsLit,
    ClipmapLit,
    UnifiedLit,
    ErrorX8SplitNaiveVsUnified,
    Debug23ErrorX8Unified,
    Debug24ToroidalCascades,
    Debug25StaticVsDynamic,
    Debug26PodsAndLod,
};

struct RenderContext
{
    std::vector<UnifiedLightSource> RawLights;
    std::vector<UnifiedLightSource> NaiveLights;
    UnifiedLightingSystem*          System = nullptr;
    UnifiedLightingSyncOptions      Opts {};
};

std::vector<UnifiedLightSource> BuildNaiveCollapsedLights(const std::vector<UnifiedLightSource>& Raw)
{
    // Collapse each RigGroupId to its center (specifically merges Left+Right Headlamps @ x=0.0!)
    std::vector<UnifiedLightSource> Out;
    for (uint32_t Rig = 1u; Rig <= 6u; ++Rig)
    {
        std::array<float, 3> Pos { 0,0,0 }, Dir { 0,0,0 }, Col { 0,0,0 };
        float Flux = 0.0f, MinCos = 1.0f, MaxInf = 10.0f;
        int Cnt = 0;
        LightMobility Mob = LightMobility::Static;
        for (const auto& L : Raw)
        {
            if (L.RigGroupId != Rig) continue;
            Pos[0] += L.PositionWorld[0]; Pos[1] += L.PositionWorld[1]; Pos[2] += L.PositionWorld[2];
            Dir[0] += L.DirectionWorld[0]; Dir[1] += L.DirectionWorld[1]; Dir[2] += L.DirectionWorld[2];
            Col[0] += L.ColorLinear[0] * L.LuminousFlux;
            Col[1] += L.ColorLinear[1] * L.LuminousFlux;
            Col[2] += L.ColorLinear[2] * L.LuminousFlux;
            Flux += L.LuminousFlux;
            MinCos = std::min(MinCos, L.CosOuterCone);
            MaxInf = std::max(MaxInf, L.InfluenceRadiusM);
            Mob = L.Mobility;
            ++Cnt;
        }
        if (Cnt == 0) continue;
        UnifiedLightSource M {};
        M.RigGroupId = Rig; M.Mobility = Mob;
        M.PositionWorld = { Pos[0] / Cnt, Pos[1] / Cnt, Pos[2] / Cnt };
        M.DirectionWorld = Normalize3(Dir, { 0.0f, -1.0f, 0.0f });
        M.ColorLinear = { Col[0] / Flux, Col[1] / Flux, Col[2] / Flux };
        M.LuminousFlux = Flux; M.CosOuterCone = MinCos; M.InfluenceRadiusM = MaxInf;
        Out.push_back(M);
    }
    return Out;
}

ImageRgba8 RenderPanel(uint32_t W, uint32_t H, const ProofCamera& Cam, const RenderContext& Ctx, VisualMode Mode)
{
    ImageRgba8 Img(W, H);
    const float Aspect = static_cast<float>(W) / static_cast<float>(H);
    const auto& FineC = Ctx.System->InspectStaticClipmap().InspectFineCascade();
    const auto& CoarseC = Ctx.System->InspectStaticClipmap().InspectCoarseCascade();

    auto EvalExact = [&](const std::array<float, 3>& P, const std::array<float, 3>& N, const std::array<float, 3>& V, const std::array<float, 3>& Albedo, float Rough) {
        std::array<float, 3> Sum { Albedo[0] * 0.016f, Albedo[1] * 0.020f, Albedo[2] * 0.028f };
        for (const auto& L : Ctx.RawLights)
        {
            const auto C = ShadePbrLight(P, N, V, Albedo, Rough, L.PositionWorld, L.DirectionWorld, L.ColorLinear, L.LuminousFlux, L.CosOuterCone, L.InfluenceRadiusM, L.EceLowBeamCutoff);
            Sum[0] += C[0]; Sum[1] += C[1]; Sum[2] += C[2];
        }
        return Sum;
    };

    auto EvalNaive = [&](const std::array<float, 3>& P, const std::array<float, 3>& N, const std::array<float, 3>& V, const std::array<float, 3>& Albedo, float Rough) {
        std::array<float, 3> Sum { Albedo[0] * 0.016f, Albedo[1] * 0.020f, Albedo[2] * 0.028f };
        for (const auto& L : Ctx.NaiveLights)
        {
            const auto C = ShadePbrLight(P, N, V, Albedo, Rough, L.PositionWorld, L.DirectionWorld, L.ColorLinear, L.LuminousFlux, L.CosOuterCone, L.InfluenceRadiusM, false);
            Sum[0] += C[0]; Sum[1] += C[1]; Sum[2] += C[2];
        }
        return Sum;
    };

    auto EvalClipmapStatic = [&](const std::array<float, 3>& P, const std::array<float, 3>& N, const std::array<float, 3>& V, const std::array<float, 3>& Albedo, float Rough, float& OutErr, float& OutFineW) {
        const ToroidalClipmapSample S = Ctx.System->InspectStaticClipmap().SampleAtWorldXZ(P[0], P[2]);
        OutErr = S.CellError;
        OutFineW = S.FineWeight;
        const float NdotL = std::max(Dot3(N, S.DominantDirL1), 0.0f);
        const auto HVec = Normalize3({ S.DominantDirL1[0] + V[0], S.DominantDirL1[1] + V[1], S.DominantDirL1[2] + V[2] }, N);
        const float NdotH = std::max(Dot3(N, HVec), 0.0f);
        const float NdotV = std::max(Dot3(N, V), 0.05f);
        const float A = std::max(0.08f, Rough * Rough);
        const float A2 = A * A;
        const float Den = NdotH * NdotH * (A2 - 1.0f) + 1.0f;
        const float Spec = ((A2 / (PiF * Den * Den + 1.0e-5f)) * (0.04f + 0.96f * std::pow(1.0f - std::max(Dot3(HVec, V), 0.0f), 5.0f))) / (4.0f * NdotV * std::max(NdotL, 0.15f) + 0.15f);
        const float Mod = (1.0f - S.Directionality * 0.85f) + (S.Directionality * 0.85f) * std::max(0.16f, NdotL / std::max(0.25f, S.DominantDirL1[1]));
        return std::array<float, 3>{
            Albedo[0] * S.IrradianceL0[0] * Mod + S.IrradianceL0[0] * (S.Directionality * Spec * 0.72f),
            Albedo[1] * S.IrradianceL0[1] * Mod + S.IrradianceL0[1] * (S.Directionality * Spec * 0.72f),
            Albedo[2] * S.IrradianceL0[2] * Mod + S.IrradianceL0[2] * (S.Directionality * Spec * 0.72f)
        };
    };

    auto EvalPodsAll = [&](const std::array<float, 3>& P, const std::array<float, 3>& N, const std::array<float, 3>& V, const std::array<float, 3>& Albedo, float Rough, bool DynamicOnly) {
        std::array<float, 3> Sum { 0.0f, 0.0f, 0.0f };
        const auto Pods = DynamicOnly ? Ctx.System->InspectDynamicPods() : Ctx.System->InspectAllPods();
        for (const CompiledCubeGridPod& Pod : Pods)
        {
            const float Vis = TraceShadowRay({ P[0] + N[0] * 0.02f, P[1] + N[1] * 0.02f, P[2] + N[2] * 0.02f }, Pod.ActiveCellsLod0.size() == 1u ? Pod.ActiveCellsLod0[0].VoxelCenterWorld : Pod.FluxCentroidWorld);
            // For multi-cell pods (like dual headlights), trace per active cell at LOD 0 so bollards cast crisp twin shadows!
            const float Dist = std::sqrt(
                (P[0] - Pod.FluxCentroidWorld[0]) * (P[0] - Pod.FluxCentroidWorld[0]) +
                (P[1] - Pod.FluxCentroidWorld[1]) * (P[1] - Pod.FluxCentroidWorld[1]) +
                (P[2] - Pod.FluxCentroidWorld[2]) * (P[2] - Pod.FluxCentroidWorld[2]));
            const float Win = EvaluateQuarticInfluenceWindow(Dist, Pod.InfluenceRadiusM);
            if (Win <= 1.0e-4f) continue;

            const float Lod = ComputeContinuousPodLod(Dist, Pod.BoundingDiameterM, 0.0f);
            if (Lod < 0.85f)
            {
                for (const CubeGridActiveCell& Cell : Pod.ActiveCellsLod0)
                {
                    const std::array<float, 3> Anchor {
                        Cell.VoxelCenterWorld[0] + Cell.SubVoxelOffset[0],
                        Cell.VoxelCenterWorld[1] + Cell.SubVoxelOffset[1],
                        Cell.VoxelCenterWorld[2] + Cell.SubVoxelOffset[2]
                    };
                    const float CVis = TraceShadowRay({ P[0] + N[0] * 0.02f, P[1] + N[1] * 0.02f, P[2] + N[2] * 0.02f }, Anchor);
                    if (CVis <= 0.0f) continue;
                    const auto Ray = Normalize3({ P[0] - Anchor[0], P[1] - Anchor[1], P[2] - Anchor[2] });
                    const auto Cd = SampleOctahedralTileBilinear(Cell.TileLod0, Ray);
                    const float CMax = std::max({ Cd[0], Cd[1], Cd[2], 1.0e-5f });
                    const auto Sh = ShadePbrLight(P, N, V, Albedo, Rough, Anchor, Ray, { Cd[0] / CMax, Cd[1] / CMax, Cd[2] / CMax }, CMax * Win, -1.0f, 100.0f, false);
                    Sum[0] += Sh[0]; Sum[1] += Sh[1]; Sum[2] += Sh[2];
                }
            }
            else if (Vis > 0.0f)
            {
                const auto C = EvaluateCubeGridPodAtSurface(Pod, P, N, V, Rough, Albedo, 0.0f, nullptr);
                Sum[0] += C[0] * Vis; Sum[1] += C[1] * Vis; Sum[2] += C[2] * Vis;
            }
        }
        return Sum;
    };

    auto EvalUnified = [&](const std::array<float, 3>& P, const std::array<float, 3>& N, const std::array<float, 3>& V, const std::array<float, 3>& Albedo, float Rough,
                           std::array<float, 3>& OutStatic, std::array<float, 3>& OutDyn, float& OutFineW, float& OutPromoW) {
        float CellErr = 0.0f;
        const auto ClipS = EvalClipmapStatic(P, N, V, Albedo, Rough, CellErr, OutFineW);
        OutPromoW = SmoothStep(Ctx.Opts.StaticClipmapErrorGate * 0.75f, Ctx.Opts.StaticClipmapErrorGate * 1.35f, CellErr);
        OutStatic = ClipS;
        if (OutPromoW > 0.02f)
        {
            const auto SCut = ResolveLightCutForReceiver(Ctx.System->InspectStaticBvh(), P, Ctx.Opts.NaniteErrorBudget * 0.85f, Ctx.Opts.MaxActiveLightCut);
            std::array<float, 3> Promo { 0.0f, 0.0f, 0.0f };
            for (const auto& Node : SCut)
            {
                const auto C = ShadePbrLight(P, N, V, Albedo, Rough, Node.PositionWorld, Node.DirectionWorld, Node.ColorLinear, Node.LuminousFlux, Node.CosOuterCone, Node.InfluenceRadiusM, false);
                Promo[0] += C[0]; Promo[1] += C[1]; Promo[2] += C[2];
            }
            for (int C = 0; C < 3; ++C) OutStatic[C] = ClipS[C] * (1.0f - OutPromoW) + Promo[C] * OutPromoW;
        }

        const auto DynCut = ResolveLightCutForReceiver(Ctx.System->InspectDynamicBvh(), P, Ctx.Opts.NaniteErrorBudget * 0.75f, Ctx.Opts.MaxActiveLightCut);
        OutDyn = { 0.0f, 0.0f, 0.0f };
        for (const auto& Node : DynCut)
        {
            const bool IsEce = (Node.RigGroupId == 1u);
            const auto C = ShadePbrLight(P, N, V, Albedo, Rough, Node.PositionWorld, Node.DirectionWorld, Node.ColorLinear, Node.LuminousFlux, Node.CosOuterCone, Node.InfluenceRadiusM, IsEce);
            OutDyn[0] += C[0]; OutDyn[1] += C[1]; OutDyn[2] += C[2];
        }
        return std::array<float, 3>{
            Albedo[0] * 0.016f + OutStatic[0] + OutDyn[0],
            Albedo[1] * 0.020f + OutStatic[1] + OutDyn[1],
            Albedo[2] * 0.028f + OutStatic[2] + OutDyn[2]
        };
    };

    #pragma omp parallel for schedule(dynamic, 4)
    for (int Py = 0; Py < static_cast<int>(H); ++Py)
    {
        const float NdcY = 1.0f - 2.0f * (static_cast<float>(Py) + 0.5f) / static_cast<float>(H);
        for (uint32_t Px = 0u; Px < W; ++Px)
        {
            const float NdcX = 2.0f * (static_cast<float>(Px) + 0.5f) / static_cast<float>(W) - 1.0f;
            const auto Rd = Normalize3({
                Cam.Fwd[0] + Cam.Right[0] * (NdcX * Aspect * Cam.TanHalfFov) + Cam.Up[0] * (NdcY * Cam.TanHalfFov),
                Cam.Fwd[1] + Cam.Right[1] * (NdcX * Aspect * Cam.TanHalfFov) + Cam.Up[1] * (NdcY * Cam.TanHalfFov),
                Cam.Fwd[2] + Cam.Right[2] * (NdcX * Aspect * Cam.TanHalfFov) + Cam.Up[2] * (NdcY * Cam.TanHalfFov)
            });

            float BestT = 1.0e6f;
            std::array<float, 3> HitN { 0.0f, 1.0f, 0.0f };
            std::array<float, 3> HitCol { 0.3f, 0.3f, 0.32f };
            float HitRough = 0.28f;
            bool HitFloor = false;

            if (Rd[1] < -1.0e-4f)
            {
                const float Tf = -Cam.Pos[1] / Rd[1];
                if (Tf > 0.0f && Tf < 160.0f)
                {
                    const float Fx = Cam.Pos[0] + Rd[0] * Tf;
                    const float Fz = Cam.Pos[2] + Rd[2] * Tf;
                    if (std::fabs(Fx) <= WorldHalfExtent && std::fabs(Fz) <= WorldHalfExtent)
                    {
                        BestT = Tf;
                        HitFloor = true;
                        const int Tx = static_cast<int>(std::floor(Fx * 0.5f));
                        const int Tz = static_cast<int>(std::floor(Fz * 0.5f));
                        const float Chk = static_cast<float>((Tx + Tz) & 1);
                        HitCol = { 0.23f + 0.08f * Chk, 0.25f + 0.08f * Chk, 0.29f + 0.08f * Chk };
                        HitRough = 0.22f + 0.14f * Chk;
                    }
                }
            }

            std::array<float, 3> BoxN {};
            for (const SceneBox& B : kSceneBoxes)
            {
                const float T = IntersectBox(Cam.Pos, Rd, B, BoxN);
                if (T > 0.0f && T < BestT)
                {
                    BestT = T; HitN = BoxN; HitCol = B.Albedo; HitRough = B.Roughness; HitFloor = false;
                }
            }

            std::array<float, 3> OutSrgb {};
            if (BestT > 1.0e5f)
            {
                const float Sky = std::clamp(Rd[1] * 0.5f + 0.5f, 0.0f, 1.0f);
                OutSrgb = { 0.04f + 0.04f * Sky, 0.05f + 0.05f * Sky, 0.07f + 0.07f * Sky };
            }
            else
            {
                const std::array<float, 3> P { Cam.Pos[0] + Rd[0] * BestT, Cam.Pos[1] + Rd[1] * BestT, Cam.Pos[2] + Rd[2] * BestT };
                const std::array<float, 3> V { -Rd[0], -Rd[1], -Rd[2] };

                if (Mode == VisualMode::ExactLit)
                {
                    OutSrgb = ToneMapSrgb(EvalExact(P, HitN, V, HitCol, HitRough));
                }
                else if (Mode == VisualMode::NaiveLit)
                {
                    OutSrgb = ToneMapSrgb(EvalNaive(P, HitN, V, HitCol, HitRough));
                }
                else if (Mode == VisualMode::PodsLit)
                {
                    const auto PodsC = EvalPodsAll(P, HitN, V, HitCol, HitRough, false);
                    OutSrgb = ToneMapSrgb({ HitCol[0] * 0.016f + PodsC[0], HitCol[1] * 0.020f + PodsC[1], HitCol[2] * 0.028f + PodsC[2] });
                }
                else if (Mode == VisualMode::ClipmapLit)
                {
                    float E = 0.0f, Fw = 0.0f;
                    const auto S = EvalClipmapStatic(P, HitN, V, HitCol, HitRough, E, Fw);
                    const auto D = EvalPodsAll(P, HitN, V, HitCol, HitRough, true);
                    OutSrgb = ToneMapSrgb({ HitCol[0] * 0.016f + S[0] + D[0], HitCol[1] * 0.020f + S[1] + D[1], HitCol[2] * 0.028f + S[2] + D[2] });
                }
                else if (Mode == VisualMode::UnifiedLit)
                {
                    std::array<float, 3> S {}, D {};
                    float Fw = 0.0f, Pw = 0.0f;
                    OutSrgb = ToneMapSrgb(EvalUnified(P, HitN, V, HitCol, HitRough, S, D, Fw, Pw));
                }
                else if (Mode == VisualMode::ErrorX8SplitNaiveVsUnified || Mode == VisualMode::Debug23ErrorX8Unified)
                {
                    const auto Truth = ToneMapSrgb(EvalExact(P, HitN, V, HitCol, HitRough));
                    std::array<float, 3> Approx {};
                    const bool ShowNaiveLeft = (Mode == VisualMode::ErrorX8SplitNaiveVsUnified) && (Px < W / 2u);
                    if (ShowNaiveLeft)
                    {
                        Approx = ToneMapSrgb(EvalNaive(P, HitN, V, HitCol, HitRough));
                    }
                    else
                    {
                        std::array<float, 3> S {}, D {}; float Fw = 0.0f, Pw = 0.0f;
                        Approx = ToneMapSrgb(EvalUnified(P, HitN, V, HitCol, HitRough, S, D, Fw, Pw));
                    }
                    const float Err = std::sqrt(
                        (Truth[0] - Approx[0]) * (Truth[0] - Approx[0]) +
                        (Truth[1] - Approx[1]) * (Truth[1] - Approx[1]) +
                        (Truth[2] - Approx[2]) * (Truth[2] - Approx[2])) * 0.57735f;
                    OutSrgb = InfernoPalette(Err * 8.0f);
                    if (Mode == VisualMode::ErrorX8SplitNaiveVsUnified && std::abs(static_cast<int>(Px) - static_cast<int>(W / 2u)) <= 1)
                    {
                        OutSrgb = { 1.0f, 1.0f, 1.0f };
                    }
                }
                else if (Mode == VisualMode::Debug24ToroidalCascades)
                {
                    std::array<float, 3> S {}, D {}; float Fw = 0.0f, Pw = 0.0f;
                    const auto Lit = ToneMapSrgb(EvalUnified(P, HitN, V, HitCol, HitRough, S, D, Fw, Pw));
                    std::array<float, 3> Tint { 0.88f * (1.0f - Fw) + 0.14f * Fw, 0.55f * (1.0f - Fw) + 0.84f * Fw, 0.18f * (1.0f - Fw) + 0.96f * Fw };
                    if (Pw > 0.15f)
                    {
                        Tint = { Tint[0] * (1.0f - Pw * 0.7f) + 0.96f * Pw * 0.7f, Tint[1] * (1.0f - Pw * 0.7f) + 0.22f * Pw * 0.7f, Tint[2] * (1.0f - Pw * 0.7f) + 0.65f * Pw * 0.7f };
                    }
                    for (int C = 0; C < 3; ++C) OutSrgb[C] = Lit[C] * 0.55f + Tint[C] * 0.45f;

                    if (HitFloor)
                    {
                        const float Fu = (P[0] - (FineC.CenterWorldX - FineC.HalfSpanMeters)) / (2.0f * FineC.HalfSpanMeters);
                        const float Fv = (P[2] - (FineC.CenterWorldZ - FineC.HalfSpanMeters)) / (2.0f * FineC.HalfSpanMeters);
                        if (Fu >= 0.0f && Fu <= 1.0f && Fv >= 0.0f && Fv <= 1.0f)
                        {
                            const float Cu = std::fmod(Fu * FineC.Resolution, 1.0f);
                            const float Cv = std::fmod(Fv * FineC.Resolution, 1.0f);
                            const float Line = 1.0f - SmoothStep(0.0f, 0.06f, std::min({ Cu, 1.0f - Cu, Cv, 1.0f - Cv }));
                            const float Border = 1.0f - SmoothStep(0.0f, 0.015f, std::min({ Fu, 1.0f - Fu, Fv, 1.0f - Fv }));
                            const float K = std::max(Line * 0.45f, Border * 0.95f);
                            OutSrgb = { OutSrgb[0] * (1.0f - K) + 0.30f * K, OutSrgb[1] * (1.0f - K) + 0.95f * K, OutSrgb[2] * (1.0f - K) + 1.0f * K };
                        }
                        else
                        {
                            const float Cu = std::fmod(std::fabs(P[0] / CoarseC.CellSizeMeters), 1.0f);
                            const float Cv = std::fmod(std::fabs(P[2] / CoarseC.CellSizeMeters), 1.0f);
                            const float Line = 1.0f - SmoothStep(0.0f, 0.05f, std::min({ Cu, 1.0f - Cu, Cv, 1.0f - Cv }));
                            const float K = Line * 0.35f;
                            OutSrgb = { OutSrgb[0] * (1.0f - K) + 0.95f * K, OutSrgb[1] * (1.0f - K) + 0.65f * K, OutSrgb[2] * (1.0f - K) + 0.25f * K };
                        }
                    }
                }
                else if (Mode == VisualMode::Debug25StaticVsDynamic)
                {
                    std::array<float, 3> S {}, D {}; float Fw = 0.0f, Pw = 0.0f;
                    (void)EvalUnified(P, HitN, V, HitCol, HitRough, S, D, Fw, Pw);
                    const auto Ts = ToneMapSrgb(S);
                    const auto Td = ToneMapSrgb(D);
                    const float SLum = 0.3f * Ts[0] + 0.6f * Ts[1] + 0.1f * Ts[2];
                    const float DLum = 0.3f * Td[0] + 0.6f * Td[1] + 0.1f * Td[2];
                    OutSrgb = {
                        std::min(1.0f, 0.04f + 0.10f * SLum * 1.55f + 1.00f * DLum * 1.55f),
                        std::min(1.0f, 0.04f + 0.88f * SLum * 1.55f + 0.68f * DLum * 1.55f),
                        std::min(1.0f, 0.05f + 0.98f * SLum * 1.55f + 0.16f * DLum * 1.55f)
                    };
                }
                else // Debug26PodsAndLod
                {
                    std::array<float, 3> S {}, D {}; float Fw = 0.0f, Pw = 0.0f;
                    const auto Lit = ToneMapSrgb(EvalUnified(P, HitN, V, HitCol, HitRough, S, D, Fw, Pw));
                    // Color ground by continuous Nanite Pod LOD ring distance from hero Dual Headlamps pod
                    const auto& HeroPod = Ctx.System->InspectAllPods()[0];
                    const float Dist = std::sqrt(
                        (P[0] - HeroPod.FluxCentroidWorld[0]) * (P[0] - HeroPod.FluxCentroidWorld[0]) +
                        (P[2] - HeroPod.FluxCentroidWorld[2]) * (P[2] - HeroPod.FluxCentroidWorld[2]));
                    const float Lod = ComputeContinuousPodLod(Dist, HeroPod.BoundingDiameterM, 0.0f);
                    std::array<float, 3> LodTint {};
                    if (Lod < 1.0f)      LodTint = { 0.18f, 0.86f, 0.54f }; // LOD 0 Near (Split 8x4x4) = Emerald
                    else if (Lod < 2.0f) LodTint = { 0.25f, 0.72f, 0.98f }; // LOD 1 Mid (Bimodal Poles) = Cyan
                    else                 LodTint = { 0.98f, 0.64f, 0.20f }; // LOD 2/3 Far (1-Tap Grid) = Amber
                    const float Ring = 1.0f - SmoothStep(0.0f, 0.06f, std::fabs(std::fmod(Lod, 1.0f) - 0.5f) * 2.0f);
                    for (int C = 0; C < 3; ++C)
                    {
                        OutSrgb[C] = std::min(1.0f, Lit[C] * 0.62f + LodTint[C] * 0.34f + (Ring > 0.92f ? 0.35f : 0.0f));
                    }
                }
            }

            Img.SetPixel(
                Px, static_cast<uint32_t>(Py),
                static_cast<uint8_t>(std::clamp(OutSrgb[0], 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(OutSrgb[1], 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(OutSrgb[2], 0.0f, 1.0f) * 255.0f));
        }
    }

    return Img;
}

void StampPanelBanner(ImageRgba8& Panel, const std::string& Line1, const std::string& Line2, uint8_t DotR, uint8_t DotG, uint8_t DotB)
{
    Panel.FillRect(8, 8, std::min(Panel.W - 16u, 580u), 44, 10, 12, 16, 215);
    Panel.FillRect(16, 17, 9, 9, DotR, DotG, DotB, 255);
    DrawText(Panel, 32, 14, Line1, 2, 242, 244, 248);
    DrawText(Panel, 32, 34, Line2, 1, 166, 178, 198);
}

} // namespace

int main()
{
    const std::filesystem::path OutDir = "Exhibits/Images/UnifiedLightingProof";
    std::filesystem::create_directories(OutDir);

    std::printf("=== Rendering Real C++20 Unified 3-System Lighting Visual Proof ===\n");
    const auto RawLights   = BuildFull32EmitterScene();
    const auto NaiveLights = BuildNaiveCollapsedLights(RawLights);

    UnifiedLightingSystem Sys {};
    UnifiedLightingSyncOptions Opts {};
    Opts.Mode                   = UnifiedLightingMode::UnifiedAll3;
    Opts.ScalePreset            = LightClipmapScalePreset::MediumPlaza;
    Opts.AutoBakeStaticClipmap  = true;
    Opts.OptimizePodResiduals   = true;
    Opts.PodOptimizationSteps   = 6u;
    Opts.NaniteErrorBudget      = 0.24f;
    Opts.StaticClipmapErrorGate = 0.18f;

    const ClipmapVisibilityCallback ShadowCb = [](const std::array<float, 3>& P, const std::array<float, 3>& L) {
        return TraceShadowRay(P, L);
    };

    Sys.Synchronize(RawLights, 1ull, 1ull, { 0.0f, 0.0f, 2.2f }, Opts, ShadowCb);
    const auto Bench = Sys.EvaluateAllFiveModeMetrics(1280u, 720u, 11.5f, Opts, ShadowCb);

    RenderContext Ctx { RawLights, NaiveLights, &Sys, Opts };
    const ProofCamera MainCam = MakeCameraLookAt({ 6.8f, 11.2f, 13.8f }, { 0.0f, 0.5f, 0.6f }, 44.0f);
    const ProofCamera HeadlightCam = MakeCameraLookAt({ 2.4f, 4.2f, 7.6f }, { 0.0f, 0.4f, 0.2f }, 42.0f);

    const auto FmtMetrics = [&](std::size_t Idx) -> std::string {
        char Buf[128]{};
        std::snprintf(Buf, sizeof(Buf), "EVALS %.1f/PX | %.1fX | ATLAS %.0fKB | RMSE %.1f%%",
            Bench[Idx].AvgEvalsPerPixel, Bench[Idx].SpeedupVsExact, Bench[Idx].LightAtlasVramKb, Bench[Idx].RelativeRmsePercent);
        return std::string(Buf);
    };

    // -----------------------------------------------------------------------------------------------------------------
    // SHEET 1: ALL 5 RENDER MODES + SIDE-BY-SIDE ERROR x8 COMPARISON (1920 x 1080)
    // -----------------------------------------------------------------------------------------------------------------
    constexpr uint32_t Pw = 626u, Ph = 486u;
    ImageRgba8 PExact   = RenderPanel(Pw, Ph, MainCam, Ctx, VisualMode::ExactLit);
    ImageRgba8 PNaive   = RenderPanel(Pw, Ph, MainCam, Ctx, VisualMode::NaiveLit);
    ImageRgba8 PPods    = RenderPanel(Pw, Ph, MainCam, Ctx, VisualMode::PodsLit);
    ImageRgba8 PClipmap = RenderPanel(Pw, Ph, MainCam, Ctx, VisualMode::ClipmapLit);
    ImageRgba8 PUnified = RenderPanel(Pw, Ph, MainCam, Ctx, VisualMode::UnifiedLit);
    ImageRgba8 PErrCmp  = RenderPanel(Pw, Ph, MainCam, Ctx, VisualMode::ErrorX8SplitNaiveVsUnified);

    StampPanelBanner(PExact,   "1. EXACT REFERENCE (LOD 0)",     FmtMetrics(0),  91, 219, 135);
    StampPanelBanner(PNaive,   "2. NAIVE POINT MERGE (BROKEN)",  FmtMetrics(1), 255,  95,  86);
    StampPanelBanner(PPods,    "3. 1 CUBE-GRID / POD (8X4X4)",   FmtMetrics(2),  56, 217, 245);
    StampPanelBanner(PClipmap, "4. TOROIDAL CLIPMAP + PODS",     FmtMetrics(3), 255, 180,  84);
    StampPanelBanner(PUnified, "5. UNIFIED ALL-3 (NANITE+CLIP)", FmtMetrics(4),  91, 219, 135);
    StampPanelBanner(PErrCmp,  "6. ERROR X8: NAIVE | UNIFIED",   "LEFT: NAIVE CENTER-MERGE ERROR | RIGHT: UNIFIED ALL-3 ERROR", 255, 95, 86);

    // Save individual mode renders too
    WritePngRgba8(OutDir / "mode_01_exact.png", PExact);
    WritePngRgba8(OutDir / "mode_02_naive.png", PNaive);
    WritePngRgba8(OutDir / "mode_03_cube_grid_pods.png", PPods);
    WritePngRgba8(OutDir / "mode_04_toroidal_clipmap.png", PClipmap);
    WritePngRgba8(OutDir / "mode_05_unified_all3.png", PUnified);

    ImageRgba8 Sheet1(1920u, 1080u);
    Sheet1.FillRect(0, 0, 1920, 1080, 10, 11, 15, 255);
    DrawText(Sheet1, 20, 18, "FRONTIER C++20 UNIFIED 3-SYSTEM LIGHTING PROOF - ALL 5 MODES + LIVE ERROR X8", 2, 240, 242, 248);
    DrawText(Sheet1, 20, 42, "32 EMITTERS (19 STATIC -> TOROIDAL CLIPMAP | 13 DYNAMIC -> 8X4X4 PODS + NANITE BVH) | ZERO CENTER COLLAPSE", 1, 145, 160, 182);

    Sheet1.Blit(PExact,   14,  68);
    Sheet1.Blit(PNaive,   648, 68);
    Sheet1.Blit(PPods,    1282, 68);
    Sheet1.Blit(PClipmap, 14,  574);
    Sheet1.Blit(PUnified, 648, 574);
    Sheet1.Blit(PErrCmp,  1282, 574);
    WritePngRgba8(OutDir / "01_All5Modes_ComparisonSheet.png", Sheet1);
    std::printf("[SAVED] %s\n", (OutDir / "01_All5Modes_ComparisonSheet.png").c_str());

    // -----------------------------------------------------------------------------------------------------------------
    // SHEET 2: ALL 4 PROJECT ZERO DEBUG VIEWS (1600 x 960)
    // -----------------------------------------------------------------------------------------------------------------
    constexpr uint32_t Dw = 780u, Dh = 430u;
    ImageRgba8 Dbg23 = RenderPanel(Dw, Dh, MainCam, Ctx, VisualMode::Debug23ErrorX8Unified);
    ImageRgba8 Dbg24 = RenderPanel(Dw, Dh, MainCam, Ctx, VisualMode::Debug24ToroidalCascades);
    ImageRgba8 Dbg25 = RenderPanel(Dw, Dh, MainCam, Ctx, VisualMode::Debug25StaticVsDynamic);
    ImageRgba8 Dbg26 = RenderPanel(Dw, Dh, MainCam, Ctx, VisualMode::Debug26PodsAndLod);

    StampPanelBanner(Dbg23, "DEBUG 23: ERROR X8 HEATMAP",      "UNIFIED ALL-3 VS EXACT LOD 0 (INFERNO X8)", 255, 95, 86);
    StampPanelBanner(Dbg24, "DEBUG 24: TOROIDAL CLIPMAP",      "CYAN: FINE CASCADE | AMBER: COARSE | MAGENTA: HIGH-ERR SPLIT", 56, 217, 245);
    StampPanelBanner(Dbg25, "DEBUG 25: STATIC VS DYNAMIC",     "CYAN: STATIC TOROIDAL CLIPMAP | GOLD: DYNAMIC PODS + NANITE BVH", 255, 180, 84);
    StampPanelBanner(Dbg26, "DEBUG 26: CUBE-GRID PODS & LOD",  "EMERALD: LOD 0 (8X4X4) | CYAN: LOD 1 | AMBER: LOD 2/3 FAR", 91, 219, 135);

    ImageRgba8 Sheet2(1600u, 960u);
    Sheet2.FillRect(0, 0, 1600, 960, 10, 11, 15, 255);
    DrawText(Sheet2, 20, 18, "PROJECT ZERO DEBUG VIEWS (DEBUG CHANNELS 23..26) - RENDERED FROM C++20 ENGINE", 2, 240, 242, 248);
    DrawText(Sheet2, 20, 42, "VERIFYING STATIC/DYNAMIC ISOLATION, TOROIDAL FINE/COARSE BOUNDARIES, AND NANITE DISTANCE LOD RINGS", 1, 145, 160, 182);
    Sheet2.Blit(Dbg23, 14,  68);
    Sheet2.Blit(Dbg24, 806, 68);
    Sheet2.Blit(Dbg25, 14,  512);
    Sheet2.Blit(Dbg26, 806, 512);
    WritePngRgba8(OutDir / "02_DebugViews_4Up.png", Sheet2);
    std::printf("[SAVED] %s\n", (OutDir / "02_DebugViews_4Up.png").c_str());

    // -----------------------------------------------------------------------------------------------------------------
    // SHEET 3: CLOSE-UP DUAL HEADLAMP PARALLAX & SHADOW PROOF (1600 x 540)
    // -----------------------------------------------------------------------------------------------------------------
    constexpr uint32_t Cw = 516u, Ch = 452u;
    ImageRgba8 HExact   = RenderPanel(Cw, Ch, HeadlightCam, Ctx, VisualMode::ExactLit);
    ImageRgba8 HNaive   = RenderPanel(Cw, Ch, HeadlightCam, Ctx, VisualMode::NaiveLit);
    ImageRgba8 HUnified = RenderPanel(Cw, Ch, HeadlightCam, Ctx, VisualMode::UnifiedLit);

    StampPanelBanner(HExact,   "EXACT GROUND TRUTH",          "TWIN ECE HEADLAMPS + DUAL BOLLARD SHADOWS", 91, 219, 135);
    StampPanelBanner(HNaive,   "NAIVE CENTER-AVERAGED",       "COLLAPSED @ X=0 -> WRONG SHADOWS!", 255, 95, 86);
    StampPanelBanner(HUnified, "UNIFIED ALL-3 (C++20)",       "EMPTY CENTER [2..5] + SPLIT HEADLAMPS!", 56, 217, 245);

    ImageRgba8 Sheet3(1600u, 540u);
    Sheet3.FillRect(0, 0, 1600, 540, 10, 11, 15, 255);
    DrawText(Sheet3, 20, 16, "NEAR-FIELD DUAL HEADLAMP PARALLAX & SHADOW PROOF (ZERO CENTER COLLAPSE)", 2, 240, 242, 248);
    DrawText(Sheet3, 20, 40, "CLOSE-UP ON FRONT BOLLARDS: TWIN BEAMS STAY SPLIT UP CLOSE AND MATCH EXACT REFERENCE", 1, 145, 160, 182);
    Sheet3.Blit(HExact,   14,  68);
    Sheet3.Blit(HNaive,   542, 68);
    Sheet3.Blit(HUnified, 1070, 68);
    WritePngRgba8(OutDir / "03_HeadlampParallax_CloseUpProof.png", Sheet3);
    std::printf("[SAVED] %s\n", (OutDir / "03_HeadlampParallax_CloseUpProof.png").c_str());

    std::printf("=== ALL C++20 VISUAL PROOF RENDERS COMPLETED ===\n");
    return 0;
}
