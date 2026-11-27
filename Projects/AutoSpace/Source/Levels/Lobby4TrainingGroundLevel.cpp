/*============================================================================================================================================
                                                    LOBBY4TRAININGGROUNDLEVEL.CPP
============================================================================================================================================*/
// 🧩 Lobby 4 · Training Ground: flat proving-ground asphalt plane with full Jolt driving physics & live Eulerian tire smoke

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Levels/StudioLobbyWorldBuilder.h"

#include "Engine/Constants/MathConstants.h"

#include <cmath>

namespace AutoSpace
{

void BuildLobby4TrainingGroundLevel(IStudioLobbySceneBuilderContext& Ctx, const LobbyPolicy& /*Policy*/)
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::Mathematics;
    using namespace Frontier::World;

    const auto& VehicleTemplate = Ctx.InspectControlVehicle();
    const auto& Palette         = Ctx.InspectMaterialPalette();
    auto&       Scene           = Ctx.AccessWorldScene();

    // 1. Hero Driving Vehicle Folder (1:1 physical meters scale, facing +X matching Jolt VehiclePhysicsRig)
    const InstanceToken VehiclesFolder = Ctx.CreateFolderEntry(
        "Training Vehicle",
        "Live Jolt VehiclePhysicsRig + Eulerian Tire Fluid Smoke",
        {});
    {
        const uint16_t HeroPaint = RegisterVehicleBodyPaintMaterial(
            Scene.MaterialRegister,
            "ProvingGroundSapphire",
            SrgbHexToLinearRgb(0x143b78),
            0.86f,
            0.13f,
            1.00f);
        const uint16_t HeroBrake = RegisterVehicleBodyPaintMaterial(
            Scene.MaterialRegister,
            "TrackCrimsonCaliper",
            SrgbHexToLinearRgb(0xd42228),
            0.35f,
            0.20f,
            0.88f);
        (void)Ctx.AppendSurfaceEntity(
            "vehicle-hero",
            "Hero Coupe · Training Ground",
            "Live drivable Jolt vehicle (1:1 scale, +X heading) with GT cockpit, HMI & 3D Eulerian tire smoke",
            BuildAssembledControlVehicleSurface(
                VehicleTemplate,
                Palette,
                HeroPaint,
                HeroBrake,
                { 1.0f, 0.0f, 0.0f, 0.0f },
                "HeroCoupeTrainingGround",
                true),
            { 0.0f, 0.0f, 0.0f },
            HeroPaint,
            VehiclesFolder,
            true);
        Ctx.IncrementVehicleCount();
    }

    // 2. Flat Proving Ground Plane Folder (240 m x 240 m flat asphalt deck at Z = 0)
    const InstanceToken GroundFolder = Ctx.CreateFolderEntry(
        "Training Ground Plane",
        "240 m x 240 m flat asphalt proving surface with lane & 18 m skidpad markings",
        {});
    {
        (void)Ctx.AppendSurfaceEntity(
            "training-flat-plane",
            "Flat Proving Ground Plane",
            "240 m x 240 m level asphalt plane at Z = 0.0 m",
            BuildTrainingFlatPlaneSurface(240.0f, 12.0f, Palette),
            { 0.0f, 0.0f, 0.0f },
            Palette.Resolve(StudioMaterialCode::TrackAsphalt),
            GroundFolder,
            false);
    }

    // 3. Daylight Atmosphere & Directional Solar Lighting (enables crisp vehicle shadows + volumetric tire smoke scattering)
    const InstanceToken SkyFolder = Ctx.CreateFolderEntry(
        "Daylight Atmosphere",
        "Clear morning proving-ground sun & volumetric smoke scattering sky",
        {});
    {
        constexpr float SunAzimuthDeg   = 132.0f;
        constexpr float SunElevationDeg = 36.0f;
        const float AzRad = SunAzimuthDeg * Constants::DegToRadFloat;
        const float ElRad = SunElevationDeg * Constants::DegToRadFloat;
        const Axis3D SunDir = NormalizeAxis(
            { std::cos(ElRad) * std::cos(AzRad),
              std::cos(ElRad) * std::sin(AzRad),
              std::sin(ElRad) },
            { -0.5f, 0.6f, 0.6f });

        Scene.Atmosphere.Medium.AtmosphereEnabled       = true;
        Scene.Atmosphere.Medium.SkyTint                 = { 0.92f, 0.97f, 1.04f };
        Scene.Atmosphere.Medium.SkyBrightnessMultiplier = 1.15f;
        Scene.Atmosphere.Solar.SolarEnabled             = true;
        Scene.Atmosphere.Solar.ManualDirectionOverride  = true;
        Scene.Atmosphere.Solar.ManualAzimuthDegrees     = SunAzimuthDeg;
        Scene.Atmosphere.Solar.ManualElevationDegrees   = SunElevationDeg;
        Scene.Atmosphere.Solar.SolarIntensityMultiplier = 24.0f;
        Scene.Atmosphere.Solar.DirectGainMultiplier     = 3.0f;
        Scene.Atmosphere.Clouds.CloudsEnabled           = false;
        Scene.Atmosphere.Fog.HeightFog.Enabled          = false;
        Scene.Atmosphere.Fog.AerialFog.Enabled          = false;
        Scene.Atmosphere.Fog.GodRays.Enabled            = false;
        Scene.Atmosphere.Fog.LocalFogs.clear();

        Scene.Sun.DirectionToSun  = SunDir;
        Scene.Sun.SolarIrradiance = { 3.35f, 3.22f, 2.98f };
        Scene.SkyRadiance         = { 0.36f, 0.52f, 0.78f };

        AtmosphereInstanceParameters AtmoParams {};
        AtmoParams.VisibilityEnabled = true;
        Ctx.AppendAtmosphereEntity(
            "Proving Ground Daylight Sky",
            "Physical Rayleigh/Mie daylight sky + raking 36 deg morning sun",
            AtmoParams,
            SkyFolder);
    }

    // 4. Driving & Cockpit Camera Presets (all auto-track live vehicle pose when driving in AutoSpace Game Mode)
    const InstanceToken CamerasFolder = Ctx.CreateFolderEntry(
        "Driving Cameras",
        "Chase, Drift Quarter, Interior Cockpit, HMI Focus & Showcase Orbit views",
        {});
    auto RegisterDrivingPreset = [&](std::string_view Id, std::string_view Label, Axis3D Eye, Axis3D LookTarget, float FovDeg)
    {
        PerspectiveCameraBasis Cam {};
        Cam.CameraOrigin       = Eye;
        Cam.TargetPoint        = LookTarget;
        Cam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
        Cam.VerticalFovRadians = FovDeg * Constants::DegToRadFloat;
        Cam.ViewportWidthPx    = 1280u;
        Cam.ViewportHeightPx   = 720u;
        Cam.NearClipMeters     = 0.04f;
        Ctx.AppendCameraPreset({ std::string(Id), std::string(Label), Cam });

        CameraInstanceParameters CamP {};
        CamP.FocalLengthMillimeters = 18.0f / std::max(0.15f, std::tan(0.5f * FovDeg * Constants::DegToRadFloat));
        CamP.NearPlaneMeters        = 0.04f;
        (void)Ctx.AppendCameraEntity(Label, "Training Ground driving camera preset", Eye, CamP, CamerasFolder, true);
    };

    const VehicleCockpitAnchorLayout Cockpit = QueryControlVehicleCockpitLayout();
    // Preset 0: Dynamic Chase Cam (behind car along -X looking forward along +X)
    RegisterDrivingPreset("training-chase", "Chase Cam", { -6.6f, -0.35f, 2.25f }, { 1.6f, 0.0f, 0.92f }, 48.0f);
    // Preset 1: Drift Quarter Cam (low rear-left 3/4 framing rear tyres & smoke plumes)
    RegisterDrivingPreset("training-drift", "Drift Quarter Cam", { -5.3f, 3.35f, 1.55f }, { -0.45f, 0.0f, 0.72f }, 46.0f);
    // Preset 2: Interior Driver Cockpit Cam (frames steering wheel, cluster, windshield HUD & angled HMI)
    RegisterDrivingPreset("training-cockpit", "Interior Cockpit Cam", Cockpit.DriverEyeLocal, Cockpit.DriverLookLocal, 64.0f);
    // Preset 3: HMI Touchscreen Focus Cam (angled close-up for ergonomic 3D button clicking & 2nd-engine feed)
    RegisterDrivingPreset("training-hmi", "HMI Focus Cam", Cockpit.HmiFocusEyeLocal, Cockpit.HmiFocusLookLocal, 46.0f);
    // Preset 4: Front 3/4 Showcase Orbit Cam
    RegisterDrivingPreset("training-orbit", "Showcase Orbit Cam", { 5.8f, -4.4f, 2.15f }, { 0.0f, 0.0f, 0.78f }, 42.0f);
}

} // namespace AutoSpace
