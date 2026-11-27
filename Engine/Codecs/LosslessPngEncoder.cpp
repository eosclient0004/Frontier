/*============================================================================================================================================
                                                           LOSSLESSPNGENCODER.CPP
============================================================================================================================================*/
// 🧩 Implementation of canonical zero-dependency lossless RGBA8 PNG encoding (CRC-32, Adler-32, stored DEFLATE blocks)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/LosslessPngEncoder.h"
#include "Engine/Serialization/Container/BinaryFormatPrimitives.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace Frontier::Codecs
{

namespace
{

constexpr std::array<uint8_t, 8u> PngSignature = {
    0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  CHECKSUM PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

uint32_t ComputeIsoCrc32(const uint8_t* SourceBytes, std::size_t ByteCount) noexcept
{
    uint32_t Crc = 0xFFFFFFFFu;
    for (std::size_t Index = 0u; Index < ByteCount; ++Index)
    {
        Crc ^= SourceBytes[Index];
        for (int Bit = 0; Bit < 8; ++Bit)
        {
            Crc = (Crc & 1u) != 0u ? ((Crc >> 1u) ^ 0xEDB88320u) : (Crc >> 1u);
        }
    }
    return ~Crc;
}

uint32_t ComputeZlibAdler32(const uint8_t* SourceBytes, std::size_t ByteCount) noexcept
{
    uint32_t SumA = 1u;
    uint32_t SumB = 0u;
    for (std::size_t Index = 0u; Index < ByteCount; ++Index)
    {
        SumA = (SumA + SourceBytes[Index]) % 65521u;
        SumB = (SumB + SumA) % 65521u;
    }
    return (SumB << 16u) | SumA;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PNG CHUNK ENCODING
//------------------------------------------------------------------------------------------------------------------------

void AppendPngChunk(
    std::vector<uint8_t>&    Destination,
    const char               ChunkTag[4],
    std::span<const uint8_t> ChunkPayload)
{
    Serialization::AppendBigEndian32(Destination, static_cast<uint32_t>(ChunkPayload.size()));
    const std::size_t TagOffset = Destination.size();
    Destination.insert(Destination.end(), reinterpret_cast<const uint8_t*>(ChunkTag), reinterpret_cast<const uint8_t*>(ChunkTag) + 4u);
    Destination.insert(Destination.end(), ChunkPayload.begin(), ChunkPayload.end());
    const uint32_t Crc = ComputeIsoCrc32(Destination.data() + TagOffset, 4u + ChunkPayload.size());
    Serialization::AppendBigEndian32(Destination, Crc);
}

void AppendPngChunk(
    std::vector<std::byte>&  Destination,
    const char               ChunkTag[4],
    std::span<const uint8_t> ChunkPayload)
{
    Serialization::AppendBigEndian32(Destination, static_cast<uint32_t>(ChunkPayload.size()));
    const std::size_t TagOffset = Destination.size();
    for (std::size_t Index = 0u; Index < 4u; ++Index)
    {
        Destination.push_back(static_cast<std::byte>(ChunkTag[Index]));
    }
    for (const uint8_t Octet : ChunkPayload)
    {
        Destination.push_back(static_cast<std::byte>(Octet));
    }
    const uint32_t Crc = ComputeIsoCrc32(
        reinterpret_cast<const uint8_t*>(Destination.data() + TagOffset),
        4u + ChunkPayload.size());
    Serialization::AppendBigEndian32(Destination, Crc);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 LOSSLESS RGBA8 PNG WRITER
//------------------------------------------------------------------------------------------------------------------------

bool EncodeLosslessRgba8Png(
    uint32_t                 WidthPx,
    uint32_t                 HeightPx,
    std::span<const uint8_t> RgbaPixels,
    std::vector<uint8_t>&    OutPngBytes)
{
    OutPngBytes.clear();
    const std::size_t ExpectedBytes = static_cast<std::size_t>(WidthPx) * static_cast<std::size_t>(HeightPx) * 4u;
    if (WidthPx == 0u || HeightPx == 0u || RgbaPixels.size() < ExpectedBytes)
    {
        return false;
    }

    const std::size_t RowBytes = static_cast<std::size_t>(WidthPx) * 4u;
    std::vector<uint8_t> FilteredRows;
    FilteredRows.reserve(static_cast<std::size_t>(HeightPx) * (RowBytes + 1u));
    for (uint32_t Row = 0u; Row < HeightPx; ++Row)
    {
        FilteredRows.push_back(0u); // Filter type 0 (None)
        const uint8_t* RowStart = RgbaPixels.data() + static_cast<std::size_t>(Row) * RowBytes;
        FilteredRows.insert(FilteredRows.end(), RowStart, RowStart + RowBytes);
    }

    std::vector<uint8_t> ZlibPayload;
    ZlibPayload.reserve(FilteredRows.size() + (FilteredRows.size() / 65535u + 2u) * 5u + 8u);
    ZlibPayload.push_back(0x78u);
    ZlibPayload.push_back(0x01u);

    std::size_t Cursor = 0u;
    while (Cursor < FilteredRows.size())
    {
        const std::size_t BlockLength = std::min<std::size_t>(65535u, FilteredRows.size() - Cursor);
        const bool        FinalBlock  = (Cursor + BlockLength) >= FilteredRows.size();
        const uint16_t    Len16       = static_cast<uint16_t>(BlockLength);
        const uint16_t    Nlen16      = static_cast<uint16_t>(~Len16);
        ZlibPayload.push_back(FinalBlock ? 0x01u : 0x00u);
        ZlibPayload.push_back(static_cast<uint8_t>(Len16 & 0xFFu));
        ZlibPayload.push_back(static_cast<uint8_t>((Len16 >> 8u) & 0xFFu));
        ZlibPayload.push_back(static_cast<uint8_t>(Nlen16 & 0xFFu));
        ZlibPayload.push_back(static_cast<uint8_t>((Nlen16 >> 8u) & 0xFFu));
        ZlibPayload.insert(ZlibPayload.end(), FilteredRows.begin() + static_cast<std::ptrdiff_t>(Cursor),
                           FilteredRows.begin() + static_cast<std::ptrdiff_t>(Cursor + BlockLength));
        Cursor += BlockLength;
    }

    Serialization::AppendBigEndian32(ZlibPayload, ComputeZlibAdler32(FilteredRows.data(), FilteredRows.size()));

    OutPngBytes.reserve(ZlibPayload.size() + 128u);
    OutPngBytes.insert(OutPngBytes.end(), PngSignature.begin(), PngSignature.end());

    std::vector<uint8_t> Ihdr;
    Ihdr.reserve(13u);
    Serialization::AppendBigEndian32(Ihdr, WidthPx);
    Serialization::AppendBigEndian32(Ihdr, HeightPx);
    Ihdr.push_back(8u); // 8-bit depth
    Ihdr.push_back(6u); // RGBA colour type
    Ihdr.push_back(0u); // Deflate compression
    Ihdr.push_back(0u); // Adaptive filter method 0
    Ihdr.push_back(0u); // Non-interlaced
    AppendPngChunk(OutPngBytes, "IHDR", Ihdr);
    AppendPngChunk(OutPngBytes, "IDAT", ZlibPayload);
    AppendPngChunk(OutPngBytes, "IEND", {});
    return true;
}

bool EncodeLosslessRgba8Png(
    uint32_t                 WidthPx,
    uint32_t                 HeightPx,
    std::span<const uint8_t> RgbaPixels,
    std::vector<std::byte>&  OutPngBytes)
{
    std::vector<uint8_t> Encoded;
    if (!EncodeLosslessRgba8Png(WidthPx, HeightPx, RgbaPixels, Encoded))
    {
        OutPngBytes.clear();
        return false;
    }
    OutPngBytes.resize(Encoded.size());
    std::memcpy(OutPngBytes.data(), Encoded.data(), Encoded.size());
    return true;
}

bool WriteLosslessRgba8PngFile(
    const std::filesystem::path& TargetPath,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    std::span<const uint8_t>     RgbaPixels)
{
    std::vector<uint8_t> PngBytes;
    if (!EncodeLosslessRgba8Png(WidthPx, HeightPx, RgbaPixels, PngBytes))
    {
        return false;
    }
    std::error_code FsErr;
    if (TargetPath.has_parent_path())
    {
        std::filesystem::create_directories(TargetPath.parent_path(), FsErr);
    }
    std::ofstream Stream(TargetPath, std::ios::binary | std::ios::trunc);
    if (!Stream.is_open())
    {
        return false;
    }
    Stream.write(reinterpret_cast<const char*>(PngBytes.data()), static_cast<std::streamsize>(PngBytes.size()));
    return Stream.good();
}

} // namespace Frontier::Codecs
