/*============================================================================================================================================
                                                        CONTENTHASHCALCULATOR.H
============================================================================================================================================*/
// 🧩 Deterministic cross-platform xxHash64, FIPS 180-4 SHA-256, and FNV-1a 64 hashing primitives for DependencyOrder and container integrity

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/EngineConstants.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct Sha256Hash
{
    std::array<uint8_t, 32u> Octets {}; // [B] - 256-bit cryptographic hash bytes

    [[nodiscard]] bool operator==(const Sha256Hash& Other) const noexcept = default;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr uint64_t MixFnv1a64Byte(uint64_t RunningHash, uint8_t Octet) noexcept
{
    return (RunningHash ^ static_cast<uint64_t>(Octet)) * Constants::Fnv1a64Prime;
}

[[nodiscard]] constexpr uint64_t MixFnv1a64Word(uint64_t RunningHash, uint64_t Word) noexcept
{
    return (RunningHash ^ Word) * Constants::Fnv1a64Prime;
}

[[nodiscard]] inline uint64_t MixFnv1a64FloatBits(uint64_t RunningHash, float Sample) noexcept
{
    return MixFnv1a64Word(RunningHash, static_cast<uint64_t>(std::bit_cast<uint32_t>(Sample)));
}

[[nodiscard]] inline uint64_t MixFnv1a64Bytes(uint64_t RunningHash, const void* SourceBytes, std::size_t ByteCount) noexcept
{
    const auto* Octets = static_cast<const uint8_t*>(SourceBytes);
    for (std::size_t Index = 0u; Index < ByteCount; ++Index)
    {
        RunningHash = MixFnv1a64Byte(RunningHash, Octets[Index]);
    }
    return RunningHash;
}

[[nodiscard]] inline uint64_t ComputeFnv1a64Bytes(
    const void* SourceBytes,
    std::size_t ByteCount,
    uint64_t    Seed = Constants::Fnv1a64OffsetBasis) noexcept
{
    return MixFnv1a64Bytes(Seed, SourceBytes, ByteCount);
}

[[nodiscard]] constexpr uint64_t ComputeFnv1a64String(
    std::string_view Text,
    uint64_t         Seed = Constants::Fnv1a64OffsetBasis) noexcept
{
    uint64_t RunningHash = Seed;
    for (const unsigned char Character : Text)
    {
        RunningHash = MixFnv1a64Byte(RunningHash, static_cast<uint8_t>(Character));
    }
    return RunningHash;
}

[[nodiscard]] uint64_t ComputeFnv1a64(std::span<const std::byte> InputBytes) noexcept;

[[nodiscard]] uint64_t ComputeXxHash64(std::span<const std::byte> InputBytes, uint64_t Seed = 0u) noexcept;

[[nodiscard]] uint64_t ComputeXxHash64String(std::string_view Text, uint64_t Seed = 0u) noexcept;

[[nodiscard]] Sha256Hash ComputeSha256(std::span<const std::byte> InputBytes) noexcept;

[[nodiscard]] Sha256Hash ComputeSha256String(std::string_view Text) noexcept;

[[nodiscard]] std::string FormatSha256Hex(const Sha256Hash& Hash) noexcept;

[[nodiscard]] std::string FormatHex64(uint64_t Checksum64) noexcept;

[[nodiscard]] bool ParseHex64(std::string_view HexText, uint64_t& OutChecksum64) noexcept;

} // namespace Frontier::Serialization
