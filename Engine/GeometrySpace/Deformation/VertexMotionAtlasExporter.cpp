/*============================================================================================================================================
                                                   VERTEXMOTIONATLASEXPORTER.CPP
============================================================================================================================================*/
// 🧩 Phase D05 Zero-dependency Unreal/Unity VAT interchange exporter (quantized RGBA8 PNG + displacement bounds JSON)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasExporter.h"
#include "Engine/Codecs/LosslessPngEncoder.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Frontier::GeometrySpace::Deformation
{

namespace
{




} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                VERTEX MOTION ATLAS EXPORTER
//------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> VertexMotionAtlasExporter::QuantizeToRgba8(
    const VertexMotionAtlas& atlas,
    const HighPolySurfaceRest& restSurface)
{
    const uint32_t W = atlas.AtlasWidth;
    const uint32_t H = atlas.AtlasHeight;
    std::vector<uint8_t> pixels(static_cast<size_t>(W) * H * 4u, 0u);
    if (!atlas.Ready())
    {
        return pixels;
    }

    const float rx = std::max(1.0e-4f, atlas.DisplacementMax[0] - atlas.DisplacementMin[0]);
    const float ry = std::max(1.0e-4f, atlas.DisplacementMax[1] - atlas.DisplacementMin[1]);
    const float rz = std::max(1.0e-4f, atlas.DisplacementMax[2] - atlas.DisplacementMin[2]);

    const auto toByte = [](float norm) -> uint8_t
    {
        return static_cast<uint8_t>(std::clamp(std::lround(norm * 255.0f), 0l, 255l));
    };

    const float* rPos = restSurface.RestPositions.data();
    for (uint32_t f = 0u; f < atlas.SliceCount; ++f)
    {
        for (uint32_t vi = 0u; vi < atlas.VertexCount; ++vi)
        {
            const size_t px = atlas.TexelOffset(vi, f);
            const size_t v3 = static_cast<size_t>(vi) * 3u;
            pixels[px + 0u] = toByte((atlas.PositionStrainRgba[px + 0u] - rPos[v3 + 0u] - atlas.DisplacementMin[0]) / rx);
            pixels[px + 1u] = toByte((atlas.PositionStrainRgba[px + 1u] - rPos[v3 + 1u] - atlas.DisplacementMin[1]) / ry);
            pixels[px + 2u] = toByte((atlas.PositionStrainRgba[px + 2u] - rPos[v3 + 2u] - atlas.DisplacementMin[2]) / rz);
            pixels[px + 3u] = toByte(std::min(1.0f, atlas.PositionStrainRgba[px + 3u] * 0.25f));
        }
    }
    return pixels;
}

std::vector<std::byte> VertexMotionAtlasExporter::EncodeRgba8Png(
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& rgbaPixels)
{
    std::vector<std::byte> out;
    (void)Frontier::Codecs::EncodeLosslessRgba8Png(width, height, rgbaPixels, out);
    return out;
}

std::string VertexMotionAtlasExporter::FormatBoundsJson(const VertexMotionAtlas& atlas)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << "{\n"
       << "  \"format\": \"Frontier-VertexAtlas-RGBA8\",\n"
       << "  \"solver\": \"" << atlas.SolverLabel << "\",\n"
       << "  \"vertexCount\": " << atlas.VertexCount << ",\n"
       << "  \"triangleCount\": " << atlas.TriangleCount << ",\n"
       << "  \"sliceCount\": " << atlas.SliceCount << ",\n"
       << "  \"slicesPerSecond\": " << atlas.SlicesPerSecond << ",\n"
       << "  \"texWidth\": " << atlas.AtlasWidth << ",\n"
       << "  \"texHeight\": " << atlas.AtlasHeight << ",\n"
       << "  \"rowsPerSlice\": " << atlas.RowsPerSlice << ",\n"
       << "  \"displacementBoundsMin\": [" << atlas.DisplacementMin[0] << ", " << atlas.DisplacementMin[1] << ", " << atlas.DisplacementMin[2] << "],\n"
       << "  \"displacementBoundsMax\": [" << atlas.DisplacementMax[0] << ", " << atlas.DisplacementMax[1] << ", " << atlas.DisplacementMax[2] << "],\n"
       << "  \"packing\": \"col = vIdx & 255; row = (vIdx >> 8) + slice * rowsPerSlice; rgb = normalized (pos - restPos); a = strain/4\"\n"
       << "}\n";
    return ss.str();
}

bool VertexMotionAtlasExporter::ExportToFiles(
    const std::filesystem::path& pngPath,
    const std::filesystem::path& jsonPath,
    const VertexMotionAtlas& atlas,
    const HighPolySurfaceRest& restSurface)
{
    if (!atlas.Ready())
    {
        return false;
    }
    const std::vector<uint8_t> pixels = QuantizeToRgba8(atlas, restSurface);
    const std::vector<std::byte> pngBytes = EncodeRgba8Png(atlas.AtlasWidth, atlas.AtlasHeight, pixels);
    if (pngBytes.empty())
    {
        return false;
    }

    std::ofstream pngFile(pngPath, std::ios::binary);
    if (!pngFile)
    {
        return false;
    }
    pngFile.write(reinterpret_cast<const char*>(pngBytes.data()), static_cast<std::streamsize>(pngBytes.size()));

    std::ofstream jsonFile(jsonPath, std::ios::binary);
    if (!jsonFile)
    {
        return false;
    }
    const std::string json = FormatBoundsJson(atlas);
    jsonFile.write(json.data(), static_cast<std::streamsize>(json.size()));
    return pngFile.good() && jsonFile.good();
}

} // namespace Frontier::GeometrySpace::Deformation
