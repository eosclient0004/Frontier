/*============================================================================================================================================
                                                             LOBBYPOLICY.H
============================================================================================================================================*/
// 🧩 Declarative LobbyPolicy selecting which Studio Lobby populates MainWorld.WorldSpace (single level, zero level switching)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <string_view>

namespace AutoSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class StudioLobbyId : uint8_t
{
    StudioLobby1Cyclorama    = 1u, // [-] - Lobby 1 · Cyclorama (photographic infinity cove, 3 vehicles, 5 lighting rigs)
    StudioLobby2ForestStage  = 2u, // [-] - Lobby 2 · Forest Stage (LED backdrop, 4 vehicles, wind fan + engine dust, rocks/plants left blank)
    StudioLobby3RacingGarage = 3u, // [-] - Lobby 3 · Racing Garage (3D track & grandstands, pit garage, car lift, tools, smoke & dust)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LobbyPolicy
{
    StudioLobbyId SelectedLobby              = StudioLobbyId::StudioLobby1Cyclorama; // [-]   - Active lobby loaded into MainWorld
    bool          EnableEngineParticles      = true;  // [-] - Load and simulate Frontier::ParticleSpace emitters
    bool          LeaveBotanicalAndRockBlank = true;  // [-] - Leave rocks, moss, wood, ferns, grass, and leaves blank
    bool          EnableStudioLightingRigs   = true;  // [-] - Mount physical light stands/modifiers & Area/IES luminaires
    bool          EnableCameraRigs           = true;  // [-] - Mount procedural cinema tripods, bodies & scene cameras
    bool          EnableCrewProps            = true;  // [-] - Mount tether workstation, Pelican cases, garage props & cables
    bool          EnableConfiguratorOrbit    = true;  // [-] - Advance configurator camera orbit when ticking
    bool          EnableHeroVehiclePhysics   = false; // [-] - Bind live Jolt VehiclePhysicsRig (3-lane fan trace + Constraints) to Hero Vehicle
    uint32_t      ActiveCameraPresetIndex    = 0u;    // [idx] - Active configurator camera preset [0..5]
};

using StudioLobbyPolicy = LobbyPolicy;

//------------------------------------------------------------------------------------------------------------------------
//                                                     LOBBY POLICY ENGINE
//------------------------------------------------------------------------------------------------------------------------

class LobbyPolicyEngine
{
public:
    [[nodiscard]] static LobbyPolicy      BuildDefault(StudioLobbyId Lobby = StudioLobbyId::StudioLobby1Cyclorama) noexcept;
    static void                           ClampToSafeBounds(LobbyPolicy& InOutPolicy) noexcept;
    [[nodiscard]] static std::string_view QuerySlug(StudioLobbyId Lobby) noexcept;
    [[nodiscard]] static std::string_view QueryLabel(StudioLobbyId Lobby) noexcept;
    [[nodiscard]] static bool             ParseLobbyId(std::string_view Text, StudioLobbyId& OutLobby) noexcept;
};

} // namespace AutoSpace
