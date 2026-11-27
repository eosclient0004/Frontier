/*============================================================================================================================================
                                                         GAMEPLAYRULEPOLICY.CPP
============================================================================================================================================*/
// 🧩 Implementation of GameplayRulePolicy deterministic world alignment hash, directive validation, wire codec, and DocumentContainer codec

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Policies/GameplayRulePolicy.h"

#include "Engine/Serialization/DependencyOrder/ContentHashCalculator.h"
#include "Engine/Constants/EngineConstants.h"
#include "Engine/Serialization/Container/BinaryFormatPrimitives.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <toml++/toml.hpp>

namespace Frontier::SimulationSpace::Policies
{

namespace
{

constexpr uint8_t WireMagic[4] = { 'F', 'G', 'R', 'P' };



void WriteLe64(std::byte* Dest, uint64_t Value) noexcept
{
    for (std::size_t ByteIdx = 0u; ByteIdx < 8u; ++ByteIdx)
    {
        Dest[ByteIdx] = static_cast<std::byte>((Value >> (ByteIdx * 8u)) & 0xFFull);
    }
}

[[nodiscard]] uint64_t ReadLe64(const std::byte* Src) noexcept
{
    uint64_t Value = 0ull;
    for (std::size_t ByteIdx = 0u; ByteIdx < 8u; ++ByteIdx)
    {
        Value |= (static_cast<uint64_t>(Src[ByteIdx]) << (ByteIdx * 8u));
    }
    return Value;
}

[[nodiscard]] uint32_t FloatToBits(float Value) noexcept
{
    uint32_t Bits = 0u;
    std::memcpy(&Bits, &Value, sizeof(uint32_t));
    return Bits;
}

[[nodiscard]] float BitsToFloat(uint32_t Bits) noexcept
{
    float Value = 0.0f;
    std::memcpy(&Value, &Bits, sizeof(float));
    return Value;
}

[[nodiscard]] int64_t QuantizeMillimeters(double Meters) noexcept
{
    if (!std::isfinite(Meters))
    {
        return 0;
    }
    return static_cast<int64_t>(std::llround(Meters * 1000.0));
}

[[nodiscard]] int32_t QuantizeMilliUnits(float Value) noexcept
{
    if (!std::isfinite(Value))
    {
        return 0;
    }
    return static_cast<int32_t>(std::lround(static_cast<double>(Value) * 1000.0));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             GAMEPLAY RULE POLICY ENGINE
//------------------------------------------------------------------------------------------------------------------------

void GameplayRulePolicyEngine::ClampToSafeBounds(GameplayRulePolicy& Policy) noexcept
{
    Policy.SchemaVersion              = std::max(1u, Policy.SchemaVersion);
    Policy.FixedSimulationHz          = std::clamp(Policy.FixedSimulationHz, 15u, 480u);
    Policy.AlignmentHashIntervalTicks = std::clamp(Policy.AlignmentHashIntervalTicks, 1u, 3600u);
    Policy.SpeedLimitKmh              = std::isfinite(Policy.SpeedLimitKmh)
                                            ? std::clamp(Policy.SpeedLimitKmh, 10.0f, 500.0f)
                                            : 120.0f;
    Policy.RedSignalPenaltyScore      = std::isfinite(Policy.RedSignalPenaltyScore)
                                            ? std::clamp(Policy.RedSignalPenaltyScore, 0.0f, 10000.0f)
                                            : 50.0f;
    Policy.CollisionImpulseThresholdNs = std::isfinite(Policy.CollisionImpulseThresholdNs)
                                             ? std::clamp(Policy.CollisionImpulseThresholdNs, 100.0f, 1000000.0f)
                                             : 12000.0f;
    Policy.PursuitEscalationStages    = std::clamp(Policy.PursuitEscalationStages, 1u, 10u);
    Policy.MaximumSteerMagnitude      = std::isfinite(Policy.MaximumSteerMagnitude)
                                            ? std::clamp(Policy.MaximumSteerMagnitude, 0.05f, 1.0f)
                                            : 1.0f;
    Policy.MaximumDirectiveRateHz     = std::clamp(Policy.MaximumDirectiveRateHz, 15u, 480u);
}

uint64_t GameplayRulePolicyEngine::ComputeWorldAlignmentHash(
    uint64_t                                   TickOrdinal,
    std::span<const WorldKinematicsDigestEntry> Entries) noexcept
{
    // Deterministic FNV-1a 64 over tick ordinal + millimeter-quantized kinematics
    uint64_t Digest = Frontier::Constants::Fnv1a64OffsetBasis;
    auto MixBytes = [&Digest](const void* Ptr, std::size_t ByteCount) noexcept
    {
        const auto* Bytes = static_cast<const uint8_t*>(Ptr);
        for (std::size_t Idx = 0u; Idx < ByteCount; ++Idx)
        {
            Digest ^= static_cast<uint64_t>(Bytes[Idx]);
            Digest *= Frontier::Constants::Fnv1a64Prime;
        }
    };

    std::byte TickBuf[8]{};
    WriteLe64(TickBuf, TickOrdinal);
    MixBytes(TickBuf, sizeof(TickBuf));

    for (const WorldKinematicsDigestEntry& Entry : Entries)
    {
        std::byte ActorBuf[44]{};
        Frontier::Serialization::WriteLittleEndian32(ActorBuf + 0u,  Entry.ActorIdentifier);
        WriteLe64(ActorBuf + 4u,  static_cast<uint64_t>(QuantizeMillimeters(Entry.PositionMetersX)));
        WriteLe64(ActorBuf + 12u, static_cast<uint64_t>(QuantizeMillimeters(Entry.PositionMetersY)));
        WriteLe64(ActorBuf + 20u, static_cast<uint64_t>(QuantizeMillimeters(Entry.PositionMetersZ)));
        Frontier::Serialization::WriteLittleEndian32(ActorBuf + 28u, static_cast<uint32_t>(QuantizeMilliUnits(Entry.VelocityMetersSecX)));
        Frontier::Serialization::WriteLittleEndian32(ActorBuf + 32u, static_cast<uint32_t>(QuantizeMilliUnits(Entry.VelocityMetersSecY)));
        Frontier::Serialization::WriteLittleEndian32(ActorBuf + 36u, static_cast<uint32_t>(QuantizeMilliUnits(Entry.VelocityMetersSecZ)));
        Frontier::Serialization::WriteLittleEndian32(ActorBuf + 40u, static_cast<uint32_t>(QuantizeMilliUnits(Entry.YawRadians)));
        MixBytes(ActorBuf, sizeof(ActorBuf));
    }

    return Digest;
}

bool GameplayRulePolicyEngine::VerifyWorldAlignmentHash(
    uint64_t                                   TickOrdinal,
    std::span<const WorldKinematicsDigestEntry> Entries,
    uint64_t                                   ExpectedDigest) noexcept
{
    return ComputeWorldAlignmentHash(TickOrdinal, Entries) == ExpectedDigest;
}

DirectiveValidationOutcome GameplayRulePolicyEngine::ValidateOwnerDirective(
    const GameplayRulePolicy&    Policy,
    const OwnerControlDirective& Directive,
    uint32_t                     AuthoritativeOwnerEndpoint,
    uint32_t                     ObservedClientRateHz) noexcept
{
    if (!std::isfinite(Directive.Steer) ||
        !std::isfinite(Directive.Throttle) ||
        !std::isfinite(Directive.Brake))
    {
        return DirectiveValidationOutcome::RejectedNonFiniteValue;
    }

    if (Directive.SenderEndpointIndex != AuthoritativeOwnerEndpoint)
    {
        return DirectiveValidationOutcome::RejectedOwnerMismatch;
    }

    if (std::fabs(Directive.Steer) > Policy.MaximumSteerMagnitude + 1e-4f)
    {
        return DirectiveValidationOutcome::RejectedSteerOutOfBounds;
    }

    if (Directive.Throttle < -1e-4f || Directive.Throttle > 1.0f + 1e-4f)
    {
        return DirectiveValidationOutcome::RejectedThrottleBounds;
    }

    if (Directive.Brake < -1e-4f || Directive.Brake > 1.0f + 1e-4f)
    {
        return DirectiveValidationOutcome::RejectedBrakeBounds;
    }

    if (ObservedClientRateHz > Policy.MaximumDirectiveRateHz)
    {
        return DirectiveValidationOutcome::RejectedRateLimitExceeded;
    }

    return DirectiveValidationOutcome::Accepted;
}

bool GameplayRulePolicyEngine::EncodePolicyWireBytes(
    const GameplayRulePolicy&                                    Policy,
    std::array<std::byte, GameplayRulePolicy::WirePayloadBytes>& OutWireBytes) noexcept
{
    GameplayRulePolicy Clamped = Policy;
    ClampToSafeBounds(Clamped);

    OutWireBytes.fill(std::byte{ 0 });
    for (std::size_t Idx = 0u; Idx < 4u; ++Idx)
    {
        OutWireBytes[Idx] = static_cast<std::byte>(WireMagic[Idx]);
    }

    Frontier::Serialization::WriteLittleEndian32(OutWireBytes.data() + 4u,  Clamped.SchemaVersion);
    Frontier::Serialization::WriteLittleEndian32(OutWireBytes.data() + 8u,  Clamped.FixedSimulationHz);
    Frontier::Serialization::WriteLittleEndian32(OutWireBytes.data() + 12u, Clamped.AlignmentHashIntervalTicks);
    Frontier::Serialization::WriteLittleEndian32(OutWireBytes.data() + 16u, FloatToBits(Clamped.SpeedLimitKmh));
    Frontier::Serialization::WriteLittleEndian32(OutWireBytes.data() + 20u, FloatToBits(Clamped.RedSignalPenaltyScore));
    Frontier::Serialization::WriteLittleEndian32(OutWireBytes.data() + 24u, FloatToBits(Clamped.CollisionImpulseThresholdNs));
    Frontier::Serialization::WriteLittleEndian32(OutWireBytes.data() + 28u, Clamped.PursuitEscalationStages);
    Frontier::Serialization::WriteLittleEndian32(OutWireBytes.data() + 32u, FloatToBits(Clamped.MaximumSteerMagnitude));

    const uint32_t PackedTail = (Clamped.MaximumDirectiveRateHz & 0xFFFFu) |
                                (Clamped.JoinInProgressAllowed ? (1u << 16u) : 0u) |
                                (Clamped.HostMigrationEnabled ? (1u << 17u) : 0u);
    Frontier::Serialization::WriteLittleEndian32(OutWireBytes.data() + 36u, PackedTail);

    const uint64_t Checksum = Serialization::ComputeFnv1a64(
        std::span<const std::byte>(OutWireBytes.data(), 40u));
    WriteLe64(OutWireBytes.data() + 40u, Checksum);
    return true;
}

bool GameplayRulePolicyEngine::DecodePolicyWireBytes(
    std::span<const std::byte> WireBytes,
    GameplayRulePolicy&        OutPolicy,
    std::string&               OutDiagnostic)
{
    if (WireBytes.size() < GameplayRulePolicy::WirePayloadBytes)
    {
        OutDiagnostic = "GameplayRulePolicy wire payload truncated (< 48 bytes).";
        return false;
    }

    for (std::size_t Idx = 0u; Idx < 4u; ++Idx)
    {
        if (WireBytes[Idx] != static_cast<std::byte>(WireMagic[Idx]))
        {
            OutDiagnostic = "GameplayRulePolicy wire magic mismatch (expected 'FGRP').";
            return false;
        }
    }

    const uint64_t ExpectedChecksum = ReadLe64(WireBytes.data() + 40u);
    const uint64_t ComputedChecksum = Serialization::ComputeFnv1a64(
        WireBytes.subspan(0u, 40u));
    if (ExpectedChecksum != ComputedChecksum)
    {
        OutDiagnostic = "GameplayRulePolicy wire integrity checksum mismatch.";
        return false;
    }

    OutPolicy                             = {};
    OutPolicy.SchemaVersion               = Frontier::Serialization::ReadLittleEndian32(WireBytes.data() + 4u);
    OutPolicy.FixedSimulationHz           = Frontier::Serialization::ReadLittleEndian32(WireBytes.data() + 8u);
    OutPolicy.AlignmentHashIntervalTicks  = Frontier::Serialization::ReadLittleEndian32(WireBytes.data() + 12u);
    OutPolicy.SpeedLimitKmh               = BitsToFloat(Frontier::Serialization::ReadLittleEndian32(WireBytes.data() + 16u));
    OutPolicy.RedSignalPenaltyScore       = BitsToFloat(Frontier::Serialization::ReadLittleEndian32(WireBytes.data() + 20u));
    OutPolicy.CollisionImpulseThresholdNs = BitsToFloat(Frontier::Serialization::ReadLittleEndian32(WireBytes.data() + 24u));
    OutPolicy.PursuitEscalationStages     = Frontier::Serialization::ReadLittleEndian32(WireBytes.data() + 28u);
    OutPolicy.MaximumSteerMagnitude       = BitsToFloat(Frontier::Serialization::ReadLittleEndian32(WireBytes.data() + 32u));

    const uint32_t PackedTail        = Frontier::Serialization::ReadLittleEndian32(WireBytes.data() + 36u);
    OutPolicy.MaximumDirectiveRateHz = PackedTail & 0xFFFFu;
    OutPolicy.JoinInProgressAllowed  = (PackedTail & (1u << 16u)) != 0u;
    OutPolicy.HostMigrationEnabled   = (PackedTail & (1u << 17u)) != 0u;

    ClampToSafeBounds(OutPolicy);
    OutDiagnostic.clear();
    return true;
}

bool GameplayRulePolicyEngine::EncodeToDocument(
    const GameplayRulePolicy&         Policy,
    Serialization::DocumentContainer& OutDocument)
{
    GameplayRulePolicy Clamped = Policy;
    ClampToSafeBounds(Clamped);

    OutDocument                    = {};
    OutDocument.SuffixCategory     = Serialization::FormatSuffixCategory::BehaviorSpace;
    OutDocument.DocumentTitle      = "GameplayRulePolicy";
    OutDocument.AuthoringWorkspace = "Frontier.SimulationSpace";
    OutDocument.StableIdentifier   = "0193b5c0-7a1e-7000-8000-000000000302";
    OutDocument.SchemaVersion      = Clamped.SchemaVersion;

    toml::table Section{};
    Section.insert("SchemaVersion",               static_cast<int64_t>(Clamped.SchemaVersion));
    Section.insert("FixedSimulationHz",           static_cast<int64_t>(Clamped.FixedSimulationHz));
    Section.insert("AlignmentHashIntervalTicks",  static_cast<int64_t>(Clamped.AlignmentHashIntervalTicks));
    Section.insert("SpeedLimitKmh",               static_cast<double>(Clamped.SpeedLimitKmh));
    Section.insert("RedSignalPenaltyScore",       static_cast<double>(Clamped.RedSignalPenaltyScore));
    Section.insert("CollisionImpulseThresholdNs", static_cast<double>(Clamped.CollisionImpulseThresholdNs));
    Section.insert("PursuitEscalationStages",     static_cast<int64_t>(Clamped.PursuitEscalationStages));
    Section.insert("MaximumSteerMagnitude",       static_cast<double>(Clamped.MaximumSteerMagnitude));
    Section.insert("MaximumDirectiveRateHz",      static_cast<int64_t>(Clamped.MaximumDirectiveRateHz));
    Section.insert("JoinInProgressAllowed",       Clamped.JoinInProgressAllowed);
    Section.insert("HostMigrationEnabled",        Clamped.HostMigrationEnabled);
    OutDocument.TomlHeader.insert("GameplayRulePolicy", std::move(Section));

    std::array<std::byte, GameplayRulePolicy::WirePayloadBytes> WireSlab{};
    (void)EncodePolicyWireBytes(Clamped, WireSlab);

    Serialization::BinarySectionBlock BinarySection{};
    BinarySection.SectionIdentifier = "GameplayRuleWireSlab";
    BinarySection.SubFormatTag      = ".BehaviorSpace";
    BinarySection.PayloadBytes.assign(WireSlab.begin(), WireSlab.end());
    OutDocument.BinarySections.push_back(std::move(BinarySection));
    return true;
}

bool GameplayRulePolicyEngine::DecodeFromDocument(
    const Serialization::DocumentContainer& Document,
    GameplayRulePolicy&                     OutPolicy,
    std::string&                            OutDiagnostic)
{
    if (const auto* WireSection = Serialization::DocumentContainerDecoder::FindBinarySection(
            Document, "GameplayRuleWireSlab"))
    {
        if (DecodePolicyWireBytes(WireSection->PayloadBytes, OutPolicy, OutDiagnostic))
        {
            return true;
        }
    }

    const auto* Section = Document.TomlHeader["GameplayRulePolicy"].as_table();
    if (Section == nullptr)
    {
        OutDiagnostic = "Missing [GameplayRulePolicy] section in document.";
        return false;
    }

    OutPolicy                             = {};
    OutPolicy.SchemaVersion               = static_cast<uint32_t>((*Section)["SchemaVersion"].value_or<int64_t>(1));
    OutPolicy.FixedSimulationHz           = static_cast<uint32_t>((*Section)["FixedSimulationHz"].value_or<int64_t>(120));
    OutPolicy.AlignmentHashIntervalTicks  = static_cast<uint32_t>((*Section)["AlignmentHashIntervalTicks"].value_or<int64_t>(60));
    OutPolicy.SpeedLimitKmh               = static_cast<float>((*Section)["SpeedLimitKmh"].value_or<double>(120.0));
    OutPolicy.RedSignalPenaltyScore       = static_cast<float>((*Section)["RedSignalPenaltyScore"].value_or<double>(50.0));
    OutPolicy.CollisionImpulseThresholdNs = static_cast<float>((*Section)["CollisionImpulseThresholdNs"].value_or<double>(12000.0));
    OutPolicy.PursuitEscalationStages     = static_cast<uint32_t>((*Section)["PursuitEscalationStages"].value_or<int64_t>(5));
    OutPolicy.MaximumSteerMagnitude       = static_cast<float>((*Section)["MaximumSteerMagnitude"].value_or<double>(1.0));
    OutPolicy.MaximumDirectiveRateHz      = static_cast<uint32_t>((*Section)["MaximumDirectiveRateHz"].value_or<int64_t>(240));
    OutPolicy.JoinInProgressAllowed       = (*Section)["JoinInProgressAllowed"].value_or<bool>(true);
    OutPolicy.HostMigrationEnabled        = (*Section)["HostMigrationEnabled"].value_or<bool>(true);

    ClampToSafeBounds(OutPolicy);
    OutDiagnostic.clear();
    return true;
}

} // namespace Frontier::SimulationSpace::Policies
