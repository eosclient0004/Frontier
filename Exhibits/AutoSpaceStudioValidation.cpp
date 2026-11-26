/*============================================================================================================================================
                                                    AUTOSPACESTUDIOVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Standalone Exhibit validating AutoSpace single-level Studio Lobbies (1 Cyclorama, 2 Forest Soundstage without rocks/foliage,
//    3 Racing Pit Garage), Engine ParticleSpace simulation/rendering & Vulkan GI visual proofs into Exhibits/Images/AutoSpace/

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/CoordinateSpace.h"
#include "Engine/ParticleSpace/ParticleGpuRenderer.h"
#include "Engine/ParticleSpace/ParticleGpuSimulation.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"
#include "Projects/AutoSpace/Source/Gameplay/AutoSpaceLifecycle.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::GeometricRaster;
using namespace Frontier::Mathematics;
using namespace Frontier::ParticleSpace;

struct ImageMetrics
{
    double MeanLuma      = 0.0;
    double P95Luma       = 0.0;
    double NonBlackRatio = 0.0;
    bool   AllFinite     = true;
};

struct ValidationTracker
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Check(bool Condition, const std::string& Label)
    {
        if (Condition)
        {
            ++Passed;
            std::cout << "  [PASS] " << Label << "\n";
        }
        else
        {
            ++Failed;
            std::cerr << "  [FAIL] " << Label << "\n";
        }
    }
};

[[nodiscard]] std::filesystem::path ResolveRepositoryRoot()
{
#ifdef FRONTIER_REPO_ROOT_DIR
    const std::filesystem::path Root(FRONTIER_REPO_ROOT_DIR);
    if (std::filesystem::exists(Root / "Projects" / "AutoSpace" / "AutoSpace.projectspace"))
    {
        return Root;
    }
#endif
    std::error_code Ec {};
    std::filesystem::path Cursor = std::filesystem::current_path(Ec);
    for (int Depth = 0; Depth < 8; ++Depth)
    {
        if (std::filesystem::exists(Cursor / "Projects" / "AutoSpace" / "AutoSpace.projectspace", Ec))
        {
            return Cursor;
        }
        if (!Cursor.has_parent_path() || Cursor.parent_path() == Cursor)
        {
            break;
        }
        Cursor = Cursor.parent_path();
    }
    return std::filesystem::current_path(Ec);
}

[[nodiscard]] std::filesystem::path ResolveShaderDir(const std::filesystem::path& RepoRoot)
{
#ifdef FRONTIER_COMPILED_SHADER_DIR
    const std::filesystem::path Configured(FRONTIER_COMPILED_SHADER_DIR);
    if (std::filesystem::exists(Configured))
    {
        return Configured;
    }
#endif
    for (const auto& Candidate : {
             RepoRoot / "Build" / "Editor" / "CompiledShaders",
             RepoRoot / "Build" / "Shipping" / "CompiledShaders",
             std::filesystem::current_path() / "CompiledShaders",
         })
    {
        if (std::filesystem::exists(Candidate))
        {
            return Candidate;
        }
    }
    return RepoRoot / "Build" / "Editor" / "CompiledShaders";
}

[[nodiscard]] bool SaveRgbaPng(
    const std::filesystem::path& OutPath,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    const std::vector<uint8_t>&  RgbaBytes)
{
    if (WidthPx == 0u || HeightPx == 0u || RgbaBytes.size() < static_cast<std::size_t>(WidthPx) * HeightPx * 4u)
    {
        return false;
    }
    std::error_code Ec {};
    std::filesystem::create_directories(OutPath.parent_path(), Ec);
    return stbi_write_png(
               OutPath.string().c_str(),
               static_cast<int>(WidthPx),
               static_cast<int>(HeightPx),
               4,
               RgbaBytes.data(),
               static_cast<int>(WidthPx * 4u)) != 0;
}

[[nodiscard]] ImageMetrics AnalyzeRgbaImage(const std::vector<uint8_t>& RgbaBytes)
{
    ImageMetrics M {};
    const std::size_t PixelCount = RgbaBytes.size() / 4u;
    if (PixelCount == 0u)
    {
        M.AllFinite = false;
        return M;
    }

    std::array<uint32_t, 256u> Hist {};
    double SumLuma = 0.0;
    uint64_t NonBlack = 0ull;
    for (std::size_t I = 0u; I < PixelCount; ++I)
    {
        const double R = static_cast<double>(RgbaBytes[I * 4u + 0u]);
        const double G = static_cast<double>(RgbaBytes[I * 4u + 1u]);
        const double B = static_cast<double>(RgbaBytes[I * 4u + 2u]);
        const double Y = 0.2126 * R + 0.7152 * G + 0.0722 * B;
        SumLuma += Y;
        if (Y > 4.0)
        {
            ++NonBlack;
        }
        const uint32_t Bin = static_cast<uint32_t>(std::clamp(std::lround(Y), 0L, 255L));
        ++Hist[Bin];
    }

    M.MeanLuma      = SumLuma / static_cast<double>(PixelCount);
    M.NonBlackRatio = static_cast<double>(NonBlack) / static_cast<double>(PixelCount);

    const uint64_t TargetRank = (PixelCount * 95ull) / 100ull;
    uint64_t Acc = 0ull;
    for (uint32_t Bin = 0u; Bin < 256u; ++Bin)
    {
        Acc += Hist[Bin];
        if (Acc >= TargetRank)
        {
            M.P95Luma = static_cast<double>(Bin);
            break;
        }
    }
    return M;
}

[[nodiscard]] double ComputeMeanAbsDiff(
    const std::vector<uint8_t>& A,
    const std::vector<uint8_t>& B) noexcept
{
    if (A.size() != B.size() || A.empty())
    {
        return 0.0;
    }
    double Sum = 0.0;
    for (std::size_t I = 0u; I < A.size(); I += 4u)
    {
        Sum += std::abs(static_cast<int>(A[I + 0u]) - static_cast<int>(B[I + 0u]));
        Sum += std::abs(static_cast<int>(A[I + 1u]) - static_cast<int>(B[I + 1u]));
        Sum += std::abs(static_cast<int>(A[I + 2u]) - static_cast<int>(B[I + 2u]));
    }
    return Sum / static_cast<double>((A.size() / 4u) * 3u);
}

void CompositeOccludedParticleClouds(
    const WorldSceneSpecification&            Scene,
    const PerspectiveCameraBasis&             Camera,
    std::span<const ParticleCloudNode>        ParticleClouds,
    const ParticleGpuSimulation&              ParticleSimulation,
    const ParticleGpuRenderer&                ParticleRenderer,
    const ParticleSimulationRegion&           ParticleRegion,
    double                                    SimulationClockSeconds,
    std::vector<uint8_t>&                     InOutRgba8,
    uint32_t&                                 OutDrawCallCount,
    uint32_t&                                 OutDrawnSprites)
{
    OutDrawCallCount = 0u;
    OutDrawnSprites  = 0u;
    if (ParticleClouds.empty())
    {
        return;
    }

    const uint32_t W = Camera.ViewportWidthPx;
    const uint32_t H = Camera.ViewportHeightPx;
    if (InOutRgba8.size() < static_cast<std::size_t>(W) * H * 4u)
    {
        return;
    }

    Axis3D Right {};
    Axis3D Up {};
    Axis3D Forward {};
    EvaluateOrthonormalCameraBasis(
        Camera.CameraOrigin,
        Camera.TargetPoint,
        WorldCoordinateBasis::RightHandedZUp,
        Right,
        Up,
        Forward);

    const float Aspect = static_cast<float>(std::max(1u, W)) / static_cast<float>(std::max(1u, H));
    const float FovDeg = Camera.VerticalFovRadians * Constants::RadToDegFloat;
    const ClipTransform4x4 ClipMat = ConstructVulkanPerspectiveViewClip(
        Camera.CameraOrigin,
        Right,
        Up,
        Forward,
        FovDeg,
        Aspect,
        std::max(0.05f, Camera.NearClipMeters),
        1000.0f);

    ParticleCameraView PView {};
    for (int R = 0; R < 4; ++R)
    {
        for (int C = 0; C < 4; ++C)
        {
            PView.ViewProjection[static_cast<std::size_t>(R * 4 + C)] = ClipMat.Rows[R][C];
        }
    }
    PView.CameraPosition   = { Camera.CameraOrigin.X, Camera.CameraOrigin.Y, Camera.CameraOrigin.Z };
    PView.CameraForward    = { Forward.X, Forward.Y, Forward.Z };
    PView.ViewportWidthPx  = static_cast<float>(W);
    PView.ViewportHeightPx = static_cast<float>(H);
    PView.VerticalFovRad   = Camera.VerticalFovRadians;
    PView.PixelRatio       = 1.0f;

    std::vector<ParticleCloudNode> CloudsCopy(ParticleClouds.begin(), ParticleClouds.end());
    const auto DrawCalls = ParticleRenderer.BuildSpriteRenderCalls(
        CloudsCopy,
        ParticleSimulation,
        PView,
        static_cast<float>(SimulationClockSeconds),
        ParticleRegion.HalfExtentMeters[1] * 2.0f);
    OutDrawCallCount = static_cast<uint32_t>(DrawCalls.size());

    for (const auto& Call : DrawCalls)
    {
        const auto* Rec = ParticleSimulation.FindCloudRecord(Call.NodeId);
        if (Rec == nullptr || Rec->ActiveCount == 0u)
        {
            continue;
        }
        const bool IsAdditive = (Call.Blend == ParticleBlendOrdinal::Additive) || Call.IsHaloCall;
        for (uint32_t I = 0u; I < Rec->ActiveCount && I < Rec->ReadStates.size(); ++I)
        {
            const auto& State = Rec->ReadStates[I];
            if (State.PositionAge[3] < 0.0f)
            {
                continue;
            }
            const Axis3D WorldP = ConvertVectorBetweenBases(
                { State.PositionAge[0], State.PositionAge[1], State.PositionAge[2] },
                WorldCoordinateBasis::RightHandedYUp,
                WorldCoordinateBasis::RightHandedZUp);

            const float ClipX = ClipMat.Rows[0][0] * WorldP.X + ClipMat.Rows[0][1] * WorldP.Y + ClipMat.Rows[0][2] * WorldP.Z + ClipMat.Rows[0][3];
            const float ClipY = ClipMat.Rows[1][0] * WorldP.X + ClipMat.Rows[1][1] * WorldP.Y + ClipMat.Rows[1][2] * WorldP.Z + ClipMat.Rows[1][3];
            const float ClipW = ClipMat.Rows[3][0] * WorldP.X + ClipMat.Rows[3][1] * WorldP.Y + ClipMat.Rows[3][2] * WorldP.Z + ClipMat.Rows[3][3];
            if (ClipW <= 0.1f)
            {
                continue;
            }
            const float NdcX = ClipX / ClipW;
            const float NdcY = ClipY / ClipW;
            if (std::fabs(NdcX) > 1.05f || std::fabs(NdcY) > 1.05f)
            {
                continue;
            }
            // Vulkan NDC Y points downward (-1 top .. +1 bottom)
            const float ScreenX = (NdcX * 0.5f + 0.5f) * static_cast<float>(W);
            const float ScreenY = (NdcY * 0.5f + 0.5f) * static_cast<float>(H);

            const auto Shade = EvaluateParticleShadeCpu(
                Call.UniformBlock.Shape,
                Call.UniformBlock.Shade,
                Rec->AnchorTexels.Data,
                static_cast<int32_t>(I),
                State.PositionAge,
                State.VelocitySeed);

            float DiamPx = 4.0f;
            float DofAlpha = 1.0f;
            ComputeSpriteDofMetricsCpu(
                Call.UniformBlock,
                Call.UniformBlock.ViewportYSizeParams[1],
                ClipW,
                DiamPx,
                DofAlpha);

            const Axis3D RayDir = NormalizeAxis(WorldP - Camera.CameraOrigin, { 0.0f, 1.0f, 0.0f });
            const float  Dist   = std::sqrt(EvaluateLengthSquared(WorldP - Camera.CameraOrigin));
            SpatialAcceleration::RaySegmentQuery VisRay {};
            VisRay.Origin          = Camera.CameraOrigin;
            VisRay.MinimumDistance = 0.05f;
            VisRay.UnitDirection   = RayDir;
            VisRay.MaximumDistance = std::max(0.06f, Dist - 0.08f);
            if (Scene.SceneTree.QueryRayOccluded(VisRay))
            {
                continue;
            }

            const float RadiusPx     = std::clamp(DiamPx * 0.65f, 2.2f, 22.0f);
            const float SoftAlphaMul = (Call.NodeId.find("smoke") != std::string::npos) ? 0.24f : 0.42f;
            const float BaseAlpha    = std::clamp(Shade.Alpha * DofAlpha * SoftAlphaMul, 0.0f, 0.55f);
            if (BaseAlpha <= 0.004f)
            {
                continue;
            }

            ++OutDrawnSprites;
            const int X0 = std::max(0, static_cast<int>(std::floor(ScreenX - RadiusPx)));
            const int X1 = std::min(static_cast<int>(W) - 1, static_cast<int>(std::ceil(ScreenX + RadiusPx)));
            const int Y0 = std::max(0, static_cast<int>(std::floor(ScreenY - RadiusPx)));
            const int Y1 = std::min(static_cast<int>(H) - 1, static_cast<int>(std::ceil(ScreenY + RadiusPx)));
            for (int Py = Y0; Py <= Y1; ++Py)
            {
                for (int Px = X0; Px <= X1; ++Px)
                {
                    const float U = ((static_cast<float>(Px) + 0.5f - ScreenX) / RadiusPx) * 0.5f + 0.5f;
                    const float V = ((static_cast<float>(Py) + 0.5f - ScreenY) / RadiusPx) * 0.5f + 0.5f;
                    const auto Cut = EvaluateSpriteCutoutCpu(
                        0u,
                        static_cast<uint32_t>(Call.UniformBlock.HaloSpriteStyleTrail[2]),
                        { Shade.Colour[0], Shade.Colour[1], Shade.Colour[2], BaseAlpha },
                        1.0f,
                        0.5f,
                        { 0.0f, 0.0f, 0.0f, 0.0f },
                        { U, V });
                    if (Cut.Discarded || Cut.Alpha <= 0.002f)
                    {
                        continue;
                    }
                    const std::size_t Off = (static_cast<std::size_t>(Py) * W + static_cast<std::size_t>(Px)) * 4u;
                    const float SrcA = std::clamp(Cut.Alpha, 0.0f, 1.0f);
                    for (int C = 0; C < 3; ++C)
                    {
                        const float Dst = static_cast<float>(InOutRgba8[Off + C]) / 255.0f;
                        const float Src = std::clamp(Cut.Rgb[C], 0.0f, 1.0f);
                        const float Blended = IsAdditive
                            ? std::min(1.0f, Dst + Src * SrcA)
                            : (Dst * (1.0f - SrcA) + Src * SrcA);
                        InOutRgba8[Off + C] = static_cast<uint8_t>(std::lround(Blended * 255.0f));
                    }
                }
            }
        }
    }
}

[[nodiscard]] std::vector<uint8_t> BuildProofSheet2x2(
    const std::array<const std::vector<uint8_t>*, 4u>& Tiles,
    uint32_t                                           TileW,
    uint32_t                                           TileH,
    uint32_t&                                          OutSheetW,
    uint32_t&                                          OutSheetH)
{
    constexpr uint32_t Gap = 8u;
    const uint32_t HalfW = std::max(160u, TileW / 2u);
    const uint32_t HalfH = std::max(90u, TileH / 2u);
    OutSheetW = HalfW * 2u + Gap * 3u;
    OutSheetH = HalfH * 2u + Gap * 3u;
    std::vector<uint8_t> Sheet(static_cast<std::size_t>(OutSheetW) * OutSheetH * 4u, 255u);

    for (std::size_t I = 0u; I < Sheet.size(); I += 4u)
    {
        Sheet[I + 0u] = 16u;
        Sheet[I + 1u] = 18u;
        Sheet[I + 2u] = 24u;
        Sheet[I + 3u] = 255u;
    }

    for (uint32_t Slot = 0u; Slot < 4u; ++Slot)
    {
        if (Tiles[Slot] == nullptr || Tiles[Slot]->size() < static_cast<std::size_t>(TileW) * TileH * 4u)
        {
            continue;
        }
        const uint32_t DestX0 = Gap + (Slot % 2u) * (HalfW + Gap);
        const uint32_t DestY0 = Gap + (Slot / 2u) * (HalfH + Gap);
        for (uint32_t Y = 0u; Y < HalfH; ++Y)
        {
            const uint32_t SrcY = std::min(TileH - 1u, (Y * TileH) / HalfH);
            for (uint32_t X = 0u; X < HalfW; ++X)
            {
                const uint32_t SrcX = std::min(TileW - 1u, (X * TileW) / HalfW);
                const std::size_t SrcOff = (static_cast<std::size_t>(SrcY) * TileW + SrcX) * 4u;
                const std::size_t DstOff = (static_cast<std::size_t>(DestY0 + Y) * OutSheetW + (DestX0 + X)) * 4u;
                std::memcpy(&Sheet[DstOff], &(*Tiles[Slot])[SrcOff], 4u);
            }
        }
    }
    return Sheet;
}

[[nodiscard]] bool RenderLobbyFrame(
    AutoSpace::LobbyLifecycle&        Lobby,
    const ParticleGpuRenderer&        ParticleRenderer,
    VulkanWorldRenderSequence*        GpuSequence,
    TieredWorldRenderSequence&        CpuSequence,
    uint32_t                          PresetIndex,
    float                             ExposureGain,
    uint32_t                          FrameIndex,
    uint32_t                          WidthPx,
    uint32_t                          HeightPx,
    std::vector<uint8_t>&             OutRgba8,
    uint32_t&                         OutParticleDrawCalls,
    uint32_t&                         OutDrawnSprites,
    std::string&                      OutDiagnostic)
{
    auto&       Scene   = Lobby.AccessWorldScene();
    const auto& Presets = Lobby.InspectCameraPresets();
    if (Presets.empty())
    {
        OutDiagnostic = "Lobby camera presets are empty.";
        return false;
    }

    CpuSequence.PrepareWorldScene(Scene);
    CpuSequence.SynchronizeWorldAtmosphere(Scene);

    const uint32_t SafeIdx = std::min(PresetIndex, static_cast<uint32_t>(Presets.size() - 1u));
    PerspectiveCameraBasis Cam = Presets[SafeIdx].Camera;
    Cam.ViewportWidthPx  = WidthPx;
    Cam.ViewportHeightPx = HeightPx;

    if (GpuSequence != nullptr && GpuSequence->QueryReady())
    {
        GpuSequence->SynchronizeSceneResources(Scene, true);

        WorldRenderConfiguration Cfg {};
        Cfg.Engine                = WorldRenderEngine::RealTimeGiGtxLowEnd;
        Cfg.ShadowAlgorithm       = SolarShadowAlgorithm::WideTreeRayQuery;
        Cfg.ReflectionSource      = SpecularReflectionSource::WideTreeRaytrace;
        Cfg.LightingMode          = UnifiedLightingMode::ExactReference; // Simple studio lobby: direct lights, no clustering/baking
        Cfg.AutoBakeStaticClipmap = false;
        Cfg.ReflectionsEnabled    = true;
        Cfg.RaytracingEnabled     = true;
        Cfg.SdfGiIntensity        = 1.05f;
        Cfg.ExposureGain          = ExposureGain;

        if (!GpuSequence->RenderWorldViewportWithEngine(Cfg, FrameIndex, Cam, Scene, OutRgba8))
        {
            OutDiagnostic = "VulkanWorldRenderSequence::RenderWorldViewportWithEngine failed.";
            return false;
        }
    }
    else
    {
        WorldRenderConfiguration CpuCfg {};
        CpuCfg.Engine                = WorldRenderEngine::OpenPbrDirect;
        CpuCfg.ShadowAlgorithm       = SolarShadowAlgorithm::WideTreeRayQuery;
        CpuCfg.ReflectionSource      = SpecularReflectionSource::WideTreeRaytrace;
        CpuCfg.LightingMode          = UnifiedLightingMode::ExactReference; // Simple studio lobby: direct lights, no clustering/baking
        CpuCfg.AutoBakeStaticClipmap = false;
        CpuCfg.ReflectionsEnabled    = true;
        CpuCfg.RaytracingEnabled     = true;
        CpuCfg.ExposureGain          = ExposureGain * 1.15f;

        auto CpuOut = CpuSequence.RenderWorldFrameWithEngine(CpuCfg, Cam, Cam, Scene);
        OutRgba8 = std::move(CpuOut.DisplayRgba8);
    }

    OutParticleDrawCalls = 0u;
    OutDrawnSprites      = 0u;
    if (Lobby.InspectPolicy().EnableEngineParticles)
    {
        CompositeOccludedParticleClouds(
            Scene,
            Cam,
            Lobby.InspectParticleClouds(),
            Lobby.InspectParticleSimulation(),
            ParticleRenderer,
            Lobby.InspectParticleRegion(),
            Lobby.InspectSimulationClockSeconds(),
            OutRgba8,
            OutParticleDrawCalls,
            OutDrawnSprites);
    }

    return true;
}

} // namespace

int main(int Argc, char** Argv)
{
    uint32_t WidthPx    = 1280u;
    uint32_t HeightPx   = 720u;
    bool     SkipRender = false;
    for (int I = 1; I < Argc; ++I)
    {
        const char* Arg = Argv[I];
        if (std::strncmp(Arg, "--width=", 8) == 0)
        {
            const long V = std::strtol(Arg + 8, nullptr, 10);
            if (V >= 320L && V <= 3840L) WidthPx = static_cast<uint32_t>(V);
        }
        else if (std::strncmp(Arg, "--height=", 9) == 0)
        {
            const long V = std::strtol(Arg + 9, nullptr, 10);
            if (V >= 240L && V <= 2160L) HeightPx = static_cast<uint32_t>(V);
        }
        else if (std::strcmp(Arg, "--skip-render") == 0)
        {
            SkipRender = true;
        }
    }

    const std::filesystem::path RepoRoot    = ResolveRepositoryRoot();
    const std::filesystem::path ProjectRoot = RepoRoot / "Projects" / "AutoSpace";
    const std::filesystem::path OutputDir   = RepoRoot / "Exhibits" / "Images" / "AutoSpace";
    const std::filesystem::path ShaderDir   = ResolveShaderDir(RepoRoot);

    std::cout << "=== AutoSpaceStudioValidation Exhibit ===\n";
    std::cout << "  RepoRoot  : " << RepoRoot.generic_string() << "\n";
    std::cout << "  OutputDir : " << OutputDir.generic_string() << "\n";

    ValidationTracker Tracker {};

    AutoSpace::AutoSpaceLifecycle Lifecycle {};
    std::string Diagnostic {};
    const bool BootOk = Lifecycle.Initialize(
        ProjectRoot,
        true,
        Diagnostic,
        AutoSpace::StudioLobbyId::StudioLobby1Cyclorama);
    Tracker.Check(BootOk, "AutoSpaceLifecycle initialized cleanly inside MainWorld.WorldSpace");
    if (!BootOk)
    {
        std::cerr << "  Diagnostic: " << Diagnostic << "\n";
        return 1;
    }

    const auto BootMetrics = Lifecycle.InspectMetrics();
    Tracker.Check(BootMetrics.BootLoadedFromDisk, "Project boot configuration loaded from Projects/AutoSpace");
    Tracker.Check(BootMetrics.MainWorldLoaded && BootMetrics.SunEnabled && BootMetrics.SkyAtmosphereEnabled,
        "MainWorld.WorldSpace loaded with Sun & Sky enabled");
    Tracker.Check(BootMetrics.LoadedGeometryInstances == 0u,
        "MainWorld.WorldSpace stays single-level with 0 baked disk placements");

    TieredWorldRenderSequence CpuSequence {};
    ParticleGpuRenderer       ParticleRenderer {};
    VulkanInterchange         VkDevice {};
    VulkanWorldRenderSequence GpuSequence {};
    bool                      VulkanReady = false;
    if (!SkipRender)
    {
        std::string VkDiag {};
        if (VkDevice.InitializeInterchange(RepoRoot, &VkDiag) && GpuSequence.InitializeSequence(VkDevice, ShaderDir))
        {
            VulkanReady = true;
            std::cout << "  Vulkan GPU: " << GpuSequence.QueryAdapterTitle() << "\n";
        }
        else
        {
            std::cout << "  Vulkan unavailable (" << VkDiag << "), falling back to CPU OpenPbrDirect.\n";
        }
    }

    std::vector<uint8_t> L1HeroRgba {};
    std::vector<uint8_t> L1CrewRgba {};
    std::vector<uint8_t> L2HeroRgba {};
    std::vector<uint8_t> L3HeroRgba {};

    //--------------------------------------------------------------------------------------------------------------------
    // 1. Lobby 1 · Studio Cyclorama
    //--------------------------------------------------------------------------------------------------------------------
    {
        std::string Err {};
        Tracker.Check(Lifecycle.VerifyProjectContracts(Err), "Lobby 1 VerifyProjectContracts passes");
        const auto Tel = Lifecycle.InspectLobbyLifecycle().InspectTelemetry();
        Tracker.Check(Tel.ActiveLobby == AutoSpace::StudioLobbyId::StudioLobby1Cyclorama, "Lobby 1 active slug == studio-lobby");
        Tracker.Check(Tel.VehicleCount == 3u && Tel.AreaLightCount == 3u && Tel.PhotometricLightCount == 2u,
            "Lobby 1 counts: 3 vehicles, 3 area lights, 2 IES lights");
        Tracker.Check(Tel.CameraRigCount == 3u && Tel.ParticleCloudCount == 0u && Tel.TotalMeshTriangles >= 85000u,
            "Lobby 1 rigs & mesh budget verified (3 camera rigs, 0 particles, >= 85k tris)");
#if FRONTIER_PROFILE_EDITOR
        Tracker.Check(Tel.OutlinerRowCount == 36u,
            "Lobby 1 Editor Outliner populated cleanly via WorldInstanceEditorBridge (rows=" + std::to_string(Tel.OutlinerRowCount) + ")");
#else
        Tracker.Check(Tel.OutlinerRowCount == 0u, "Lobby 1 Shipping build carries 0 Editor Outliner rows");
#endif
        const uint32_t Ticks = Lifecycle.RunBoundedTicks(12u, 1.0 / 60.0);
        Tracker.Check(Ticks == 12u, "Lobby 1 deterministic 12 fixed-step ticks executed");

        if (!SkipRender)
        {
            uint32_t Calls = 0u, Sprites = 0u;
            const bool HOk = RenderLobbyFrame(
                Lifecycle.AccessLobbyLifecycle(), ParticleRenderer,
                VulkanReady ? &GpuSequence : nullptr, CpuSequence,
                0u, 1.05f, 0u, WidthPx, HeightPx, L1HeroRgba, Calls, Sprites, Err);
            Tracker.Check(HOk, "Lobby 1 Hero34 frame rendered");
            if (HOk)
            {
                const auto M = AnalyzeRgbaImage(L1HeroRgba);
                Tracker.Check(M.MeanLuma >= 12.0 && M.P95Luma >= 60.0 && M.NonBlackRatio >= 0.65,
                    "Lobby 1 Hero34 visual metrics valid (Mean=" + std::to_string(M.MeanLuma) + ", P95=" + std::to_string(M.P95Luma) + ")");
                Tracker.Check(SaveRgbaPng(OutputDir / "studio-lobby-Hero34-VulkanGi.png", WidthPx, HeightPx, L1HeroRgba),
                    "Saved Exhibits/Images/AutoSpace/studio-lobby-Hero34-VulkanGi.png");
            }

            const bool COk = RenderLobbyFrame(
                Lifecycle.AccessLobbyLifecycle(), ParticleRenderer,
                VulkanReady ? &GpuSequence : nullptr, CpuSequence,
                5u, 1.15f, 1u, WidthPx, HeightPx, L1CrewRgba, Calls, Sprites, Err);
            Tracker.Check(COk, "Lobby 1 CrewBTS frame rendered");
            if (COk)
            {
                const auto M = AnalyzeRgbaImage(L1CrewRgba);
                Tracker.Check(M.MeanLuma >= 12.0 && M.P95Luma >= 60.0 && M.NonBlackRatio >= 0.65,
                    "Lobby 1 CrewBTS visual metrics valid (Mean=" + std::to_string(M.MeanLuma) + ", P95=" + std::to_string(M.P95Luma) + ")");
                Tracker.Check(SaveRgbaPng(OutputDir / "studio-lobby-CrewBTS-VulkanGi.png", WidthPx, HeightPx, L1CrewRgba),
                    "Saved Exhibits/Images/AutoSpace/studio-lobby-CrewBTS-VulkanGi.png");
            }
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 2. Lobby 2 · Forest Soundstage (Foliage & Rocks Intentionally Blank/Omitted)
    //--------------------------------------------------------------------------------------------------------------------
    {
        std::string Err {};
        const bool SwOk = Lifecycle.SelectStudioLobby(AutoSpace::StudioLobbyId::StudioLobby2ForestStage, Err);
        Tracker.Check(SwOk && Lifecycle.VerifyProjectContracts(Err), "Lobby 2 single-level switch + contracts pass");
        const auto Tel = Lifecycle.InspectLobbyLifecycle().InspectTelemetry();
        Tracker.Check(Tel.ActiveLobby == AutoSpace::StudioLobbyId::StudioLobby2ForestStage, "Lobby 2 active slug == studio-lobby-2");
        Tracker.Check(Tel.VehicleCount == 4u && Tel.AreaLightCount == 3u && Tel.PhotometricLightCount == 2u,
            "Lobby 2 counts: 4 vehicles, 3 area lights, 2 IES lights");
        Tracker.Check(Tel.ParticleCloudCount == 1u && Tel.SimulatedParticleCount == 280u,
            "Lobby 2 ParticleSpace cloud active (ground-dust, 280 simulated particles)");
#if FRONTIER_PROFILE_EDITOR
        Tracker.Check(Tel.OutlinerRowCount == 24u,
            "Lobby 2 Editor Outliner populated without leak/rocks/foliage (rows=" + std::to_string(Tel.OutlinerRowCount) + ")");
#else
        Tracker.Check(Tel.OutlinerRowCount == 0u, "Lobby 2 Shipping build carries 0 Editor Outliner rows");
#endif

        // Confirm zero rock or foliage entities exist in Lobby 2 scene surface registry (4 vehicles + 3 set + 1 fan + 1 softbox + 2 crew = 11)
        bool HasFoliageOrRocks = (Tel.SurfaceInstanceCount != 11u);
        const auto& DustNodeList = Lifecycle.InspectLobbyLifecycle().InspectParticleClouds();
        for (const auto& Cloud : DustNodeList)
        {
            if (Cloud.NodeId.find("rock") != std::string::npos ||
                Cloud.NodeId.find("leaf") != std::string::npos ||
                Cloud.NodeId.find("fern") != std::string::npos ||
                Cloud.NodeId.find("foliage") != std::string::npos)
            {
                HasFoliageOrRocks = true;
            }
        }
        Tracker.Check(!HasFoliageOrRocks, "Lobby 2 leaves rocks & botanical foliage completely blank/omitted (surfaces=11)");

        const uint32_t Ticks = Lifecycle.RunBoundedTicks(30u, 1.0 / 60.0);
        Tracker.Check(Ticks == 30u, "Lobby 2 simulated 30 deterministic ticks cleanly");

        if (!SkipRender)
        {
            uint32_t Calls = 0u, Sprites = 0u;
            const bool RenderOk = RenderLobbyFrame(
                Lifecycle.AccessLobbyLifecycle(), ParticleRenderer,
                VulkanReady ? &GpuSequence : nullptr, CpuSequence,
                0u, 1.10f, 2u, WidthPx, HeightPx, L2HeroRgba, Calls, Sprites, Err);
            Tracker.Check(RenderOk && Calls >= 1u && Sprites >= 10u,
                "Lobby 2 Hero34 rendered with occluded ParticleSpace dust overlay (calls=" +
                    std::to_string(Calls) + ", sprites=" + std::to_string(Sprites) + ")");
            if (RenderOk)
            {
                const auto M = AnalyzeRgbaImage(L2HeroRgba);
                const double DiffVsL1 = ComputeMeanAbsDiff(L1HeroRgba, L2HeroRgba);
                Tracker.Check(M.MeanLuma >= 12.0 && M.P95Luma >= 60.0 && M.NonBlackRatio >= 0.50 && DiffVsL1 >= 8.0,
                    "Lobby 2 visual proof verified distinct from Lobby 1 (Mean=" + std::to_string(M.MeanLuma) +
                        ", P95=" + std::to_string(M.P95Luma) +
                        ", NonBlack=" + std::to_string(M.NonBlackRatio) +
                        ", DiffVsL1=" + std::to_string(DiffVsL1) + ")");
                Tracker.Check(SaveRgbaPng(OutputDir / "studio-lobby-2-Hero34-VulkanGi.png", WidthPx, HeightPx, L2HeroRgba),
                    "Saved Exhibits/Images/AutoSpace/studio-lobby-2-Hero34-VulkanGi.png");
            }
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 3. Lobby 3 · Racing Pit Garage & Stadium Track
    //--------------------------------------------------------------------------------------------------------------------
    {
        std::string Err {};
        const bool SwOk = Lifecycle.SelectStudioLobby(AutoSpace::StudioLobbyId::StudioLobby3RacingGarage, Err);
        Tracker.Check(SwOk && Lifecycle.VerifyProjectContracts(Err), "Lobby 3 single-level switch + contracts pass");
        const auto Tel = Lifecycle.InspectLobbyLifecycle().InspectTelemetry();
        Tracker.Check(Tel.ActiveLobby == AutoSpace::StudioLobbyId::StudioLobby3RacingGarage, "Lobby 3 active slug == studio-lobby-3");
        Tracker.Check(Tel.VehicleCount == 4u && Tel.AreaLightCount == 2u && Tel.PhotometricLightCount == 2u,
            "Lobby 3 counts: 4 vehicles (1 on hydraulic lift), 2 area lights, 2 IES lights");
        Tracker.Check(Tel.ParticleCloudCount == 2u && Tel.SimulatedParticleCount == 310u && Tel.TotalMeshTriangles >= 115000u,
            "Lobby 3 multi-cloud ParticleSpace active (mist-spray + ground-dust, 310 active particles, >= 115k tris)");
#if FRONTIER_PROFILE_EDITOR
        Tracker.Check(Tel.OutlinerRowCount == 27u,
            "Lobby 3 Editor Outliner populated without leak (rows=" + std::to_string(Tel.OutlinerRowCount) + ")");
#else
        Tracker.Check(Tel.OutlinerRowCount == 0u, "Lobby 3 Shipping build carries 0 Editor Outliner rows");
#endif

        const uint32_t Ticks = Lifecycle.RunBoundedTicks(30u, 1.0 / 60.0);
        Tracker.Check(Ticks == 30u, "Lobby 3 simulated 30 deterministic ticks cleanly");

        if (!SkipRender)
        {
            uint32_t Calls = 0u, Sprites = 0u;
            const bool RenderOk = RenderLobbyFrame(
                Lifecycle.AccessLobbyLifecycle(), ParticleRenderer,
                VulkanReady ? &GpuSequence : nullptr, CpuSequence,
                0u, 1.05f, 3u, WidthPx, HeightPx, L3HeroRgba, Calls, Sprites, Err);
            Tracker.Check(RenderOk && Calls >= 2u && Sprites >= 10u,
                "Lobby 3 Hero34 rendered with occluded ParticleSpace smoke + dust (calls=" +
                    std::to_string(Calls) + ", sprites=" + std::to_string(Sprites) + ")");
            if (RenderOk)
            {
                const auto M = AnalyzeRgbaImage(L3HeroRgba);
                const double DiffVsL1 = ComputeMeanAbsDiff(L1HeroRgba, L3HeroRgba);
                const double DiffVsL2 = ComputeMeanAbsDiff(L2HeroRgba, L3HeroRgba);
                Tracker.Check(M.MeanLuma >= 14.0 && M.P95Luma >= 75.0 && M.NonBlackRatio >= 0.70 && DiffVsL1 >= 8.0 && DiffVsL2 >= 8.0,
                    "Lobby 3 visual proof verified distinct (Mean=" + std::to_string(M.MeanLuma) +
                        ", DiffVsL2=" + std::to_string(DiffVsL2) + ")");
                Tracker.Check(SaveRgbaPng(OutputDir / "studio-lobby-3-Hero34-VulkanGi.png", WidthPx, HeightPx, L3HeroRgba),
                    "Saved Exhibits/Images/AutoSpace/studio-lobby-3-Hero34-VulkanGi.png");
            }
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 4. Composite 2x2 Visual Proof Sheet
    //--------------------------------------------------------------------------------------------------------------------
    if (!SkipRender && !L1HeroRgba.empty() && !L1CrewRgba.empty() && !L2HeroRgba.empty() && !L3HeroRgba.empty())
    {
        uint32_t SheetW = 0u, SheetH = 0u;
        const auto Sheet = BuildProofSheet2x2({ &L1HeroRgba, &L1CrewRgba, &L2HeroRgba, &L3HeroRgba }, WidthPx, HeightPx, SheetW, SheetH);
        Tracker.Check(SaveRgbaPng(OutputDir / "AutoSpaceStudioLobbiesProofSheet.png", SheetW, SheetH, Sheet),
            "Saved Exhibits/Images/AutoSpace/AutoSpaceStudioLobbiesProofSheet.png");
    }

    if (VulkanReady)
    {
        GpuSequence.TeardownSequence();
    }
    Lifecycle.Finalize();

    std::cout << "=== Summary: Passed=" << Tracker.Passed << " Failed=" << Tracker.Failed << " ===\n";
    return (Tracker.Failed == 0u) ? 0 : 1;
}
