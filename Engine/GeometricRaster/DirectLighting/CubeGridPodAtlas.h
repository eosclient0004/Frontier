/*============================================================================================================================================
                                                           CUBEGRIDPODATLAS.H
============================================================================================================================================*/
// 🧩 Non-Averaged Sparse 8x4x4 Spatial-Directional Cube-Grid Pod Compiler, Continuous Nanite LOD (0..3) & Residual-Error Optimizer
//    - Preserves empty center columns between dual headlamps (zero center collapse)
//    - -Y-pole octahedral directional parameterization packed into a bleed-free RGBA32F GPU sheet (integer texelFetch bilinear)
//    - Background residual optimizer refines SubVoxelOffset, ApertureRadius & Non-Negative Octahedral Deconvolution

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/UnifiedLightTypes.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct OctahedralDirectionalTile
{
    uint32_t           Resolution = 16u; // [px]    - Tile width & height (16, 12, or 4)
    std::vector<float> TexelsRgb  {};    // [cd]    - Resolution * Resolution * 3 non-negative directional candela values
    uint32_t           AtlasOriginX = 0u;// [px]    - Packed X offset inside GPU RGBA32F sheet
    uint32_t           AtlasOriginY = 0u;// [px]    - Packed Y offset inside GPU RGBA32F sheet
};

struct CubeGridActiveCell
{
    uint32_t                  GridCoord[3]      {};                     // [idx] - Discrete (ix, iy, iz) in [0..7, 0..3, 0..3]
    std::array<float, 3>      VoxelCenterWorld  { 0.0f, 0.0f, 0.0f };   // [m]   - Quantized voxel center
    std::array<float, 3>      SubVoxelOffset    { 0.0f, 0.0f, 0.0f };   // [m]   - Residual-optimized anchor offset within [-0.5h, +0.5h]
    float                     ApertureRadiusM   = 0.24f;                // [m]   - Residual-optimized finite-aperture radius
    std::array<float, 3>      DominantDirWorld  { 0.0f, -1.0f, 0.0f };  // [-]   - Peak directional emission vector
    float                     TotalFlux         = 0.0f;                 // [lm]  - Summed flux of sources binned to this voxel
    OctahedralDirectionalTile TileLod0          {};                     // [-]   - 16x16 octahedral directional tile
};

struct CubeGridBimodalPole
{
    std::array<float, 3>      AnchorWorld     { 0.0f, 0.0f, 0.0f }; // [m]  - Flux-weighted half-cluster origin
    float                     ApertureRadiusM = 0.28f;              // [m]  - Effective pole aperture radius
    OctahedralDirectionalTile TileLod1        {};                   // [-]  - 12x12 octahedral directional tile
};

struct CompiledCubeGridPod
{
    uint32_t                        RigGroupId         = 0u;
    LightMobility                   Mobility           = LightMobility::Dynamic;
    std::array<float, 3>            BoundsMinWorld     { 0.0f, 0.0f, 0.0f };
    std::array<float, 3>            BoundsMaxWorld     { 0.0f, 0.0f, 0.0f };
    std::array<float, 3>            FluxCentroidWorld  { 0.0f, 0.0f, 0.0f };
    float                           BoundingDiameterM  = 1.0f;
    float                           InfluenceRadiusM   = 20.0f;
    uint32_t                        SourceCount        = 0u;
    std::vector<CubeGridActiveCell> ActiveCellsLod0    {}; // Sparse occupied cells of 8x4x4 grid (empty center stays empty!)
    std::vector<CubeGridBimodalPole>BimodalPolesLod1   {}; // 2 spatial poles along primary separation axis
    OctahedralDirectionalTile       UnifiedTileLod2    {}; // 12x12 far-field single-sample tile
    OctahedralDirectionalTile       CoarseTileLod3     {}; // 4x4 ultra-far directional tile
    float                           EstimatedVramKb    = OneCubeGridPodVramKilobytes;
};

struct ResidualOptimizationReport
{
    float InitialRelativeRmsePercent   = 0.0f;
    float OptimizedRelativeRmsePercent = 0.0f;
    float InitialPsnrDecibels          = 0.0f;
    float OptimizedPsnrDecibels        = 0.0f;
    uint32_t CompletedIterations       = 0u;
};

struct PackedPodAtlasSheet
{
    uint32_t           WidthPx   = 256u;
    uint32_t           HeightPx  = 256u;
    std::vector<float> Rgba32F   {}; // Bleed-free RGBA32F GPU texture payload (WidthPx * HeightPx * 4)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// -Y-pole octahedral directional codec (bit-exact match with UnifiedLightEvaluation.slang)
[[nodiscard]] std::array<float, 2> OctahedralEncodeMinusY(const std::array<float, 3>& UnitDir) noexcept;
[[nodiscard]] std::array<float, 3> OctahedralDecodeMinusY(float U, float V) noexcept;

// Evaluates directional candela intensity I_rgb(dir) of one raw UnifiedLightSource along world ray vector RayDir
[[nodiscard]] std::array<float, 3> EvaluateRawSourceCandela(
    const UnifiedLightSource&   Source,
    const std::array<float, 3>& UnitRayFromLight) noexcept;

// Bleed-free bilinear sample matching GPU integer texelFetch bilinear interpolation
[[nodiscard]] std::array<float, 3> SampleOctahedralTileBilinear(
    const OctahedralDirectionalTile& Tile,
    const std::array<float, 3>&      UnitRayFromLight) noexcept;

// Evaluates smooth quartic perceptual influence window w_inf(d) = max(0, 1 - (d/R_inf)^4)^2
[[nodiscard]] float EvaluateQuarticInfluenceWindow(float DistanceMeters, float InfluenceRadiusMeters) noexcept;

// Continuous logarithmic Nanite Light LOD [0..3] with smooth Hermite transitions
[[nodiscard]] float ComputeContinuousPodLod(
    float DistanceMeters,
    float BoundingDiameterMeters,
    float LodBias = 0.0f) noexcept;

// Compiles a group of sources into a sparse 8x4x4 Cube-Grid Pod (zero center-averaging at LOD 0)
[[nodiscard]] CompiledCubeGridPod CompileCubeGridPod(
    uint32_t                             RigGroupId,
    std::span<const UnifiedLightSource>  GroupSources);

// Evaluates shaded Lambertian + Karis-aperture GGX radiance from a CompiledCubeGridPod at ShadingPos
[[nodiscard]] std::array<float, 3> EvaluateCubeGridPodAtSurface(
    const CompiledCubeGridPod&  Pod,
    const std::array<float, 3>& ShadingPos,
    const std::array<float, 3>& SurfaceNormal,
    const std::array<float, 3>& ViewDir,
    float                       Roughness,
    const std::array<float, 3>& Albedo,
    float                       LodBias = 0.0f,
    uint32_t*                   OutTileLookupCount = nullptr) noexcept;

// Iterative Residual-Error Optimizer: refines SubVoxelOffset, ApertureRadius & Non-Negative Octahedral Deconvolution
[[nodiscard]] ResidualOptimizationReport OptimizeCubeGridPodResidual(
    CompiledCubeGridPod&                InOutPod,
    std::span<const UnifiedLightSource> GroundTruthGroupSources,
    uint32_t                            Iterations = 12u);

// Packs all tiles of compiled pods into a bleed-free RGBA32F GPU atlas sheet
[[nodiscard]] PackedPodAtlasSheet PackPodAtlasSheetRgba32F(
    std::span<CompiledCubeGridPod> Pods,
    uint32_t                       SheetResolution = 256u);

} // namespace Frontier::GeometricRaster
