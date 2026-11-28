/*============================================================================================================================================
                                                    LOBBY4TRAININGGROUNDLEVEL.CPP
============================================================================================================================================*/
// 🧩 Lobby 4 · Training Ground: flat proving-ground asphalt plane with full Jolt driving physics & live Eulerian tire smoke

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Levels/StudioLobbyWorldBuilder.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/World/PortalSystem.h"

#include <cmath>

namespace AutoSpace
{

namespace
{

[[nodiscard]] StudioBuiltMesh BuildWorldBSunsetDeckSurface(const StudioMaterialPalette& Palette, uint16_t DeckMat, uint16_t PillarLeftMat, uint16_t PillarRightMat, uint16_t ArchGoldMat, uint16_t BeaconMat)
{
    using namespace Frontier::Mathematics;

    StudioBuiltMesh Out {};
    Out.Network.NetworkTitle = "WorldBSunsetProvingDeck";

    auto AppendBox = [&](Axis3D Center, Axis3D HalfExt, uint16_t MatSlot)
    {
        const float X0 = Center.X - HalfExt.X, X1 = Center.X + HalfExt.X;
        const float Y0 = Center.Y - HalfExt.Y, Y1 = Center.Y + HalfExt.Y;
        const float Z0 = Center.Z - HalfExt.Z, Z1 = Center.Z + HalfExt.Z;

        auto PushQuad = [&](Axis3D A, Axis3D B, Axis3D C, Axis3D D, Axis3D N)
        {
            const uint32_t IA = Out.Network.AppendVertex(A);
            const uint32_t IB = Out.Network.AppendVertex(B);
            const uint32_t IC = Out.Network.AppendVertex(C);
            const uint32_t ID = Out.Network.AppendVertex(D);
            Out.Network.Vertices[IA].Normal = N;
            Out.Network.Vertices[IB].Normal = N;
            Out.Network.Vertices[IC].Normal = N;
            Out.Network.Vertices[ID].Normal = N;

            const std::array<uint32_t, 3u> T0 { IA, IB, IC };
            const std::array<uint32_t, 3u> T1 { IA, IC, ID };
            const std::array<UVAxes, 3u>   Uv0 { UVAxes { 0.0f, 0.0f }, UVAxes { 1.0f, 0.0f }, UVAxes { 1.0f, 1.0f } };
            const std::array<UVAxes, 3u>   Uv1 { UVAxes { 0.0f, 0.0f }, UVAxes { 1.0f, 1.0f }, UVAxes { 0.0f, 1.0f } };
            Frontier::GeometrySpace::FaceShadingAssignment Shading {};
            Shading.AlbedoLinearRGB = { 0.5f, 0.5f, 0.5f };

            const uint32_t F0 = Out.Network.AppendFace(T0, Uv0, Shading, 0u, MatSlot);
            const uint32_t F1 = Out.Network.AppendFace(T1, Uv1, Shading, 0u, MatSlot);
            if (F0 != Frontier::GeometrySpace::InvalidNetworkIndex)
            {
                for (int K = 0; K < 3; ++K)
                {
                    Out.AuthoredCornerNormals.push_back(N.X);
                    Out.AuthoredCornerNormals.push_back(N.Y);
                    Out.AuthoredCornerNormals.push_back(N.Z);
                }
            }
            if (F1 != Frontier::GeometrySpace::InvalidNetworkIndex)
            {
                for (int K = 0; K < 3; ++K)
                {
                    Out.AuthoredCornerNormals.push_back(N.X);
                    Out.AuthoredCornerNormals.push_back(N.Y);
                    Out.AuthoredCornerNormals.push_back(N.Z);
                }
            }
        };

        PushQuad({ X0, Y0, Z1 }, { X1, Y0, Z1 }, { X1, Y1, Z1 }, { X0, Y1, Z1 }, { 0.0f, 0.0f, 1.0f });
        PushQuad({ X0, Y1, Z0 }, { X1, Y1, Z0 }, { X1, Y0, Z0 }, { X0, Y0, Z0 }, { 0.0f, 0.0f,-1.0f });
        PushQuad({ X1, Y0, Z0 }, { X1, Y1, Z0 }, { X1, Y1, Z1 }, { X1, Y0, Z1 }, { 1.0f, 0.0f, 0.0f });
        PushQuad({ X0, Y1, Z0 }, { X0, Y0, Z0 }, { X0, Y0, Z1 }, { X0, Y1, Z1 }, {-1.0f, 0.0f, 0.0f });
        PushQuad({ X0, Y1, Z0 }, { X0, Y1, Z1 }, { X1, Y1, Z1 }, { X1, Y1, Z0 }, { 0.0f, 1.0f, 0.0f });
        PushQuad({ X1, Y0, Z0 }, { X1, Y0, Z1 }, { X0, Y0, Z1 }, { X0, Y0, Z0 }, { 0.0f,-1.0f, 0.0f });
    };

    // Subdivided 6x6 terracotta-slate sunset deck (48 m wide x 72 m long, flush at local Z = 0 -> world Z = 0.0 m)
    constexpr int GridX = 6;
    constexpr int GridY = 6;
    for (int IX = 0; IX < GridX; ++IX)
    {
        for (int IY = 0; IY < GridY; ++IY)
        {
            const float CX = -10.0f + (static_cast<float>(IX) + 0.5f) * 12.0f;
            const float CY = -30.0f + (static_cast<float>(IY) + 0.5f) * 10.0f;
            AppendBox({ CX, CY, -0.25f }, { 6.0f, 5.0f, 0.25f }, DeckMat);
        }
    }

    // Vivid Crimson-Coral Left Colonnade + Emerald-Teal Right Colonnade + Golden Sunset Overhead Arches
    // (unmistakably distinct destination world visible through the 2D portal opening + rich multi-bounce albedo color bleed!)
    for (int Bay = 0; Bay < 4; ++Bay)
    {
        const float BayX = 9.5f + static_cast<float>(Bay) * 8.5f;
        AppendBox({ BayX,  4.8f, 2.6f }, { 0.55f, 0.55f, 2.6f }, PillarLeftMat);
        AppendBox({ BayX, -4.8f, 2.6f }, { 0.55f, 0.55f, 2.6f }, PillarRightMat);
        AppendBox({ BayX,  0.0f, 5.35f }, { 0.50f, 5.4f, 0.35f }, ArchGoldMat);
        // Glowing warm sunset lane beacons flanking the runway
        AppendBox({ BayX,  3.5f, 0.14f }, { 0.90f, 0.14f, 0.14f }, BeaconMat);
        AppendBox({ BayX, -3.5f, 0.14f }, { 0.90f, 0.14f, 0.14f }, BeaconMat);
    }

    // Distant Sunset Monolith Backdrop at X = +46 m (frames dramatic crimson/gold silhouette inside the portal view)
    AppendBox({ 46.0f,  0.0f, 4.5f }, { 0.8f, 11.0f, 4.5f }, ArchGoldMat);
    AppendBox({ 45.1f,  0.0f, 3.2f }, { 0.2f,  6.5f, 0.45f }, BeaconMat);
    (void)Palette;
    return Out;
}

} // namespace

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
        "Live Jolt VehiclePhysicsRig",
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
            "Live drivable Jolt vehicle (1:1 scale, +X heading) with GT cockpit & HMI",
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

        // Synchronized Exit-Side Portal Clone (hidden unless straddling a portal plane; clipped strictly to exposed half!)
        (void)Ctx.AppendSurfaceEntity(
            "vehicle-hero-portal-clone",
            "Hero Coupe · Portal Exit Projection",
            "Synchronized half-clipped exit projection active only while straddling a multi-world portal",
            BuildAssembledControlVehicleSurface(
                VehicleTemplate,
                Palette,
                HeroPaint,
                HeroBrake,
                { 1.0f, 0.0f, 0.0f, 0.0f },
                "HeroCoupePortalExitClone",
                false),
            { 0.0f, -680.0f, 0.0f },
            HeroPaint,
            VehiclesFolder,
            true);
        if (!Scene.ClusterInstances.empty())
        {
            Scene.ClusterInstances.back().VisibilityEnabled = false;
        }
    }

    // 2. Flat Proving Ground Plane Folder (World A at Z = 0 m + World B Sunset Deck at X = 0, Y = -680, Z = 0 m)
    const InstanceToken GroundFolder = Ctx.CreateFolderEntry(
        "Training Ground Plane",
        "World A daylight asphalt (Z=0) + World B sunset proving deck (X=0, Y=-680, Z=0) linked by 2D portal",
        {});
    {
        (void)Ctx.AppendSurfaceEntity(
            "training-flat-plane",
            "Flat Proving Ground Plane (World A)",
            "240 m x 240 m level asphalt plane at Z = 0.0 m",
            BuildTrainingFlatPlaneSurface(240.0f, 12.0f, Palette),
            { 0.0f, 0.0f, 0.0f },
            Palette.Resolve(StudioMaterialCode::TrackAsphalt),
            GroundFolder,
            false);

        const uint16_t WorldBDeckMat = RegisterVehicleBodyPaintMaterial(
            Scene.MaterialRegister, "WorldBTerracottaDeck", SrgbHexToLinearRgb(0x6d4a3a), 0.04f, 0.62f, 0.0f);
        const uint16_t WorldBCrimsonMat = RegisterVehicleBodyPaintMaterial(
            Scene.MaterialRegister, "WorldBCrimsonPillar", SrgbHexToLinearRgb(0xdc3428), 0.06f, 0.44f, 0.15f);
        const uint16_t WorldBEmeraldMat = RegisterVehicleBodyPaintMaterial(
            Scene.MaterialRegister, "WorldBEmeraldPillar", SrgbHexToLinearRgb(0x18a88c), 0.06f, 0.44f, 0.15f);
        const uint16_t WorldBGoldMat = RegisterVehicleBodyPaintMaterial(
            Scene.MaterialRegister, "WorldBGoldArch", SrgbHexToLinearRgb(0xd89b38), 0.58f, 0.28f, 0.30f);
        uint16_t WorldBBeaconMat = 0u;
        {
            Frontier::MaterialSpace::SurfaceMaterial BeaconSpec {};
            BeaconSpec.Diffuse.BaseColor                        = { 1.0f, 0.72f, 0.24f };
            BeaconSpec.Specular.Roughness                       = 0.35f;
            BeaconSpec.PorosityLight.EmissionLuminanceNits      = 12000.0f;
            BeaconSpec.PorosityLight.BlackbodyTemperatureKelvin = 0.0f;
            BeaconSpec.PorosityLight.EmissionTint               = { 1.0f, 0.68f, 0.22f };
            WorldBBeaconMat = Scene.MaterialRegister.RegisterMaterial(BeaconSpec);
        }

        (void)Ctx.AppendSurfaceEntity(
            "world-b-sunset-deck",
            "World B · Sunset Proving Deck",
            "Distant sunset proving deck (X=0, Y=-680, Z=0 m) with crimson/emerald bounce colonnade & low-grip surface",
            BuildWorldBSunsetDeckSurface(Palette, WorldBDeckMat, WorldBCrimsonMat, WorldBEmeraldMat, WorldBGoldMat, WorldBBeaconMat),
            { 0.0f, -680.0f, 0.0f },
            WorldBDeckMat,
            GroundFolder,
            true);

        // Configure Live Multi-World 2D Portal Pair linking World A (X = 11.5 m) <-> World B (X = 0, Y = -680, Z = 0 m)
        PortalPairSpecification HeroPortal {};
        HeroPortal.Enabled            = true;
        HeroPortal.Bidirectional      = true;
        HeroPortal.Shape              = PortalApertureShapeCategory::Hexagon;
        HeroPortal.HalfWidthMeters    = 2.85f;
        HeroPortal.HalfHeightMeters   = 1.80f;
        HeroPortal.RimThicknessMeters = 0.14f;
        HeroPortal.RimColorEntryRgb   = { 0.16f, 1.15f, 2.40f }; // Luminous Cyan rim on World A side
        HeroPortal.RimColorExitRgb    = { 2.45f, 0.92f, 0.16f }; // Luminous Sunset Gold rim on World B side

        HeroPortal.Entry.CenterWorld               = { 11.5f, 0.0f, 1.56f };
        HeroPortal.Entry.RightAxisWorld            = { 0.0f, -1.0f, 0.0f };
        HeroPortal.Entry.UpAxisWorld               = { 0.0f,  0.0f, 1.0f };
        HeroPortal.Entry.NormalAxisWorld           = { -1.0f, 0.0f, 0.0f };
        HeroPortal.Entry.WorldRegionOrdinal        = 0u;
        HeroPortal.Entry.SurfaceFrictionMultiplier = 1.12f; // Dry daylight asphalt
        HeroPortal.Entry.GravityScale              = 1.00f;
        HeroPortal.Entry.SkyUpperRgb              = { 0.14f, 0.34f, 0.78f };
        HeroPortal.Entry.SkyLowerRgb             = { 0.68f, 0.80f, 0.94f };
        HeroPortal.Entry.SunDirectionWorld         = NormalizeAxis({ -0.52f, 0.56f, 0.64f }, { 0.0f, 0.0f, 1.0f });
        HeroPortal.Entry.SunRadianceRgb            = { 3.35f, 3.22f, 2.98f };

        HeroPortal.Exit.CenterWorld                = { 0.0f, -680.0f, 1.56f };
        HeroPortal.Exit.RightAxisWorld             = { 0.0f,  1.0f, 0.0f };
        HeroPortal.Exit.UpAxisWorld                = { 0.0f,  0.0f, 1.0f };
        HeroPortal.Exit.NormalAxisWorld            = { 1.0f,  0.0f, 0.0f };
        HeroPortal.Exit.WorldRegionOrdinal         = 1u;
        HeroPortal.Exit.SurfaceFrictionMultiplier  = 0.48f; // Wet/low-grip sunset deck
        HeroPortal.Exit.GravityScale               = 0.82f; // Slightly lighter elevated world gravity
        HeroPortal.Exit.SkyUpperRgb               = { 0.11f, 0.07f, 0.30f }; // Deep twilight violet zenith
        HeroPortal.Exit.SkyLowerRgb              = { 2.15f, 0.66f, 0.22f }; // Glowing golden-crimson sunset horizon
        HeroPortal.Exit.SunDirectionWorld          = NormalizeAxis({ 0.84f, 0.28f, 0.46f }, { 1.0f, 0.0f, 0.0f });
        HeroPortal.Exit.SunRadianceRgb             = { 3.80f, 1.65f, 0.55f };
        HeroPortal.NormalizeAxes();

        Scene.Portals.clear();
        Scene.Portals.push_back(HeroPortal);
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
    // Preset 5: World B Portal Exit Observer Cam (frames the World B sunset deck & emerging vehicle half!)
    RegisterDrivingPreset("training-portal-exit", "World B Portal Exit Cam", { 7.4f, -680.0f - 3.6f, 2.05f }, { 0.35f, -680.0f, 1.05f }, 48.0f);
}

} // namespace AutoSpace
