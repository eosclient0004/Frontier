/*============================================================================================================================================
                                                       BOTANICALTHUMBNAILBAKER.H
============================================================================================================================================*/
// 🧩 Bakes crisp 128x128 RGBA 3D Studio Plant Render Thumbnails for Frontier's Outliner!
//    - Individual botanical items in the Outliner display a baked general 3D render of the plant (NOT an SVG).
//    - Saved to EngineContent/Thumbnails/Botanical/<SpeciesId>.png.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalGenerator.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Frontier::GeometrySpace::Botanical
{

struct BakedPlantThumbnail
{
    uint32_t             Width    = 128u;
    uint32_t             Height   = 128u;
    std::vector<uint8_t> Rgba8    {}; // Row-major top-down RGBA8 (Width * Height * 4)
};

/// Renders a studio-lit 3/4-angle 3D thumbnail of `plant` (auto-framed to fill ~82% of the frame with soft contact shadow
/// and subtle frosted studio backdrop matching Frontier's dark-slate Outliner!).
[[nodiscard]] BakedPlantThumbnail BakePlant3dThumbnail(
    const BotanicalPlantBuildResult& plant,
    uint32_t                         sizePx = 128u,
    bool                             transparentBackdrop = false);

/// Writes a self-contained RGBA8 PNG file (zero external dependencies, bit-identical across Windows & Linux).
[[nodiscard]] bool WriteRgba8PngFile(
    const std::filesystem::path& filePath,
    uint32_t                     width,
    uint32_t                     height,
    const std::vector<uint8_t>&  rgba8);

} // namespace Frontier::GeometrySpace::Botanical
