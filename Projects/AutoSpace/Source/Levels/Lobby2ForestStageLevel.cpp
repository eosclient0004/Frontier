/*============================================================================================================================================
                                                      LOBBY2FORESTSTAGELEVEL.CPP
============================================================================================================================================*/
// 🧩 Lobby 2 · Forest Soundstage population inside single MainWorld.WorldSpace level (rocks, plants & leaves left blank)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Levels/StudioLobbyWorldBuilder.h"

#include "Engine/Constants/MathConstants.h"

namespace AutoSpace
{

void BuildLobby2ForestStageLevel(IStudioLobbySceneBuilderContext& Ctx, const LobbyPolicy& Policy)
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::Mathematics;
    using namespace Frontier::World;

    const auto& VehicleTemplate = Ctx.InspectControlVehicle();
    const auto& Palette         = Ctx.InspectMaterialPalette();
    auto&       Scene           = Ctx.AccessWorldScene();

    const Axis3D HeroAimHtml { 0.0f, 1.10f, -0.35f };
    const Axis3D HeroAimZUp  = ConvertHtmlPositionToWorldZUp(HeroAimHtml.X, HeroAimHtml.Y, HeroAimHtml.Z);

    // 1. Vehicles Folder (4 cars; hero centered directly on stage since rocks/moss/wood/plants remain intentionally blank)
    const InstanceToken VehiclesFolder = Ctx.CreateFolderEntry("Vehicles", "Forest Soundstage lineup · 4 cars (rocks & botanical props left blank)", {});
    {
        const uint16_t SagePaint     = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "ForestSage", SrgbHexToLinearRgb(0x7ea689), 0.42f, 0.16f, 0.98f);
        const uint16_t SageBrake     = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "SageCaliper", SrgbHexToLinearRgb(0x8c9c7a), 0.35f, 0.22f, 0.85f);
        const uint16_t ObsidianPaint = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "ObsidianGloss", SrgbHexToLinearRgb(0x111319), 0.84f, 0.14f, 1.00f);
        const uint16_t WhitePaint    = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "AlpineWhite", SrgbHexToLinearRgb(0xeceff3), 0.18f, 0.16f, 0.98f);
        const uint16_t SilverPaint   = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "LiquidTitanium", SrgbHexToLinearRgb(0x9ca5b4), 0.92f, 0.18f, 0.95f);
        const uint16_t RedBrake      = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "CrimsonCaliper", SrgbHexToLinearRgb(0xb81d24), 0.35f, 0.20f, 0.85f);

        (void)Ctx.AppendSurfaceEntity(
            "vehicle-hero", "Hero Vehicle · Forest Stage", "ControlledIntegrator · Forest Sage center stage",
            BuildAssembledControlVehicleSurface(VehicleTemplate, Palette, SagePaint, SageBrake, { 0.78f, -22.0f, 0.0f, 0.0f }, "HeroForestStage"),
            ConvertHtmlPositionToWorldZUp(0.0f, 0.0f, -0.35f), SagePaint, VehiclesFolder, true);
        Ctx.IncrementVehicleCount();

        (void)Ctx.AppendSurfaceEntity(
            "vehicle-gt3", "Obsidian Coupe · Left Stage", "ControlledIntegrator · Companion coupe",
            BuildAssembledControlVehicleSurface(VehicleTemplate, Palette, ObsidianPaint, RedBrake, { 0.74f, -24.0f, 0.0f, 0.0f }, "ObsidianOuterLeft"),
            ConvertHtmlPositionToWorldZUp(-11.8f, 0.0f, 4.2f), ObsidianPaint, VehiclesFolder, true);
        Ctx.IncrementVehicleCount();

        (void)Ctx.AppendSurfaceEntity(
            "vehicle-tourer", "Alpine GT · Right Stage", "ControlledIntegrator · Companion coupe",
            BuildAssembledControlVehicleSurface(VehicleTemplate, Palette, WhitePaint, RedBrake, { 0.74f, -156.0f, 0.0f, 0.0f }, "AlpineOuterRight"),
            ConvertHtmlPositionToWorldZUp(11.8f, 0.0f, 4.2f), WhitePaint, VehiclesFolder, true);
        Ctx.IncrementVehicleCount();

        (void)Ctx.AppendSurfaceEntity(
            "vehicle-reserve", "Liquid Tourer · Reserve Bay", "ControlledIntegrator · Reserve vehicle",
            BuildAssembledControlVehicleSurface(VehicleTemplate, Palette, SilverPaint, RedBrake, { 0.74f, -138.0f, 0.0f, 0.0f }, "LiquidReserve"),
            ConvertHtmlPositionToWorldZUp(10.8f, 0.0f, 9.4f), SilverPaint, VehiclesFolder, true);
        Ctx.IncrementVehicleCount();
    }

    // 2. Forest Soundstage Set Folder (LED Backdrop, Concrete Floor, Ceiling Truss; rocks/fallen wood/ferns/grass omitted)
    const InstanceToken SetFolder = Ctx.CreateFolderEntry("Forest Soundstage Set", "Backlit forest LED backdrop, studio floor & truss (rocks/plants left blank)", {});
    {
        (void)Ctx.AppendSurfaceEntity(
            "l2-forest-backdrop", "Forest LED Backdrop & Flags",
            "Procedural studio.forest_backdrop · backlit LED volume & V-flats",
            BuildForestBackdropSurface({ 12.4f, 6.2f, 20.5f, 7.25f, true }, Palette),
            ConvertHtmlPositionToWorldZUp(0.0f, 0.0f, -5.65f),
            Palette.Resolve(StudioMaterialCode::ForestBackdrop), SetFolder, true);

        (void)Ctx.AppendSurfaceEntity(
            "studio-floor", "Studio Concrete Floor",
            "Procedural studio.tiled_floor · matte studio concrete floor",
            BuildTiledFloorSurface({ 46.0f, 40.0f, 1.8f, true }, Palette),
            ConvertHtmlPositionToWorldZUp(0.0f, 0.0f, 1.5f),
            Palette.Resolve(StudioMaterialCode::TiledFloor), SetFolder, false);

        (void)Ctx.AppendSurfaceEntity(
            "studio-ceiling", "Ribbed Ceiling & Truss",
            "Procedural studio.ceiling_truss · overhead studio truss",
            BuildCeilingTrussSurface({ 26.0f, 22.0f, 7.25f, 14u, true }, Palette),
            ConvertHtmlPositionToWorldZUp(0.0f, 0.0f, -0.8f),
            Palette.Resolve(StudioMaterialCode::CeilingRib), SetFolder, false);
    }

    // 3. Wind Fan & Engine ParticleSpace Dust FX
    const InstanceToken AtmosFolder = Ctx.CreateFolderEntry("Wind Fan & Dust FX", "Procedural studio wind blower & Frontier::ParticleSpace dust motes", {});
    {
        const Axis3D FanHtml { -5.25f, 0.0f, 1.65f };
        const HtmlAimAngles AimFan = ComputeHtmlAimAngles({ FanHtml.X, 1.28f, FanHtml.Z }, HeroAimHtml);
        (void)Ctx.AppendSurfaceEntity(
            "l2-studio-fan", "Studio Wind Fan (Blower)",
            "Procedural studio.studio_fan · wheeled wind blower aimed across stage",
            BuildStudioFanSurface({ 1.28f, 0.52f, -4.0f }, Palette, { 1.0f, AimFan.YawDeg, 0.0f, 0.0f }),
            ConvertHtmlPositionToWorldZUp(FanHtml.X, FanHtml.Y, FanHtml.Z),
            Palette.Resolve(StudioMaterialCode::FanHousing), AtmosFolder, true);

        Ctx.AppendParticleEmitter(
            "l2-dust-particles", "Sunbeam Dust Particles",
            "Frontier::ParticleSpace (ground-dust) · 280 atmospheric stage dust motes",
            "ground-dust", ConvertHtmlPositionToWorldZUp(0.0f, 1.65f, -0.2f), 280u, 96.0f, AtmosFolder);
    }

    // 4. Lighting Rigs
    const InstanceToken LightFolder = Ctx.CreateFolderEntry("Lighting Rigs", "Softbox, warm sun Fresnel & studio bounce rigs", {});
    if (Policy.EnableStudioLightingRigs)
    {
        const InstanceToken SbParent = Ctx.AppendSurfaceEntity(
            "rig-softbox-fixture", "Overhead Softbox Bank",
            "Procedural studio.light_fixture · overhead silk softbox",
            BuildLightFixtureSurface({ "Overhead Softbox", 6.4f, 3.8f, 0.30f, 0.0f, 40.0f, 16u, 2.68f }, Palette),
            ConvertHtmlPositionToWorldZUp(0.0f, 4.42f, -0.45f),
            Palette.Resolve(StudioMaterialCode::DarkMetal), LightFolder, false);
        const Axis3D SbLightPos = ConvertHtmlPositionToWorldZUp(0.0f, 4.18f, -0.45f);
        Scene.AreaLights.push_back(MakeAimedAreaQuadLight(SbLightPos, HeroAimZUp, 6.4f, 3.8f, { 1.10f, 1.15f, 1.22f }));
        (void)Ctx.AppendLuminaireEntity("Overhead Softbox Area Light", "Rect Area Light · 6.4 x 3.8 m diffuser", "light:area", SbLightPos, { 2u, 1.10f, 1.15f, 1.22f, 22.0f, 125.0f, true }, SbParent);

        const Axis3D KeyPos = ConvertHtmlPositionToWorldZUp(-4.75f, 2.70f, 1.45f);
        Scene.PhotometricRadiators.push_back(MakeAimedPhotometricSpotLight(KeyPos, HeroAimZUp, { 19.0f, 14.0f, 8.5f }, 24.0f, 2u));
        (void)Ctx.AppendLuminaireEntity("Key Sun Fresnel IES Light", "IES Photometric · 3450 K warm woodland sunbeam", "light:ies", KeyPos, { 3u, 19.0f, 14.0f, 8.5f, 24.0f, 54.0f, true }, LightFolder);

        const Axis3D UmbPos = ConvertHtmlPositionToWorldZUp(4.85f, 2.35f, 2.15f);
        Scene.AreaLights.push_back(MakeAimedAreaQuadLight(UmbPos, HeroAimZUp, 1.55f, 1.55f, { 1.40f, 1.55f, 1.65f }));
        (void)Ctx.AppendLuminaireEntity("Umbrella Bounce Area Light", "Rect Area Light · ambient stage fill", "light:area", UmbPos, { 2u, 1.40f, 1.55f, 1.65f, 18.0f, 135.0f, true }, LightFolder);

        const Axis3D RimPos = ConvertHtmlPositionToWorldZUp(-4.95f, 2.75f, -2.55f);
        Scene.AreaLights.push_back(MakeAimedAreaQuadLight(RimPos, HeroAimZUp, 1.05f, 0.62f, { 2.20f, 2.50f, 3.00f }));
        (void)Ctx.AppendLuminaireEntity("Rim LED Area Light", "Rect Area Light · canopy edge rim", "light:area", RimPos, { 2u, 2.20f, 2.50f, 3.00f, 18.0f, 110.0f, true }, LightFolder);

        const Axis3D AccPos = ConvertHtmlPositionToWorldZUp(5.05f, 2.65f, -2.25f);
        Scene.PhotometricRadiators.push_back(MakeAimedPhotometricSpotLight(AccPos, HeroAimZUp, { 16.0f, 11.2f, 6.8f }, 22.0f, 2u));
        (void)Ctx.AppendLuminaireEntity("Accent Strobe IES Light", "IES Photometric · 3250 K warm sun-rim backlight", "light:ies", AccPos, { 3u, 16.0f, 11.2f, 6.8f, 22.0f, 52.0f, true }, LightFolder);
    }

    // 5. Camera Rigs & Crew Props
    const InstanceToken CamFolder = Ctx.CreateFolderEntry("Camera Rigs", "Editor camera & foreground tripod rigs", {});
    (void)Ctx.AppendCameraEntity("Editor Camera", "Unrestricted 3D editor navigation camera", ConvertHtmlPositionToWorldZUp(5.45f, 2.05f, 7.15f), { 35.0f, 36.0f, 0.1f, 2500.0f, true }, CamFolder, true);

    if (Policy.EnableCrewProps)
    {
        const InstanceToken PropsFolder = Ctx.CreateFolderEntry("Studio Props & Crew", "Tether desk & grip cases outside soundstage", {});
        (void)Ctx.AppendSurfaceEntity(
            "prop-workstation", "Tether Workstation & Desk", "Hairpin desk & calibration laptop",
            BuildWorkstationSurface({ 1.48f, 0.72f, 0.78f, true, true, true }, Palette, { 1.0f, -28.0f, 0.0f, 0.0f }),
            ConvertHtmlPositionToWorldZUp(6.4f, 0.0f, 6.85f), Palette.Resolve(StudioMaterialCode::DarkMetal), PropsFolder, true);
        (void)Ctx.AppendSurfaceEntity(
            "prop-grip-cases", "Grip Cases & Cables", "Pelican flight cases & coiled power cable",
            BuildWorkstationSurface({ 1.0f, 0.55f, 0.65f, false, true, true }, Palette, { 1.0f, 22.0f, 0.0f, 0.0f }),
            ConvertHtmlPositionToWorldZUp(-6.8f, 0.0f, 6.55f), Palette.Resolve(StudioMaterialCode::PelicanCase), PropsFolder, true);
    }

    Scene.Atmosphere.Medium.AtmosphereEnabled = false;
    Scene.Atmosphere.Solar.SolarEnabled       = false;
    Scene.Atmosphere.Clouds.CloudsEnabled     = false;
    Scene.Sun.SolarIrradiance                  = { 0.0f, 0.0f, 0.0f };
    Scene.SkyRadiance                          = { 0.018f, 0.024f, 0.022f };

    PerspectiveCameraBasis Cam {};
    Cam.CameraOrigin       = ConvertHtmlPositionToWorldZUp(5.45f, 2.05f, 7.15f);
    Cam.TargetPoint        = HeroAimZUp;
    Cam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
    Cam.VerticalFovRadians = 36.0f * Constants::DegToRadFloat;
    Cam.ViewportWidthPx    = 1280u;
    Cam.ViewportHeightPx   = 720u;
    Cam.NearClipMeters     = 0.10f;
    Ctx.AppendCameraPreset({ "hero-34", "Hero 3/4 · Forest Stage", Cam });
}

} // namespace AutoSpace
