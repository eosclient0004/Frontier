/*============================================================================================================================================
                                                       STATICTOROIDALLIGHTCLIPMAP.H
============================================================================================================================================*/
// 🧩 Automatic 2-Cascade Toroidal Light Clipmap (Fine + Coarse) for STATIC Lights
//    - Voxel-snapped toroidal scrolling updates ONLY newly entered boundary slabs (zero voxel swimming, zero full-world stall)
//    - Strictly isolated from Dynamic light motion (only StaticLightGeneration edits trigger static dirty re-bakes)
//    - Supports Auto / SmallInterior (16m) / MediumPlaza (36m) / SuperLargeWorld (160m+) scene scales

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/UnifiedLightTypes.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ToroidalClipmapCell
{
    std::array<float, 3> WorldCenter    { 0.0f, 0.0f, 0.0f }; // [m]   - Cell center position
    std::array<float, 3> IrradianceL0   { 0.0f, 0.0f, 0.0f }; // [lin] - Hemispherical static irradiance L0
    std::array<float, 3> DominantDirL1  { 0.0f, 1.0f, 0.0f }; // [-]   - Unit dominant static light direction L1
    float                Directionality = 0.0f;               // [0-1] - |L1| / L0 directional coherence
    float                CellError      = 0.0f;               // [-]   - Measured center vs sub-corner / shadow error
};

struct ToroidalLightCascade
{
    uint32_t                         Resolution     = 16u;  // [cnt] - Cells along X and Z
    float                            HalfSpanMeters = 6.0f; // [m]   - World half-extent
    float                            CellSizeMeters = 0.75f;// [m]   - World cell size (2 * HalfSpan / Res)
    int32_t                          OriginCellX    = 0;    // [idx] - Snapped integer toroidal origin X
    int32_t                          OriginCellZ    = 0;    // [idx] - Snapped integer toroidal origin Z
    float                            CenterWorldX   = 0.0f; // [m]   - OriginCellX * CellSizeMeters
    float                            CenterWorldZ   = 0.0f; // [m]   - OriginCellZ * CellSizeMeters
    float                            MaxIrradiance  = 0.25f;// [lin] - Normalization scale for compact RGBA upload
    float                            MaxCellError   = 0.20f;// [-]   - Normalization scale for error channel
    std::vector<ToroidalClipmapCell> Cells          {};     // [cnt] - Res * Res cells stored in toroidal wrapped layout
    std::vector<float>               TexIrradianceRgba32F {};// [4f] - Res * Res * 4 (R, G, B, Directionality)
    std::vector<float>               TexDirectionRgba32F  {};// [4f] - Res * Res * 4 (DomDir.xyz, CellError)
};

struct StaticClipmapTelemetry
{
    uint64_t LastBakedStaticGeneration = 0ull; // [gen] - Most recent StaticLightGeneration baked
    uint32_t TotalFullBakeCount        = 0u;   // [cnt] - Full Fine+Coarse static bakes
    uint32_t TotalDirtySlabScrollCount = 0u;   // [cnt] - Incremental boundary-slab toroidal scrolls
    uint32_t LastUpdatedCellCount      = 0u;   // [cnt] - Cells computed during the latest update (0 when clean)
    float    LastBakeMilliseconds      = 0.0f; // [ms]  - Wall-clock bake duration of the latest update
    float    TotalVramKilobytes        = 0.0f; // [KB]  - GPU 3D/2D toroidal cascade texture footprint
};

struct ToroidalClipmapSample
{
    std::array<float, 3> IrradianceL0   { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> DominantDirL1  { 0.0f, 1.0f, 0.0f };
    float                Directionality = 0.0f;
    float                CellError      = 0.0f;
    float                FineWeight     = 0.0f; // 1.0 inside Fine cascade, smooth transition to 0.0 in Coarse
};

// Optional shadow visibility callback: VisibilityFn(ReceiverPos, LightPos) -> [0..1]
using ClipmapVisibilityCallback = std::function<float(const std::array<float, 3>&, const std::array<float, 3>&)>;

//------------------------------------------------------------------------------------------------------------------------
//                                           STATIC TOROIDAL LIGHT CLIPMAP
//------------------------------------------------------------------------------------------------------------------------

class StaticToroidalLightClipmap
{
public:
    StaticToroidalLightClipmap() = default;

    // Automatically updates ONLY if:
    //   (1) First bake or scale config changed -> full Fine+Coarse bake
    //   (2) StaticLightGeneration changed      -> automatic static re-bake
    //   (3) Focus crossed Fine cell boundary   -> incremental dirty-slab toroidal scroll (only boundary cells baked!)
    //   (4) Otherwise (including Dynamic light motion!) -> 0 cells baked, instant O(1) return!
    [[nodiscard]] bool UpdateAutomatic(
        std::span<const UnifiedLightSource> StaticSources,
        uint64_t                            StaticLightGeneration,
        const std::array<float, 3>&         FocusWorldPos,
        const LightClipmapScaleConfig&      ScaleConfig,
        bool                                ForceFullRebake = false,
        const ClipmapVisibilityCallback&    VisibilityFn    = nullptr);

    [[nodiscard]] ToroidalClipmapSample SampleAtWorldXZ(float WorldX, float WorldZ) const noexcept;

    [[nodiscard]] const ToroidalLightCascade&   InspectFineCascade() const noexcept { return FineCascade; }
    [[nodiscard]] const ToroidalLightCascade&   InspectCoarseCascade() const noexcept { return CoarseCascade; }
    [[nodiscard]] const StaticClipmapTelemetry& QueryTelemetry() const noexcept { return Telemetry; }
    [[nodiscard]] bool                          QueryInitialized() const noexcept { return Initialized; }

private:
    static void BakeSingleCell(
        ToroidalLightCascade&               Cascade,
        int32_t                             WorldCellX,
        int32_t                             WorldCellZ,
        std::span<const UnifiedLightSource> StaticSources,
        const ClipmapVisibilityCallback&    VisibilityFn);

    static void RefreshCascadeNormalization(ToroidalLightCascade& Cascade) noexcept;

    ToroidalLightCascade   FineCascade   {};
    ToroidalLightCascade   CoarseCascade {};
    LightClipmapScaleConfig ActiveConfig {};
    StaticClipmapTelemetry Telemetry     {};
    bool                   Initialized   = false;
};

} // namespace Frontier::GeometricRaster
