/*============================================================================================================================================
                                                    PORTALANDVEHICLEVERIFICATION.CPP
============================================================================================================================================*/
// 🧩 Automated verification of 2D multi-world portal math, half-through-portal vehicle straddle & clip planes,
//    all-lobby live vehicle driving (including Lobby 1 -> Lobby 4 hot-switch), and World A -> World B Jolt portal transit.

#include "Engine/World/PortalSystem.h"
#include "Source/Gameplay/AutoSpaceLifecycle.h"
#include "Source/Gameplay/LobbyLifecycle.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{

bool CheckNear(float A, float B, float Tol, const char* Label)
{
    if (std::fabs(A - B) > Tol)
    {
        std::cerr << "[FAIL] " << Label << ": got " << A << ", expected " << B << " (tol=" << Tol << ")\n";
        return false;
    }
    return true;
}

bool VerifyPortalMathematics()
{
    using namespace Frontier::Mathematics;
    using namespace Frontier::World;

    PortalPairSpecification P {};
    P.Enabled            = true;
    P.Bidirectional      = true;
    P.Shape              = PortalApertureShapeCategory::Hexagon;
    P.HalfWidthMeters    = 2.85f;
    P.HalfHeightMeters   = 1.80f;
    P.RimThicknessMeters = 0.14f;
    P.Entry.CenterWorld     = { 11.5f, 0.0f, 1.56f };
    P.Entry.RightAxisWorld  = { 0.0f, -1.0f, 0.0f };
    P.Entry.UpAxisWorld     = { 0.0f,  0.0f, 1.0f };
    P.Entry.NormalAxisWorld = { -1.0f, 0.0f, 0.0f };
    P.Exit.CenterWorld      = { 0.0f, -680.0f, 1.56f };
    P.Exit.RightAxisWorld   = { 0.0f,  1.0f, 0.0f };
    P.Exit.UpAxisWorld      = { 0.0f,  0.0f, 1.0f };
    P.Exit.NormalAxisWorld  = { 1.0f,  0.0f, 0.0f };
    P.NormalizeAxes();

    // 1. Exact involution: Exit->Entry(Entry->Exit(X)) == X & det = +1 (zero handedness inversion!)
    const Axis3D SamplePoint { 10.2f, 0.65f, 1.95f };
    const Axis3D InWorldB  = P.TransformPointEntryToExit(SamplePoint);
    const Axis3D RoundTrip = P.TransformPointExitToEntry(InWorldB);
    if (!CheckNear(RoundTrip.X, SamplePoint.X, 1e-4f, "PointInvolution.X") ||
        !CheckNear(RoundTrip.Y, SamplePoint.Y, 1e-4f, "PointInvolution.Y") ||
        !CheckNear(RoundTrip.Z, SamplePoint.Z, 1e-4f, "PointInvolution.Z"))
    {
        return false;
    }

    const Axis3D TX = P.TransformVectorEntryToExit({ 1.0f, 0.0f, 0.0f });
    const Axis3D TY = P.TransformVectorEntryToExit({ 0.0f, 1.0f, 0.0f });
    const Axis3D TZ = P.TransformVectorEntryToExit({ 0.0f, 0.0f, 1.0f });
    const Axis3D Cxy {
        TX.Y * TY.Z - TX.Z * TY.Y,
        TX.Z * TY.X - TX.X * TY.Z,
        TX.X * TY.Y - TX.Y * TY.X
    };
    const float Det = Cxy.X * TZ.X + Cxy.Y * TZ.Y + Cxy.Z * TZ.Z;
    if (!CheckNear(Det, 1.0f, 1e-4f, "TransportDeterminantPlusOne"))
    {
        return false;
    }

    // 2. All 5 2D Planar Portal Aperture Shapes (center must be interior < 0, far point exterior > 0)
    const PortalApertureShapeCategory AllShapes[5] = {
        PortalApertureShapeCategory::Rectangle,
        PortalApertureShapeCategory::Circle,
        PortalApertureShapeCategory::Hexagon,
        PortalApertureShapeCategory::RegularPolygon,
        PortalApertureShapeCategory::CustomCutout,
    };
    P.PolygonSides = 8u;
    P.CustomPolygonUv = {
        { -2.45f, -1.85f }, {  2.45f, -1.85f }, {  2.75f,  0.15f },
        {  1.65f,  0.65f }, {  2.05f,  1.65f }, {  0.00f,  2.08f },
        { -2.05f,  1.65f }, { -1.65f,  0.65f }, { -2.75f,  0.15f },
    };
    for (const auto S : AllShapes)
    {
        P.Shape = S;
        const float CenterSdf = P.EvaluateShapeSignedDistance2D(0.0f, 0.0f);
        const float FarSdf    = P.EvaluateShapeSignedDistance2D(6.0f, 0.0f);
        if (!(CenterSdf < -0.5f && FarSdf > 1.0f))
        {
            std::cerr << "[FAIL] Shape " << static_cast<int>(S)
                      << " SDF check failed: center=" << CenterSdf << " far=" << FarSdf << "\n";
            return false;
        }
    }
    // Verify concave notch on CustomCutout is exterior (> 0) even though inside bounding box!
    P.Shape = PortalApertureShapeCategory::CustomCutout;
    const float ConcaveNotchSdf = P.EvaluateShapeSignedDistance2D(2.35f, 0.90f);
    if (!(ConcaveNotchSdf > 0.05f))
    {
        std::cerr << "[FAIL] Concave cutout notch should be exterior (>0), got " << ConcaveNotchSdf << "\n";
        return false;
    }

    // 3. Straddle evaluation right at portal threshold
    P.Shape = PortalApertureShapeCategory::Hexagon;
    const std::vector<PortalPairSpecification> PortalList { P };
    const auto Straddle = EvaluateVehiclePortalOverlap(
        PortalList,
        { 11.5f, 0.0f, 0.55f },
        0.0f,
        2.35f,
        0.96f,
        0.65f);
    if (!Straddle.OverlapActive ||
        !CheckNear(Straddle.ExposedFractionDst, 0.5f, 0.08f, "StraddleFractionHalf") ||
        !CheckNear(Straddle.ClonePositionWorld.Y, -680.0f, 0.1f, "CloneWorldBY"))
    {
        std::cerr << "[FAIL] Straddle evaluation failed: overlapping=" << Straddle.OverlapActive
                  << " frac=" << Straddle.ExposedFractionDst << "\n";
        return false;
    }

    std::cout << "[OK] Portal 2D shapes (5/5 incl. concave cutout), det=+1 involution, and half-straddle clip planes\n";
    return true;
}

bool VerifyLobbySwitchDrivingTransit()
{
    using namespace AutoSpace;
    using namespace Frontier::Mathematics;
    using namespace Frontier::SimulationSpace::PhysicsSpace;

    const std::filesystem::path ProjectRoot = std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Projects" / "AutoSpace";
    AutoSpaceLifecycle Session {};
    std::string Diagnostic {};

    // Boot in Lobby 1 (reproducing the exact user workflow where Lobby 1 booted first, then switched to Lobby 4!)
    if (!Session.Initialize(ProjectRoot, /*HeadlessWindow=*/true, Diagnostic, StudioLobbyId::StudioLobby1Cyclorama))
    {
        std::cerr << "[FAIL] Session.Initialize(Lobby1) failed: " << Diagnostic << "\n";
        return false;
    }

    // 1. Verify Lobby 1 hero car actually drives!
    if (!Session.InspectLobbyLifecycle().QueryHeroVehiclePhysicsActive())
    {
        std::cerr << "[FAIL] Lobby 1 hero vehicle physics is inactive!\n";
        return false;
    }
    {
        auto& Lobby = Session.AccessLobbyLifecycle();
        Axis3D StartPos {};
        float  StartYaw = 0.0f;
        Lobby.QueryHeroVehicleWorldPose(StartPos, StartYaw);
        VehicleControlDirective Cmd {};
        Cmd.Category           = VehicleDirectiveCategory::DriverInput;
        Cmd.ThrottleNormalized = 1.0f;
        (void)Lobby.SubmitHeroVehicleDirective(Cmd);
        for (int Step = 0; Step < 90; ++Step)
        {
            Lobby.TickSimulation(1.0 / 60.0);
        }
        Axis3D EndPos {};
        float  EndYaw = 0.0f;
        Lobby.QueryHeroVehicleWorldPose(EndPos, EndYaw);
        const float Dist = std::hypot(EndPos.X - StartPos.X, EndPos.Y - StartPos.Y);
        if (Dist < 4.0f)
        {
            std::cerr << "[FAIL] Lobby 1 car barely moved: dist=" << Dist << " m\n";
            return false;
        }
        std::cout << "[OK] Lobby 1 live driving: moved " << Dist << " m (yaw=" << EndYaw << " deg)\n";
    }

    // 2. Hot-switch Lobby 1 -> Lobby 4 (the exact bug where physics stayed disabled after switching!)
    if (!Session.SelectStudioLobby(StudioLobbyId::StudioLobby4TrainingGround, Diagnostic))
    {
        std::cerr << "[FAIL] SelectStudioLobby(Lobby4) failed: " << Diagnostic << "\n";
        return false;
    }
    if (!Session.InspectLobbyLifecycle().QueryHeroVehiclePhysicsActive())
    {
        std::cerr << "[FAIL] Lobby 4 hero vehicle physics stayed disabled after hot-switch from Lobby 1!\n";
        return false;
    }

    // 3. Drive straight into the World A 2D portal (X=11.5m) and confirm seamless World A -> World B Jolt transit!
    {
        auto& Lobby = Session.AccessLobbyLifecycle();
        VehicleControlDirective Cmd {};
        Cmd.Category           = VehicleDirectiveCategory::DriverInput;
        Cmd.ThrottleNormalized = 1.0f;
        (void)Lobby.SubmitHeroVehicleDirective(Cmd);
        bool ObservedStraddle = false;
        bool ObservedSplitWheelWorlds = false;
        for (int Step = 0; Step < 140; ++Step)
        {
            Lobby.TickSimulation(1.0 / 60.0);
            Axis3D CurPos {};
            float  CurYaw = 0.0f;
            Lobby.QueryHeroVehicleWorldPose(CurPos, CurYaw);
            const auto Ov = Frontier::World::EvaluateVehiclePortalOverlap(
                Lobby.InspectWorldScene().Portals, CurPos, CurYaw, 2.35f, 0.96f, 0.65f);
            if (Ov.OverlapActive)
            {
                ObservedStraddle = true;
            }
            if (Lobby.QueryHeroWheelPortalSide(0u) != Lobby.QueryHeroWheelPortalSide(2u))
            {
                ObservedSplitWheelWorlds = true;
            }
        }
        Axis3D FinalPos {};
        float  FinalYaw = 0.0f;
        Lobby.QueryHeroVehicleWorldPose(FinalPos, FinalYaw);
        if (!ObservedStraddle || !ObservedSplitWheelWorlds)
        {
            std::cerr << "[FAIL] Vehicle did not experience split-world straddle (straddle=" << ObservedStraddle
                      << ", splitWheels=" << ObservedSplitWheelWorlds << ")!\n";
            return false;
        }
        if (Lobby.QueryHeroPortalTransitCount() < 1u ||
            !(FinalPos.Y < -600.0f && FinalPos.Z > -1.0f && FinalPos.Z < 3.0f))
        {
            std::cerr << "[FAIL] Vehicle did not cleanly transit onto World B deck (transits="
                      << Lobby.QueryHeroPortalTransitCount() << ", pos=("
                      << FinalPos.X << ", " << FinalPos.Y << ", " << FinalPos.Z << "))\n";
            return false;
        }
        std::cout << "[OK] Lobby 1 -> Lobby 4 hot-switch driving + split-wheel dual-world physics + World A -> World B transit: transits="
                  << Lobby.QueryHeroPortalTransitCount()
                  << " final=(" << FinalPos.X << ", " << FinalPos.Y << ", " << FinalPos.Z << ") m\n";
    }

    Session.Finalize();
    return true;
}

} // namespace

int main()
{
    if (!VerifyPortalMathematics())
    {
        return 1;
    }
    if (!VerifyLobbySwitchDrivingTransit())
    {
        return 2;
    }
    std::cout << "[ALL OK] PortalVehicleVerification succeeded!\n";
    return 0;
}
