/*============================================================================================================================================
                                                    OUTLINERSPECIMENICONSVALIDATION.CPP
============================================================================================================================================*/
// Phase 5 Verification Exhibit:
//   - Boots Vulkan 1.3 + InspectorGpuBaker::AttachDevice (ThumbnailSpecimenCompute.comp.spv)
//   - Loads AutoSpace Lobby 1 directly from disk-backed native .WorldSpace resources
//   - Bakes unique 128x128 GPU specimen icons for every geometry, luminaire, and camera Outliner entry
//   - Verifies 100% pure-GPU baking (0 CPU fallbacks), valid ThumbnailUvQuad bindings,
//     distinct per-entry pixel hashes, non-trivial L1 visual separation across specimen silhouettes,
//     and writes a crisp contact-sheet PNG proof to Exhibits/Images/AutoSpace/OutlinerBakedSpecimenIconsProof.png.

#include "Engine/Components/ContentBrowser/ThumbnailSheetRepository.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Editor/Inspectors/InspectorGpuBaker.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Lifecycle/ProjectEditorSceneRegistry.h"
#include "Source/Editor/AutoSpaceEditorSceneAdapter.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <set>
#include <span>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::GeometricRaster;

namespace
{
uint64_t ComputeTilePixelHash(std::span<const uint8_t> Pixels) noexcept
{
    uint64_t H = 14695981039346656037ull;
    for (const uint8_t B : Pixels)
    {
        H ^= static_cast<uint64_t>(B);
        H *= 1099511628211ull;
    }
    return H;
}

double ComputeMeanAbsDiff(std::span<const uint8_t> A, std::span<const uint8_t> B) noexcept
{
    if (A.size() != B.size() || A.empty())
    {
        return 0.0;
    }
    double Acc = 0.0;
    for (std::size_t I = 0u; I < A.size(); I += 4u)
    {
        Acc += std::abs(static_cast<double>(A[I + 0u]) - static_cast<double>(B[I + 0u]));
        Acc += std::abs(static_cast<double>(A[I + 1u]) - static_cast<double>(B[I + 1u]));
        Acc += std::abs(static_cast<double>(A[I + 2u]) - static_cast<double>(B[I + 2u]));
    }
    return Acc / (static_cast<double>(A.size() / 4u) * 3.0);
}

double ComputeMeanLuminance(std::span<const uint8_t> Pixels) noexcept
{
    if (Pixels.empty())
    {
        return 0.0;
    }
    double Acc = 0.0;
    for (std::size_t I = 0u; I < Pixels.size(); I += 4u)
    {
        Acc += 0.2126 * Pixels[I + 0u] + 0.7152 * Pixels[I + 1u] + 0.0722 * Pixels[I + 2u];
    }
    return Acc / static_cast<double>(Pixels.size() / 4u);
}

std::filesystem::path LocateCompiledShaderDirectory(const std::filesystem::path& RepoRoot)
{
    const std::filesystem::path Candidates[] = {
#ifdef FRONTIER_COMPILED_SHADER_DIR
        std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR),
#endif
        RepoRoot / "build" / "CompiledShaders",
        RepoRoot / "build" / "Build" / "Editor" / "CompiledShaders",
        RepoRoot / "Build" / "Editor" / "CompiledShaders",
        std::filesystem::current_path() / "CompiledShaders",
    };
    for (const auto& Dir : Candidates)
    {
        if (std::filesystem::exists(Dir / "ThumbnailSpecimenCompute.comp.spv"))
        {
            return Dir;
        }
    }
    return Candidates[0];
}

int CheckCounter = 0;
int FailCounter  = 0;

void CheckCond(bool Passed, const char* Label)
{
    ++CheckCounter;
    if (Passed)
    {
        std::printf("  [PASS] %s\n", Label);
    }
    else
    {
        ++FailCounter;
        std::printf("  [FAIL] %s\n", Label);
    }
}
} // namespace

int main()
{
    const std::filesystem::path RepoRoot { FRONTIER_REPO_ROOT_DIR };
    const std::filesystem::path ShaderDir = LocateCompiledShaderDirectory(RepoRoot);
    const std::filesystem::path ProjectFile = RepoRoot / "Projects" / "AutoSpace" / "AutoSpace.projectspace";

    std::printf("=== Phase 5 Outliner Specimen Icons GPU Validation ===\n");
    std::printf("  ShaderDir   : %s\n", ShaderDir.string().c_str());
    std::printf("  ProjectFile : %s\n", ProjectFile.string().c_str());

    VulkanInterchange Device {};
    const bool VulkanOk = Device.InitializeInterchange(RepoRoot);
    CheckCond(VulkanOk && Device.Ready(), "Vulkan 1.3 interchange initialized");
    if (!VulkanOk || !Device.Ready())
    {
        return 1;
    }

    const bool BakerReady = InspectorGpuBaker::AttachDevice(&Device, ShaderDir);
    CheckCond(BakerReady, "InspectorGpuBaker attached and ThumbnailSpecimenCompute.comp.spv compiled");
    if (!BakerReady)
    {
        return 1;
    }

    AutoSpace::RegisterAutoSpaceEditorSceneAdapter();
    const Lifecycle::ProjectEditorSceneAdapter* Adapter =
        Lifecycle::ProjectEditorSceneRegistry::FindMatchingAdapter(ProjectFile);
    CheckCond(Adapter != nullptr && static_cast<bool>(Adapter->ConstructEditorScene),
              "AutoSpace ProjectEditorSceneAdapter registered");

    Lifecycle::ProjectEditorSceneRequest Req {};
    Req.ProjectSpacePath  = ProjectFile;
    Req.RepositoryRoot    = RepoRoot;
    Req.SceneVariantIndex = 0u;
    Req.ViewportWidthPx   = 640u;
    Req.ViewportHeightPx  = 360u;

    ShowcaseWorldSceneBundle            Bundle {};
    Lifecycle::ProjectEditorSceneResult LoadResult {};
    std::string                         Diag {};
    const bool LoadedOk = (Adapter != nullptr) &&
                          Adapter->ConstructEditorScene(Req, Bundle, LoadResult, Diag);
    CheckCond(LoadedOk && !Bundle.Objects.empty(), "AutoSpace Lobby 1 loaded from disk-backed .WorldSpace");

    OutlinerPanel Outliner {};
    RegisterShowcaseGalleryInOutliner(Outliner, Bundle, true);
    for (const auto& ExtraCam : LoadResult.ExtraCameras)
    {
        OutlinerRecordEntry CamRec {};
        CamRec.EnclosingToken    = Bundle.CamerasFolderToken;
        CamRec.Category          = ConstructSlotCategory::OpticsCamera;
        CamRec.Title             = ExtraCam.Title;
        CamRec.CategoryLabel     = "Scene / Cameras";
        CamRec.Subtitle          = ExtraCam.Subtitle;
        CamRec.DomainTag         = "camera:perspective";
        CamRec.VisibilityEnabled = true;
        (void)Outliner.ConstructDirectEntry(std::move(CamRec));
    }

    const uint64_t GpuBefore = ThumbnailSheetRepository::QueryGpuBakedTileCount();
    const uint64_t CpuBefore = ThumbnailSheetRepository::QueryCpuBakedTileCount();
    ThumbnailSheetRepository IconSheet {};
    const uint32_t BakedCount = BakeShowcaseOutlinerIcons(Outliner, Bundle, IconSheet);
    Outliner.AttachIconSheetRepository(&IconSheet);

    const uint64_t GpuBaked = ThumbnailSheetRepository::QueryGpuBakedTileCount() - GpuBefore;
    const uint64_t CpuBaked = ThumbnailSheetRepository::QueryCpuBakedTileCount() - CpuBefore;
    std::printf("  Baked Outliner Icons: %u | GPU Tiles: %llu | CPU Fallback Tiles: %llu | Atlas Pages: %u\n",
                BakedCount,
                static_cast<unsigned long long>(GpuBaked),
                static_cast<unsigned long long>(CpuBaked),
                IconSheet.PageCount());

    CheckCond(BakedCount >= 20u, "At least 20 scene/light/camera Outliner entries baked");
    CheckCond(GpuBaked == static_cast<uint64_t>(BakedCount), "100% of Outliner specimen icons baked on GPU compute");
    CheckCond(CpuBaked == 0ull, "Zero CPU fallback bakes occurred");
    CheckCond(IconSheet.PageCount() >= 1u, "ThumbnailSheetRepository allocated at least 1 RGBA8 atlas page");

    struct SpecimenRecord
    {
        std::string          Title;
        std::size_t          TileIndex = OutlinerNoIconTile;
        std::vector<uint8_t> Pixels;
        uint64_t             Hash64    = 0ull;
        double               MeanLuma  = 0.0;
    };

    std::vector<SpecimenRecord> Specimens;
    std::set<uint64_t>          DistinctHashes;
    bool                        AllUvValid      = true;
    bool                        AllNonBlank     = true;
    std::size_t                 CoupeIdx        = ~std::size_t{ 0u };
    std::size_t                 SoftboxIdx      = ~std::size_t{ 0u };
    std::size_t                 FresnelIdx      = ~std::size_t{ 0u };
    std::size_t                 CameraIdx       = ~std::size_t{ 0u };
    std::size_t                 CycloramaIdx    = ~std::size_t{ 0u };
    std::size_t                 EmissiveOrbIdx  = ~std::size_t{ 0u };

    for (const OutlinerRecordEntry& Rec : Outliner.QueryEntries())
    {
        if (Rec.Category == ConstructSlotCategory::Folder || Rec.DomainTag.starts_with("env:"))
        {
            continue;
        }
        if (Rec.IconTileIndex == OutlinerNoIconTile)
        {
            AllUvValid = false;
            continue;
        }
        const ThumbnailUvQuad Uv = IconSheet.QueryTileUv(Rec.IconTileIndex);
        std::vector<uint8_t>  Pixels;
        const bool            PixelsRead = IconSheet.QueryTileRgbaPixels(Rec.IconTileIndex, Pixels);
        if (!Uv.Valid || Uv.TexturePointer == nullptr || !PixelsRead || Pixels.size() != 128u * 128u * 4u)
        {
            AllUvValid = false;
            continue;
        }
        const double   Luma   = ComputeMeanLuminance(Pixels);
        const uint64_t Hash64 = ComputeTilePixelHash(Pixels);
        if (Luma < 8.0 || Luma > 252.0)
        {
            AllNonBlank = false;
        }
        DistinctHashes.insert(Hash64);

        const std::size_t LocalIdx = Specimens.size();
        if (CoupeIdx == ~std::size_t{ 0u } && Rec.Title.find("Coupe") != std::string::npos)
        {
            CoupeIdx = LocalIdx;
        }
        if (SoftboxIdx == ~std::size_t{ 0u } && Rec.Title.find("Softbox") != std::string::npos)
        {
            SoftboxIdx = LocalIdx;
        }
        if (FresnelIdx == ~std::size_t{ 0u } &&
            (Rec.Title.find("Fresnel") != std::string::npos || Rec.Title.find("IES") != std::string::npos))
        {
            FresnelIdx = LocalIdx;
        }
        if (CameraIdx == ~std::size_t{ 0u } && Rec.Category == ConstructSlotCategory::OpticsCamera)
        {
            CameraIdx = LocalIdx;
        }
        if (CycloramaIdx == ~std::size_t{ 0u } &&
            Rec.Title.find("Cyclorama") != std::string::npos &&
            Rec.Title.find("Coupe") == std::string::npos)
        {
            CycloramaIdx = LocalIdx;
        }
        if (EmissiveOrbIdx == ~std::size_t{ 0u } && Rec.Title.find("Orb") != std::string::npos)
        {
            EmissiveOrbIdx = LocalIdx;
        }

        Specimens.push_back(SpecimenRecord { Rec.Title, Rec.IconTileIndex, std::move(Pixels), Hash64, Luma });
    }

    std::printf("  Validated %zu Outliner rows -> %zu distinct pixel hashes\n",
                Specimens.size(),
                DistinctHashes.size());

    CheckCond(AllUvValid, "Every non-folder Outliner entry resolves to a valid ThumbnailUvQuad + 128x128 pixel tile");
    CheckCond(AllNonBlank, "Every baked specimen icon has well-exposed studio luminance (8 < Luma < 252)");
    CheckCond(DistinctHashes.size() >= 10u, "At least 10 visually distinct baked icons across scene entries");
    CheckCond(CoupeIdx != ~std::size_t{ 0u } && SoftboxIdx != ~std::size_t{ 0u } &&
                  FresnelIdx != ~std::size_t{ 0u } && CameraIdx != ~std::size_t{ 0u } &&
                  CycloramaIdx != ~std::size_t{ 0u } && EmissiveOrbIdx != ~std::size_t{ 0u },
              "Found all 6 iconic hero archetypes (Coupe, Softbox, Fresnel IES, Camera, Cyclorama, Emissive Orb)");

    if (CoupeIdx != ~std::size_t{ 0u } && SoftboxIdx != ~std::size_t{ 0u } &&
        FresnelIdx != ~std::size_t{ 0u } && CameraIdx != ~std::size_t{ 0u } &&
        CycloramaIdx != ~std::size_t{ 0u } && EmissiveOrbIdx != ~std::size_t{ 0u })
    {
        const double DiffCoupeSoftbox   = ComputeMeanAbsDiff(Specimens[CoupeIdx].Pixels, Specimens[SoftboxIdx].Pixels);
        const double DiffSoftboxFresnel = ComputeMeanAbsDiff(Specimens[SoftboxIdx].Pixels, Specimens[FresnelIdx].Pixels);
        const double DiffCoupeCamera    = ComputeMeanAbsDiff(Specimens[CoupeIdx].Pixels, Specimens[CameraIdx].Pixels);
        const double DiffCoupeCyclo     = ComputeMeanAbsDiff(Specimens[CoupeIdx].Pixels, Specimens[CycloramaIdx].Pixels);
        const double DiffOrbCamera      = ComputeMeanAbsDiff(Specimens[EmissiveOrbIdx].Pixels, Specimens[CameraIdx].Pixels);

        std::printf("  Pairwise L1 Delta [0..255]: Coupe/Softbox=%.2f | Softbox/Fresnel=%.2f | Coupe/Camera=%.2f | Coupe/Cyclorama=%.2f | Orb/Camera=%.2f\n",
                    DiffCoupeSoftbox, DiffSoftboxFresnel, DiffCoupeCamera, DiffCoupeCyclo, DiffOrbCamera);

        CheckCond(DiffCoupeSoftbox > 10.0, "Automotive Coupe vs Area Softbox specimen silhouettes clearly differ");
        CheckCond(DiffSoftboxFresnel > 6.0, "Area Softbox vs Fresnel IES Lamp specimen silhouettes clearly differ");
        CheckCond(DiffCoupeCamera > 8.0, "Automotive Coupe vs Cinema Camera specimen silhouettes clearly differ");
        CheckCond(DiffCoupeCyclo > 8.0, "Automotive Coupe vs Cyclorama Stage specimen silhouettes clearly differ");
        CheckCond(DiffOrbCamera > 10.0, "Emissive Orb vs Cinema Camera specimen silhouettes clearly differ");
    }

    // Export contact-sheet PNG proof of all baked 128x128 icons (8 columns x N rows with 10px studio slate padding)
    const uint32_t TilePx  = 128u;
    const uint32_t PadPx   = 10u;
    const uint32_t Cols    = 8u;
    const uint32_t Rows    = std::max(1u, static_cast<uint32_t>((Specimens.size() + Cols - 1u) / Cols));
    const uint32_t SheetW  = Cols * TilePx + (Cols + 1u) * PadPx;
    const uint32_t SheetH  = Rows * TilePx + (Rows + 1u) * PadPx;
    std::vector<uint8_t> SheetRgba(static_cast<std::size_t>(SheetW) * SheetH * 4u, 0u);
    for (std::size_t I = 0u; I < SheetRgba.size(); I += 4u)
    {
        SheetRgba[I + 0u] = 18u;
        SheetRgba[I + 1u] = 20u;
        SheetRgba[I + 2u] = 24u;
        SheetRgba[I + 3u] = 255u;
    }

    for (std::size_t Idx = 0u; Idx < Specimens.size(); ++Idx)
    {
        const uint32_t C = static_cast<uint32_t>(Idx % Cols);
        const uint32_t R = static_cast<uint32_t>(Idx / Cols);
        const uint32_t DstX = PadPx + C * (TilePx + PadPx);
        const uint32_t DstY = PadPx + R * (TilePx + PadPx);
        const auto& Src = Specimens[Idx].Pixels;
        for (uint32_t Y = 0u; Y < TilePx; ++Y)
        {
            for (uint32_t X = 0u; X < TilePx; ++X)
            {
                const std::size_t SrcOff = (static_cast<std::size_t>(Y) * TilePx + X) * 4u;
                const std::size_t DstOff = (static_cast<std::size_t>(DstY + Y) * SheetW + (DstX + X)) * 4u;
                SheetRgba[DstOff + 0u] = Src[SrcOff + 0u];
                SheetRgba[DstOff + 1u] = Src[SrcOff + 1u];
                SheetRgba[DstOff + 2u] = Src[SrcOff + 2u];
                SheetRgba[DstOff + 3u] = 255u;
            }
        }
    }

    const std::filesystem::path ProofPath =
        RepoRoot / "Exhibits" / "Images" / "AutoSpace" / "OutlinerBakedSpecimenIconsProof.png";
    std::filesystem::create_directories(ProofPath.parent_path());
    const int WrotePng = stbi_write_png(
        ProofPath.string().c_str(),
        static_cast<int>(SheetW),
        static_cast<int>(SheetH),
        4,
        SheetRgba.data(),
        static_cast<int>(SheetW * 4u));
    CheckCond(WrotePng != 0, "Wrote contact-sheet PNG proof Exhibits/Images/AutoSpace/OutlinerBakedSpecimenIconsProof.png");

    Outliner.AttachIconSheetRepository(nullptr);
    InspectorGpuBaker::DetachDevice();

    std::printf("=== Result: %d/%d checks passed ===\n", CheckCounter - FailCounter, CheckCounter);
    return (FailCounter == 0) ? 0 : 1;
}
