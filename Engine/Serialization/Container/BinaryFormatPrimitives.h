/*============================================================================================================================================
                                                          BINARYFORMATPRIMITIVES.H
============================================================================================================================================*/
// 🧩 Canonical POD and fixed-endian binary read/write primitives shared across codecs, containers, and geometry exporters

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 POD BUFFER READ/WRITE PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

template <typename PodType>
    requires std::is_trivially_copyable_v<PodType>
inline void AppendPodBytes(std::vector<uint8_t>& Destination, const PodType& Source)
{
    const std::size_t WriteOffset = Destination.size();
    Destination.resize(WriteOffset + sizeof(PodType));
    std::memcpy(Destination.data() + WriteOffset, &Source, sizeof(PodType));
}

template <typename PodType>
    requires std::is_trivially_copyable_v<PodType>
inline void AppendPodBytes(std::vector<std::byte>& Destination, const PodType& Source)
{
    const std::size_t WriteOffset = Destination.size();
    Destination.resize(WriteOffset + sizeof(PodType));
    std::memcpy(Destination.data() + WriteOffset, &Source, sizeof(PodType));
}

template <typename PodType>
    requires std::is_trivially_copyable_v<PodType>
[[nodiscard]] inline bool ReadPodBytes(
    std::span<const uint8_t> SourceBytes,
    std::size_t&             Cursor,
    PodType&                 OutTarget) noexcept
{
    if (Cursor + sizeof(PodType) > SourceBytes.size())
    {
        return false;
    }
    std::memcpy(&OutTarget, SourceBytes.data() + Cursor, sizeof(PodType));
    Cursor += sizeof(PodType);
    return true;
}

template <typename PodType>
    requires std::is_trivially_copyable_v<PodType>
[[nodiscard]] inline bool ReadPodBytes(
    std::span<const std::byte> SourceBytes,
    std::size_t&               Cursor,
    PodType&                   OutTarget) noexcept
{
    if (Cursor + sizeof(PodType) > SourceBytes.size())
    {
        return false;
    }
    std::memcpy(&OutTarget, SourceBytes.data() + Cursor, sizeof(PodType));
    Cursor += sizeof(PodType);
    return true;
}

template <typename PodType>
    requires std::is_trivially_copyable_v<PodType>
[[nodiscard]] inline bool ReadPodBytes(
    std::span<const std::byte>& Cursor,
    PodType&                    OutTarget) noexcept
{
    if (Cursor.size() < sizeof(PodType))
    {
        return false;
    }
    std::memcpy(&OutTarget, Cursor.data(), sizeof(PodType));
    Cursor = Cursor.subspan(sizeof(PodType));
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 FIXED-ENDIAN WORD PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

inline void AppendBigEndian32(std::vector<uint8_t>& Destination, uint32_t Word)
{
    Destination.push_back(static_cast<uint8_t>((Word >> 24u) & 0xFFu));
    Destination.push_back(static_cast<uint8_t>((Word >> 16u) & 0xFFu));
    Destination.push_back(static_cast<uint8_t>((Word >> 8u) & 0xFFu));
    Destination.push_back(static_cast<uint8_t>(Word & 0xFFu));
}

inline void AppendBigEndian32(std::vector<std::byte>& Destination, uint32_t Word)
{
    Destination.push_back(static_cast<std::byte>((Word >> 24u) & 0xFFu));
    Destination.push_back(static_cast<std::byte>((Word >> 16u) & 0xFFu));
    Destination.push_back(static_cast<std::byte>((Word >> 8u) & 0xFFu));
    Destination.push_back(static_cast<std::byte>(Word & 0xFFu));
}

inline void AppendLittleEndian32(std::vector<uint8_t>& Destination, uint32_t Word)
{
    Destination.push_back(static_cast<uint8_t>(Word & 0xFFu));
    Destination.push_back(static_cast<uint8_t>((Word >> 8u) & 0xFFu));
    Destination.push_back(static_cast<uint8_t>((Word >> 16u) & 0xFFu));
    Destination.push_back(static_cast<uint8_t>((Word >> 24u) & 0xFFu));
}

inline void AppendLittleEndian32(std::vector<std::byte>& Destination, uint32_t Word)
{
    Destination.push_back(static_cast<std::byte>(Word & 0xFFu));
    Destination.push_back(static_cast<std::byte>((Word >> 8u) & 0xFFu));
    Destination.push_back(static_cast<std::byte>((Word >> 16u) & 0xFFu));
    Destination.push_back(static_cast<std::byte>((Word >> 24u) & 0xFFu));
}

inline void WriteLittleEndian32(std::byte* Destination, uint32_t Word) noexcept
{
    Destination[0u] = static_cast<std::byte>(Word & 0xFFu);
    Destination[1u] = static_cast<std::byte>((Word >> 8u) & 0xFFu);
    Destination[2u] = static_cast<std::byte>((Word >> 16u) & 0xFFu);
    Destination[3u] = static_cast<std::byte>((Word >> 24u) & 0xFFu);
}

[[nodiscard]] constexpr uint32_t ReadLittleEndian32(const uint8_t* SourceBytes) noexcept
{
    return static_cast<uint32_t>(SourceBytes[0u]) |
           (static_cast<uint32_t>(SourceBytes[1u]) << 8u) |
           (static_cast<uint32_t>(SourceBytes[2u]) << 16u) |
           (static_cast<uint32_t>(SourceBytes[3u]) << 24u);
}

[[nodiscard]] constexpr uint32_t ReadLittleEndian32(const std::byte* SourceBytes) noexcept
{
    return static_cast<uint32_t>(std::to_integer<uint8_t>(SourceBytes[0u])) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(SourceBytes[1u])) << 8u) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(SourceBytes[2u])) << 16u) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(SourceBytes[3u])) << 24u);
}

[[nodiscard]] inline float ReadLittleEndianFloat32(const uint8_t* SourceBytes) noexcept
{
    return std::bit_cast<float>(ReadLittleEndian32(SourceBytes));
}

} // namespace Frontier::Serialization
