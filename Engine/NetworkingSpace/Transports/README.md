# 🧩 `Engine/NetworkingSpace/Transports/`

> **TL;DR**: Epic Online Services Auth/Connect identity, P2P datagrams, and Sessions fallback behind an opt-in SDK adapter, with an offline no-SDK stub. Photon remains an external adapter seam pending its SDK.

---

## Files

| File | Purpose |
| :--- | :--- |
| `EpicOnlineTransport.cpp` | EOS SDK-backed Account Portal Auth, Connect product-user consent, P2P packet send/receive, and Sessions create/start/search/join/end/destroy operations; compiled only when `FRONTIER_ENABLE_EOS_SDK=ON` |
| `EpicOnlineTransport.h` | SDK-neutral identity/session state API, credential inputs, explicit product-user consent, endpoint-to-ProductUserId mapping, and packet transport |
| `EpicStubTransport.cpp` | Open-source no-SDK implementation; it reports cloud login/session APIs unavailable and supports only the explicit loopback test bridge |
| `LoopbackTransport.cpp` | Deterministic SplitMix64 intra-application packet switch and zero-socket LoopbackTransport implementation |
| `LoopbackTransport.h` | Deterministic intra-application packet switch with SplitMix64 latency, jitter, reordering, and loss simulation for offline/CI runs |
| `PhotonRealtimeTransport.cpp` | SDK-gated Photon translation unit; currently validates configuration and keeps a loopback seam, but live client, room, and event wiring remains unfinished |
| `PhotonRealtimeTransport.h` | SDK-neutral Photon transport interface, room/auth configuration, relay-region selection, and token-scrubbing contract |
| `PhotonStubTransport.cpp` | Open-source zero-proprietary-SDK Photon Realtime stub enforcing Epic JWT verification, token scrubbing, and failover routing |
| `SocketTransport.cpp` | Non-blocking BSD/Winsock2 UDP socket, RFC 5389 STUN reflexive discovery, RFC 8439 ChaCha20-Poly1305 AEAD, and zero-key relay |
| `SocketTransport.h` | Sovereign non-blocking OS UDP transport with RFC 5389 STUN NAT discovery, RFC 8439 ChaCha20-Poly1305 AEAD, and stateless relay |
| `TransportInterface.h` | Zero-vtable C++20 PacketTransportChannel concept and canonical wire datagram carrier across all four Frontier transports |

## Namespaces & Primary Types

- **Namespaces**: `Frontier::NetworkingSpace`
- **Primary Types**: `EpicSessionCredentials`, `LoopbackImpairmentProfile`, `PhotonRoomConfiguration`, `IpSocketAddress`, `StatelessRelayRouteEntry`, `PacketDatagram`

## Usage

```cpp
#include "Engine/NetworkingSpace/Transports/EpicOnlineTransport.h"
#include "Engine/NetworkingSpace/Transports/LoopbackTransport.h"
#include "Engine/NetworkingSpace/Transports/PhotonRealtimeTransport.h"

using namespace Frontier::NetworkingSpace;
EpicSessionCredentials Instance {};
```

## EOS Setup and Lifecycle

- EOS is opt-in and proprietary. Keep the authorized SDK outside Git, then configure with `-DFRONTIER_ENABLE_EOS_SDK=ON -DFRONTIER_EOS_SDK_ROOT=<local EOS SDK root>`; the root must contain `Include/`, `Lib/`, and `Bin/` (platform-specific runtime names are checked by CMake).
- Photon Realtime Core is also opt-in and proprietary. Keep the authorized package outside Git and configure with `-DFRONTIER_ENABLE_PHOTON_SDK=ON -DFRONTIER_PHOTON_SDK_ROOT=<package root containing RealtimeCore/>`; CMake resolves `RealtimeCore::Matchmaking` and links the selected platform variant. The 6.0.0-preview-347 package supplied in Drive has not been staged into this checkout, so this option cannot yet be configured or built here.
- Configure `EpicSessionCredentials` from a secure runtime configuration source, then call `InitializeSdk()` and `BeginEpicAccountPortalLogin()`. Real SDK initialization requires both EOS client ID and client secret. The optional attached loopback bridge is a deterministic test-only seam; it never establishes an EOS identity or cloud session. `NetworkingExtension::Tick()` drives EOS callbacks even while another transport is selected.
- The required profile/friends/presence/country scopes are always requested. A Connect `EOS_InvalidUser` response pauses at `ProductUserConsentRequired`; the caller must obtain an explicit user decision and invoke `ResolveProductUserConsent(true|false)` within the policy's 180-second window. There is no silent profile creation.
- Once authenticated, register each remote endpoint's EOS Product User ID with `RegisterPeer()`, then activate routing. P2P accepts connection requests only from registered Product User IDs and enforces EOS's 1170-byte datagram limit.
- Sessions support public host creation, start, bucket-filtered search, join-by-result, end, and destroy. Poll `QuerySessionPhase()` and `QuerySessionSearchResultCount()`; call `LeaveMatchSession()` and keep ticking until the phase returns to `None` before signing out or destroying the transport.
- Client secrets and identity JWTs are runtime-only. They are not logged or included in project TOML; the client secret is scrubbed immediately after platform creation, and JWT copies returned for Photon must be scrubbed by the caller.

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- The no-SDK stub translation unit was compiled in this workspace. The proprietary SDK-enabled build, CTest suites, and live EOS staging test were not run because the SDK binaries have not been staged locally and CMake is not installed in this workspace.
