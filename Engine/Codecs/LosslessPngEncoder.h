/*============================================================================================================================================
                                                            LOSSLESSPNGENCODER.H
============================================================================================================================================*/
// 🧩 Canonical zero-dependency RFC 1950 / ISO 15948 lossless RGBA8 PNG encoder, CRC-32, and Adler-32 checksum primitives

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint32_t ComputeIsoCrc32(const uint8_t* SourceBytes, std::size_t ByteCount) noexcept;

[[nodiscard]] uint32_t ComputeZlibAdler32(const uint8_t* SourceBytes, std::size_t ByteCount) noexcept;

void AppendPngChunk(
    std::vector<uint8_t>&    Destination,
    const char               ChunkTag[4],
    std::span<const uint8_t> ChunkPayload);

void AppendPngChunk(
    std::vector<std::byte>&  Destination,
    const char               ChunkTag[4],
    std::span<const uint8_t> ChunkPayload);

[[nodiscard]] bool EncodeLosslessRgba8Png(
    uint32_t                 WidthPx,
    uint32_t                 HeightPx,
    std::span<const uint8_t> RgbaPixels,
    std::vector<uint8_t>&    OutPngBytes);

[[nodiscard]] bool EncodeLosslessRgba8Png(
    uint32_t                 WidthPx,
    uint32_t                 HeightPx,
    std::span<const uint8_t> RgbaPixels,
    std::vector<std::byte>&  OutPngBytes);

[[nodiscard]] bool WriteLosslessRgba8PngFile(
    const std::filesystem::path& TargetPath,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    std::span<const uint8_t>     RgbaPixels);

} // namespace Frontier::Codecs
