/*============================================================================================================================================
                                                            EPICSTUBTRANSPORT.CPP
============================================================================================================================================*/
// Open-source EOS stub. It never authenticates or claims cloud readiness; the explicit loopback bridge exists only for tests.

#include "Engine/NetworkingSpace/Transports/EpicOnlineTransport.h"

namespace Frontier::NetworkingSpace
{

struct EpicOnlineTransport::SdkState {};

EpicOnlineTransport::EpicOnlineTransport() = default;

EpicOnlineTransport::~EpicOnlineTransport() noexcept
{
    CloseChannel();
    ScrubSensitiveCredentials();
}

bool EpicOnlineTransport::QuerySdkCompiled() noexcept
{
    return false;
}

bool EpicOnlineTransport::ConfigureCredentials(const EpicSessionCredentials& Candidate)
{
    ScrubSensitiveCredentials();
    if (Candidate.ProductIdentifier.empty() || Candidate.SandboxIdentifier.empty() ||
        Candidate.DeploymentIdentifier.empty() || !VerifyMandatoryIdentityScopes(Candidate.RequestedScopeMask) ||
        (!Candidate.EncryptionHexKey.empty() && !VerifyHexStorageKeyFormat(Candidate.EncryptionHexKey)))
    {
        RedactedDiagnostic = "eos=stub_rejected_invalid_configuration";
        return false;
    }
    Credentials = Candidate;
    CredentialsValid = true;
    IdentityPhase = EpicIdentityPhase::PlatformReady;
    RedactedDiagnostic = "eos=sdk_not_compiled; cloud_login_unavailable";
    return true;
}

bool EpicOnlineTransport::InitializeSdk() noexcept { return false; }
bool EpicOnlineTransport::BeginEpicAccountPortalLogin() noexcept { return false; }
bool EpicOnlineTransport::ResolveProductUserConsent([[maybe_unused]] bool CreateNewProductUser) noexcept { return false; }
void EpicOnlineTransport::SignOut() noexcept { ChannelOpen = false; IdentityPhase = EpicIdentityPhase::Unconfigured; }
std::string EpicOnlineTransport::CopyEpicIdentityJwt() const { return {}; }
bool EpicOnlineTransport::RegisterPeer([[maybe_unused]] uint32_t EndpointIdentifier, [[maybe_unused]] std::string_view ProductId) { return false; }
void EpicOnlineTransport::UnregisterPeer([[maybe_unused]] uint32_t EndpointIdentifier) noexcept {}
bool EpicOnlineTransport::CreateMatchSession([[maybe_unused]] std::string_view SessionName, [[maybe_unused]] std::string_view BucketIdentifier, [[maybe_unused]] uint32_t MaxPlayers) noexcept { return false; }
bool EpicOnlineTransport::StartMatchSession() noexcept { return false; }
bool EpicOnlineTransport::SearchMatchSessions([[maybe_unused]] std::string_view BucketIdentifier, [[maybe_unused]] uint32_t MaxResults) noexcept { return false; }
bool EpicOnlineTransport::JoinMatchSession([[maybe_unused]] uint32_t SearchResultIndex, [[maybe_unused]] std::string_view LocalSessionName) noexcept { return false; }
bool EpicOnlineTransport::LeaveMatchSession() noexcept { return false; }

void EpicOnlineTransport::AttachSimulatedBridgeSwitch(LoopbackPacketSwitch* BridgeSwitch, uint32_t LocalEndpointIdentifier) noexcept
{
    SimulatedBridge = BridgeSwitch;
    LocalEndpoint = LocalEndpointIdentifier != 0u ? LocalEndpointIdentifier : 1u;
}

void EpicOnlineTransport::ScrubSensitiveCredentials() noexcept
{
    for (std::string* Secret : {&Credentials.ClientSecret, &Credentials.EncryptionHexKey})
    {
        if (!Secret->empty())
        {
            ZeroSensitiveBytes(std::span<char>(Secret->data(), Secret->size()));
            Secret->clear();
        }
    }
    CredentialsValid = false;
    if (IdentityPhase != EpicIdentityPhase::Failed)
        IdentityPhase = EpicIdentityPhase::Unconfigured;
}

bool EpicOnlineTransport::OpenChannel() noexcept
{
    if (SimulatedBridge == nullptr || !CredentialsValid)
        return false;
    ChannelOpen = true;
    return true;
}

void EpicOnlineTransport::CloseChannel() noexcept { ChannelOpen = false; }
void EpicOnlineTransport::AdvanceChannel([[maybe_unused]] double Δt) noexcept {}

bool EpicOnlineTransport::TransmitDatagram(uint32_t Target, DeliveryChannel Channel, std::span<const uint8_t> Bytes)
{
    return ChannelOpen && SimulatedBridge != nullptr && SimulatedBridge->EnqueueFlightDatagram(LocalEndpoint, Target, Channel, Bytes);
}

bool EpicOnlineTransport::ReceiveDatagram(PacketDatagram& OutDatagram)
{
    return ChannelOpen && SimulatedBridge != nullptr && SimulatedBridge->PopArrivedDatagram(LocalEndpoint, OutDatagram);
}

bool EpicOnlineTransport::QueryChannelReady() const noexcept
{
    return CredentialsValid && (SimulatedBridge != nullptr);
}

} // namespace Frontier::NetworkingSpace
