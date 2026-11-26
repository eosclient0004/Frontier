/*============================================================================================================================================
                                                            LOBBYPOLICY.CPP
============================================================================================================================================*/
// 🧩 Declarative LobbyPolicy engine for single-level Studio Lobby selection inside AutoSpace

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Policies/LobbyPolicy.h"

#include <algorithm>

namespace AutoSpace
{

LobbyPolicy LobbyPolicyEngine::BuildDefault(StudioLobbyId Lobby) noexcept
{
    LobbyPolicy Policy {};
    Policy.SelectedLobby              = Lobby;
    Policy.EnableEngineParticles      = true;
    Policy.LeaveBotanicalAndRockBlank = true;
    Policy.EnableStudioLightingRigs   = true;
    Policy.EnableCameraRigs           = true;
    Policy.EnableCrewProps            = true;
    Policy.EnableConfiguratorOrbit    = true;
    Policy.ActiveCameraPresetIndex    = 0u;
    ClampToSafeBounds(Policy);
    return Policy;
}

void LobbyPolicyEngine::ClampToSafeBounds(LobbyPolicy& InOutPolicy) noexcept
{
    const uint8_t Raw = static_cast<uint8_t>(InOutPolicy.SelectedLobby);
    if (Raw < 1u || Raw > 3u)
    {
        InOutPolicy.SelectedLobby = StudioLobbyId::StudioLobby1Cyclorama;
    }
    InOutPolicy.ActiveCameraPresetIndex = std::min(InOutPolicy.ActiveCameraPresetIndex, 5u);
    // Standing constraint: rocks, plants, and leaves stay blank across single-level lobbies
    InOutPolicy.LeaveBotanicalAndRockBlank = true;
}

std::string_view LobbyPolicyEngine::QuerySlug(StudioLobbyId Lobby) noexcept
{
    switch (Lobby)
    {
        case StudioLobbyId::StudioLobby2ForestStage:  return "studio-lobby-2";
        case StudioLobbyId::StudioLobby3RacingGarage: return "studio-lobby-3";
        case StudioLobbyId::StudioLobby1Cyclorama:
        default:                                      return "studio-lobby";
    }
}

std::string_view LobbyPolicyEngine::QueryLabel(StudioLobbyId Lobby) noexcept
{
    switch (Lobby)
    {
        case StudioLobbyId::StudioLobby2ForestStage:  return "Lobby 2 · Forest Stage";
        case StudioLobbyId::StudioLobby3RacingGarage: return "Lobby 3 · Racing Garage";
        case StudioLobbyId::StudioLobby1Cyclorama:
        default:                                      return "Lobby 1 · Cyclorama";
    }
}

bool LobbyPolicyEngine::ParseLobbyId(std::string_view Text, StudioLobbyId& OutLobby) noexcept
{
    if (Text == "1" || Text == "lobby1" || Text == "lobby-1" || Text == "studio-lobby" || Text == "cyclorama")
    {
        OutLobby = StudioLobbyId::StudioLobby1Cyclorama;
        return true;
    }
    if (Text == "2" || Text == "lobby2" || Text == "lobby-2" || Text == "studio-lobby-2" || Text == "forest")
    {
        OutLobby = StudioLobbyId::StudioLobby2ForestStage;
        return true;
    }
    if (Text == "3" || Text == "lobby3" || Text == "lobby-3" || Text == "studio-lobby-3" || Text == "garage")
    {
        OutLobby = StudioLobbyId::StudioLobby3RacingGarage;
        return true;
    }
    return false;
}

} // namespace AutoSpace
