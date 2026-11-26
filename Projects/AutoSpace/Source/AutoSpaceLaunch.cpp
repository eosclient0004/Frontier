/*============================================================================================================================================
                                                           AUTOSPACELAUNCH.CPP
============================================================================================================================================*/
// 🧩 Standalone executable entry point for project AutoSpace (single-level policy-driven 3 Studio Lobbies)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Gameplay/AutoSpaceLifecycle.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

int main(int ArgumentCount, char** ArgumentVector)
{
    uint32_t                 TickBudget       = 60u;
    bool                     HeadlessMode     = true;
    bool                     VerifyAllLobbies = false;
    AutoSpace::StudioLobbyId SelectedLobby    = AutoSpace::StudioLobbyId::StudioLobby1Cyclorama;
#ifdef AUTOSPACE_PROJECT_ROOT_DIR
    std::filesystem::path    ProjectRoot      = std::filesystem::path(AUTOSPACE_PROJECT_ROOT_DIR);
#else
    std::filesystem::path    ProjectRoot      = std::filesystem::path("Projects") / "AutoSpace";
#endif

    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const char* Arg = ArgumentVector[Index];
        if (std::strcmp(Arg, "--verify") == 0)
        {
            TickBudget       = 60u;
            HeadlessMode     = true;
            VerifyAllLobbies = true;
        }
        else if (std::strcmp(Arg, "--headless") == 0)
        {
            HeadlessMode = true;
        }
        else if (std::strncmp(Arg, "--ticks=", 8) == 0)
        {
            const long Parsed = std::strtol(Arg + 8, nullptr, 10);
            if (Parsed > 0L && Parsed <= 100000L)
            {
                TickBudget = static_cast<uint32_t>(Parsed);
            }
        }
        else if (std::strncmp(Arg, "--project-root=", 15) == 0)
        {
            ProjectRoot = std::filesystem::path(Arg + 15);
        }
        else if (std::strncmp(Arg, "--lobby=", 8) == 0)
        {
            (void)AutoSpace::LobbyPolicyEngine::ParseLobbyId(Arg + 8, SelectedLobby);
        }
    }

    AutoSpace::AutoSpaceLifecycle Lifecycle {};
    std::string                   Diagnostic {};
    if (!Lifecycle.Initialize(ProjectRoot, HeadlessMode, Diagnostic, SelectedLobby))
    {
        std::fprintf(stderr, "[AutoSpace] Initialization failed: %s\n", Diagnostic.c_str());
        return 1;
    }

    if (!Lifecycle.VerifyProjectContracts(Diagnostic))
    {
        std::fprintf(stderr, "[AutoSpace] Contract validation failed: %s\n", Diagnostic.c_str());
        Lifecycle.Finalize();
        return 2;
    }

    if (VerifyAllLobbies)
    {
        const AutoSpace::StudioLobbyId CheckOrder[3] = {
            AutoSpace::StudioLobbyId::StudioLobby2ForestStage,
            AutoSpace::StudioLobbyId::StudioLobby3RacingGarage,
            SelectedLobby,
        };
        for (const auto L : CheckOrder)
        {
            if (!Lifecycle.SelectStudioLobby(L, Diagnostic) || !Lifecycle.VerifyProjectContracts(Diagnostic))
            {
                const auto Slug = AutoSpace::LobbyPolicyEngine::QuerySlug(L);
                std::fprintf(stderr, "[AutoSpace] Lobby switch validation failed (%.*s): %s\n",
                    static_cast<int>(Slug.size()),
                    Slug.data(),
                    Diagnostic.c_str());
                Lifecycle.Finalize();
                return 2;
            }
            const auto Tel  = Lifecycle.InspectLobbyLifecycle().InspectTelemetry();
            const auto Slug = AutoSpace::LobbyPolicyEngine::QuerySlug(L);
            std::printf(
                "[AutoSpace] Verified single-level lobby '%.*s': vehicles=%u surfaces=%u areaLights=%u iesLights=%u cameras=%u particles=%u (%u active) tris=%u outlinerRows=%u\n",
                static_cast<int>(Slug.size()),
                Slug.data(),
                Tel.VehicleCount,
                Tel.SurfaceInstanceCount,
                Tel.AreaLightCount,
                Tel.PhotometricLightCount,
                Tel.CameraRigCount,
                Tel.ParticleCloudCount,
                Tel.SimulatedParticleCount,
                Tel.TotalMeshTriangles,
                Tel.OutlinerRowCount);
        }
    }

    const uint32_t CompletedTicks = Lifecycle.RunBoundedTicks(TickBudget, 1.0 / 60.0);

    const auto Metrics    = Lifecycle.InspectMetrics();
    const auto ActiveSlug = AutoSpace::LobbyPolicyEngine::QuerySlug(Metrics.LobbyTelemetry.ActiveLobby);
    std::printf(
        "[AutoSpace] Completed %u outer ticks (%llu fixed steps, %.3f s, lobby=%.*s, outlinerRows=%u, bootDisk=%s)\n",
        CompletedTicks,
        static_cast<unsigned long long>(Metrics.TotalFixedStepsExecuted),
        Metrics.SimulatedElapsedSeconds,
        static_cast<int>(ActiveSlug.size()),
        ActiveSlug.data(),
        Metrics.LobbyTelemetry.OutlinerRowCount,
        Metrics.BootLoadedFromDisk ? "true" : "false");

    Lifecycle.Finalize();
    return 0;
}
