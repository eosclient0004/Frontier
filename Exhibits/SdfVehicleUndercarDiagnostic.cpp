/*============================================================================================================================================
                                                    SDFVEHICLEUNDERCAR DIAGNOSTIC
============================================================================================================================================*/
// Real-Vulkan pixel & visual diagnostic reproducing SDF under-car shading across camera dolly & angle changes
// on BOTH isolated vehicle AND multi-object ShowcaseGalleryScene (where Fine GDF / Radiosity / Shadow cascades slide across the car).

#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::GeometricRaster;

struct ProjectedPixel
{
    int32_t X     = -1;
    int32_t Y     = -1;
    bool    Valid = false;
};

[[nodiscard]] ProjectedPixel ProjectWorldToPixel(
    const PerspectiveCameraBasis& Cam,
    const Mathematics::Axis3D&    WorldPos)
{
    const Mathematics::Axis3D Fwd = Mathematics::NormalizeAxis(Cam.TargetPoint - Cam.CameraOrigin, { 0.0f, 1.0f, 0.0f });
    const Mathematics::Axis3D Right = Mathematics::NormalizeAxis(
        Mathematics::EvaluateCross(Fwd, Cam.WorldUpAxis), { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D Up = Mathematics::NormalizeAxis(
        Mathematics::EvaluateCross(Right, Fwd), { 0.0f, 0.0f, 1.0f });

    const Mathematics::Axis3D Rel = WorldPos - Cam.CameraOrigin;
    const float Z = Mathematics::EvaluateDot(Rel, Fwd);
    if (Z <= 0.05f) return {};
    const float TanHalfY = std::tan(Cam.VerticalFovRadians * 0.5f);
    const float Aspect   = static_cast<float>(Cam.ViewportWidthPx) / static_cast<float>(std::max(1u, Cam.ViewportHeightPx));
    const float TanHalfX = TanHalfY * Aspect;
    const float NdcX = Mathematics::EvaluateDot(Rel, Right) / (Z * TanHalfX);
    const float NdcY = Mathematics::EvaluateDot(Rel, Up)    / (Z * TanHalfY);
    const float U = (NdcX + 1.0f) * 0.5f;
    const float V = (1.0f - NdcY) * 0.5f;
    if (U < 0.0f || U >= 1.0f || V < 0.0f || V >= 1.0f) return {};
    return {
        static_cast<int32_t>(U * static_cast<float>(Cam.ViewportWidthPx)),
        static_cast<int32_t>(V * static_cast<float>(Cam.ViewportHeightPx)),
        true
    };
}

[[nodiscard]] double SamplePatchLuma(
    const std::vector<uint8_t>& Rgba,
    uint32_t                    W,
    uint32_t                    H,
    int32_t                     Cx,
    int32_t                     Cy,
    int32_t                     Radius = 1)
{
    if (Rgba.size() < static_cast<std::size_t>(W) * H * 4u || Cx < 0 || Cy < 0) return -1.0;
    double Sum = 0.0;
    int    Cnt = 0;
    for (int32_t Dy = -Radius; Dy <= Radius; ++Dy)
    {
        for (int32_t Dx = -Radius; Dx <= Radius; ++Dx)
        {
            const int32_t Px = std::clamp(Cx + Dx, 0, static_cast<int32_t>(W) - 1);
            const int32_t Py = std::clamp(Cy + Dy, 0, static_cast<int32_t>(H) - 1);
            const std::size_t Off = (static_cast<std::size_t>(Py) * W + static_cast<std::size_t>(Px)) * 4u;
            Sum += 0.2126 * Rgba[Off + 0u] + 0.7152 * Rgba[Off + 1u] + 0.0722 * Rgba[Off + 2u];
            ++Cnt;
        }
    }
    return Cnt > 0 ? (Sum / static_cast<double>(Cnt)) : -1.0;
}

[[nodiscard]] std::array<double, 3u> SamplePatchRgb(
    const std::vector<uint8_t>& Rgba,
    uint32_t                    W,
    uint32_t                    H,
    int32_t                     Cx,
    int32_t                     Cy,
    int32_t                     Radius = 1)
{
    std::array<double, 3u> Acc {{ 0.0, 0.0, 0.0 }};
    if (Rgba.size() < static_cast<std::size_t>(W) * H * 4u || Cx < 0 || Cy < 0) return Acc;
    int Cnt = 0;
    for (int32_t Dy = -Radius; Dy <= Radius; ++Dy)
    {
        for (int32_t Dx = -Radius; Dx <= Radius; ++Dx)
        {
            const int32_t Px = std::clamp(Cx + Dx, 0, static_cast<int32_t>(W) - 1);
            const int32_t Py = std::clamp(Cy + Dy, 0, static_cast<int32_t>(H) - 1);
            const std::size_t Off = (static_cast<std::size_t>(Py) * W + static_cast<std::size_t>(Px)) * 4u;
            Acc[0] += Rgba[Off + 0u];
            Acc[1] += Rgba[Off + 1u];
            Acc[2] += Rgba[Off + 2u];
            ++Cnt;
        }
    }
    if (Cnt > 0) { Acc[0] /= Cnt; Acc[1] /= Cnt; Acc[2] /= Cnt; }
    return Acc;
}

void DrawCrosshair(std::vector<uint8_t>& Rgba, uint32_t W, uint32_t H, int32_t Cx, int32_t Cy)
{
    if (Cx < 0 || Cy < 0) return;
    for (int32_t D = -5; D <= 5; ++D)
    {
        if (std::abs(D) <= 1) continue;
        auto Put = [&](int32_t X, int32_t Y)
        {
            if (X < 0 || Y < 0 || X >= static_cast<int32_t>(W) || Y >= static_cast<int32_t>(H)) return;
            const std::size_t Off = (static_cast<std::size_t>(Y) * W + static_cast<std::size_t>(X)) * 4u;
            Rgba[Off + 0u] = 255u;
            Rgba[Off + 1u] = 48u;
            Rgba[Off + 2u] = 48u;
            Rgba[Off + 3u] = 255u;
        };
        Put(Cx + D, Cy);
        Put(Cx, Cy + D);
    }
}

} // namespace

int main(int argc, char** argv)
{
    const std::string Tag = (argc >= 2 && argv[1] != nullptr) ? std::string(argv[1]) : std::string("Before");
    const std::filesystem::path RepoRoot(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path ShaderDir(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path OutDir = RepoRoot / "Exhibits/Images/SdfUndercarDiagnostic";
    std::filesystem::create_directories(OutDir);

    VulkanInterchange Device {};
    std::string VkDiag {};
    if (!Device.InitializeInterchange(RepoRoot, &VkDiag))
    {
        std::fprintf(stderr, "[SdfVehicleUndercarDiagnostic] Vulkan init failed: %s\n", VkDiag.c_str());
        return 1;
    }

    VulkanWorldRenderSequence GpuSeq {};
    if (!GpuSeq.InitializeSequence(Device, ShaderDir))
    {
        std::fprintf(stderr, "[SdfVehicleUndercarDiagnostic] VulkanWorldRenderSequence init failed.\n");
        Device.TeardownInterchange();
        return 1;
    }

    constexpr uint32_t TileW = 260u;
    constexpr uint32_t TileH = 170u;

    ShowcaseWorldSceneBundle Bundle {};
    BuildVehicleClearSkyDefaultScene(Bundle, TileW, TileH);
    // Vehicle is at (0,0,0.88), half=(3.11, 1.25, 0.88) -> wheels & rocker panel sit at Y = -1.25..+1.25, X = -3.11..+3.11, GroundZ = 0.0
    const Mathematics::Axis3D VehicleCenter = Bundle.Scene.ClusterInstances[1].WorldTranslation;
    const float GroundZ = 0.0f;

    // Ground point visible DIRECTLY UNDER the car rocker panel (between front & rear wheels, tucked under the car shadow!)
    const Mathematics::Axis3D UndercarGround { VehicleCenter.X + 0.0f, VehicleCenter.Y - 1.12f, GroundZ + 0.005f };
    // Ground point directly beside the car overhang
    const Mathematics::Axis3D ShadowGround   { VehicleCenter.X + 0.0f, VehicleCenter.Y - 1.55f, GroundZ + 0.005f };

    GpuSeq.SynchronizeSceneResources(Bundle.Scene, true);

    struct CameraPose
    {
        const char*         Name;
        Mathematics::Axis3D Origin;
        Mathematics::Axis3D Target;
    };

    // Poses specifically looking LOW at the ground UNDER the car from close -> far AND rotated camera angles
    // where the camera view ray shifts the Fine GDF / Radiosity / Fine Shadow cascade away from the car!
    const std::array<CameraPose, 6u> Poses {{
        { "1_CloseLow_4m",    {  1.2f, -4.4f,  0.42f }, {  0.0f, -0.4f,  0.30f } },
        { "2_MidLow_8m",      {  2.4f, -8.2f,  0.72f }, {  0.0f, -0.4f,  0.30f } },
        { "3_FarLow_15m",     {  4.5f, -15.0f, 1.35f }, {  0.0f, -0.4f,  0.30f } },
        { "4_TurnedNear",     {  2.8f, -6.4f,  0.65f }, { -7.5f,  2.2f,  0.20f } },
        { "5_TurnedMid",      {  4.5f, -9.8f,  1.10f }, { -9.5f,  4.0f,  0.20f } },
        { "6_TurnedFar",      {  6.2f, -14.5f, 1.80f }, { -12.0f, 6.0f,  0.20f } },
    }};

    struct ChannelInfo
    {
        const char* Name;
        uint32_t    DebugId;
    };
    const std::array<ChannelInfo, 7u> Channels {{
        { "Shaded(0)",     0u },
        { "SunShad(8)",    8u },
        { "FiltAO(9)",     9u },
        { "RawUdfAO(15)", 15u },
        { "DiffGI(10)",   10u },
        { "Spec(11)",     11u },
        { "Cascade(16)",  16u },
    }};

    const uint32_t SheetW = static_cast<uint32_t>(Channels.size()) * TileW;
    const uint32_t SheetH = static_cast<uint32_t>(Poses.size())    * TileH;
    Exhibits::SoftwareRasterCanvas ContactSheet(SheetW, SheetH, { 16u, 18u, 24u, 255u });

    std::printf("\n=== SDF UNDER-CAR GROUND PIXEL DIAGNOSTIC (%s) | Live Editor Defaults (Engine=2 SDF, Shadow=0 ContactPcss, Nanite=ON) ===\n", Tag.c_str());
    std::printf("Probe = UndercarGround (0.00, -1.12, 0.005) directly on ground under car rocker panel\n");
    std::printf("%-15s | %-8s | %-8s | %-8s | %-8s | %-8s | %-8s | %-15s | ShadGnd(0)\n",
        "Pose", "Shad(0)", "Sun(8)", "FltAO(9)", "RawAO(15)", "DffGI10", "Spec(11)", "Casc(16) RGB");
    std::printf("-----------------------------------------------------------------------------------------------------------------------\n");

    std::array<double, 6u> Spreads[6] {};

    for (std::size_t PIdx = 0u; PIdx < Poses.size(); ++PIdx)
    {
        PerspectiveCameraBasis Cam = Bundle.EditorCamera;
        Cam.CameraOrigin     = Poses[PIdx].Origin;
        Cam.TargetPoint      = Poses[PIdx].Target;
        Cam.ViewportWidthPx  = TileW;
        Cam.ViewportHeightPx = TileH;

        const ProjectedPixel PUnder = ProjectWorldToPixel(Cam, UndercarGround);
        const ProjectedPixel PShad  = ProjectWorldToPixel(Cam, ShadowGround);

        double Vals[6] { -1.0, -1.0, -1.0, -1.0, -1.0, -1.0 };
        std::array<double, 3u> CascRgb {{ 0.0, 0.0, 0.0 }};
        double ShadGndVal = -1.0;

        for (std::size_t CIdx = 0u; CIdx < Channels.size(); ++CIdx)
        {
            WorldRenderConfiguration Cfg {};
            Cfg.Engine           = WorldRenderEngine::RealTimeGiGtxLowEnd; // SDF
            Cfg.ShadowAlgorithm  = SolarShadowAlgorithm::ContactPcss;
            Cfg.NaniteLodEnabled = true;  // Live editor default!
            Cfg.NanitePixelError = 1.25f;
            Cfg.DebugChannel     = Channels[CIdx].DebugId;
            Cfg.XeGtaoEnabled    = false;

            std::vector<uint8_t> Pixels {};
            const bool Ok = GpuSeq.RenderWorldViewportWithEngine(Cfg, 0u, Cam, Bundle.Scene, Pixels);
            if (Ok && Pixels.size() == static_cast<std::size_t>(TileW) * TileH * 4u)
            {
                if (CIdx < 6u)
                {
                    Vals[CIdx] = PUnder.Valid ? SamplePatchLuma(Pixels, TileW, TileH, PUnder.X, PUnder.Y, 0) : -1.0;
                    Spreads[CIdx][PIdx] = Vals[CIdx];
                }
                else
                {
                    CascRgb = PUnder.Valid ? SamplePatchRgb(Pixels, TileW, TileH, PUnder.X, PUnder.Y, 0) : std::array<double, 3u>{ 0, 0, 0 };
                }
                if (CIdx == 0u && PShad.Valid)
                {
                    ShadGndVal = SamplePatchLuma(Pixels, TileW, TileH, PShad.X, PShad.Y, 0);
                }
                // Mark exact probe location on Cascade(16) and Shaded(0) tiles so we can visually verify probe placement!
                if (CIdx == 0u || CIdx == 6u)
                {
                    DrawCrosshair(Pixels, TileW, TileH, PUnder.X, PUnder.Y);
                }
                ContactSheet.BlitRgbaRegion(
                    static_cast<uint32_t>(CIdx) * TileW,
                    static_cast<uint32_t>(PIdx) * TileH,
                    TileW,
                    TileH,
                    Pixels.data());
            }
        }

        std::printf("%-15s | %8.1f | %8.1f | %8.1f | %8.1f | %8.1f | %8.1f | (%3.0f,%3.0f,%3.0f)   | %8.1f\n",
            Poses[PIdx].Name,
            Vals[0], Vals[1], Vals[2], Vals[3], Vals[4], Vals[5],
            CascRgb[0], CascRgb[1], CascRgb[2], ShadGndVal);
    }

    // Also test OpenGround stability vs UndercarGround on Nanite OFF vs ON to isolate Nanite vs SDF!
    std::printf("\n=== ISOLATION CHECK: Nanite ON vs Nanite OFF at Close(4m) vs Far(15m) vs TurnedFar ===\n");
    for (int NaniteMode = 0; NaniteMode <= 1; ++NaniteMode)
    {
        const std::array<std::size_t, 3u> CheckIdx {{ 0u, 2u, 5u }};
        double ShadedV[3]{}, AoV[3]{}, SunV[3]{}, GiV[3]{};
        for (std::size_t K = 0u; K < 3u; ++K)
        {
            PerspectiveCameraBasis Cam = Bundle.EditorCamera;
            Cam.CameraOrigin     = Poses[CheckIdx[K]].Origin;
            Cam.TargetPoint      = Poses[CheckIdx[K]].Target;
            Cam.ViewportWidthPx  = TileW;
            Cam.ViewportHeightPx = TileH;
            const ProjectedPixel PUnder = ProjectWorldToPixel(Cam, UndercarGround);
            auto SampleCh = [&](uint32_t Ch) -> double
            {
                WorldRenderConfiguration Cfg {};
                Cfg.Engine           = WorldRenderEngine::RealTimeGiGtxLowEnd;
                Cfg.ShadowAlgorithm  = SolarShadowAlgorithm::ContactPcss;
                Cfg.NaniteLodEnabled = (NaniteMode == 1);
                Cfg.NanitePixelError = 1.25f;
                Cfg.DebugChannel     = Ch;
                std::vector<uint8_t> Px {};
                if (GpuSeq.RenderWorldViewportWithEngine(Cfg, 0u, Cam, Bundle.Scene, Px) && PUnder.Valid)
                {
                    return SamplePatchLuma(Px, TileW, TileH, PUnder.X, PUnder.Y, 0);
                }
                return -1.0;
            };
            ShadedV[K] = SampleCh(0u);
            SunV[K]    = SampleCh(8u);
            AoV[K]     = SampleCh(9u);
            GiV[K]     = SampleCh(10u);
        }
        std::printf("  Nanite=%s | Shaded: Close=%5.1f Far=%5.1f TurnFar=%5.1f | Sun: %5.1f/%5.1f/%5.1f | FltAO: %5.1f/%5.1f/%5.1f | DiffGI: %5.1f/%5.1f/%5.1f\n",
            NaniteMode ? "ON " : "OFF",
            ShadedV[0], ShadedV[1], ShadedV[2],
            SunV[0], SunV[1], SunV[2],
            AoV[0], AoV[1], AoV[2],
            GiV[0], GiV[1], GiV[2]);
    }

    // Verify XeGTAO is strictly PBR-only: toggling XeGtaoEnabled in SDF mode must produce 0.0 luma difference!
    {
        PerspectiveCameraBasis Cam = Bundle.EditorCamera;
        Cam.CameraOrigin     = Poses[0].Origin;
        Cam.TargetPoint      = Poses[0].Target;
        Cam.ViewportWidthPx  = TileW;
        Cam.ViewportHeightPx = TileH;
        WorldRenderConfiguration CfgOff {};
        CfgOff.Engine           = WorldRenderEngine::RealTimeGiGtxLowEnd;
        CfgOff.ShadowAlgorithm  = SolarShadowAlgorithm::ContactPcss;
        CfgOff.NaniteLodEnabled = true;
        CfgOff.XeGtaoEnabled    = false;
        WorldRenderConfiguration CfgOn = CfgOff;
        CfgOn.XeGtaoEnabled     = true;
        std::vector<uint8_t> PxOff {}, PxOn {};
        (void)GpuSeq.RenderWorldViewportWithEngine(CfgOff, 0u, Cam, Bundle.Scene, PxOff);
        (void)GpuSeq.RenderWorldViewportWithEngine(CfgOn,  0u, Cam, Bundle.Scene, PxOn);
        double MaxDiff = 0.0;
        for (std::size_t I = 0u; I < PxOff.size() && I < PxOn.size(); ++I)
        {
            MaxDiff = std::max(MaxDiff, std::abs(static_cast<double>(PxOff[I]) - static_cast<double>(PxOn[I])));
        }
        std::printf("  XeGTAO PBR-Only Gate in SDF Mode: max_channel_diff = %.1f (expected 0.0)\n", MaxDiff);
    }

    const std::filesystem::path OutPath = OutDir / (Tag + "-UndercarGroundSheet.png");
    (void)ContactSheet.WritePngFile(OutPath);
    std::printf("\nWrote contact sheet: %s\n", OutPath.string().c_str());

    const char* Names[6] = { "Shaded(0)", "SunShad(8)", "FiltAO(9)", "RawUdfAO(15)", "DiffGI(10)", "Spec(11)" };
    std::printf("\n=== CAMERA-MOVEMENT SPREAD ON UNDER-CAR GROUND PIXEL ===\n");
    for (int C = 0; C < 6; ++C)
    {
        double Mn = 1e9, Mx = -1e9;
        for (double V : Spreads[C]) { if (V >= 0.0) { Mn = std::min(Mn, V); Mx = std::max(Mx, V); } }
        std::printf("  %-13s: min=%5.1f  max=%5.1f  delta=%5.1f luma\n", Names[C], Mn, Mx, Mx - Mn);
    }

    GpuSeq.TeardownSequence();
    Device.TeardownInterchange();
    return 0;
}
