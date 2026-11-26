/*============================================================================================================================================
                                                    OUTLINERWORLDLIFECYCLEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Strict 4-Gate Validation of World <-> Outliner <-> Inspector RAII Constructor/Destructor Invariants:
//    Gate 1: Loading Projects/AutoSpace/Content/Worlds/MainWorld.WorldSpace produces ZERO phantom entries
//            (NO Moon, NO Stars, NO Volumetric Clouds, NO Local Cloud, NO empty Showcase folder) in Outliner, WorldRegister, or Inspector.
//    Gate 2: True RAII Constructor/Destructor lifecycle — adding an environment object (Moon/Clouds/Stars) or CategoryInstanceHandle
//            constructs its WorldRegister slot + WorldScene feature + Outliner row; destroying it reclaims WorldRegister, disables
//            WorldScene, deletes Outliner row, AND erases InspectorInstanceRegister atomically.
//    Gate 3: Phantom-pruning & stale Inspector protection — pre-existing phantom Outliner rows (env:moon, env:clouds, env:stars,
//            env:local-cloud) and orphaned Inspector entries are 100% purged by AdoptInitialSceneState + PruneOrphanedEntries.
//    Gate 4: Multi-Moon add/delete/undo + external Outliner deletion reconciliation (ReconcileDeletedOutlinerSlots).

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"
#include "Engine/Editor/WorldEnvironmentRegistry.h"
#include "Engine/Editor/WorldInstanceEditorBridge.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/World/CategoryInstances.h"
#include "Engine/World/WorldInstanceRegister.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

using namespace Frontier;

namespace
{

int gFailures = 0;

bool Check(bool Condition, const char* Message)
{
    if (!Condition)
    {
        std::cerr << "[FAIL] " << Message << "\n";
        ++gFailures;
        return false;
    }
    return true;
}

uint32_t CountOutlinerTag(const OutlinerPanel& Outliner, std::string_view Tag)
{
    uint32_t Count = 0u;
    for (const OutlinerRecordEntry& Entry : Outliner.QueryEntries())
    {
        if (Entry.DomainTag == Tag)
        {
            ++Count;
        }
    }
    return Count;
}

bool HasOutlinerTitle(const OutlinerPanel& Outliner, std::string_view Title)
{
    for (const OutlinerRecordEntry& Entry : Outliner.QueryEntries())
    {
        if (Entry.Title == Title)
        {
            return true;
        }
    }
    return false;
}

std::filesystem::path LocateRepoRoot()
{
    std::error_code Ec{};
#if defined(FRONTIER_REPO_ROOT_DIR)
    const std::filesystem::path Defined(FRONTIER_REPO_ROOT_DIR);
    if (std::filesystem::exists(Defined / "Projects" / "AutoSpace" / "Content" / "Worlds" / "MainWorld.WorldSpace", Ec))
    {
        return Defined;
    }
#endif
    std::filesystem::path Cur = std::filesystem::current_path();
    for (int Depth = 0; Depth < 6; ++Depth)
    {
        if (std::filesystem::exists(Cur / "Projects" / "AutoSpace" / "Content" / "Worlds" / "MainWorld.WorldSpace", Ec))
        {
            return Cur;
        }
        if (!Cur.has_parent_path()) break;
        Cur = Cur.parent_path();
    }
    return std::filesystem::current_path();
}

//------------------------------------------------------------------------------------------------------------------------
// GATE 1: Loading Projects/AutoSpace/Content/Worlds/MainWorld.WorldSpace has ZERO phantom Moon/Stars/Clouds/Showcase!
//------------------------------------------------------------------------------------------------------------------------
void ValidateGate1_AutoSpaceZeroPhantoms()
{
    const std::filesystem::path RepoRoot = LocateRepoRoot();
    const std::filesystem::path AutoSpaceWorld =
        RepoRoot / "Projects" / "AutoSpace" / "Content" / "Worlds" / "MainWorld.WorldSpace";

    GeometricRaster::ShowcaseWorldSceneBundle Bundle{};
    std::string Diag{};
    const bool Loaded = GeometricRaster::LoadWorldSceneFromSpaceFile(
        AutoSpaceWorld, RepoRoot, Bundle, 320u, 180u, Diag);
    Check(Loaded, "Gate 1: LoadWorldSceneFromSpaceFile succeeds on Projects/AutoSpace/Content/Worlds/MainWorld.WorldSpace");

    OutlinerPanel                 Outliner{};
    ViewportDisplaySettings       Display{};
    World::WorldInstanceRegister  WorldRegister{};
    Editor::WorldInstanceEditorBridge Bridge{};
    WorldEnvironmentRegistry      EnvRegistry{};
    InspectorInstanceRegister     Inspectors{};

    GeometricRaster::RegisterShowcaseGalleryInOutliner(Outliner, Bundle, false);

    const auto EraseInspector = [&](OutlinerSlotToken Tok) { Inspectors.EraseEntry(Tok); };
    Bridge.BindOutliner(WorldRegister, Outliner, EraseInspector);
    EnvRegistry.BindWorldRegister(&WorldRegister);
    EnvRegistry.BindRemovalCallback(EraseInspector);
    EnvRegistry.AdoptInitialSceneState(Outliner, Bundle.Scene, &Display);
    Bridge.AdoptWorldSceneIntoRegister(WorldRegister, Bundle.Scene);
    (void)Inspectors.PruneOrphanedEntries(Outliner);
    Inspectors.CombineIntoScene(Outliner, Bundle.Scene, &Display);

    Check(CountOutlinerTag(Outliner, "env:atmosphere") == 1u, "Gate 1: AutoSpace Outliner has 1 Atmosphere (enabled in ClearSkyDaylight)");
    Check(CountOutlinerTag(Outliner, "env:sun") == 1u,        "Gate 1: AutoSpace Outliner has 1 Sun (enabled in ClearMorningSun)");
    Check(CountOutlinerTag(Outliner, "env:moon") == 0u,       "Gate 1: AutoSpace Outliner has ZERO phantom Moon entries");
    Check(CountOutlinerTag(Outliner, "env:stars") == 0u,      "Gate 1: AutoSpace Outliner has ZERO phantom Stars entries");
    Check(CountOutlinerTag(Outliner, "env:clouds") == 0u,     "Gate 1: AutoSpace Outliner has ZERO phantom Volumetric Clouds entries");
    Check(CountOutlinerTag(Outliner, "env:local-cloud") == 0u,"Gate 1: AutoSpace Outliner has ZERO phantom Local Cloud entries");
    Check(!HasOutlinerTitle(Outliner, "Showcase"),            "Gate 1: AutoSpace Outliner has ZERO empty Showcase folder");
    Check(HasOutlinerTitle(Outliner, "Editor Camera"),        "Gate 1: AutoSpace Outliner keeps permanent Editor Camera");

    Check(WorldRegister.AccessAtmosphereRegister().QueryActiveCount() == 1u, "Gate 1: WorldRegister has exactly 1 AtmosphereInstance");
    Check(WorldRegister.AccessSolarRegister().QueryActiveCount() == 1u,      "Gate 1: WorldRegister has exactly 1 SolarInstance");
    Check(WorldRegister.AccessLunarRegister().QueryActiveCount() == 0u,      "Gate 1: WorldRegister has 0 LunarInstances");
    Check(WorldRegister.AccessCloudRegister().QueryActiveCount() == 0u,      "Gate 1: WorldRegister has 0 CloudInstances");

    for (uint32_t I = 0u; I < 6u; ++I)
    {
        Check(!Bundle.Scene.Atmosphere.Moons[I].Visible, "Gate 1: AutoSpace WorldScene has Moons[i].Visible == false");
    }
    Check(!Bundle.Scene.Atmosphere.Stars.StarfieldEnabled,   "Gate 1: AutoSpace WorldScene has Stars disabled");
    Check(!Bundle.Scene.Atmosphere.Clouds.CloudsEnabled,     "Gate 1: AutoSpace WorldScene has Clouds disabled");
    Check(!Bundle.Scene.Atmosphere.LocalCloud.LocalEnabled,  "Gate 1: AutoSpace WorldScene has LocalCloud disabled");
    Check(Inspectors.QueryTrackedEntryCount() == 0u,         "Gate 1: AutoSpace InspectorInstanceRegister starts with 0 ghost entries");

    EnvRegistry.Reset();
    Bridge.Unbind();
    std::cout << "[Gate 1/4] AutoSpace MainWorld.WorldSpace zero-phantom check : PASS\n";
}

//------------------------------------------------------------------------------------------------------------------------
// GATE 2: True RAII Constructor/Destructor lifecycle across WorldRegister + WorldScene + Outliner + Inspector!
//------------------------------------------------------------------------------------------------------------------------
void ValidateGate2_RaiiConstructorDestructorLifecycle()
{
    GeometricRaster::ShowcaseWorldSceneBundle Bundle{};
    for (auto& M : Bundle.Scene.Atmosphere.Moons) M.Visible = false;
    Bundle.Scene.Atmosphere.Medium.AtmosphereEnabled = true;
    Bundle.Scene.Atmosphere.Solar.SolarEnabled       = true;
    Bundle.Scene.Atmosphere.Clouds.CloudsEnabled     = false;
    Bundle.Scene.Atmosphere.Stars.StarfieldEnabled   = false;

    OutlinerPanel                     Outliner{};
    ViewportDisplaySettings           Display{};
    World::WorldInstanceRegister      WorldRegister{};
    Editor::WorldInstanceEditorBridge Bridge{};
    WorldEnvironmentRegistry          EnvRegistry{};
    InspectorInstanceRegister         Inspectors{};

    const auto EraseInspector = [&](OutlinerSlotToken Tok) { Inspectors.EraseEntry(Tok); };
    Bridge.BindOutliner(WorldRegister, Outliner, EraseInspector);
    EnvRegistry.BindWorldRegister(&WorldRegister);
    EnvRegistry.BindRemovalCallback(EraseInspector);
    EnvRegistry.AdoptInitialSceneState(Outliner, Bundle.Scene, &Display);

    // 2A. Construct a Moon -> WorldRegister + WorldScene + Outliner + Inspector all live!
    const OutlinerSlotToken MoonTok = EnvRegistry.RegisterFromRegistryKey("env:moon", "Moon", Outliner, Bundle.Scene, &Display);
    Check(MoonTok.Valid(),                                           "Gate 2A: Construct Moon returns valid Outliner token");
    Check(CountOutlinerTag(Outliner, "env:moon") == 1u,              "Gate 2A: Outliner has 1 Moon row");
    Check(WorldRegister.AccessLunarRegister().QueryActiveCount() == 1u, "Gate 2A: LunarInstance constructor registered in WorldRegister");
    Check(Bundle.Scene.Atmosphere.Moons[0].Visible,                  "Gate 2A: Scene.Atmosphere.Moons[0].Visible is true");

    // Touch Inspector for that Moon
    auto& MoonInsp = Inspectors.AccessLunar(MoonTok, Bundle.Scene, 0u);
    MoonInsp.Moon.BrightnessMultiplier = 2.1f;
    Check(Inspectors.QueryTrackedEntryCount() == 1u,                 "Gate 2A: Inspector tracks 1 Moon entry");

    // 2B. Remove Moon -> RAII destructor reclaims WorldRegister, disables Scene, deletes Outliner row, AND erases Inspector!
    const OutlinerSlotToken ToRemove[1] = { MoonTok };
    const uint32_t Removed = EnvRegistry.UnregisterSlots(ToRemove, Outliner, Bundle.Scene, &Display);
    Check(Removed == 1u,                                             "Gate 2B: UnregisterSlots removed Moon");
    Check(CountOutlinerTag(Outliner, "env:moon") == 0u,              "Gate 2B: Outliner has 0 Moon rows after removal");
    Check(WorldRegister.AccessLunarRegister().QueryActiveCount() == 0u, "Gate 2B: RAII ~CategoryInstanceHandle() reclaimed WorldRegister LunarInstance");
    Check(!Bundle.Scene.Atmosphere.Moons[0].Visible,                 "Gate 2B: Scene.Atmosphere.Moons[0].Visible is false after removal");
    Check(Inspectors.QueryTrackedEntryCount() == 0u,                 "Gate 2B: Inspector entry automatically erased on removal");

    // 2C. Direct RAII scope Constructor / Destructor proof (Editor constructor -> Outliner + Inspector -> ~Destructor wipes all!)
    OutlinerSlotToken ScopedSlot{};
    {
        World::LuminaireInstanceParameters LampParams{};
        LampParams.VisibilityEnabled = true;
        World::LuminaireInstance ScopedLamp(WorldRegister, "Studio Key Lamp", LampParams);
        ScopedSlot = Bridge.QueryOutlinerSlot(ScopedLamp.QueryToken());
        Check(ScopedSlot.Valid() && Outliner.FindEntry(ScopedSlot) != nullptr,
              "Gate 2C: Editor LuminaireInstance constructor adds Outliner row automatically");
        Check(WorldRegister.AccessLuminaireRegister().QueryActiveCount() == 1u,
              "Gate 2C: WorldRegister holds 1 LuminaireInstance");
        (void)Inspectors.AccessPointLight(ScopedSlot, Bundle.Scene);
        Check(Inspectors.QueryTrackedEntryCount() == 1u,
              "Gate 2C: Inspector tracks ScopedLamp");
    } // <--- ~LuminaireInstance() runs HERE!
    Check(WorldRegister.AccessLuminaireRegister().QueryActiveCount() == 0u,
          "Gate 2C: ~LuminaireInstance() destructor reclaimed WorldRegister slot");
    Check(Outliner.FindEntry(ScopedSlot) == nullptr,
          "Gate 2C: ~LuminaireInstance() destructor deleted Outliner row automatically");
    Check(Inspectors.QueryTrackedEntryCount() == 0u,
          "Gate 2C: ~LuminaireInstance() destructor erased Inspector entry automatically");

    EnvRegistry.Reset();
    Bridge.Unbind();
    std::cout << "[Gate 2/4] RAII Constructor/Destructor World<->Outliner<->Inspector : PASS\n";
}

//------------------------------------------------------------------------------------------------------------------------
// GATE 3: Phantom-pruning & stale Inspector protection
//------------------------------------------------------------------------------------------------------------------------
void ValidateGate3_PhantomAndStaleInspectorPruning()
{
    GeometricRaster::WorldSceneSpecification Scene{};
    Scene.Atmosphere.Medium.AtmosphereEnabled = true;
    Scene.Atmosphere.Solar.SolarEnabled       = true;
    Scene.Atmosphere.Clouds.CloudsEnabled     = false;
    Scene.Atmosphere.LocalCloud.LocalEnabled  = false;
    Scene.Atmosphere.Stars.StarfieldEnabled   = false;
    for (auto& M : Scene.Atmosphere.Moons) M.Visible = false;

    OutlinerPanel             Outliner{};
    ViewportDisplaySettings   Display{};
    WorldEnvironmentRegistry  EnvRegistry{};
    InspectorInstanceRegister Inspectors{};

    // Deliberately inject phantom Outliner rows + Inspector entries that do NOT exist in Scene!
    auto InjectPhantom = [&](const char* Title, const char* Tag) -> OutlinerSlotToken
    {
        OutlinerRecordEntry Rec{};
        Rec.Title             = Title;
        Rec.DomainTag         = Tag;
        Rec.Category          = ConstructSlotCategory::LayerGroup;
        Rec.VisibilityEnabled = false;
        return Outliner.ConstructDirectEntry(std::move(Rec));
    };
    const OutlinerSlotToken PhMoon   = InjectPhantom("Moon", "env:moon");
    const OutlinerSlotToken PhStars  = InjectPhantom("Stars", "env:stars");
    const OutlinerSlotToken PhClouds = InjectPhantom("Volumetric Clouds", "env:clouds");
    const OutlinerSlotToken PhLocal  = InjectPhantom("Local Cloud", "env:local-cloud");
    (void)Inspectors.AccessLunar(PhMoon, Scene, 0u);
    (void)Inspectors.AccessStarfield(PhStars, Scene);
    (void)Inspectors.AccessCloud(PhClouds, Scene);
    (void)Inspectors.AccessLocalCloud(PhLocal, Scene);

    Check(Inspectors.QueryTrackedEntryCount() == 4u, "Gate 3: 4 phantom Inspector entries seeded before adoption");

    EnvRegistry.BindRemovalCallback([&](OutlinerSlotToken Tok) { Inspectors.EraseEntry(Tok); });
    EnvRegistry.AdoptInitialSceneState(Outliner, Scene, &Display);
    (void)Inspectors.PruneOrphanedEntries(Outliner);

    Check(CountOutlinerTag(Outliner, "env:moon") == 0u,       "Gate 3: Phantom Moon pruned from Outliner");
    Check(CountOutlinerTag(Outliner, "env:stars") == 0u,      "Gate 3: Phantom Stars pruned from Outliner");
    Check(CountOutlinerTag(Outliner, "env:clouds") == 0u,     "Gate 3: Phantom Clouds pruned from Outliner");
    Check(CountOutlinerTag(Outliner, "env:local-cloud") == 0u,"Gate 3: Phantom Local Cloud pruned from Outliner");
    Check(Inspectors.QueryTrackedEntryCount() == 0u,          "Gate 3: All 4 phantom Inspector entries purged cleanly");

    std::cout << "[Gate 3/4] Phantom Outliner & Stale Inspector pruning : PASS\n";
}

//------------------------------------------------------------------------------------------------------------------------
// GATE 4: Multi-Moon add/delete/undo + external Outliner delete reconciliation
//------------------------------------------------------------------------------------------------------------------------
void ValidateGate4_MultiMoonUndoAndExternalDeleteReconcile()
{
    GeometricRaster::WorldSceneSpecification Scene{};
    for (auto& M : Scene.Atmosphere.Moons) M.Visible = false;
    Scene.Atmosphere.Medium.AtmosphereEnabled = true;
    Scene.Atmosphere.Solar.SolarEnabled       = true;
    Scene.Atmosphere.Clouds.CloudsEnabled     = false;
    Scene.Atmosphere.Stars.StarfieldEnabled   = false;

    OutlinerPanel                     Outliner{};
    ViewportDisplaySettings           Display{};
    World::WorldInstanceRegister      WorldRegister{};
    Editor::WorldInstanceEditorBridge Bridge{};
    WorldEnvironmentRegistry          EnvRegistry{};
    InspectorInstanceRegister         Inspectors{};

    const auto EraseInspector = [&](OutlinerSlotToken Tok) { Inspectors.EraseEntry(Tok); };
    Bridge.BindOutliner(WorldRegister, Outliner, EraseInspector);
    EnvRegistry.BindWorldRegister(&WorldRegister);
    EnvRegistry.BindRemovalCallback(EraseInspector);
    EnvRegistry.AdoptInitialSceneState(Outliner, Scene, &Display);

    const OutlinerSlotToken M1 = EnvRegistry.RegisterFromRegistryKey("env:moon", "Moon",   Outliner, Scene, &Display);
    const OutlinerSlotToken M2 = EnvRegistry.RegisterFromRegistryKey("env:moon", "Moon 2", Outliner, Scene, &Display);
    const OutlinerSlotToken M3 = EnvRegistry.RegisterFromRegistryKey("env:moon", "Moon 3", Outliner, Scene, &Display);
    Check(M1.Valid() && M2.Valid() && M3.Valid(), "Gate 4: Registered 3 Moons");
    Check(WorldRegister.AccessLunarRegister().QueryActiveCount() == 3u, "Gate 4: WorldRegister has 3 LunarInstances");

    // Delete Moon 2 -> Undo -> delete all 3 via external Outliner.DeleteSlot + SyncToWorld!
    const OutlinerSlotToken DelM2[1] = { M2 };
    (void)EnvRegistry.UnregisterSlots(DelM2, Outliner, Scene, &Display);
    Check(WorldRegister.AccessLunarRegister().QueryActiveCount() == 2u, "Gate 4: Deleting Moon 2 leaves 2 LunarInstances");

    Check(EnvRegistry.UndoLastRemoval(Outliner, Scene, &Display), "Gate 4: UndoLastRemoval restores Moon 2");
    Check(WorldRegister.AccessLunarRegister().QueryActiveCount() == 3u, "Gate 4: WorldRegister restored to 3 LunarInstances");

    // External direct Outliner deletion (simulates any non-registry deletion path)
    std::vector<OutlinerSlotToken> AllMoons{};
    for (const auto& E : Outliner.QueryEntries())
    {
        if (E.DomainTag == "env:moon") AllMoons.push_back(E.Token);
    }
    for (const auto Tok : AllMoons)
    {
        (void)Inspectors.AccessLunar(Tok, Scene, 0u);
        (void)Outliner.DeleteSlot(Tok);
    }
    (void)EnvRegistry.SyncToWorld(Outliner, Scene, &Display);
    (void)Bridge.ReconcileDeletedOutlinerSlots();
    (void)Inspectors.PruneOrphanedEntries(Outliner);

    Check(WorldRegister.AccessLunarRegister().QueryActiveCount() == 0u, "Gate 4: External deletion reclaimed all 3 LunarInstances");
    Check(Inspectors.QueryTrackedEntryCount() == 0u,                    "Gate 4: External deletion purged all 3 Inspector entries");
    for (uint32_t I = 0u; I < 6u; ++I)
    {
        Check(!Scene.Atmosphere.Moons[I].Visible, "Gate 4: All 6 WorldScene moon slots disabled after external delete");
    }

    EnvRegistry.Reset();
    Bridge.Unbind();
    std::cout << "[Gate 4/4] Multi-Moon Undo & External Deletion Reconciliation : PASS\n";
}

} // namespace

int main()
{
    std::cout << "================================================================================\n";
    std::cout << " FRONTIER WORLD <-> OUTLINER <-> INSPECTOR RAII LIFECYCLE VALIDATION\n";
    std::cout << "================================================================================\n";

    ValidateGate1_AutoSpaceZeroPhantoms();
    ValidateGate2_RaiiConstructorDestructorLifecycle();
    ValidateGate3_PhantomAndStaleInspectorPruning();
    ValidateGate4_MultiMoonUndoAndExternalDeleteReconcile();

    if (gFailures > 0)
    {
        std::cerr << "\nSUMMARY: FAILED (" << gFailures << " checks failed)\n";
        return 1;
    }
    std::cout << "================================================================================\n";
    std::cout << " SUMMARY: 4 / 4 STRICT GATES PASSED!\n";
    std::cout << "================================================================================\n";
    return 0;
}
