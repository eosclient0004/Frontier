/*============================================================================================================================================
                                                VEHICLECOCKPITINSTRUMENTATION.H
============================================================================================================================================*/
// 🚘 Native automotive cockpit displays, 2nd-render-engine HMI compositor, optical windshield HUD, and 3D ray-plane picking

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/Mathematics/CoordinateSpace.h"
#include "Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h"
#include "Source/Geometry/StudioProceduralSurface.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace AutoSpace
{

// Selectable camera feeds rendered by the secondary VulkanWorldRenderSequence inside the in-car HMI
enum class HmiCameraFeedMode : uint8_t
{
    RearBackupCam = 0u,
    ChaseFollowCam,
    FrontBumperCam,
    DriftWheelCam,
    Count
};

// Render engine override mode for the HMI secondary renderer (SyncToMain strictly mirrors main viewport engine)
enum class HmiEngineSyncMode : uint8_t
{
    SyncToMain = 0u,
    ForceOpenPbr,
    ForceSdfGi,
    ForceMatCap,
    ForcePathTracer,
    Count
};

// Interactive clickable buttons rendered on the 3D center-console HMI touchscreen
enum class HmiButtonId : uint8_t
{
    None = 0u,
    CycleFeedCam,
    CycleHmiEngine,
    ToggleWindshieldHud,
    ToggleStabilityAssist,
    ToggleTransmissionMode,
    Count
};

struct HmiControlPanel
{
    HmiCameraFeedMode FeedMode               = HmiCameraFeedMode::RearBackupCam;
    HmiEngineSyncMode EngineMode             = HmiEngineSyncMode::SyncToMain;
    bool              WindshieldHudEnabled   = true;
    bool              OnScreenHudEnabled     = true;
    HmiButtonId       HoveredButton          = HmiButtonId::None;
    HmiButtonId       LastClickedButton      = HmiButtonId::None;
    float             ClickFlashSeconds      = 0.0f;
};

struct CockpitTelemetryReading
{
    float       SpeedKph              = 0.0f;
    float       EngineRpm             = 850.0f;
    float       MaxRpm                = 7800.0f;
    int32_t     Gear                  = 1;
    float       Throttle01            = 0.0f;
    float       Brake01               = 0.0f;
    float       Handbrake01           = 0.0f;
    float       SteerInput            = 0.0f;
    float       VehicleYawDeg         = 0.0f;
    float       RearSlipRatio         = 0.0f;
    float       WheelSlipRatios[4]    { 0.0f, 0.0f, 0.0f, 0.0f };
    bool        StabilityAssistOn     = true;
    bool        AutomaticTransmission = true;
    bool        AbsActive             = false;
    bool        InteriorViewActive    = false;
    std::string ActiveCameraLabel     {};
    Frontier::GeometricRaster::WorldRenderEngine MainRenderEngine =
        Frontier::GeometricRaster::WorldRenderEngine::RealTimeGiGtxLowEnd;
    Frontier::GeometricRaster::WorldRenderEngine ActiveHmiRenderEngine =
        Frontier::GeometricRaster::WorldRenderEngine::RealTimeGiGtxLowEnd;
};

struct HmiRayHitResult
{
    bool        HitScreen     = false;
    float       U             = 0.0f;
    float       V             = 0.0f;
    HmiButtonId HoveredButton = HmiButtonId::None;
};

// Owns the 4 packed cockpit RGBA8 display buffers (HMI, Dashboard Cluster, Windshield Glass HUD, On-Screen HUD)
class VehicleCockpitInstrumentation
{
public:
    static constexpr uint32_t HmiWidthPx         = 512u;
    static constexpr uint32_t HmiHeightPx        = 288u;
    static constexpr uint32_t HmiFeedWidthPx     = 304u;
    static constexpr uint32_t HmiFeedHeightPx    = 216u;
    static constexpr uint32_t ClusterWidthPx     = 512u;
    static constexpr uint32_t ClusterHeightPx    = 192u;
    static constexpr uint32_t WindshieldWidthPx  = 512u;
    static constexpr uint32_t WindshieldHeightPx = 256u;
    static constexpr uint32_t OverlayWidthPx     = 640u;
    static constexpr uint32_t OverlayHeightPx    = 360u;

    VehicleCockpitInstrumentation();

    [[nodiscard]] static const char* FeedModeShortLabel(HmiCameraFeedMode Mode) noexcept;
    [[nodiscard]] static const char* RenderEngineShortLabel(Frontier::GeometricRaster::WorldRenderEngine Engine) noexcept;
    [[nodiscard]] static Frontier::GeometricRaster::WorldRenderEngine ResolveHmiRenderEngine(
        Frontier::GeometricRaster::WorldRenderEngine MainEngine,
        HmiEngineSyncMode                            SyncMode) noexcept;

    // Build camera basis for the secondary VulkanWorldRenderSequence feeding the HMI screen
    [[nodiscard]] static Frontier::GeometricRaster::PerspectiveCameraBasis BuildHmiFeedCamera(
        const Frontier::Mathematics::Axis3D& VehiclePosWorld,
        float                                VehicleYawDeg,
        HmiCameraFeedMode                    FeedMode) noexcept;

    // Transform local cockpit display anchors into live world-space specs for VulkanWorldRenderSequence
    [[nodiscard]] std::array<Frontier::GeometricRaster::CockpitDisplaySurfaceSpec, 4u> BuildWorldDisplaySpecs(
        const Frontier::Mathematics::Axis3D& VehiclePosWorld,
        float                                VehicleYawDeg,
        const HmiControlPanel&               HmiPanel,
        bool                                 IncludeInCarDisplays) const noexcept;

    // Exact 3D ray-plane intersection from main viewport cursor (NormX, NormY in [0..1]) against center-console HMI
    [[nodiscard]] HmiRayHitResult EvaluateHmiRayHit(
        const Frontier::GeometricRaster::PerspectiveCameraBasis& MainCamera,
        float                                                    CursorNormX,
        float                                                    CursorNormY,
        const Frontier::Mathematics::Axis3D&                     VehiclePosWorld,
        float                                                    VehicleYawDeg) const noexcept;

    // Rasterize all 4 automotive displays + blit live 2nd-engine RGBA8 frame into WorldSceneSpecification buffer
    void PublishCockpitDisplaysToScene(
        Frontier::GeometricRaster::WorldSceneSpecification& Scene,
        const CockpitTelemetryReading&                      Telemetry,
        const HmiControlPanel&                              HmiPanel,
        const std::vector<uint8_t>&                         HmiFeedRgba8,
        uint32_t                                            HmiFeedW,
        uint32_t                                            HmiFeedH,
        const Frontier::Mathematics::Axis3D&                VehiclePosWorld,
        float                                               VehicleYawDeg,
        bool                                                IncludeInCarDisplays);

private:
    struct ButtonRectPx
    {
        HmiButtonId Id = HmiButtonId::None;
        int32_t     X0 = 0;
        int32_t     Y0 = 0;
        int32_t     X1 = 0;
        int32_t     Y1 = 0;
    };

    void RenderHmiCanvas(
        uint32_t*                      Dst,
        const CockpitTelemetryReading& T,
        const HmiControlPanel&         S,
        const std::vector<uint8_t>&    FeedRgba8,
        uint32_t                       FeedW,
        uint32_t                       FeedH) const;

    void RenderClusterCanvas(
        uint32_t*                      Dst,
        const CockpitTelemetryReading& T) const;

    void RenderWindshieldHudCanvas(
        uint32_t*                      Dst,
        const CockpitTelemetryReading& T) const;

    void RenderOnScreenOverlayCanvas(
        uint32_t*                      Dst,
        const CockpitTelemetryReading& T) const;

    VehicleCockpitAnchorLayout  Layout_        {};
    std::array<ButtonRectPx, 5u> HmiButtons_   {};
    uint32_t                    HmiOffset_     = 0u;
    uint32_t                    ClusterOffset_ = 0u;
    uint32_t                    WindOffset_    = 0u;
    uint32_t                    OverlayOffset_ = 0u;
    uint32_t                    TotalTexels_   = 0u;
    uint64_t                    AtlasGen_      = 1ull;
};

} // namespace AutoSpace
