/*============================================================================================================================================
                                                           EPICONLINETRANSPORT.CPP
============================================================================================================================================*/
// EOS SDK-backed Auth -> Connect identity, EOS P2P packets, and EOS Sessions host/search/join/leave operations.

#include "Engine/NetworkingSpace/Transports/EpicOnlineTransport.h"

#include <eos_auth.h>
#include <eos_connect.h>
#include <eos_p2p.h>
#include <eos_sessions.h>
#include <eos_sdk.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Frontier::NetworkingSpace
{
namespace
{
std::mutex EosRuntimeMutex{};
uint32_t EosRuntimeReferences = 0u;

bool AcquireEosRuntime(const EpicSessionCredentials& Credentials)
{
    std::scoped_lock Lock(EosRuntimeMutex);
    if (EosRuntimeReferences == 0u)
    {
        EOS_InitializeOptions InitializeOptions{};
        InitializeOptions.ApiVersion = EOS_INITIALIZE_API_LATEST;
        InitializeOptions.ProductName = Credentials.ProductName.c_str();
        InitializeOptions.ProductVersion = Credentials.ProductVersion.c_str();
        if (EOS_Initialize(&InitializeOptions) != EOS_EResult::EOS_Success)
            return false;
    }
    ++EosRuntimeReferences;
    return true;
}

void ReleaseEosRuntime() noexcept
{
    std::scoped_lock Lock(EosRuntimeMutex);
    if (EosRuntimeReferences == 0u)
        return;
    --EosRuntimeReferences;
    if (EosRuntimeReferences == 0u)
        (void)EOS_Shutdown();
}

uint32_t ToEosScopeMask(uint32_t Requested) noexcept
{
    uint32_t Result = 0u;
    if ((Requested & static_cast<uint32_t>(IdentityScopeMask::EnableBasicProfile)) != 0u)
        Result |= static_cast<uint32_t>(EOS_EAuthScopeFlags::EOS_AS_BasicProfile);
    if ((Requested & static_cast<uint32_t>(IdentityScopeMask::EnableFriendsRoster)) != 0u)
        Result |= static_cast<uint32_t>(EOS_EAuthScopeFlags::EOS_AS_FriendsList);
    if ((Requested & static_cast<uint32_t>(IdentityScopeMask::EnablePresenceBroadcast)) != 0u)
        Result |= static_cast<uint32_t>(EOS_EAuthScopeFlags::EOS_AS_Presence);
    if ((Requested & static_cast<uint32_t>(IdentityScopeMask::EnableCountryCode)) != 0u)
        Result |= static_cast<uint32_t>(EOS_EAuthScopeFlags::EOS_AS_Country);
    return Result;
}

std::string RedactedResult(const char* Operation, EOS_EResult Result)
{
    return std::string("eos=") + Operation + "; result=" + EOS_EResult_ToString(Result);
}
}

struct EpicOnlineTransport::SdkState
{
    EpicOnlineTransport* Owner = nullptr;

    ~SdkState() { ReleasePlatform(); }
    EOS_HPlatform Platform = nullptr;
    EOS_HAuth Auth = nullptr;
    EOS_HConnect Connect = nullptr;
    EOS_HP2P P2P = nullptr;
    EOS_HSessions Sessions = nullptr;
    EOS_EpicAccountId EpicAccount = nullptr;
    EOS_ProductUserId ProductUser = nullptr;
    EOS_ContinuanceToken PendingContinuanceToken = nullptr;
    double ConsentElapsedSeconds = 0.0;
    EOS_HSessionSearch SearchHandle = nullptr;
    EOS_HSessionDetails PendingSessionDetails = nullptr;
    bool SearchInProgress = false;
    EOS_NotificationId PeerRequestNotification = EOS_INVALID_NOTIFICATIONID;
    EOS_P2P_SocketId Socket{};
    std::unordered_map<uint32_t, EOS_ProductUserId> Peers{};
    std::string IdentityJwt{};
    bool OwnsGlobalRuntime = false;

    void ReleaseSearch() noexcept
    {
        SearchInProgress = false;
        if (PendingSessionDetails != nullptr)
        {
            EOS_SessionDetails_Release(PendingSessionDetails);
            PendingSessionDetails = nullptr;
        }
        if (SearchHandle != nullptr)
        {
            EOS_SessionSearch_Release(SearchHandle);
            SearchHandle = nullptr;
        }
    }

    void ReleasePlatform() noexcept
    {
        ReleaseSearch();
        if (P2P != nullptr && PeerRequestNotification != EOS_INVALID_NOTIFICATIONID)
        {
            EOS_P2P_RemoveNotifyPeerConnectionRequest(P2P, PeerRequestNotification);
            PeerRequestNotification = EOS_INVALID_NOTIFICATIONID;
        }
        if (Platform != nullptr)
        {
            EOS_Platform_Release(Platform);
            Platform = nullptr;
        }
        Auth = nullptr;
        Connect = nullptr;
        P2P = nullptr;
        Sessions = nullptr;
        EpicAccount = nullptr;
        ProductUser = nullptr;
        PendingContinuanceToken = nullptr;
        Peers.clear();
        if (!IdentityJwt.empty())
        {
            ZeroSensitiveBytes(std::span<char>(IdentityJwt.data(), IdentityJwt.size()));
            IdentityJwt.clear();
        }
        if (OwnsGlobalRuntime)
        {
            ReleaseEosRuntime();
            OwnsGlobalRuntime = false;
        }
    }

    void Fail(const char* Operation, EOS_EResult Result) noexcept
    {
        Owner->IdentityPhase = EpicIdentityPhase::Failed;
        Owner->SessionPhase = EpicSessionPhase::Failed;
        Owner->RedactedDiagnostic = RedactedResult(Operation, Result);
    }

    static void EOS_CALL OnAuthLogin(const EOS_Auth_LoginCallbackInfo* Info)
    {
        if (Info == nullptr || Info->ClientData == nullptr)
            return;
        auto& State = *static_cast<SdkState*>(Info->ClientData);
        EpicOnlineTransport& Self = *State.Owner;
        if (Info->ResultCode != EOS_EResult::EOS_Success || !EOS_EpicAccountId_IsValid(Info->SelectedAccountId))
        {
            State.Fail("auth_login_failed", Info->ResultCode);
            return;
        }
        State.EpicAccount = Info->SelectedAccountId;
        EOS_Auth_CopyIdTokenOptions CopyOptions{};
        CopyOptions.ApiVersion = EOS_AUTH_COPYIDTOKEN_API_LATEST;
        CopyOptions.AccountId = State.EpicAccount;
        EOS_Auth_IdToken* IdToken = nullptr;
        const EOS_EResult CopyResult = EOS_Auth_CopyIdToken(State.Auth, &CopyOptions, &IdToken);
        if (CopyResult != EOS_EResult::EOS_Success || IdToken == nullptr || IdToken->JsonWebToken == nullptr)
        {
            if (IdToken != nullptr)
                EOS_Auth_IdToken_Release(IdToken);
            State.Fail("auth_identity_token_unavailable", CopyResult);
            return;
        }
        State.IdentityJwt.assign(IdToken->JsonWebToken);
        EOS_Auth_IdToken_Release(IdToken);

        EOS_Connect_Credentials Credentials{};
        Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
        Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_EPIC_ID_TOKEN;
        Credentials.Token = State.IdentityJwt.c_str();
        EOS_Connect_LoginOptions LoginOptions{};
        LoginOptions.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
        LoginOptions.Credentials = &Credentials;
        Self.IdentityPhase = EpicIdentityPhase::ConnectPending;
        EOS_Connect_Login(State.Connect, &LoginOptions, &State, OnConnectLogin);
    }

    static void EOS_CALL OnConnectLogin(const EOS_Connect_LoginCallbackInfo* Info)
    {
        if (Info == nullptr || Info->ClientData == nullptr)
            return;
        auto& State = *static_cast<SdkState*>(Info->ClientData);
        EpicOnlineTransport& Self = *State.Owner;
        if (Info->ResultCode == EOS_EResult::EOS_InvalidUser && Info->ContinuanceToken != nullptr)
        {
            State.PendingContinuanceToken = Info->ContinuanceToken;
            State.ConsentElapsedSeconds = 0.0;
            Self.IdentityPhase = EpicIdentityPhase::ProductUserConsentRequired;
            Self.RedactedDiagnostic = "eos=product_user_creation_requires_explicit_consent";
            return;
        }
        if (Info->ResultCode != EOS_EResult::EOS_Success || !EOS_ProductUserId_IsValid(Info->LocalUserId))
        {
            State.Fail("connect_login_failed", Info->ResultCode);
            return;
        }
        State.AcceptProductUser(Info->LocalUserId);
    }

    static void EOS_CALL OnCreateProductUser(const EOS_Connect_CreateUserCallbackInfo* Info)
    {
        if (Info == nullptr || Info->ClientData == nullptr)
            return;
        auto& State = *static_cast<SdkState*>(Info->ClientData);
        if (Info->ResultCode != EOS_EResult::EOS_Success || !EOS_ProductUserId_IsValid(Info->LocalUserId))
        {
            State.Fail("connect_create_user_failed", Info->ResultCode);
            return;
        }
        State.AcceptProductUser(Info->LocalUserId);
    }

    void AcceptProductUser(EOS_ProductUserId User)
    {
        ProductUser = User;
        std::array<char, 128> ProductUserText{};
        int32_t BufferLength = static_cast<int32_t>(ProductUserText.size());
        if (EOS_ProductUserId_ToString(ProductUser, ProductUserText.data(), &BufferLength) != EOS_EResult::EOS_Success)
        {
            Fail("connect_product_user_id_conversion_failed", EOS_EResult::EOS_UnexpectedError);
            return;
        }
        Owner->ProductUserIdentifier.assign(ProductUserText.data());
        Owner->IdentityPhase = EpicIdentityPhase::Authenticated;
        Owner->RedactedDiagnostic = "eos=authenticated; identity_token=redacted";
        PendingContinuanceToken = nullptr;
    }

    static void EOS_CALL OnPeerConnectionRequest(const EOS_P2P_OnIncomingConnectionRequestInfo* Info)
    {
        if (Info == nullptr || Info->ClientData == nullptr || Info->SocketId == nullptr)
            return;
        auto& State = *static_cast<SdkState*>(Info->ClientData);
        if (State.P2P == nullptr || State.ProductUser == nullptr || Info->LocalUserId != State.ProductUser)
            return;
        const bool KnownPeer = std::any_of(State.Peers.begin(), State.Peers.end(), [Info](const auto& Entry)
        {
            return Entry.second == Info->RemoteUserId;
        });
        if (!KnownPeer)
            return;
        EOS_P2P_AcceptConnectionOptions Accept{};
        Accept.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
        Accept.LocalUserId = State.ProductUser;
        Accept.RemoteUserId = Info->RemoteUserId;
        Accept.SocketId = Info->SocketId;
        (void)EOS_P2P_AcceptConnection(State.P2P, &Accept);
    }

    static void EOS_CALL OnSessionUpdated(const EOS_Sessions_UpdateSessionCallbackInfo* Info)
    {
        if (Info == nullptr || Info->ClientData == nullptr)
            return;
        auto& State = *static_cast<SdkState*>(Info->ClientData);
        if (Info->ResultCode != EOS_EResult::EOS_Success)
        {
            State.Owner->SessionPhase = EpicSessionPhase::Failed;
            State.Owner->RedactedDiagnostic = RedactedResult("session_create_failed", Info->ResultCode);
            return;
        }
        State.Owner->SessionPhase = EpicSessionPhase::Ready;
        if (Info->SessionName != nullptr)
            State.Owner->ActiveSessionName = Info->SessionName;
        State.Owner->RedactedDiagnostic = "eos=session_ready";
    }

    static void EOS_CALL OnSessionStarted(const EOS_Sessions_StartSessionCallbackInfo* Info)
    {
        if (Info == nullptr || Info->ClientData == nullptr)
            return;
        auto& State = *static_cast<SdkState*>(Info->ClientData);
        if (Info->ResultCode != EOS_EResult::EOS_Success)
        {
            State.Owner->SessionPhase = EpicSessionPhase::Ready;
            State.Owner->RedactedDiagnostic = RedactedResult("session_start_failed", Info->ResultCode);
            return;
        }
        State.Owner->SessionPhase = EpicSessionPhase::Active;
        State.Owner->RedactedDiagnostic = "eos=session_started";
    }

    static void EOS_CALL OnSearchComplete(const EOS_SessionSearch_FindCallbackInfo* Info)
    {
        if (Info == nullptr || Info->ClientData == nullptr)
            return;
        auto& State = *static_cast<SdkState*>(Info->ClientData);
        State.SearchInProgress = false;
        if (Info->ResultCode != EOS_EResult::EOS_Success || State.SearchHandle == nullptr)
        {
            State.Owner->SessionPhase = EpicSessionPhase::Failed;
            State.Owner->SessionSearchResultCount = 0u;
            State.Owner->RedactedDiagnostic = RedactedResult("session_search_failed", Info->ResultCode);
            return;
        }
        EOS_SessionSearch_GetSearchResultCountOptions CountOptions{};
        CountOptions.ApiVersion = EOS_SESSIONSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
        State.Owner->SessionSearchResultCount = EOS_SessionSearch_GetSearchResultCount(State.SearchHandle, &CountOptions);
        State.Owner->SessionPhase = EpicSessionPhase::Searching;
        State.Owner->RedactedDiagnostic = "eos=session_search_complete";
    }

    static void EOS_CALL OnSessionJoined(const EOS_Sessions_JoinSessionCallbackInfo* Info)
    {
        if (Info == nullptr || Info->ClientData == nullptr)
            return;
        auto& State = *static_cast<SdkState*>(Info->ClientData);
        const std::string JoinedSessionName = Info->SessionName != nullptr ? Info->SessionName : "";
        State.ReleaseSearch();
        if (Info->ResultCode != EOS_EResult::EOS_Success)
        {
            State.Owner->SessionPhase = EpicSessionPhase::Failed;
            State.Owner->RedactedDiagnostic = RedactedResult("session_join_failed", Info->ResultCode);
            return;
        }
        State.Owner->ActiveSessionName = JoinedSessionName;
        State.Owner->SessionPhase = EpicSessionPhase::Active;
        State.Owner->RedactedDiagnostic = "eos=session_joined";
    }

    static void EOS_CALL OnSessionEnded(const EOS_Sessions_EndSessionCallbackInfo* Info)
    {
        if (Info == nullptr || Info->ClientData == nullptr)
            return;
        auto& State = *static_cast<SdkState*>(Info->ClientData);
        if (Info->ResultCode != EOS_EResult::EOS_Success)
        {
            State.Owner->SessionPhase = EpicSessionPhase::Failed;
            State.Owner->RedactedDiagnostic = RedactedResult("session_end_failed", Info->ResultCode);
            return;
        }
        EOS_Sessions_DestroySessionOptions DestroyOptions{};
        DestroyOptions.ApiVersion = EOS_SESSIONS_DESTROYSESSION_API_LATEST;
        DestroyOptions.SessionName = State.Owner->ActiveSessionName.c_str();
        State.Owner->SessionPhase = EpicSessionPhase::Leaving;
        EOS_Sessions_DestroySession(State.Sessions, &DestroyOptions, &State, OnSessionDestroyed);
    }

    static void EOS_CALL OnSessionDestroyed(const EOS_Sessions_DestroySessionCallbackInfo* Info)
    {
        if (Info == nullptr || Info->ClientData == nullptr)
            return;
        auto& State = *static_cast<SdkState*>(Info->ClientData);
        if (Info->ResultCode != EOS_EResult::EOS_Success)
        {
            State.Owner->SessionPhase = EpicSessionPhase::Failed;
            State.Owner->RedactedDiagnostic = RedactedResult("session_destroy_failed", Info->ResultCode);
            return;
        }
        State.Owner->ActiveSessionName.clear();
        State.Owner->SessionPhase = EpicSessionPhase::None;
        State.Owner->RedactedDiagnostic = "eos=session_closed";
    }
};

EpicOnlineTransport::EpicOnlineTransport() : Sdk(std::make_unique<SdkState>())
{
    Sdk->Owner = this;
    Sdk->Socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    constexpr char SocketName[] = "FrontierNet";
    std::memcpy(Sdk->Socket.SocketName, SocketName, sizeof(SocketName));
}

EpicOnlineTransport::~EpicOnlineTransport() noexcept
{
    CloseChannel();
    SignOut();
    ScrubSensitiveCredentials();
}

bool EpicOnlineTransport::QuerySdkCompiled() noexcept { return true; }

bool EpicOnlineTransport::ConfigureCredentials(const EpicSessionCredentials& Candidate)
{
    SignOut();
    ScrubSensitiveCredentials();
    const auto IsAsciiCredential = [](const std::string& Value)
    {
        return Value.size() <= 64u && !Value.empty() && std::all_of(Value.begin(), Value.end(), [](unsigned char Character)
        {
            return Character > 32u && Character < 127u;
        });
    };
    const bool ClientCredentialsAbsent = Candidate.ClientIdentifier.empty() && Candidate.ClientSecret.empty();
    const bool ClientCredentialsValid = IsAsciiCredential(Candidate.ClientIdentifier) && IsAsciiCredential(Candidate.ClientSecret);
    if (Candidate.ProductIdentifier.empty() || Candidate.SandboxIdentifier.empty() ||
        Candidate.DeploymentIdentifier.empty() || (!ClientCredentialsAbsent && !ClientCredentialsValid) ||
        !VerifyMandatoryIdentityScopes(Candidate.RequestedScopeMask) ||
        (!Candidate.EncryptionHexKey.empty() && !VerifyHexStorageKeyFormat(Candidate.EncryptionHexKey)))
    {
        IdentityPhase = EpicIdentityPhase::Failed;
        RedactedDiagnostic = "eos=configuration_rejected";
        return false;
    }
    Credentials = Candidate;
    CredentialsValid = true;
    IdentityPhase = EpicIdentityPhase::Unconfigured;
    RedactedDiagnostic = "eos=credentials_configured; authentication_not_attempted";
    return true;
}

bool EpicOnlineTransport::InitializeSdk() noexcept
{
    if (Sdk->Platform != nullptr)
        return true;
    if (!CredentialsValid || Credentials.ClientIdentifier.empty() || Credentials.ClientSecret.empty() || !QuerySdkCompiled())
        return false;
    if (!AcquireEosRuntime(Credentials))
    {
        IdentityPhase = EpicIdentityPhase::Failed;
        RedactedDiagnostic = "eos=initialize_failed";
        return false;
    }
    Sdk->OwnsGlobalRuntime = true;

    EOS_Platform_Options Options{};
    Options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    Options.ProductId = Credentials.ProductIdentifier.c_str();
    Options.SandboxId = Credentials.SandboxIdentifier.c_str();
    Options.DeploymentId = Credentials.DeploymentIdentifier.c_str();
    Options.ClientCredentials.ClientId = Credentials.ClientIdentifier.c_str();
    Options.ClientCredentials.ClientSecret = Credentials.ClientSecret.c_str();
    Options.bIsServer = EOS_FALSE;
    Options.EncryptionKey = Credentials.EncryptionHexKey.empty() ? nullptr : Credentials.EncryptionHexKey.c_str();
    Sdk->Platform = EOS_Platform_Create(&Options);
    if (Sdk->Platform == nullptr)
    {
        Sdk->ReleasePlatform();
        IdentityPhase = EpicIdentityPhase::Failed;
        RedactedDiagnostic = "eos=platform_create_failed";
        return false;
    }
    Sdk->Auth = EOS_Platform_GetAuthInterface(Sdk->Platform);
    Sdk->Connect = EOS_Platform_GetConnectInterface(Sdk->Platform);
    Sdk->P2P = EOS_Platform_GetP2PInterface(Sdk->Platform);
    Sdk->Sessions = EOS_Platform_GetSessionsInterface(Sdk->Platform);
    if (Sdk->Auth == nullptr || Sdk->Connect == nullptr || Sdk->P2P == nullptr || Sdk->Sessions == nullptr)
    {
        Sdk->ReleasePlatform();
        IdentityPhase = EpicIdentityPhase::Failed;
        RedactedDiagnostic = "eos=required_interface_unavailable";
        return false;
    }
    if (!Credentials.ClientSecret.empty())
    {
        ZeroSensitiveBytes(std::span<char>(Credentials.ClientSecret.data(), Credentials.ClientSecret.size()));
        Credentials.ClientSecret.clear();
    }
    IdentityPhase = EpicIdentityPhase::PlatformReady;
    RedactedDiagnostic = "eos=platform_ready; player_authentication_not_attempted";
    return true;
}

bool EpicOnlineTransport::BeginEpicAccountPortalLogin() noexcept
{
    if (!InitializeSdk() || IdentityPhase == EpicIdentityPhase::AuthPending || IdentityPhase == EpicIdentityPhase::ConnectPending)
        return false;
    EOS_Auth_Credentials AuthCredentials{};
    AuthCredentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
    AuthCredentials.Type = EOS_ELoginCredentialType::EOS_LCT_AccountPortal;
    EOS_Auth_LoginOptions Options{};
    Options.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
    Options.Credentials = &AuthCredentials;
    Options.ScopeFlags = static_cast<EOS_EAuthScopeFlags>(ToEosScopeMask(Credentials.RequestedScopeMask));
    IdentityPhase = EpicIdentityPhase::AuthPending;
    RedactedDiagnostic = "eos=account_portal_login_pending";
    EOS_Auth_Login(Sdk->Auth, &Options, Sdk.get(), SdkState::OnAuthLogin);
    return true;
}

bool EpicOnlineTransport::ResolveProductUserConsent(bool CreateNewProductUser) noexcept
{
    if (IdentityPhase != EpicIdentityPhase::ProductUserConsentRequired || Sdk->PendingContinuanceToken == nullptr ||
        Sdk->ConsentElapsedSeconds >= ContinuanceApprovalTimeoutSeconds)
        return false;
    EOS_ContinuanceToken Token = Sdk->PendingContinuanceToken;
    Sdk->PendingContinuanceToken = nullptr;
    if (!CreateNewProductUser)
    {
        IdentityPhase = EpicIdentityPhase::Failed;
        RedactedDiagnostic = "eos=product_user_creation_declined";
        return true;
    }
    EOS_Connect_CreateUserOptions Options{};
    Options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
    Options.ContinuanceToken = Token;
    IdentityPhase = EpicIdentityPhase::ConnectPending;
    RedactedDiagnostic = "eos=product_user_creation_pending";
    EOS_Connect_CreateUser(Sdk->Connect, &Options, Sdk.get(), SdkState::OnCreateProductUser);
    return true;
}

void EpicOnlineTransport::SignOut() noexcept
{
    CloseChannel();
    if (Sdk)
        Sdk->ReleasePlatform();
    ProductUserIdentifier.clear();
    ActiveSessionName.clear();
    SessionSearchResultCount = 0u;
    SessionPhase = EpicSessionPhase::None;
    CredentialsValid = !Credentials.ClientSecret.empty();
    IdentityPhase = CredentialsValid ? EpicIdentityPhase::PlatformReady : EpicIdentityPhase::Unconfigured;
}

std::string EpicOnlineTransport::CopyEpicIdentityJwt() const
{
    return Sdk ? Sdk->IdentityJwt : std::string{};
}

bool EpicOnlineTransport::RegisterPeer(uint32_t EndpointIdentifier, std::string_view ProductUserText)
{
    if (EndpointIdentifier == 0u || ProductUserText.empty() || !Sdk || Sdk->ProductUser == nullptr)
        return false;
    std::string Text(ProductUserText);
    EOS_ProductUserId User = EOS_ProductUserId_FromString(Text.c_str());
    if (!EOS_ProductUserId_IsValid(User))
        return false;
    Sdk->Peers[EndpointIdentifier] = User;
    return true;
}

void EpicOnlineTransport::UnregisterPeer(uint32_t EndpointIdentifier) noexcept
{
    if (Sdk)
        Sdk->Peers.erase(EndpointIdentifier);
}

void EpicOnlineTransport::AttachSimulatedBridgeSwitch(LoopbackPacketSwitch* BridgeSwitch, uint32_t LocalEndpointIdentifier) noexcept
{
    SimulatedBridge = BridgeSwitch;
    LocalEndpoint = LocalEndpointIdentifier != 0u ? LocalEndpointIdentifier : 1u;
}

bool EpicOnlineTransport::CreateMatchSession(std::string_view Name, std::string_view Bucket, uint32_t MaxPlayers) noexcept
{
    if (IdentityPhase != EpicIdentityPhase::Authenticated || Sdk->Sessions == nullptr || Name.empty() || Bucket.empty() ||
        Name.size() > 64u || Bucket.size() > 64u || MaxPlayers < 2u || MaxPlayers > 100u ||
        SessionPhase == EpicSessionPhase::Creating || SessionPhase == EpicSessionPhase::Ready ||
        SessionPhase == EpicSessionPhase::Starting || SessionPhase == EpicSessionPhase::Joining ||
        SessionPhase == EpicSessionPhase::Active || SessionPhase == EpicSessionPhase::Leaving)
        return false;
    const std::string SessionName(Name);
    const std::string BucketId(Bucket);
    EOS_Sessions_CreateSessionModificationOptions CreateOptions{};
    CreateOptions.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
    CreateOptions.SessionName = SessionName.c_str();
    CreateOptions.BucketId = BucketId.c_str();
    CreateOptions.MaxPlayers = MaxPlayers;
    CreateOptions.LocalUserId = Sdk->ProductUser;
    CreateOptions.bPresenceEnabled = EOS_FALSE;
    EOS_HSessionModification Modification = nullptr;
    if (EOS_Sessions_CreateSessionModification(Sdk->Sessions, &CreateOptions, &Modification) != EOS_EResult::EOS_Success || Modification == nullptr)
        return false;
    EOS_SessionModification_SetPermissionLevelOptions Permission{};
    Permission.ApiVersion = EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST;
    Permission.PermissionLevel = EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised;
    const EOS_EResult PermissionResult = EOS_SessionModification_SetPermissionLevel(Modification, &Permission);
    EOS_SessionModification_SetJoinInProgressAllowedOptions JoinOptions{};
    JoinOptions.ApiVersion = EOS_SESSIONMODIFICATION_SETJOININPROGRESSALLOWED_API_LATEST;
    JoinOptions.bAllowJoinInProgress = EOS_TRUE;
    const EOS_EResult JoinResult = PermissionResult == EOS_EResult::EOS_Success
        ? EOS_SessionModification_SetJoinInProgressAllowed(Modification, &JoinOptions)
        : PermissionResult;
    if (JoinResult != EOS_EResult::EOS_Success)
    {
        EOS_SessionModification_Release(Modification);
        return false;
    }
    EOS_Sessions_AttributeData BucketAttribute{};
    BucketAttribute.ApiVersion = EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
    BucketAttribute.Key = "frontier_bucket";
    BucketAttribute.ValueType = EOS_ESessionAttributeType::EOS_AT_STRING;
    BucketAttribute.Value.AsUtf8 = BucketId.c_str();
    EOS_SessionModification_AddAttributeOptions AttributeOptions{};
    AttributeOptions.ApiVersion = EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST;
    AttributeOptions.SessionAttribute = &BucketAttribute;
    AttributeOptions.AdvertisementType = EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise;
    const EOS_EResult AttributeResult = EOS_SessionModification_AddAttribute(Modification, &AttributeOptions);
    if (AttributeResult != EOS_EResult::EOS_Success)
    {
        EOS_SessionModification_Release(Modification);
        return false;
    }
    ActiveSessionName = SessionName;
    EOS_Sessions_UpdateSessionOptions UpdateOptions{};
    UpdateOptions.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
    UpdateOptions.SessionModificationHandle = Modification;
    SessionPhase = EpicSessionPhase::Creating;
    RedactedDiagnostic = "eos=session_create_pending";
    EOS_Sessions_UpdateSession(Sdk->Sessions, &UpdateOptions, Sdk.get(), SdkState::OnSessionUpdated);
    EOS_SessionModification_Release(Modification);
    return true;
}

bool EpicOnlineTransport::StartMatchSession() noexcept
{
    if (IdentityPhase != EpicIdentityPhase::Authenticated || SessionPhase != EpicSessionPhase::Ready ||
        ActiveSessionName.empty() || Sdk->Sessions == nullptr)
        return false;
    EOS_Sessions_StartSessionOptions Options{};
    Options.ApiVersion = EOS_SESSIONS_STARTSESSION_API_LATEST;
    Options.SessionName = ActiveSessionName.c_str();
    SessionPhase = EpicSessionPhase::Starting;
    RedactedDiagnostic = "eos=session_start_pending";
    EOS_Sessions_StartSession(Sdk->Sessions, &Options, Sdk.get(), SdkState::OnSessionStarted);
    return true;
}

bool EpicOnlineTransport::SearchMatchSessions(std::string_view BucketIdentifier, uint32_t MaxResults) noexcept
{
    if (IdentityPhase != EpicIdentityPhase::Authenticated || Sdk->Sessions == nullptr || BucketIdentifier.empty() ||
        MaxResults == 0u || MaxResults > 64u || Sdk->SearchInProgress || SessionPhase == EpicSessionPhase::Creating ||
        SessionPhase == EpicSessionPhase::Joining || SessionPhase == EpicSessionPhase::Leaving)
        return false;
    Sdk->ReleaseSearch();
    SessionSearchResultCount = 0u;
    EOS_Sessions_CreateSessionSearchOptions CreateOptions{};
    CreateOptions.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
    CreateOptions.MaxSearchResults = MaxResults;
    if (EOS_Sessions_CreateSessionSearch(Sdk->Sessions, &CreateOptions, &Sdk->SearchHandle) != EOS_EResult::EOS_Success ||
        Sdk->SearchHandle == nullptr)
        return false;

    const std::string Bucket(BucketIdentifier);
    EOS_Sessions_AttributeData Attribute{};
    Attribute.ApiVersion = EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
    Attribute.Key = "frontier_bucket";
    Attribute.ValueType = EOS_ESessionAttributeType::EOS_AT_STRING;
    Attribute.Value.AsUtf8 = Bucket.c_str();
    EOS_SessionSearch_SetParameterOptions Parameter{};
    Parameter.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
    Parameter.Parameter = &Attribute;
    Parameter.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
    if (EOS_SessionSearch_SetParameter(Sdk->SearchHandle, &Parameter) != EOS_EResult::EOS_Success)
    {
        Sdk->ReleaseSearch();
        return false;
    }
    EOS_SessionSearch_FindOptions FindOptions{};
    FindOptions.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
    FindOptions.LocalUserId = Sdk->ProductUser;
    SessionPhase = EpicSessionPhase::Searching;
    Sdk->SearchInProgress = true;
    RedactedDiagnostic = "eos=session_search_pending";
    EOS_SessionSearch_Find(Sdk->SearchHandle, &FindOptions, Sdk.get(), SdkState::OnSearchComplete);
    return true;
}

bool EpicOnlineTransport::JoinMatchSession(uint32_t SearchResultIndex, std::string_view LocalSessionName) noexcept
{
    if (IdentityPhase != EpicIdentityPhase::Authenticated || Sdk->Sessions == nullptr || Sdk->SearchHandle == nullptr ||
        SearchResultIndex >= SessionSearchResultCount || LocalSessionName.empty() || LocalSessionName.size() > 64u ||
        SessionPhase != EpicSessionPhase::Searching)
        return false;
    EOS_SessionSearch_CopySearchResultByIndexOptions CopyOptions{};
    CopyOptions.ApiVersion = EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
    CopyOptions.SessionIndex = SearchResultIndex;
    if (EOS_SessionSearch_CopySearchResultByIndex(Sdk->SearchHandle, &CopyOptions, &Sdk->PendingSessionDetails) != EOS_EResult::EOS_Success ||
        Sdk->PendingSessionDetails == nullptr)
        return false;
    const std::string LocalName(LocalSessionName);
    EOS_Sessions_JoinSessionOptions JoinOptions{};
    JoinOptions.ApiVersion = EOS_SESSIONS_JOINSESSION_API_LATEST;
    JoinOptions.SessionName = LocalName.c_str();
    JoinOptions.SessionHandle = Sdk->PendingSessionDetails;
    JoinOptions.LocalUserId = Sdk->ProductUser;
    JoinOptions.bPresenceEnabled = EOS_FALSE;
    ActiveSessionName = LocalName;
    SessionPhase = EpicSessionPhase::Joining;
    RedactedDiagnostic = "eos=session_join_pending";
    EOS_Sessions_JoinSession(Sdk->Sessions, &JoinOptions, Sdk.get(), SdkState::OnSessionJoined);
    return true;
}

bool EpicOnlineTransport::LeaveMatchSession() noexcept
{
    if ((SessionPhase != EpicSessionPhase::Active && SessionPhase != EpicSessionPhase::Ready) ||
        ActiveSessionName.empty() || Sdk->Sessions == nullptr)
        return false;
    const bool WasReady = SessionPhase == EpicSessionPhase::Ready;
    SessionPhase = EpicSessionPhase::Leaving;
    if (WasReady)
    {
        EOS_Sessions_DestroySessionOptions DestroyOptions{};
        DestroyOptions.ApiVersion = EOS_SESSIONS_DESTROYSESSION_API_LATEST;
        DestroyOptions.SessionName = ActiveSessionName.c_str();
        RedactedDiagnostic = "eos=session_destroy_pending";
        EOS_Sessions_DestroySession(Sdk->Sessions, &DestroyOptions, Sdk.get(), SdkState::OnSessionDestroyed);
        return true;
    }
    EOS_Sessions_EndSessionOptions Options{};
    Options.ApiVersion = EOS_SESSIONS_ENDSESSION_API_LATEST;
    Options.SessionName = ActiveSessionName.c_str();
    RedactedDiagnostic = "eos=session_end_pending";
    EOS_Sessions_EndSession(Sdk->Sessions, &Options, Sdk.get(), SdkState::OnSessionEnded);
    return true;
}

void EpicOnlineTransport::ScrubSensitiveCredentials() noexcept
{
    if (Sdk && !Sdk->IdentityJwt.empty())
    {
        ZeroSensitiveBytes(std::span<char>(Sdk->IdentityJwt.data(), Sdk->IdentityJwt.size()));
        Sdk->IdentityJwt.clear();
    }
    for (std::string* Secret : {&Credentials.ClientSecret, &Credentials.EncryptionHexKey})
    {
        if (!Secret->empty())
        {
            ZeroSensitiveBytes(std::span<char>(Secret->data(), Secret->size()));
            Secret->clear();
        }
    }
    CredentialsValid = false;
}

bool EpicOnlineTransport::OpenChannel() noexcept
{
    if (SimulatedBridge != nullptr && CredentialsValid)
    {
        ChannelOpen = true;
        return true;
    }
    if (IdentityPhase != EpicIdentityPhase::Authenticated || Sdk->P2P == nullptr || Sdk->ProductUser == nullptr)
        return false;
    if (Sdk->PeerRequestNotification == EOS_INVALID_NOTIFICATIONID)
    {
        EOS_P2P_AddNotifyPeerConnectionRequestOptions NotifyOptions{};
        NotifyOptions.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
        NotifyOptions.LocalUserId = Sdk->ProductUser;
        NotifyOptions.SocketId = &Sdk->Socket;
        Sdk->PeerRequestNotification = EOS_P2P_AddNotifyPeerConnectionRequest(
            Sdk->P2P, &NotifyOptions, Sdk.get(), SdkState::OnPeerConnectionRequest);
        if (Sdk->PeerRequestNotification == EOS_INVALID_NOTIFICATIONID)
            return false;
    }
    ChannelOpen = true;
    return true;
}

void EpicOnlineTransport::CloseChannel() noexcept
{
    ChannelOpen = false;
    if (Sdk && Sdk->P2P && Sdk->PeerRequestNotification != EOS_INVALID_NOTIFICATIONID)
    {
        EOS_P2P_RemoveNotifyPeerConnectionRequest(Sdk->P2P, Sdk->PeerRequestNotification);
        Sdk->PeerRequestNotification = EOS_INVALID_NOTIFICATIONID;
    }
    if (Sdk && Sdk->P2P && Sdk->ProductUser)
    {
        EOS_P2P_CloseConnectionsOptions Options{};
        Options.ApiVersion = EOS_P2P_CLOSECONNECTIONS_API_LATEST;
        Options.LocalUserId = Sdk->ProductUser;
        Options.SocketId = &Sdk->Socket;
        (void)EOS_P2P_CloseConnections(Sdk->P2P, &Options);
    }
}

void EpicOnlineTransport::AdvanceChannel(double Δt) noexcept
{
    if (IdentityPhase == EpicIdentityPhase::ProductUserConsentRequired && Sdk->PendingContinuanceToken != nullptr && Δt > 0.0)
    {
        Sdk->ConsentElapsedSeconds += Δt;
        if (Sdk->ConsentElapsedSeconds >= ContinuanceApprovalTimeoutSeconds)
        {
            Sdk->PendingContinuanceToken = nullptr;
            IdentityPhase = EpicIdentityPhase::Failed;
            RedactedDiagnostic = "eos=product_user_consent_expired";
        }
    }
    if (Sdk && Sdk->Platform != nullptr)
        EOS_Platform_Tick(Sdk->Platform);
}

bool EpicOnlineTransport::TransmitDatagram(uint32_t TargetEndpointIdentifier, DeliveryChannel Channel, std::span<const uint8_t> WireBytes)
{
    if (!ChannelOpen || WireBytes.empty() || WireBytes.size() > EOS_P2P_MAX_PACKET_SIZE)
        return false;
    if (SimulatedBridge != nullptr)
        return SimulatedBridge->EnqueueFlightDatagram(LocalEndpoint, TargetEndpointIdentifier, Channel, WireBytes);
    if (Sdk->P2P == nullptr || Sdk->ProductUser == nullptr)
        return false;
    const auto Peer = Sdk->Peers.find(TargetEndpointIdentifier);
    if (Peer == Sdk->Peers.end())
        return false;
    EOS_P2P_SendPacketOptions Options{};
    Options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    Options.LocalUserId = Sdk->ProductUser;
    Options.RemoteUserId = Peer->second;
    Options.SocketId = &Sdk->Socket;
    Options.Channel = static_cast<uint8_t>(Channel);
    Options.DataLengthBytes = static_cast<uint32_t>(WireBytes.size());
    Options.Data = WireBytes.data();
    Options.bAllowDelayedDelivery = EOS_TRUE;
    Options.bDisableAutoAcceptConnection = EOS_FALSE;
    Options.Reliability = Channel == DeliveryChannel::UnreliableSequenced
        ? EOS_EPacketReliability::EOS_PR_UnreliableUnordered
        : (Channel == DeliveryChannel::ReliableOrdered
            ? EOS_EPacketReliability::EOS_PR_ReliableOrdered
            : EOS_EPacketReliability::EOS_PR_ReliableUnordered);
    return EOS_P2P_SendPacket(Sdk->P2P, &Options) == EOS_EResult::EOS_Success;
}

bool EpicOnlineTransport::ReceiveDatagram(PacketDatagram& OutDatagram)
{
    if (!ChannelOpen)
        return false;
    if (SimulatedBridge != nullptr)
        return SimulatedBridge->PopArrivedDatagram(LocalEndpoint, OutDatagram);
    if (Sdk->P2P == nullptr || Sdk->ProductUser == nullptr)
        return false;
    EOS_P2P_GetNextReceivedPacketSizeOptions SizeOptions{};
    SizeOptions.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    SizeOptions.LocalUserId = Sdk->ProductUser;
    uint32_t PacketSize = 0u;
    if (EOS_P2P_GetNextReceivedPacketSize(Sdk->P2P, &SizeOptions, &PacketSize) != EOS_EResult::EOS_Success ||
        PacketSize == 0u || PacketSize > EOS_P2P_MAX_PACKET_SIZE)
        return false;
    std::vector<uint8_t> Bytes(PacketSize);
    EOS_P2P_ReceivePacketOptions ReceiveOptions{};
    ReceiveOptions.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    ReceiveOptions.LocalUserId = Sdk->ProductUser;
    ReceiveOptions.MaxDataSizeBytes = PacketSize;
    EOS_ProductUserId RemoteUser = nullptr;
    EOS_P2P_SocketId RemoteSocket{};
    uint8_t ReceivedChannel = 0u;
    uint32_t BytesWritten = PacketSize;
    if (EOS_P2P_ReceivePacket(Sdk->P2P, &ReceiveOptions, &RemoteUser, &RemoteSocket, &ReceivedChannel,
                              Bytes.data(), &BytesWritten) != EOS_EResult::EOS_Success || BytesWritten == 0u)
        return false;
    const auto Peer = std::find_if(Sdk->Peers.begin(), Sdk->Peers.end(), [RemoteUser](const auto& Entry) { return Entry.second == RemoteUser; });
    if (Peer == Sdk->Peers.end() || ReceivedChannel > static_cast<uint8_t>(DeliveryChannel::ReliableOrdered))
        return false;
    Bytes.resize(BytesWritten);
    OutDatagram.PeerEndpointIdentifier = Peer->first;
    OutDatagram.Channel = static_cast<DeliveryChannel>(ReceivedChannel);
    OutDatagram.WireBytes = std::move(Bytes);
    return true;
}

bool EpicOnlineTransport::QueryChannelReady() const noexcept
{
    return CredentialsValid && (SimulatedBridge != nullptr ||
        (IdentityPhase == EpicIdentityPhase::Authenticated && Sdk && Sdk->P2P != nullptr));
}

} // namespace Frontier::NetworkingSpace
