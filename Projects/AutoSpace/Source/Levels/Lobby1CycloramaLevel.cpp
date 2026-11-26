/*============================================================================================================================================
                                                       LOBBY1CYCLORAMALEVEL.CPP
============================================================================================================================================*/
// 🧩 Lobby 1 · Studio Lobby · Cyclorama population inside single MainWorld.WorldSpace level

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Levels/StudioLobbyWorldBuilder.h"

#include "Engine/Constants/MathConstants.h"

namespace AutoSpace
{

void BuildLobby1CycloramaLevel(IStudioLobbySceneBuilderContext& Ctx, const LobbyPolicy& Policy)
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::Mathematics;
    using namespace Frontier::World;

    const auto& VehicleTemplate = Ctx.InspectControlVehicle();
    const auto& Palette         = Ctx.InspectMaterialPalette();
    auto&       Scene           = Ctx.AccessWorldScene();

    const Axis3D HeroAimHtml { 0.0f, 0.88f, -0.15f };
    const Axis3D HeroAimZUp  = ConvertHtmlPositionToWorldZUp(HeroAimHtml.X, HeroAimHtml.Y, HeroAimHtml.Z);

    // 1. Vehicles Folder (3 vehicles matching StudioLobbyRows)
    const InstanceToken VehiclesFolder = Ctx.CreateFolderEntry("Vehicles", "Studio vehicle lineup · 3 vehicles", {});
    {
        const uint16_t HeroPaint = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "ObsidianGloss", SrgbHexToLinearRgb(0x111319), 0.84f, 0.13f, 1.00f);
        const uint16_t HeroBrake = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "HeroCrimsonCaliper", SrgbHexToLinearRgb(0xb81d24), 0.35f, 0.20f, 0.85f);
        (void)Ctx.AppendSurfaceEntity(
            "vehicle-hero", "Hero Coupe · Cyclorama",
            "ControlledIntegrator · Center hero vehicle under overhead softbox",
            BuildAssembledControlVehicleSurface(VehicleTemplate, Palette, HeroPaint, HeroBrake, { 0.76f, -16.0f, 0.0f, 0.0f }, "HeroCoupeCyclorama"),
            ConvertHtmlPositionToWorldZUp(0.0f, 0.026f, -0.15f), HeroPaint, VehiclesFolder, true);
        Ctx.IncrementVehicleCount();

        const uint16_t Gt3Paint = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "AlpineGT3White", SrgbHexToLinearRgb(0xeceff3), 0.18f, 0.16f, 0.98f);
        (void)Ctx.AppendSurfaceEntity(
            "vehicle-gt3", "Alpine GT · Outer Left Bay",
            "ControlledIntegrator · Parked outside cyclorama on outer tiled floor",
            BuildAssembledControlVehicleSurface(VehicleTemplate, Palette, Gt3Paint, HeroBrake, { 0.74f, -24.0f, 0.0f, 0.0f }, "AlpineGTOuterLeft"),
            ConvertHtmlPositionToWorldZUp(-12.2f, 0.0f, 4.8f), Gt3Paint, VehiclesFolder, true);
        Ctx.IncrementVehicleCount();

        const uint16_t TourerPaint = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "LiquidTitanium", SrgbHexToLinearRgb(0x9ca5b4), 0.92f, 0.18f, 0.96f);
        const uint16_t GoldBrake   = RegisterVehicleBodyPaintMaterial(Scene.MaterialRegister, "GoldCaliper", SrgbHexToLinearRgb(0xd49b24), 0.55f, 0.22f, 0.88f);
        (void)Ctx.AppendSurfaceEntity(
            "vehicle-tourer", "Liquid Tourer · Outer Right Bay",
            "ControlledIntegrator · Parked outside cyclorama on outer tiled floor",
            BuildAssembledControlVehicleSurface(VehicleTemplate, Palette, TourerPaint, GoldBrake, { 0.74f, -156.0f, 0.0f, 0.0f }, "LiquidTourerOuterRight"),
            ConvertHtmlPositionToWorldZUp(12.2f, 0.0f, 4.8f), TourerPaint, VehiclesFolder, true);
        Ctx.IncrementVehicleCount();
    }

    // 2. Studio Architecture Folder
    const InstanceToken ArchFolder = Ctx.CreateFolderEntry("Studio Architecture", "Cyclorama cove, glossy tiled floor, ceiling truss", {});
    {
        (void)Ctx.AppendSurfaceEntity(
            "studio-cyclorama", "Cyclorama Infinity Cove",
            "Procedural studio.cyclorama · seamless matte cove",
            BuildCycloramaSurface({ 18.0f, 13.5f, 7.0f, 2.2f, true }, Palette),
            ConvertHtmlPositionToWorldZUp(0.0f, 0.0f, -0.8f),
            Palette.Resolve(StudioMaterialCode::Cyclorama), ArchFolder, true);

        (void)Ctx.AppendSurfaceEntity(
            "studio-floor", "Glossy Tiled Studio Floor",
            "Procedural studio.tiled_floor · dark reflective slate tiles",
            BuildTiledFloorSurface({ 48.0f, 40.0f, 1.5f, true }, Palette),
            ConvertHtmlPositionToWorldZUp(0.0f, 0.0f, 0.0f),
            Palette.Resolve(StudioMaterialCode::TiledFloor), ArchFolder, false);

        (void)Ctx.AppendSurfaceEntity(
            "studio-ceiling", "Ribbed Ceiling & Truss",
            "Procedural studio.ceiling_truss · overhead grid & steel ribs",
            BuildCeilingTrussSurface({ 34.0f, 28.0f, 7.25f, 16u, true }, Palette),
            ConvertHtmlPositionToWorldZUp(0.0f, 0.0f, -0.5f),
            Palette.Resolve(StudioMaterialCode::CeilingRib), ArchFolder, false);
    }

    // 3. Lighting Rigs Folder (5 physical rigs + 3 Area + 2 IES luminaires)
    const InstanceToken LightingFolder = Ctx.CreateFolderEntry("Lighting Rigs", "Hierarchical procedural stands, modifiers & native lights", {});
    if (Policy.EnableStudioLightingRigs)
    {
        // Overhead Softbox Bank -> Rect Area Light
        {
            const InstanceToken SbParentTok = Ctx.AppendSurfaceEntity(
                "rig-softbox-fixture", "Overhead Softbox Bank",
                "Procedural studio.light_fixture · suspended silk overhead softbox",
                BuildLightFixtureSurface({ "Overhead Softbox", 7.2f, 4.0f, 0.30f, 0.0f, 28.0f, 16u, 2.88f }, Palette),
                ConvertHtmlPositionToWorldZUp(0.0f, 4.22f, -0.25f),
                Palette.Resolve(StudioMaterialCode::DarkMetal), LightingFolder, false);

            const Axis3D LightPos = ConvertHtmlPositionToWorldZUp(0.0f, 3.98f, -0.25f);
            Scene.AreaLights.push_back(MakeAimedAreaQuadLight(
                LightPos, ConvertHtmlPositionToWorldZUp(0.0f, 0.4f, -0.25f), 7.2f, 4.0f, { 0.95f, 1.00f, 1.08f }, false));
            (void)Ctx.AppendLuminaireEntity(
                "Overhead Softbox Area Light", "Rect Area Light · 7.2 x 4.0 m suspended silk diffuser", "light:area",
                LightPos, { 2u, 0.95f, 1.00f, 1.08f, 22.0f, 125.0f, true }, SbParentTok);
        }

        // Key Fresnel Tripod -> Barn-Door Fresnel -> Key Fresnel IES Light
        {
            const Axis3D KeyTripodHtml { -3.65f, 0.0f, 1.55f };
            TripodParams Tp { 1.58f, 0.34f, 27.0f, 0.017f, 3u, 0.0f, true, true, false, true };
            const float MountH = ComputeTripodMountHeight(Tp);
            const HtmlAimAngles Aim = ComputeHtmlAimAngles({ KeyTripodHtml.X, MountH, KeyTripodHtml.Z }, HeroAimHtml);
            Tp.HeadTiltDeg = Aim.PitchDeg;

            const InstanceToken TripodTok = Ctx.AppendSurfaceEntity(
                "rig-key-tripod", "Key Fresnel Tripod",
                "Shared procedural studio.tripod · wheeled studio light stand",
                BuildTripodSurface(Tp, Palette, { 1.0f, Aim.YawDeg, 0.0f, 0.0f }),
                ConvertHtmlPositionToWorldZUp(KeyTripodHtml.X, KeyTripodHtml.Y, KeyTripodHtml.Z),
                Palette.Resolve(StudioMaterialCode::DarkMetal), LightingFolder, true);

            const Axis3D FixPos = ConvertHtmlPositionToWorldZUp(KeyTripodHtml.X, MountH, KeyTripodHtml.Z);
            const InstanceToken FixTok = Ctx.AppendSurfaceEntity(
                "rig-key-fixture", "Barn-Door Fresnel Fixture",
                "Procedural studio.light_fixture · articulated 4-leaf barn doors",
                BuildLightFixtureSurface({ "Barn-Door Fresnel", 0.48f, 0.48f, 0.42f, Aim.PitchDeg, 34.0f, 12u, 0.0f }, Palette, { 1.0f, Aim.YawDeg, 0.0f, 0.0f }),
                FixPos, Palette.Resolve(StudioMaterialCode::DarkMetal), TripodTok, false);

            const Axis3D AimDir  = NormalizeAxis(HeroAimZUp - FixPos, { 0.0f, 0.0f, -1.0f });
            const Axis3D LampPos = FixPos + Axis3D{ 0.0f, 0.0f, 0.28f } + AimDir * 0.34f;
            Scene.PhotometricRadiators.push_back(MakeAimedPhotometricSpotLight(LampPos, HeroAimZUp, { 16.5f, 15.2f, 13.4f }, 22.0f, 2u));
            (void)Ctx.AppendLuminaireEntity(
                "Key Fresnel IES Light", "IES Photometric · 5400 K Fresnel lobe aimed at Hero Coupe", "light:ies",
                LampPos, { 3u, 16.5f, 15.2f, 13.4f, 22.0f, 46.0f, true }, FixTok);
        }

        // Umbrella Fill Tripod -> Parabolic Umbrella -> Umbrella Bounce Area Light
        {
            const Axis3D UmbTripodHtml { 3.45f, 0.0f, 1.85f };
            TripodParams Tp { 1.56f, 0.32f, 28.0f, 0.016f, 3u, 0.0f, true, false, false, true };
            const float MountH = ComputeTripodMountHeight(Tp);
            const HtmlAimAngles Aim = ComputeHtmlAimAngles({ UmbTripodHtml.X, MountH, UmbTripodHtml.Z }, HeroAimHtml);
            Tp.HeadTiltDeg = Aim.PitchDeg;

            const InstanceToken TripodTok = Ctx.AppendSurfaceEntity(
                "rig-umbrella-tripod", "Umbrella Fill Tripod",
                "Shared procedural studio.tripod · silver umbrella stand",
                BuildTripodSurface(Tp, Palette, { 1.0f, Aim.YawDeg, 0.0f, 0.0f }),
                ConvertHtmlPositionToWorldZUp(UmbTripodHtml.X, UmbTripodHtml.Y, UmbTripodHtml.Z),
                Palette.Resolve(StudioMaterialCode::DarkMetal), LightingFolder, true);

            const Axis3D FixPos = ConvertHtmlPositionToWorldZUp(UmbTripodHtml.X, MountH, UmbTripodHtml.Z);
            const InstanceToken FixTok = Ctx.AppendSurfaceEntity(
                "rig-umbrella-fixture", "Parabolic Umbrella Modifier",
                "Procedural studio.light_fixture · 16-rib silver bounce umbrella",
                BuildLightFixtureSurface({ "Parabolic Umbrella", 1.48f, 1.48f, 0.48f, Aim.PitchDeg, 25.0f, 16u, 0.0f }, Palette, { 1.0f, Aim.YawDeg, 0.0f, 0.0f }),
                FixPos, Palette.Resolve(StudioMaterialCode::UmbrellaSilver), TripodTok, false);

            const Axis3D AimDir  = NormalizeAxis(HeroAimZUp - FixPos, { 0.0f, 0.0f, -1.0f });
            const Axis3D LampPos = FixPos + Axis3D{ 0.0f, 0.0f, 0.24f } + AimDir * 0.28f;
            Scene.AreaLights.push_back(MakeAimedAreaQuadLight(LampPos, HeroAimZUp, 1.55f, 1.55f, { 1.45f, 1.55f, 1.72f }, false));
            (void)Ctx.AppendLuminaireEntity(
                "Umbrella Bounce Area Light", "Rect Area Light · soft 5800 K silver parabolic bounce fill", "light:area",
                LampPos, { 2u, 1.45f, 1.55f, 1.72f, 18.0f, 135.0f, true }, FixTok);
        }

        // Back-Left Rim Tripod -> Studio LED Panel -> Rim LED Area Light
        {
            const Axis3D RimTripodHtml { -4.35f, 0.0f, -2.65f };
            TripodParams Tp { 1.72f, 0.38f, 26.0f, 0.017f, 3u, 0.0f, true, true, false, true };
            const float MountH = ComputeTripodMountHeight(Tp);
            const HtmlAimAngles Aim = ComputeHtmlAimAngles({ RimTripodHtml.X, MountH, RimTripodHtml.Z }, HeroAimHtml);
            Tp.HeadTiltDeg = Aim.PitchDeg;

            const InstanceToken TripodTok = Ctx.AppendSurfaceEntity(
                "rig-rim-tripod", "Back-Left Rim Tripod",
                "Shared procedural studio.tripod · wheeled rim stand",
                BuildTripodSurface(Tp, Palette, { 1.0f, Aim.YawDeg, 0.0f, 0.0f }),
                ConvertHtmlPositionToWorldZUp(RimTripodHtml.X, RimTripodHtml.Y, RimTripodHtml.Z),
                Palette.Resolve(StudioMaterialCode::DarkMetal), LightingFolder, true);

            const Axis3D FixPos = ConvertHtmlPositionToWorldZUp(RimTripodHtml.X, MountH, RimTripodHtml.Z);
            const InstanceToken FixTok = Ctx.AppendSurfaceEntity(
                "rig-rim-fixture", "Rim LED Panel Fixture",
                "Procedural studio.light_fixture · cool edge-separation LED",
                BuildLightFixtureSurface({ "Studio LED Panel", 1.15f, 0.68f, 0.14f, Aim.PitchDeg, 24.0f, 12u, 0.0f }, Palette, { 1.0f, Aim.YawDeg, 0.0f, 0.0f }),
                FixPos, Palette.Resolve(StudioMaterialCode::DarkMetal), TripodTok, false);

            const Axis3D AimDir  = NormalizeAxis(HeroAimZUp - FixPos, { 0.0f, 0.0f, -1.0f });
            const Axis3D LampPos = FixPos + Axis3D{ 0.0f, 0.0f, 0.34f } + AimDir * 0.22f;
            Scene.AreaLights.push_back(MakeAimedAreaQuadLight(LampPos, HeroAimZUp, 1.15f, 0.68f, { 2.50f, 2.85f, 3.45f }, false));
            (void)Ctx.AppendLuminaireEntity(
                "Rim LED Area Light", "Rect Area Light · cool 6300 K contour rim separation", "light:area",
                LampPos, { 2u, 2.50f, 2.85f, 3.45f, 18.0f, 110.0f, true }, FixTok);
        }

        // Back-Right Accent Tripod -> Octa Softbox Strobe -> Accent Strobe IES Light
        {
            const Axis3D AccTripodHtml { 4.55f, 0.0f, -2.40f };
            TripodParams Tp { 1.66f, 0.36f, 26.0f, 0.016f, 3u, 0.0f, true, true, false, true };
            const float MountH = ComputeTripodMountHeight(Tp);
            const HtmlAimAngles Aim = ComputeHtmlAimAngles({ AccTripodHtml.X, MountH, AccTripodHtml.Z }, HeroAimHtml);
            Tp.HeadTiltDeg = Aim.PitchDeg;

            const InstanceToken TripodTok = Ctx.AppendSurfaceEntity(
                "rig-accent-tripod", "Back-Right Accent Tripod",
                "Shared procedural studio.tripod · wheeled accent strobe stand",
                BuildTripodSurface(Tp, Palette, { 1.0f, Aim.YawDeg, 0.0f, 0.0f }),
                ConvertHtmlPositionToWorldZUp(AccTripodHtml.X, AccTripodHtml.Y, AccTripodHtml.Z),
                Palette.Resolve(StudioMaterialCode::DarkMetal), LightingFolder, true);

            const Axis3D FixPos = ConvertHtmlPositionToWorldZUp(AccTripodHtml.X, MountH, AccTripodHtml.Z);
            const InstanceToken FixTok = Ctx.AppendSurfaceEntity(
                "rig-accent-fixture", "Octa Softbox Strobe",
                "Procedural studio.light_fixture · 8-sided studio octa modifier",
                BuildLightFixtureSurface({ "Octa Softbox", 0.96f, 0.96f, 0.42f, Aim.PitchDeg, 25.0f, 8u, 0.0f }, Palette, { 1.0f, Aim.YawDeg, 0.0f, 0.0f }),
                FixPos, Palette.Resolve(StudioMaterialCode::DarkMetal), TripodTok, false);

            const Axis3D AimDir  = NormalizeAxis(HeroAimZUp - FixPos, { 0.0f, 0.0f, -1.0f });
            const Axis3D LampPos = FixPos + Axis3D{ 0.0f, 0.0f, 0.22f } + AimDir * 0.32f;
            Scene.PhotometricRadiators.push_back(MakeAimedPhotometricSpotLight(LampPos, HeroAimZUp, { 12.5f, 13.0f, 14.5f }, 22.0f, 2u));
            (void)Ctx.AppendLuminaireEntity(
                "Accent Strobe IES Light", "IES Photometric · focused 5600 K cyclorama rim strobe", "light:ies",
                LampPos, { 3u, 12.5f, 13.0f, 14.5f, 22.0f, 42.0f, true }, FixTok);
        }
    }

    // 4. Camera Rigs Folder
    const InstanceToken CameraFolder = Ctx.CreateFolderEntry("Camera Rigs", "Editor camera & mounted studio cinema camera rigs", {});
    {
        (void)Ctx.AppendCameraEntity(
            "Editor Camera", "Permanent editor navigation camera · Orbit/Pan/Zoom",
            ConvertHtmlPositionToWorldZUp(5.75f, 1.62f, 6.45f), { 35.0f, 36.0f, 0.10f, 2500.0f, true }, CameraFolder, true);

        if (Policy.EnableCameraRigs)
        {
            const Axis3D CamATripodHtml { -3.25f, 0.0f, 5.75f };
            TripodParams TpA { 1.38f, 0.20f, 24.0f, 0.020f, 3u, 0.0f, true, false, true, true };
            const float MountHA = ComputeTripodMountHeight(TpA);
            const HtmlAimAngles AimA = ComputeHtmlAimAngles({ CamATripodHtml.X, MountHA, CamATripodHtml.Z }, HeroAimHtml);
            TpA.HeadTiltDeg = AimA.PitchDeg;

            const InstanceToken TripodATok = Ctx.AppendSurfaceEntity(
                "rig-cam-a-tripod", "Cinema Tripod A",
                "Shared procedural studio.tripod · heavy matte tripod + floor mat",
                BuildTripodSurface(TpA, Palette, { 1.0f, AimA.YawDeg, 0.0f, 0.0f }),
                ConvertHtmlPositionToWorldZUp(CamATripodHtml.X, CamATripodHtml.Y, CamATripodHtml.Z),
                Palette.Resolve(StudioMaterialCode::DarkMetal), CameraFolder, true);

            const Axis3D CamABodyPos = ConvertHtmlPositionToWorldZUp(CamATripodHtml.X, MountHA, CamATripodHtml.Z);
            const InstanceToken BodyATok = Ctx.AppendSurfaceEntity(
                "rig-cam-a-body", "Cinema Camera A",
                "Procedural studio.cinema_camera · twin-magazine cine camera",
                BuildCinemaCameraSurface({ "Cinema Matte Box", 0.34f, 0.13f, true, true, AimA.PitchDeg }, Palette, { 1.0f, AimA.YawDeg, 0.0f, 0.0f }),
                CamABodyPos, Palette.Resolve(StudioMaterialCode::DarkMetal), TripodATok, true);

            (void)Ctx.AppendCameraEntity(
                "Car Configurator Camera", "Configurator camera · 6 automotive studio presets",
                CamABodyPos, { 36.0f, 36.0f, 0.10f, 2000.0f, false }, BodyATok);

            const Axis3D CamBTripodHtml { 2.55f, 0.0f, 5.65f };
            TripodParams TpB { 1.24f, 0.18f, 25.0f, 0.018f, 3u, 0.0f, true, false, true, true };
            const float MountHB = ComputeTripodMountHeight(TpB);
            const HtmlAimAngles AimB = ComputeHtmlAimAngles({ CamBTripodHtml.X, MountHB, CamBTripodHtml.Z }, HeroAimHtml);
            TpB.HeadTiltDeg = AimB.PitchDeg;

            const InstanceToken TripodBTok = Ctx.AppendSurfaceEntity(
                "rig-cam-b-tripod", "Cinema Tripod B",
                "Shared procedural studio.tripod · foreground tether camera tripod",
                BuildTripodSurface(TpB, Palette, { 1.0f, AimB.YawDeg, 0.0f, 0.0f }),
                ConvertHtmlPositionToWorldZUp(CamBTripodHtml.X, CamBTripodHtml.Y, CamBTripodHtml.Z),
                Palette.Resolve(StudioMaterialCode::DarkMetal), CameraFolder, true);

            const Axis3D CamBBodyPos = ConvertHtmlPositionToWorldZUp(CamBTripodHtml.X, MountHB, CamBTripodHtml.Z);
            const InstanceToken BodyBTok = Ctx.AppendSurfaceEntity(
                "rig-cam-b-body", "Medium Format Camera B",
                "Procedural studio.cinema_camera · medium format studio camera",
                BuildCinemaCameraSurface({ "Medium Format Tether", 0.24f, 0.08f, true, true, AimB.PitchDeg }, Palette, { 1.0f, AimB.YawDeg, 0.0f, 0.0f }),
                CamBBodyPos, Palette.Resolve(StudioMaterialCode::DarkMetal), TripodBTok, true);

            (void)Ctx.AppendCameraEntity(
                "Profile 65mm Camera", "Side elevation 65 mm optical study camera",
                CamBBodyPos, { 65.0f, 36.0f, 0.10f, 2000.0f, false }, BodyBTok);
        }
    }

    // 5. Studio Props & Crew Folder
    const InstanceToken PropsFolder = Ctx.CreateFolderEntry("Studio Props & Crew", "Tether workstation, Pelican flight cases, cable runs & mats", {});
    if (Policy.EnableCrewProps)
    {
        (void)Ctx.AppendSurfaceEntity(
            "prop-workstation", "Tether Workstation & Desk",
            "Procedural studio.workstation · hairpin desk, laptop & flight cases",
            BuildWorkstationSurface({ 1.48f, 0.72f, 0.78f, true, true, true }, Palette, { 1.0f, -28.0f, 0.0f, 0.0f }),
            ConvertHtmlPositionToWorldZUp(5.9f, 0.0f, 6.45f), Palette.Resolve(StudioMaterialCode::DarkMetal), PropsFolder, true);

        (void)Ctx.AppendSurfaceEntity(
            "prop-grip-cases", "Grip Cases & Floor Mats",
            "Procedural studio.workstation · stage-left Pelican cases & cables",
            BuildWorkstationSurface({ 1.0f, 0.55f, 0.65f, false, true, true }, Palette, { 1.0f, 24.0f, 0.0f, 0.0f }),
            ConvertHtmlPositionToWorldZUp(-6.4f, 0.0f, 6.25f), Palette.Resolve(StudioMaterialCode::PelicanCase), PropsFolder, true);
    }

    // 6. World Atmosphere (Darkroom indoor stage)
    const InstanceToken WorldFolder = Ctx.CreateFolderEntry("World", "Interior studio ambient & tone balance", {});
    Ctx.AppendAtmosphereEntity(
        "Studio Ambient", "Interior darkroom exposure, bounce fill & floor reflection strength",
        { 8000.0f, 1200.0f, 1.0f, 120.0f, false }, WorldFolder);

    Scene.Atmosphere.Medium.AtmosphereEnabled = false;
    Scene.Atmosphere.Solar.SolarEnabled       = false;
    Scene.Atmosphere.Clouds.CloudsEnabled     = false;
    Scene.Sun.DirectionToSun                   = NormalizeAxis({ 0.25f, -0.45f, 0.85f }, { 0.0f, 0.0f, 1.0f });
    Scene.Sun.SolarIrradiance                  = { 0.0f, 0.0f, 0.0f };
    Scene.SkyRadiance                          = { 0.014f, 0.018f, 0.026f };

    const auto AddPreset = [&](std::string Id, std::string Label, Axis3D HtmlEye, Axis3D HtmlTarget, float FovDeg)
    {
        PerspectiveCameraBasis Cam {};
        Cam.CameraOrigin       = ConvertHtmlPositionToWorldZUp(HtmlEye.X, HtmlEye.Y, HtmlEye.Z);
        Cam.TargetPoint        = ConvertHtmlPositionToWorldZUp(HtmlTarget.X, HtmlTarget.Y, HtmlTarget.Z);
        Cam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
        Cam.VerticalFovRadians = FovDeg * Constants::DegToRadFloat;
        Cam.ViewportWidthPx    = 1280u;
        Cam.ViewportHeightPx   = 720u;
        Cam.NearClipMeters     = 0.10f;
        Ctx.AppendCameraPreset({ std::move(Id), std::move(Label), Cam });
    };
    AddPreset("hero-34",        "Hero 3/4",       {  5.75f, 1.62f, 6.45f }, {  0.05f, 0.78f, -0.15f }, 35.0f);
    AddPreset("low-front",      "Low Front",      {  3.85f, 0.85f, 5.15f }, { -0.10f, 0.72f,  0.00f }, 32.0f);
    AddPreset("rear-quarter",   "Rear Quarter",   { -5.45f, 1.54f, 5.45f }, {  0.15f, 0.76f, -0.25f }, 36.0f);
    AddPreset("profile-65",     "Profile 65mm",   {  0.15f, 1.22f, 8.25f }, {  0.00f, 0.78f, -0.15f }, 25.0f);
    AddPreset("overhead-crane", "Overhead Crane", {  5.40f, 4.45f, 8.10f }, {  0.00f, 0.70f, -0.40f }, 40.0f);
    AddPreset("crew-bts",       "Crew BTS",       { -7.40f, 1.92f, 8.85f }, {  0.25f, 1.12f, -0.40f }, 42.0f);
}

} // namespace AutoSpace
