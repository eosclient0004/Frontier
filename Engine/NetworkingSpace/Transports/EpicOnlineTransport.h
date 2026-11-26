/*============================================================================================================================================
                                                            EPICONLINETRANSPORT.H
============================================================================================================================================*/
// Epic Online Services Auth/Connect identity, P2P packet transport, and Sessions fallback.

#pragma once

#include "Engine/NetworkingSpace/Policies/AuthenticationScopePolicy.h"
#include "Engine/NetworkingSpace/Transports/LoopbackTransport.h"
#include "Engine/NetworkingSpace/Transports/TransportInterface.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Frontier::NetworkingSpace
{

struct EpicSessionCredentials
{
    std::string ProductIdentifier{};
    std::string SandboxIdentifier{};
    std::string DeploymentIdentifier{};
    std::string ClientIdentifier{};
    std::string ClientSecret{}; // Local secret only. Never put this in project TOML or source control.
    std::string ProductName{"Frontier"};
    std::string ProductVersion{"1.0.0"};
    std::string EncryptionHexKey{};
    uint32_t RequestedScopeMask = static_cast<uint32_t>(IdentityScopeMask::EnableMandatoryPortalMask);
};

enum class EpicIdentityPhase : uint8_t
{
    Unconfigured,
    PlatformReady,
    AuthPending,
    ConnectPending,
    ProductUserConsentRequired,
    Authenticated,
    Failed,
};

enum class EpicSessionPhase : uint8_t
{
    None,
    Creating,
    Ready,
    Starting,
    Searching,
    Joining,
    Active,
    Leaving,
    Failed,
};

class EpicOnlineTransport final
{
public:
    EpicOnlineTransport();
    ~EpicOnlineTransport() noexcept;

    EpicOnlineTransport(const EpicOnlineTransport&) = delete;
    EpicOnlineTransport& operator=(const EpicOnlineTransport&) = delete;
    EpicOnlineTransport(EpicOnlineTransport&&) = delete;
    EpicOnlineTransport& operator=(EpicOnlineTransport&&) = delete;

    [[nodiscard]] bool ConfigureCredentials(const EpicSessionCredentials& Credentials);
    [[nodiscard]] bool InitializeSdk() noexcept;
    [[nodiscard]] bool BeginEpicAccountPortalLogin() noexcept;
    [[nodiscard]] bool ResolveProductUserConsent(bool CreateNewProductUser) noexcept;
    void               SignOut() noexcept;
    [[nodiscard]] EpicIdentityPhase QueryIdentityPhase() const noexcept { return IdentityPhase; }
    [[nodiscard]] std::string_view QueryProductUserIdentifier() const noexcept { return ProductUserIdentifier; }
    [[nodiscard]] std::string       CopyEpicIdentityJwt() const;

    [[nodiscard]] bool RegisterPeer(uint32_t EndpointIdentifier, std::string_view ProductUserIdentifier);
    void               UnregisterPeer(uint32_t EndpointIdentifier) noexcept;
    void               AttachSimulatedBridgeSwitch(LoopbackPacketSwitch* BridgeSwitch, uint32_t LocalEndpointIdentifier) noexcept;

    [[nodiscard]] bool CreateMatchSession(std::string_view SessionName, std::string_view BucketIdentifier, uint32_t MaxPlayers) noexcept;
    [[nodiscard]] bool StartMatchSession() noexcept;
    [[nodiscard]] bool SearchMatchSessions(std::string_view BucketIdentifier, uint32_t MaxResults = 16u) noexcept;
    [[nodiscard]] bool JoinMatchSession(uint32_t SearchResultIndex, std::string_view LocalSessionName) noexcept;
    [[nodiscard]] bool LeaveMatchSession() noexcept;
    [[nodiscard]] EpicSessionPhase QuerySessionPhase() const noexcept { return SessionPhase; }
    [[nodiscard]] uint32_t QuerySessionSearchResultCount() const noexcept { return SessionSearchResultCount; }
    [[nodiscard]] std::string_view QueryActiveSessionName() const noexcept { return ActiveSessionName; }

    void               ScrubSensitiveCredentials() noexcept;
    [[nodiscard]] bool OpenChannel() noexcept;
    void               CloseChannel() noexcept;
    void               AdvanceChannel(double Δt) noexcept;

    [[nodiscard]] bool TransmitDatagram(
        uint32_t                 TargetEndpointIdentifier,
        DeliveryChannel          Channel,
        std::span<const uint8_t> WireBytes);
    [[nodiscard]] bool ReceiveDatagram(PacketDatagram& OutDatagram);

    [[nodiscard]] TransportChannelCategory QueryChannelCategory() const noexcept { return TransportChannelCategory::EpicOnline; }
    [[nodiscard]] bool QueryChannelOperational() const noexcept { return ChannelOpen; }
    [[nodiscard]] bool QueryChannelReady() const noexcept;
    [[nodiscard]] static bool QuerySdkCompiled() noexcept;
    [[nodiscard]] std::string_view InspectRedactedDiagnostic() const noexcept { return RedactedDiagnostic; }

private:
    struct SdkState;
    std::unique_ptr<SdkState> Sdk;
    EpicSessionCredentials Credentials{};
    LoopbackPacketSwitch* SimulatedBridge = nullptr;
    uint32_t LocalEndpoint = 1u;
    EpicIdentityPhase IdentityPhase = EpicIdentityPhase::Unconfigured;
    EpicSessionPhase SessionPhase = EpicSessionPhase::None;
    std::string ProductUserIdentifier{};
    std::string ActiveSessionName{};
    std::string RedactedDiagnostic{"eos=unconfigured"};
    uint32_t SessionSearchResultCount = 0u;
    bool CredentialsValid = false;
    bool ChannelOpen = false;
};

static_assert(PacketTransportChannel<EpicOnlineTransport>, "EpicOnlineTransport must satisfy PacketTransportChannel");

} // namespace Frontier::NetworkingSpace
