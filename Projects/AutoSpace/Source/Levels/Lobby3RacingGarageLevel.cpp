/*============================================================================================================================================
                                                      LOBBY3RACINGGARAGELEVEL.CPP
============================================================================================================================================*/
// 🧩 Lobby 3 · Racing Pit Garage population inside single MainWorld.WorldSpace level

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Levels/StudioLobbyWorldBuilder.h"

#include "Engine/Constants/MathConstants.h"

namespace AutoSpace
{

void BuildLobby3RacingGarageLevel(IStudioLobbySceneBuilderContext& Ctx, const LobbyPolicy& Policy)
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::Mathematics;
    using namespace Frontier::World;

    const auto& VehicleTemplate = Ctx.InspectControlVehicle();
    const auto& Palette         = Ctx.InspectMaterialPalette();
    auto&       Scene           = Ctx.AccessWorldScene();

    const Axis3D HeroAimHtml { 0.35f, 0.88f, 0.35f };
    const Axis3D HeroAimZUp  = ConvertHtmlPositionToWorldZUp(HeroAimHtml.X, HeroAimHtml.Y, HeroAimHtml.Z);

    // 1. Vehicles & Stadium Architecture Folders
    const InstanceToken VehiclesFolder = Ctx.CreateFolderEntry("Vehicles", "Racing Stadium Garage lineup · 4 cars (including 1 on hydraulic lift)", {});
    const InstanceToken ArchFolder     = Ctx.CreateFolderEntry("Racing Stadium & Pit Garage", "3D multi-lane track, grandstands, pit garage & hydraulic lift", {});

    const uint16_t OrangePaint   = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "SignalOrange", SrgbHexToLinearRgb(0xe86512), 0.62f, 0.14f, 1.00f);
    const uint16_t ObsidianPaint = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "ObsidianGloss", SrgbHexToLinearRgb(0x111319), 0.86f, 0.14f, 1.00f);
    const uint16_t WhitePaint    = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "AlpineWhite", SrgbHexToLinearRgb(0xeceff3), 0.18f, 0.15f, 0.98f);
    const uint16_t CobaltPaint   = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "MidnightCobalt", SrgbHexToLinearRgb(0x13294b), 0.85f, 0.16f, 1.00f);
    const uint16_t RedBrake      = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "CrimsonCaliper", SrgbHexToLinearRgb(0xb81d24), 0.35f, 0.20f, 0.85f);

    (void)Ctx.AppendSurfaceEntity(
        "vehicle-hero", "Hero GT · Pit Bay Center",
        "ControlledIntegrator · Signal Orange GT in afternoon pit-lane sunbeam",
        BuildAssembledControlVehicleSurface(VehicleTemplate, Palette, OrangePaint, OrangePaint, { 0.78f, -146.0f, 0.0f, 0.0f }, "HeroSignalOrange"),
        ConvertHtmlPositionToWorldZUp(0.35f, 0.0f, 0.25f), OrangePaint, VehiclesFolder, true);
    Ctx.IncrementVehicleCount();

    (void)Ctx.AppendSurfaceEntity(
        "vehicle-gt3", "Alpine GT · Right Pit Bay",
        "ControlledIntegrator · Paddock GT car in right bay",
        BuildAssembledControlVehicleSurface(VehicleTemplate, Palette, WhitePaint, RedBrake, { 0.75f, -142.0f, 0.0f, 0.0f }, "AlpinePitRight"),
        ConvertHtmlPositionToWorldZUp(5.45f, 0.0f, 8.65f), WhitePaint, VehiclesFolder, true);
    Ctx.IncrementVehicleCount();

    (void)Ctx.AppendSurfaceEntity(
        "vehicle-tourer", "Midnight Cobalt · Outer Paddock",
        "ControlledIntegrator · Reserve track car on left paddock apron",
        BuildAssembledControlVehicleSurface(VehicleTemplate, Palette, CobaltPaint, OrangePaint, { 0.75f, -36.0f, 0.0f, 0.0f }, "CobaltPitLeft"),
        ConvertHtmlPositionToWorldZUp(-5.35f, 0.0f, 8.75f), CobaltPaint, VehiclesFolder, true);
    Ctx.IncrementVehicleCount();

    // 2. Racing Stadium & Pit Garage Architecture + 4-Post Hydraulic Lift + Elevated Track Coupe
    (void)Ctx.AppendSurfaceEntity(
        "l3-race-track", "3D Racing Track & Stadium",
        "Procedural studio.race_track · 3-lane FIA circuit, pit wall, gantry & 14-tier grandstands",
        BuildRaceTrackSurface({ 150.0f, 3u, true, true }, Palette), ConvertHtmlPositionToWorldZUp(0.0f, 0.0f, 0.0f),
        Palette.Resolve(StudioMaterialCode::TrackAsphalt), ArchFolder, true);

    (void)Ctx.AppendSurfaceEntity(
        "l3-garage-shell", "Pit Garage, Cages & Mezzanine",
        "Procedural studio.garage_shell · pit archway, security bars, mezzanine & linear LEDs",
        BuildGarageShellSurface({ 22.0f, 18.0f, 7.15f, true, true }, Palette), ConvertHtmlPositionToWorldZUp(0.0f, 0.0f, 0.0f),
        Palette.Resolve(StudioMaterialCode::ConcreteGarage), ArchFolder, true);

    const InstanceToken LiftTok = Ctx.AppendSurfaceEntity(
        "l3-car-lift", "Hydraulic 4-Post Car Lift",
        "Procedural studio.car_lift · 4-post lift supporting elevated track coupe at Z = 2.12m",
        BuildCarLiftSurface({ 2.12f, 3.85f }, Palette, { 1.0f, 90.0f, 0.0f, 0.0f }),
        ConvertHtmlPositionToWorldZUp(-5.55f, 0.0f, -1.55f), Palette.Resolve(StudioMaterialCode::SafetyYellow), ArchFolder, true);

    (void)Ctx.AppendSurfaceEntity(
        "vehicle-lift", "Track Coupe · On Hydraulic Lift",
        "ControlledIntegrator · Elevated race car mounted on hydraulic lift deck",
        BuildAssembledControlVehicleSurface(VehicleTemplate, Palette, ObsidianPaint, RedBrake, { 0.74f, 90.0f, 0.0f, 0.0f }, "ElevatedLiftCoupe"),
        ConvertHtmlPositionToWorldZUp(-5.55f, 2.12f, -1.55f), ObsidianPaint, LiftTok, true);
    Ctx.IncrementVehicleCount();

    (void)Ctx.AppendSurfaceEntity(
        "studio-floor", "Polished Concrete Pit Floor",
        "Procedural studio.tiled_floor · motorsport garage concrete floor",
        BuildTiledFloorSurface({ 44.0f, 24.8f, 2.2f, true }, Palette), ConvertHtmlPositionToWorldZUp(0.0f, 0.0f, 6.2f),
        Palette.Resolve(StudioMaterialCode::ConcreteGarage), ArchFolder, false);

    // 3. Mechanics Equipment & Engine ParticleSpace Smoke/Dust
    const InstanceToken PropsFolder = Ctx.CreateFolderEntry("Mechanics Equipment & Smoke", "Toolboxes, workbench, air compressor, tire rack, cables & smoke FX", {});
    if (Policy.EnableCrewProps)
    {
        (void)Ctx.AppendSurfaceEntity("l3-tool-chest", "Double-Bank Cherry-Red Tool Chest", "10-drawer rolling tool chest + impact gun",
            BuildGaragePropsSurface({ "tool-chest" }, Palette, { 1.0f, -85.0f, 0.0f, 0.0f }),
            ConvertHtmlPositionToWorldZUp(7.10f, 0.0f, -2.40f), Palette.Resolve(StudioMaterialCode::CherryRedTool), PropsFolder, true);
        (void)Ctx.AppendSurfaceEntity("l3-tool-cart", "Rolling Cherry-Red Tool Cart", "Service trolley with toolboxes & slicks",
            BuildGaragePropsSurface({ "tool-cart" }, Palette, { 1.0f, -32.0f, 0.0f, 0.0f }),
            ConvertHtmlPositionToWorldZUp(5.35f, 0.0f, 2.20f), Palette.Resolve(StudioMaterialCode::CherryRedTool), PropsFolder, true);
        (void)Ctx.AppendSurfaceEntity("l3-workbench", "Mechanic's Workbench & Pegboard", "Butcher-block bench, vise, wrenches & drums",
            BuildGaragePropsSurface({ "workbench" }, Palette, { 1.0f, -90.0f, 0.0f, 0.0f }),
            ConvertHtmlPositionToWorldZUp(9.75f, 0.0f, -0.35f), Palette.Resolve(StudioMaterialCode::TimberBench), PropsFolder, true);
        (void)Ctx.AppendSurfaceEntity("l3-air-compressor", "Air Compressor & Welding Cart", "80-gallon vertical compressor & gas bottles",
            BuildGaragePropsSurface({ "air-compressor" }, Palette, { 1.0f, 90.0f, 0.0f, 0.0f }),
            ConvertHtmlPositionToWorldZUp(-9.65f, 0.0f, 3.85f), Palette.Resolve(StudioMaterialCode::CherryRedTool), PropsFolder, true);
        (void)Ctx.AppendSurfaceEntity("l3-tire-rack", "Racing Slick Tire Rack & Jack", "2-tier slick rack, trolley jack & stands",
            BuildGaragePropsSurface({ "tire-rack" }, Palette, { 1.0f, 0.0f, 0.0f, 0.0f }),
            ConvertHtmlPositionToWorldZUp(4.05f, 0.0f, -4.55f), Palette.Resolve(StudioMaterialCode::SafetyYellow), PropsFolder, true);
        (void)Ctx.AppendSurfaceEntity("l3-diag-cabinet", "Diagnostic Telemetry Cabinet", "Rolling pit telemetry chest",
            BuildGaragePropsSurface({ "diag-cabinet" }, Palette, { 1.0f, 18.0f, 0.0f, 0.0f }),
            ConvertHtmlPositionToWorldZUp(-1.55f, 0.0f, -4.65f), Palette.Resolve(StudioMaterialCode::CherryRedTool), PropsFolder, true);
        (void)Ctx.AppendSurfaceEntity("l3-garage-cables", "Floor Air Hoses & Cable Ramp", "Pneumatic hoses & yellow cable ramp",
            BuildGarageCablesSurface({ 11.5f }, Palette), ConvertHtmlPositionToWorldZUp(0.0f, 0.0f, 0.0f),
            Palette.Resolve(StudioMaterialCode::RubberMat), PropsFolder, false);
    }

    Ctx.AppendParticleEmitter(
        "l3-garage-smoke", "Volumetric Garage & Tire Smoke",
        "Frontier::ParticleSpace (mist-spray) · exhaust & pit-bay atmosphere plume",
        "mist-spray", ConvertHtmlPositionToWorldZUp(0.85f, 0.55f, 0.95f), 145u, 64.0f, PropsFolder);
    Ctx.AppendParticleEmitter(
        "l3-dust-particles", "Sunbeam Garage Dust Motes",
        "Frontier::ParticleSpace (ground-dust) · sunlit pit-archway dust motes",
        "ground-dust", ConvertHtmlPositionToWorldZUp(-0.8f, 2.0f, -0.4f), 165u, 72.0f, PropsFolder);

    // 4. Garage & Stadium Lighting
    const InstanceToken LightFolder = Ctx.CreateFolderEntry("Garage & Stadium Lighting", "Overhead garage linear LEDs & stadium afternoon sunlight", {});
    if (Policy.EnableStudioLightingRigs)
    {
        const Axis3D OverheadPos = ConvertHtmlPositionToWorldZUp(0.0f, 6.05f, 0.35f);
        Scene.AreaLights.push_back(MakeAimedAreaQuadLight(OverheadPos, ConvertHtmlPositionToWorldZUp(0.0f, 0.0f, 0.35f), 12.0f, 8.0f, { 1.15f, 1.20f, 1.28f }));
        (void)Ctx.AppendLuminaireEntity("Garage Ceiling Linear LEDs", "Rect Area Light · 5850 K industrial ceiling bank", "light:area", OverheadPos, { 2u, 1.15f, 1.20f, 1.28f, 26.0f, 125.0f, true }, LightFolder);

        const Axis3D SunPos = ConvertHtmlPositionToWorldZUp(-9.4f, 8.2f, -13.5f);
        Scene.PhotometricRadiators.push_back(MakeAimedPhotometricSpotLight(SunPos, HeroAimZUp, { 26.0f, 21.0f, 14.0f }, 36.0f, 2u));
        (void)Ctx.AppendLuminaireEntity("Afternoon Stadium Sunlight", "IES Photometric · golden sunlight through open pit archway", "light:ies", SunPos, { 3u, 26.0f, 21.0f, 14.0f, 36.0f, 72.0f, true }, LightFolder);

        const Axis3D PortalPos = ConvertHtmlPositionToWorldZUp(-4.2f, 3.1f, -6.3f);
        Scene.AreaLights.push_back(MakeAimedAreaQuadLight(PortalPos, HeroAimZUp, 7.5f, 3.6f, { 1.30f, 1.38f, 1.54f }));
        (void)Ctx.AppendLuminaireEntity("Open Pit Archway Sky Fill", "Rect Area Light · outdoor track daylight spill", "light:area", PortalPos, { 2u, 1.30f, 1.38f, 1.54f, 24.0f, 110.0f, true }, LightFolder);

        const Axis3D BayPos = ConvertHtmlPositionToWorldZUp(5.2f, 5.4f, 1.4f);
        Scene.PhotometricRadiators.push_back(MakeAimedPhotometricSpotLight(BayPos, HeroAimZUp, { 18.0f, 13.0f, 8.2f }, 22.0f, 2u));
        (void)Ctx.AppendLuminaireEntity("Mechanic Bay Overhead Lamp", "IES Photometric · warm 3400 K worklamp", "light:ies", BayPos, { 3u, 18.0f, 13.0f, 8.2f, 22.0f, 64.0f, true }, LightFolder);
    }

    const InstanceToken CamFolder = Ctx.CreateFolderEntry("Camera Rigs", "Editor camera & pit garage camera rigs", {});
    (void)Ctx.AppendCameraEntity("Editor Camera", "Unrestricted 3D editor navigation camera", ConvertHtmlPositionToWorldZUp(4.95f, 1.52f, 5.85f), { 35.0f, 36.0f, 0.1f, 2500.0f, true }, CamFolder, true);

    Scene.Atmosphere.Medium.AtmosphereEnabled = false;
    Scene.Atmosphere.Solar.SolarEnabled       = true;
    Scene.Atmosphere.Clouds.CloudsEnabled     = true;
    Scene.Sun.DirectionToSun                   = NormalizeAxis({ -0.45f, 0.65f, 0.60f }, { 0.0f, 0.0f, 1.0f });
    Scene.Sun.SolarIrradiance                  = { 3.2f, 2.8f, 2.3f };
    Scene.SkyRadiance                          = { 0.18f, 0.24f, 0.34f };

    PerspectiveCameraBasis Cam {};
    Cam.CameraOrigin       = ConvertHtmlPositionToWorldZUp(4.95f, 1.52f, 5.85f);
    Cam.TargetPoint        = HeroAimZUp;
    Cam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
    Cam.VerticalFovRadians = 36.0f * Constants::DegToRadFloat;
    Cam.ViewportWidthPx    = 1280u;
    Cam.ViewportHeightPx   = 720u;
    Cam.NearClipMeters     = 0.10f;
    Ctx.AppendCameraPreset({ "hero-34", "Hero 3/4 · Racing Garage", Cam });
}

} // namespace AutoSpace
