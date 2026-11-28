/*============================================================================================================================================
                                                VEHICLECOCKPITINSTRUMENTATION.CPP
============================================================================================================================================*/
// 🚘 Native automotive cockpit displays, 2nd-render-engine HMI compositor, optical windshield HUD, and 3D ray-plane picking

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Source/Cockpit/VehicleCockpitInstrumentation.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace AutoSpace
{
namespace
{

using Frontier::GeometricRaster::CockpitDisplaySurfaceSpec;
using Frontier::GeometricRaster::PerspectiveCameraBasis;
using Frontier::GeometricRaster::WorldRenderEngine;
using Frontier::Mathematics::Axis3D;

[[nodiscard]] constexpr uint32_t PackRgba8(uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255u) noexcept
{
    return static_cast<uint32_t>(R) |
           (static_cast<uint32_t>(G) << 8u) |
           (static_cast<uint32_t>(B) << 16u) |
           (static_cast<uint32_t>(A) << 24u);
}

[[nodiscard]] uint32_t AlphaBlendOver(uint32_t Dst, uint32_t Src) noexcept
{
    const uint32_t Sa = (Src >> 24u) & 0xFFu;
    if (Sa == 0u) return Dst;
    if (Sa == 255u) return Src;
    const uint32_t Da = (Dst >> 24u) & 0xFFu;
    const uint32_t InvSa = 255u - Sa;
    const uint32_t OutA = Sa + ((Da * InvSa + 127u) / 255u);
    if (OutA == 0u) return 0u;
    auto BlendCh = [&](uint32_t Shift) -> uint32_t
    {
        const uint32_t Sc = (Src >> Shift) & 0xFFu;
        const uint32_t Dc = (Dst >> Shift) & 0xFFu;
        const uint32_t Num = Sc * Sa + ((Dc * Da * InvSa + 127u) / 255u);
        return std::min(255u, (Num + (OutA >> 1u)) / OutA);
    };
    return PackRgba8(
        static_cast<uint8_t>(BlendCh(0u)),
        static_cast<uint8_t>(BlendCh(8u)),
        static_cast<uint8_t>(BlendCh(16u)),
        static_cast<uint8_t>(std::min(255u, OutA)));
}

struct Canvas2D
{
    uint32_t* Pixels = nullptr;
    int32_t   W      = 0;
    int32_t   H      = 0;

    void Clear(uint32_t Color) const noexcept
    {
        const size_t Count = static_cast<size_t>(W) * static_cast<size_t>(H);
        for (size_t I = 0u; I < Count; ++I) Pixels[I] = Color;
    }

    void BlendPixel(int32_t X, int32_t Y, uint32_t Color) const noexcept
    {
        if (X < 0 || Y < 0 || X >= W || Y >= H) return;
        Pixels[Y * W + X] = AlphaBlendOver(Pixels[Y * W + X], Color);
    }

    void FillRect(int32_t X0, int32_t Y0, int32_t X1, int32_t Y1, uint32_t Color) const noexcept
    {
        const int32_t Cx0 = std::max(0, X0);
        const int32_t Cy0 = std::max(0, Y0);
        const int32_t Cx1 = std::min(W, X1);
        const int32_t Cy1 = std::min(H, Y1);
        for (int32_t Y = Cy0; Y < Cy1; ++Y)
        {
            uint32_t* Row = Pixels + Y * W;
            for (int32_t X = Cx0; X < Cx1; ++X)
            {
                Row[X] = AlphaBlendOver(Row[X], Color);
            }
        }
    }

    void PaintRectBorder(int32_t X0, int32_t Y0, int32_t X1, int32_t Y1, int32_t Thickness, uint32_t Color) const noexcept
    {
        FillRect(X0, Y0, X1, Y0 + Thickness, Color);
        FillRect(X0, Y1 - Thickness, X1, Y1, Color);
        FillRect(X0, Y0 + Thickness, X0 + Thickness, Y1 - Thickness, Color);
        FillRect(X1 - Thickness, Y0 + Thickness, X1, Y1 - Thickness, Color);
    }

    void PaintLine(float X0, float Y0, float X1, float Y1, float Thickness, uint32_t Color) const noexcept
    {
        const float Dx = X1 - X0;
        const float Dy = Y1 - Y0;
        const float Len = std::sqrt(Dx * Dx + Dy * Dy);
        const int32_t Steps = std::max(1, static_cast<int32_t>(std::ceil(Len * 1.5f)));
        const int32_t Rad = std::max(0, static_cast<int32_t>(std::floor(Thickness * 0.5f)));
        for (int32_t S = 0; S <= Steps; ++S)
        {
            const float T = static_cast<float>(S) / static_cast<float>(Steps);
            const int32_t Cx = static_cast<int32_t>(std::lround(X0 + Dx * T));
            const int32_t Cy = static_cast<int32_t>(std::lround(Y0 + Dy * T));
            if (Rad == 0)
            {
                BlendPixel(Cx, Cy, Color);
            }
            else
            {
                FillRect(Cx - Rad, Cy - Rad, Cx + Rad + 1, Cy + Rad + 1, Color);
            }
        }
    }

    // Anti-aliased circular gauge arc (angles in degrees, 0 = right, 90 = down, sweeping clockwise)
    void PaintGaugeArc(
        float    Cx,
        float    Cy,
        float    InnerR,
        float    OuterR,
        float    StartDeg,
        float    EndDeg,
        uint32_t Color) const noexcept
    {
        const int32_t MinX = std::max(0, static_cast<int32_t>(std::floor(Cx - OuterR - 2.0f)));
        const int32_t MaxX = std::min(W - 1, static_cast<int32_t>(std::ceil(Cx + OuterR + 2.0f)));
        const int32_t MinY = std::max(0, static_cast<int32_t>(std::floor(Cy - OuterR - 2.0f)));
        const int32_t MaxY = std::min(H - 1, static_cast<int32_t>(std::ceil(Cy + OuterR + 2.0f)));
        const float BaseA = static_cast<float>((Color >> 24u) & 0xFFu);
        const uint8_t R = static_cast<uint8_t>(Color & 0xFFu);
        const uint8_t G = static_cast<uint8_t>((Color >> 8u) & 0xFFu);
        const uint8_t B = static_cast<uint8_t>((Color >> 16u) & 0xFFu);

        float Span = EndDeg - StartDeg;
        if (Span <= 0.1f) return;

        for (int32_t Y = MinY; Y <= MaxY; ++Y)
        {
            const float Dy = static_cast<float>(Y) + 0.5f - Cy;
            for (int32_t X = MinX; X <= MaxX; ++X)
            {
                const float Dx = static_cast<float>(X) + 0.5f - Cx;
                const float Dist = std::sqrt(Dx * Dx + Dy * Dy);
                if (Dist < InnerR - 1.2f || Dist > OuterR + 1.2f) continue;

                float Deg = std::atan2(Dy, Dx) * Frontier::Constants::RadToDegFloat;
                while (Deg < StartDeg) Deg += 360.0f;
                while (Deg >= StartDeg + 360.0f) Deg -= 360.0f;
                if (Deg > StartDeg + Span) continue;

                const float CovIn  = std::clamp(Dist - (InnerR - 0.7f), 0.0f, 1.0f);
                const float CovOut = std::clamp((OuterR + 0.7f) - Dist, 0.0f, 1.0f);
                const float Cov    = CovIn * CovOut;
                if (Cov <= 0.02f) continue;

                const uint8_t A = static_cast<uint8_t>(std::clamp(BaseA * Cov, 0.0f, 255.0f));
                BlendPixel(X, Y, PackRgba8(R, G, B, A));
            }
        }
    }

    // Crisp built-in 5x7 uppercase/numeric automotive bitmap font
    static void QueryCharacterBitmap5x7(char Ch, uint8_t OutRows[7]) noexcept
    {
        const char C = static_cast<char>(std::toupper(static_cast<unsigned char>(Ch)));
        static constexpr uint8_t Blank[7] = { 0, 0, 0, 0, 0, 0, 0 };
        const uint8_t* Src = Blank;
        switch (C)
        {
            case '0': { static constexpr uint8_t G[7]={0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}; Src=G; break; }
            case '1': { static constexpr uint8_t G[7]={0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}; Src=G; break; }
            case '2': { static constexpr uint8_t G[7]={0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}; Src=G; break; }
            case '3': { static constexpr uint8_t G[7]={0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}; Src=G; break; }
            case '4': { static constexpr uint8_t G[7]={0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; Src=G; break; }
            case '5': { static constexpr uint8_t G[7]={0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}; Src=G; break; }
            case '6': { static constexpr uint8_t G[7]={0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}; Src=G; break; }
            case '7': { static constexpr uint8_t G[7]={0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; Src=G; break; }
            case '8': { static constexpr uint8_t G[7]={0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; Src=G; break; }
            case '9': { static constexpr uint8_t G[7]={0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}; Src=G; break; }
            case 'A': { static constexpr uint8_t G[7]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; Src=G; break; }
            case 'B': { static constexpr uint8_t G[7]={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; Src=G; break; }
            case 'C': { static constexpr uint8_t G[7]={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; Src=G; break; }
            case 'D': { static constexpr uint8_t G[7]={0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}; Src=G; break; }
            case 'E': { static constexpr uint8_t G[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; Src=G; break; }
            case 'F': { static constexpr uint8_t G[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; Src=G; break; }
            case 'G': { static constexpr uint8_t G[7]={0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}; Src=G; break; }
            case 'H': { static constexpr uint8_t G[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; Src=G; break; }
            case 'I': { static constexpr uint8_t G[7]={0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}; Src=G; break; }
            case 'J': { static constexpr uint8_t G[7]={0x07,0x02,0x02,0x02,0x12,0x12,0x0C}; Src=G; break; }
            case 'K': { static constexpr uint8_t G[7]={0x11,0x12,0x14,0x18,0x14,0x12,0x11}; Src=G; break; }
            case 'L': { static constexpr uint8_t G[7]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; Src=G; break; }
            case 'M': { static constexpr uint8_t G[7]={0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; Src=G; break; }
            case 'N': { static constexpr uint8_t G[7]={0x11,0x19,0x15,0x13,0x11,0x11,0x11}; Src=G; break; }
            case 'O': { static constexpr uint8_t G[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; Src=G; break; }
            case 'P': { static constexpr uint8_t G[7]={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; Src=G; break; }
            case 'Q': { static constexpr uint8_t G[7]={0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; Src=G; break; }
            case 'R': { static constexpr uint8_t G[7]={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; Src=G; break; }
            case 'S': { static constexpr uint8_t G[7]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; Src=G; break; }
            case 'T': { static constexpr uint8_t G[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; Src=G; break; }
            case 'U': { static constexpr uint8_t G[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; Src=G; break; }
            case 'V': { static constexpr uint8_t G[7]={0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; Src=G; break; }
            case 'W': { static constexpr uint8_t G[7]={0x11,0x11,0x11,0x15,0x15,0x1B,0x11}; Src=G; break; }
            case 'X': { static constexpr uint8_t G[7]={0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; Src=G; break; }
            case 'Y': { static constexpr uint8_t G[7]={0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; Src=G; break; }
            case 'Z': { static constexpr uint8_t G[7]={0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; Src=G; break; }
            case ':': { static constexpr uint8_t G[7]={0x00,0x04,0x04,0x00,0x04,0x04,0x00}; Src=G; break; }
            case '.': { static constexpr uint8_t G[7]={0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}; Src=G; break; }
            case '-': { static constexpr uint8_t G[7]={0x00,0x00,0x00,0x1F,0x00,0x00,0x00}; Src=G; break; }
            case '+': { static constexpr uint8_t G[7]={0x00,0x04,0x04,0x1F,0x04,0x04,0x00}; Src=G; break; }
            case '/': { static constexpr uint8_t G[7]={0x01,0x02,0x02,0x04,0x08,0x08,0x10}; Src=G; break; }
            case '%': { static constexpr uint8_t G[7]={0x19,0x19,0x02,0x04,0x08,0x13,0x13}; Src=G; break; }
            case '|': { static constexpr uint8_t G[7]={0x04,0x04,0x04,0x04,0x04,0x04,0x04}; Src=G; break; }
            case '[': { static constexpr uint8_t G[7]={0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}; Src=G; break; }
            case ']': { static constexpr uint8_t G[7]={0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}; Src=G; break; }
            default:  break;
        }
        std::memcpy(OutRows, Src, 7u);
    }

    void PaintText(int32_t X, int32_t Y, std::string_view Text, int32_t Scale, uint32_t Color) const noexcept
    {
        int32_t CurX = X;
        const int32_t StepX = 6 * Scale;
        for (char Ch : Text)
        {
            uint8_t Rows[7] {};
            QueryCharacterBitmap5x7(Ch, Rows);
            for (int32_t R = 0; R < 7; ++R)
            {
                const uint8_t Bits = Rows[R];
                for (int32_t C = 0; C < 5; ++C)
                {
                    if ((Bits & (1u << (4 - C))) != 0u)
                    {
                        FillRect(CurX + C * Scale, Y + R * Scale, CurX + (C + 1) * Scale, Y + (R + 1) * Scale, Color);
                    }
                }
            }
            CurX += StepX;
        }
    }

    void PaintTextCentered(int32_t CenterX, int32_t Y, std::string_view Text, int32_t Scale, uint32_t Color) const noexcept
    {
        const int32_t PixelW = Text.empty() ? 0 : (static_cast<int32_t>(Text.size()) * 6 - 1) * Scale;
        PaintText(CenterX - PixelW / 2, Y, Text, Scale, Color);
    }
};

[[nodiscard]] Axis3D RotateYawZ(const Axis3D& V, float CosYaw, float SinYaw) noexcept
{
    return {
        V.X * CosYaw - V.Y * SinYaw,
        V.X * SinYaw + V.Y * CosYaw,
        V.Z
    };
}

[[nodiscard]] Axis3D TransformLocalPointToWorld(
    const Axis3D& Local,
    const Axis3D& VehiclePos,
    float         CosYaw,
    float         SinYaw) noexcept
{
    const Axis3D Rot = RotateYawZ(Local, CosYaw, SinYaw);
    return { VehiclePos.X + Rot.X, VehiclePos.Y + Rot.Y, VehiclePos.Z + Rot.Z };
}

[[nodiscard]] Axis3D CrossVec(const Axis3D& A, const Axis3D& B) noexcept
{
    return {
        A.Y * B.Z - A.Z * B.Y,
        A.Z * B.X - A.X * B.Z,
        A.X * B.Y - A.Y * B.X
    };
}

[[nodiscard]] float DotVec(const Axis3D& A, const Axis3D& B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

[[nodiscard]] Axis3D NormalizeVec(const Axis3D& V) noexcept
{
    const float LenSq = DotVec(V, V);
    if (LenSq <= 1.0e-12f) return { 1.0f, 0.0f, 0.0f };
    const float Inv = 1.0f / std::sqrt(LenSq);
    return { V.X * Inv, V.Y * Inv, V.Z * Inv };
}

[[nodiscard]] std::string FormatGearLabel(int32_t Gear)
{
    if (Gear < 0) return "R";
    if (Gear == 0) return "N";
    return std::to_string(Gear);
}

} // namespace

VehicleCockpitInstrumentation::VehicleCockpitInstrumentation()
    : Layout_(QueryControlVehicleCockpitLayout())
{
    HmiOffset_     = 0u;
    ClusterOffset_ = HmiOffset_ + HmiWidthPx * HmiHeightPx;
    WindOffset_    = ClusterOffset_ + ClusterWidthPx * ClusterHeightPx;
    OverlayOffset_ = WindOffset_ + WindshieldWidthPx * WindshieldHeightPx;
    TotalTexels_   = OverlayOffset_ + OverlayWidthPx * OverlayHeightPx;

    // 5 ergonomic interactive buttons stacked neatly along the right side of the 512x288 HMI screen
    constexpr int32_t BtnX0 = 324;
    constexpr int32_t BtnX1 = 502;
    constexpr int32_t TopY  = 40;
    constexpr int32_t BtnH  = 41;
    constexpr int32_t GapY  = 8;
    const std::array<HmiButtonId, 5u> Order {{
        HmiButtonId::CycleFeedCam,
        HmiButtonId::CycleHmiEngine,
        HmiButtonId::ToggleWindshieldHud,
        HmiButtonId::ToggleStabilityAssist,
        HmiButtonId::ToggleTransmissionMode,
    }};
    for (size_t I = 0u; I < Order.size(); ++I)
    {
        const int32_t Y0 = TopY + static_cast<int32_t>(I) * (BtnH + GapY);
        HmiButtons_[I] = { Order[I], BtnX0, Y0, BtnX1, Y0 + BtnH };
    }
}

const char* VehicleCockpitInstrumentation::FeedModeShortLabel(HmiCameraFeedMode Mode) noexcept
{
    switch (Mode)
    {
        case HmiCameraFeedMode::RearBackupCam:  return "REAR";
        case HmiCameraFeedMode::ChaseFollowCam: return "CHASE";
        case HmiCameraFeedMode::FrontBumperCam: return "BUMPER";
        case HmiCameraFeedMode::DriftWheelCam:  return "DRIFT";
        default:                                return "REAR";
    }
}

const char* VehicleCockpitInstrumentation::RenderEngineShortLabel(WorldRenderEngine Engine) noexcept
{
    switch (Engine)
    {
        case WorldRenderEngine::MatCap:               return "MATCAP";
        case WorldRenderEngine::OpenPbrDirect:        return "PBR";
        case WorldRenderEngine::RealTimeGiGtxLowEnd:  return "SDF+GI";
        case WorldRenderEngine::RealTimeGiHighEnd:          return "RT-GI";
        case WorldRenderEngine::PathTracerGameRealtime:     return "PATHTRACE";
        case WorldRenderEngine::PathTracerSceneFinalRender: return "PATHTRACE";
        default:                                            return "SDF+GI";
    }
}

WorldRenderEngine VehicleCockpitInstrumentation::ResolveHmiRenderEngine(
    WorldRenderEngine MainEngine,
    HmiEngineSyncMode SyncMode) noexcept
{
    switch (SyncMode)
    {
        case HmiEngineSyncMode::SyncToMain:      return MainEngine;
        case HmiEngineSyncMode::ForceOpenPbr:    return WorldRenderEngine::OpenPbrDirect;
        case HmiEngineSyncMode::ForceSdfGi:      return WorldRenderEngine::RealTimeGiGtxLowEnd;
        case HmiEngineSyncMode::ForceMatCap:     return WorldRenderEngine::MatCap;
        case HmiEngineSyncMode::ForcePathTracer: return WorldRenderEngine::PathTracerGameRealtime;
        default:                                 return MainEngine;
    }
}

PerspectiveCameraBasis VehicleCockpitInstrumentation::BuildHmiFeedCamera(
    const Axis3D&     VehiclePosWorld,
    float             VehicleYawDeg,
    HmiCameraFeedMode FeedMode) noexcept
{
    const float YawRad = VehicleYawDeg * Frontier::Constants::DegToRadFloat;
    const float C = std::cos(YawRad);
    const float S = std::sin(YawRad);

    Axis3D LocalEye {};
    Axis3D LocalTarget {};
    float  FovDeg = 58.0f;
    switch (FeedMode)
    {
        case HmiCameraFeedMode::RearBackupCam:
            // Rear bumper wide-angle backup camera cleanly aft of X = -3.11 m tail, looking back along -X
            LocalEye    = { -3.25f, 0.00f, 0.98f };
            LocalTarget = { -14.50f, 0.00f, 0.35f };
            FovDeg      = 68.0f;
            break;
        case HmiCameraFeedMode::ChaseFollowCam:
            // Exterior chase perspective embedded inside HMI
            LocalEye    = { -6.40f, 0.00f, 2.25f };
            LocalTarget = {  2.00f, 0.00f, 0.90f };
            FovDeg      = 50.0f;
            break;
        case HmiCameraFeedMode::FrontBumperCam:
            // Low front-splitter track camera cleanly ahead of X = +3.11 m nose
            LocalEye    = {  3.25f, 0.00f, 0.65f };
            LocalTarget = { 18.00f, 0.00f, 0.68f };
            FovDeg      = 60.0f;
            break;
        case HmiCameraFeedMode::DriftWheelCam:
        default:
            // Fender-mounted rear wheel & tire smoke action camera
            LocalEye    = { -4.40f, 2.75f, 1.10f };
            LocalTarget = { -1.10f, 0.25f, 0.55f };
            FovDeg      = 52.0f;
            break;
    }

    PerspectiveCameraBasis Cam {};
    Cam.CameraOrigin       = TransformLocalPointToWorld(LocalEye, VehiclePosWorld, C, S);
    Cam.TargetPoint        = TransformLocalPointToWorld(LocalTarget, VehiclePosWorld, C, S);
    Cam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
    Cam.VerticalFovRadians = FovDeg * Frontier::Constants::DegToRadFloat;
    Cam.ViewportWidthPx    = HmiFeedWidthPx;
    Cam.ViewportHeightPx   = HmiFeedHeightPx;
    Cam.NearClipMeters     = 0.08f;
    return Cam;
}

std::array<CockpitDisplaySurfaceSpec, 4u> VehicleCockpitInstrumentation::BuildWorldDisplaySpecs(
    const Axis3D&              VehiclePosWorld,
    float                      VehicleYawDeg,
    const HmiControlPanel& HmiPanel,
    bool                       IncludeInCarDisplays) const noexcept
{
    const float YawRad = VehicleYawDeg * Frontier::Constants::DegToRadFloat;
    const float C = std::cos(YawRad);
    const float S = std::sin(YawRad);

    std::array<CockpitDisplaySurfaceSpec, 4u> Specs {};

    // Slot 0: Center-Console In-Car HMI Touchscreen
    Specs[0u].OriginWorld     = TransformLocalPointToWorld(Layout_.HmiOriginLocal, VehiclePosWorld, C, S);
    Specs[0u].RightAxisWorld  = RotateYawZ(Layout_.HmiRightLocal, C, S);
    Specs[0u].UpAxisWorld     = RotateYawZ(Layout_.HmiUpLocal, C, S);
    Specs[0u].WidthMeters     = Layout_.HmiWidthMeters;
    Specs[0u].HeightMeters    = Layout_.HmiHeightMeters;
    Specs[0u].LuminanceGain   = 1.65f;
    Specs[0u].WidthPx         = HmiWidthPx;
    Specs[0u].HeightPx        = HmiHeightPx;
    Specs[0u].TexelOffset     = HmiOffset_;
    Specs[0u].Enabled         = IncludeInCarDisplays;
    Specs[0u].AdditiveOptical = false;

    // Slot 1: Recessed Dashboard Instrument Cluster
    Specs[1u].OriginWorld     = TransformLocalPointToWorld(Layout_.ClusterOriginLocal, VehiclePosWorld, C, S);
    Specs[1u].RightAxisWorld  = RotateYawZ(Layout_.ClusterRightLocal, C, S);
    Specs[1u].UpAxisWorld     = RotateYawZ(Layout_.ClusterUpLocal, C, S);
    Specs[1u].WidthMeters     = Layout_.ClusterWidthMeters;
    Specs[1u].HeightMeters    = Layout_.ClusterHeightMeters;
    Specs[1u].LuminanceGain   = 1.75f;
    Specs[1u].WidthPx         = ClusterWidthPx;
    Specs[1u].HeightPx        = ClusterHeightPx;
    Specs[1u].TexelOffset     = ClusterOffset_;
    Specs[1u].Enabled         = IncludeInCarDisplays;
    Specs[1u].AdditiveOptical = false;

    // Slot 2: Optical Windshield Glass HUD (additive emissive on glass combiner plane)
    Specs[2u].OriginWorld     = TransformLocalPointToWorld(Layout_.WindshieldOriginLocal, VehiclePosWorld, C, S);
    Specs[2u].RightAxisWorld  = RotateYawZ(Layout_.WindshieldRightLocal, C, S);
    Specs[2u].UpAxisWorld     = RotateYawZ(Layout_.WindshieldUpLocal, C, S);
    Specs[2u].WidthMeters     = Layout_.WindshieldWidthMeters;
    Specs[2u].HeightMeters    = Layout_.WindshieldHeightMeters;
    Specs[2u].LuminanceGain   = 2.60f;
    Specs[2u].WidthPx         = WindshieldWidthPx;
    Specs[2u].HeightPx        = WindshieldHeightPx;
    Specs[2u].TexelOffset     = WindOffset_;
    Specs[2u].Enabled         = IncludeInCarDisplays && HmiPanel.WindshieldHudEnabled;
    Specs[2u].AdditiveOptical = true;

    // Slot 3: On-Screen Overlay HUD (fullscreen alpha-composited)
    Specs[3u].LuminanceGain   = 1.0f;
    Specs[3u].WidthPx         = OverlayWidthPx;
    Specs[3u].HeightPx        = OverlayHeightPx;
    Specs[3u].TexelOffset     = OverlayOffset_;
    Specs[3u].Enabled         = HmiPanel.OnScreenHudEnabled;
    Specs[3u].AdditiveOptical = false;

    return Specs;
}

HmiRayHitResult VehicleCockpitInstrumentation::EvaluateHmiRayHit(
    const PerspectiveCameraBasis& MainCamera,
    float                         CursorNormX,
    float                         CursorNormY,
    const Axis3D&                 VehiclePosWorld,
    float                         VehicleYawDeg) const noexcept
{
    HmiRayHitResult Out {};
    const Axis3D Fwd = NormalizeVec({
        MainCamera.TargetPoint.X - MainCamera.CameraOrigin.X,
        MainCamera.TargetPoint.Y - MainCamera.CameraOrigin.Y,
        MainCamera.TargetPoint.Z - MainCamera.CameraOrigin.Z
    });
    const Axis3D Right = NormalizeVec(CrossVec(Fwd, MainCamera.WorldUpAxis));
    const Axis3D Up    = CrossVec(Right, Fwd);

    const float Aspect   = static_cast<float>(std::max(1u, MainCamera.ViewportWidthPx)) /
                           static_cast<float>(std::max(1u, MainCamera.ViewportHeightPx));
    const float TanHalfY = std::tan(MainCamera.VerticalFovRadians * 0.5f);
    const float TanHalfX = TanHalfY * Aspect;

    const float NdcX = CursorNormX * 2.0f - 1.0f;
    const float NdcY = 1.0f - CursorNormY * 2.0f;
    const Axis3D RayDir = NormalizeVec({
        Fwd.X + Right.X * (NdcX * TanHalfX) + Up.X * (NdcY * TanHalfY),
        Fwd.Y + Right.Y * (NdcX * TanHalfX) + Up.Y * (NdcY * TanHalfY),
        Fwd.Z + Right.Z * (NdcX * TanHalfX) + Up.Z * (NdcY * TanHalfY)
    });

    const float YawRad = VehicleYawDeg * Frontier::Constants::DegToRadFloat;
    const float C = std::cos(YawRad);
    const float S = std::sin(YawRad);
    const Axis3D OriginW = TransformLocalPointToWorld(Layout_.HmiOriginLocal, VehiclePosWorld, C, S);
    const Axis3D RightW  = RotateYawZ(Layout_.HmiRightLocal, C, S);
    const Axis3D UpW     = RotateYawZ(Layout_.HmiUpLocal, C, S);
    const Axis3D PlaneN  = NormalizeVec(CrossVec(RightW, UpW));

    const float Denom = DotVec(RayDir, PlaneN);
    if (std::fabs(Denom) < 1.0e-5f) return Out;

    const Axis3D Delta {
        OriginW.X - MainCamera.CameraOrigin.X,
        OriginW.Y - MainCamera.CameraOrigin.Y,
        OriginW.Z - MainCamera.CameraOrigin.Z
    };
    const float T = DotVec(Delta, PlaneN) / Denom;
    if (T <= 0.04f || T > 6.0f) return Out;

    const Axis3D HitPos {
        MainCamera.CameraOrigin.X + RayDir.X * T,
        MainCamera.CameraOrigin.Y + RayDir.Y * T,
        MainCamera.CameraOrigin.Z + RayDir.Z * T
    };
    const Axis3D Rel { HitPos.X - OriginW.X, HitPos.Y - OriginW.Y, HitPos.Z - OriginW.Z };
    const float U = DotVec(Rel, RightW) / std::max(1.0e-4f, Layout_.HmiWidthMeters);
    const float V = 1.0f - (DotVec(Rel, UpW) / std::max(1.0e-4f, Layout_.HmiHeightMeters));
    if (U < 0.0f || U > 1.0f || V < 0.0f || V > 1.0f) return Out;

    Out.HitScreen = true;
    Out.U         = U;
    Out.V         = V;

    const int32_t Px = static_cast<int32_t>(U * static_cast<float>(HmiWidthPx));
    const int32_t Py = static_cast<int32_t>(V * static_cast<float>(HmiHeightPx));
    for (const auto& Btn : HmiButtons_)
    {
        if (Px >= Btn.X0 && Px <= Btn.X1 && Py >= Btn.Y0 && Py <= Btn.Y1)
        {
            Out.HoveredButton = Btn.Id;
            break;
        }
    }
    return Out;
}

void VehicleCockpitInstrumentation::RenderHmiCanvas(
    uint32_t*                       Dst,
    const CockpitTelemetryReading& T,
    const HmiControlPanel&      S,
    const std::vector<uint8_t>&     FeedRgba8,
    uint32_t                        FeedW,
    uint32_t                        FeedH) const
{
    Canvas2D Cv { Dst, static_cast<int32_t>(HmiWidthPx), static_cast<int32_t>(HmiHeightPx) };

    // Automotive OLED dark obsidian canvas + outer hairline frame
    Cv.Clear(PackRgba8(9, 13, 21, 255));
    Cv.PaintRectBorder(0, 0, Cv.W, Cv.H, 2, PackRgba8(38, 66, 94, 255));

    // Top Status Header Bar
    Cv.FillRect(2, 2, Cv.W - 2, 32, PackRgba8(16, 24, 38, 255));
    Cv.FillRect(2, 31, Cv.W - 2, 33, PackRgba8(48, 178, 232, 220));
    Cv.PaintText(12, 10, "FRONTIER HMI", 2, PackRgba8(232, 244, 255, 255));

    // Active Feed & 2nd-Engine Parity Status Pills in Header
    const bool IsSynced = (S.EngineMode == HmiEngineSyncMode::SyncToMain);
    char EngPillText[48] {};
    std::snprintf(
        EngPillText,
        sizeof(EngPillText),
        "%s:%s",
        IsSynced ? "SYNC" : "ENG",
        RenderEngineShortLabel(T.ActiveHmiRenderEngine));
    Cv.FillRect(318, 6, 504, 27, IsSynced ? PackRgba8(18, 66, 56, 255) : PackRgba8(74, 48, 16, 255));
    Cv.PaintRectBorder(318, 6, 504, 27, 1, IsSynced ? PackRgba8(58, 236, 172, 255) : PackRgba8(250, 184, 52, 255));
    Cv.PaintTextCentered(411, 13, EngPillText, 1, PackRgba8(238, 252, 248, 255));

    // Left Live 2nd Render Engine Viewport (x = 10..314, y = 40..256, exact 304x216)
    constexpr int32_t VpX = 10;
    constexpr int32_t VpY = 40;
    constexpr int32_t VpW = static_cast<int32_t>(HmiFeedWidthPx);
    constexpr int32_t VpH = static_cast<int32_t>(HmiFeedHeightPx);
    Cv.FillRect(VpX, VpY, VpX + VpW, VpY + VpH, PackRgba8(14, 20, 30, 255));

    if (!FeedRgba8.empty() && FeedW > 0u && FeedH > 0u &&
        FeedRgba8.size() >= static_cast<size_t>(FeedW) * static_cast<size_t>(FeedH) * 4u)
    {
        for (int32_t Y = 0; Y < VpH; ++Y)
        {
            const uint32_t SrcY = (static_cast<uint32_t>(Y) * FeedH) / static_cast<uint32_t>(VpH);
            uint32_t* DstRow = Dst + (VpY + Y) * Cv.W + VpX;
            const uint8_t* SrcRow = FeedRgba8.data() + static_cast<size_t>(SrcY) * static_cast<size_t>(FeedW) * 4u;
            for (int32_t X = 0; X < VpW; ++X)
            {
                const uint32_t SrcX = (static_cast<uint32_t>(X) * FeedW) / static_cast<uint32_t>(VpW);
                const uint8_t* Px = SrcRow + static_cast<size_t>(SrcX) * 4u;
                DstRow[X] = PackRgba8(Px[0], Px[1], Px[2], 255u);
            }
        }
    }
    else
    {
        Cv.PaintTextCentered(VpX + VpW / 2, VpY + VpH / 2 - 7, "2ND ENGINE FEED STANDBY", 1, PackRgba8(110, 155, 195, 255));
    }

    // Subtle automotive camera viewfinder overlay on the 2nd-engine feed
    Cv.PaintRectBorder(VpX, VpY, VpX + VpW, VpY + VpH, 2, PackRgba8(62, 168, 218, 255));
    // Rear backup parking trajectory guides when RearBackupCam is active
    if (S.FeedMode == HmiCameraFeedMode::RearBackupCam)
    {
        const float SteerShift = T.SteerInput * 28.0f;
        Cv.PaintLine(VpX + 54.0f, VpY + VpH - 6.0f, VpX + 102.0f + SteerShift, VpY + VpH - 72.0f, 2.0f, PackRgba8(64, 245, 160, 210));
        Cv.PaintLine(VpX + VpW - 54.0f, VpY + VpH - 6.0f, VpX + VpW - 102.0f + SteerShift, VpY + VpH - 72.0f, 2.0f, PackRgba8(64, 245, 160, 210));
        Cv.PaintLine(VpX + 86.0f + SteerShift * 0.65f, VpY + VpH - 48.0f, VpX + VpW - 86.0f + SteerShift * 0.65f, VpY + VpH - 48.0f, 1.5f, PackRgba8(255, 196, 52, 210));
        Cv.PaintLine(VpX + 68.0f + SteerShift * 0.30f, VpY + VpH - 22.0f, VpX + VpW - 68.0f + SteerShift * 0.30f, VpY + VpH - 22.0f, 2.0f, PackRgba8(248, 74, 74, 220));
    }

    // Live feed label chip in upper-left of embedded viewport
    char FeedTag[48] {};
    std::snprintf(FeedTag, sizeof(FeedTag), "LIVE %s CAM", FeedModeShortLabel(S.FeedMode));
    Cv.FillRect(VpX + 6, VpY + 6, VpX + 132, VpY + 23, PackRgba8(8, 14, 22, 205));
    Cv.FillRect(VpX + 11, VpY + 11, VpX + 17, VpY + 17, PackRgba8(58, 245, 146, 255));
    Cv.PaintText(VpX + 22, VpY + 11, FeedTag, 1, PackRgba8(236, 248, 255, 255));

    // Bottom Live Telemetry Strip under viewport
    Cv.FillRect(VpX, 260, VpX + VpW, 282, PackRgba8(16, 23, 34, 255));
    Cv.PaintRectBorder(VpX, 260, VpX + VpW, 282, 1, PackRgba8(44, 76, 108, 255));
    char BottomTel[80] {};
    std::snprintf(
        BottomTel,
        sizeof(BottomTel),
        "%3d KPH | %4d RPM | G%s | SLIP %2d%%",
        static_cast<int>(std::lround(T.SpeedKph)),
        static_cast<int>(std::lround(T.EngineRpm)),
        FormatGearLabel(T.Gear).c_str(),
        static_cast<int>(std::lround(std::clamp(T.RearSlipRatio * 100.0f, 0.0f, 99.0f))));
    Cv.PaintText(VpX + 10, 268, BottomTel, 1, PackRgba8(178, 232, 255, 255));

    // Right Column: 6 Interactive Automotive Tactile Buttons
    for (const auto& Btn : HmiButtons_)
    {
        const bool IsHover   = (S.HoveredButton == Btn.Id);
        const bool IsClicked = (S.LastClickedButton == Btn.Id && S.ClickFlashSeconds > 0.0f);
        bool       IsActive  = false;
        std::string Title;
        std::string StatusText;
        switch (Btn.Id)
        {
            case HmiButtonId::CycleFeedCam:
                Title    = "FEED CAM";
                StatusText = FeedModeShortLabel(S.FeedMode);
                IsActive = true;
                break;
            case HmiButtonId::CycleHmiEngine:
                Title    = "2ND ENGINE";
                StatusText = (S.EngineMode == HmiEngineSyncMode::SyncToMain)
                               ? (std::string("SYNC:") + RenderEngineShortLabel(T.ActiveHmiRenderEngine))
                               : RenderEngineShortLabel(T.ActiveHmiRenderEngine);
                IsActive = (S.EngineMode == HmiEngineSyncMode::SyncToMain);
                break;
            case HmiButtonId::ToggleWindshieldHud:
                Title    = "GLASS HUD";
                StatusText = S.WindshieldHudEnabled ? "ACTIVE" : "OFF";
                IsActive = S.WindshieldHudEnabled;
                break;
            case HmiButtonId::ToggleStabilityAssist:
                Title    = "ESC / TC";
                StatusText = T.StabilityAssistOn ? "ON" : "DRIFT";
                IsActive = T.StabilityAssistOn;
                break;
            case HmiButtonId::ToggleTransmissionMode:
                Title    = "SHIFT MODE";
                StatusText = T.AutomaticTransmission ? "AUTO" : "MANUAL";
                IsActive = T.AutomaticTransmission;
                break;
            default:
                break;
        }

        uint32_t FillCol   = IsActive ? PackRgba8(20, 42, 62, 255) : PackRgba8(22, 26, 36, 255);
        uint32_t BorderCol = IsActive ? PackRgba8(58, 198, 245, 255) : PackRgba8(76, 92, 112, 255);
        uint32_t AccentCol = IsActive ? PackRgba8(64, 244, 188, 255) : PackRgba8(255, 168, 60, 255);
        if (IsHover)
        {
            FillCol   = PackRgba8(34, 72, 104, 255);
            BorderCol = PackRgba8(120, 244, 255, 255);
        }
        if (IsClicked)
        {
            FillCol   = PackRgba8(72, 164, 212, 255);
            BorderCol = PackRgba8(255, 255, 255, 255);
        }

        Cv.FillRect(Btn.X0, Btn.Y0, Btn.X1, Btn.Y1, FillCol);
        Cv.PaintRectBorder(Btn.X0, Btn.Y0, Btn.X1, Btn.Y1, IsHover ? 2 : 1, BorderCol);
        Cv.FillRect(Btn.X0 + 3, Btn.Y0 + 4, Btn.X0 + 7, Btn.Y1 - 4, AccentCol);
        Cv.PaintText(Btn.X0 + 13, Btn.Y0 + 6, Title, 1, PackRgba8(170, 196, 220, 255));
        Cv.PaintText(Btn.X0 + 13, Btn.Y0 + 19, StatusText, 1, PackRgba8(244, 252, 255, 255));
    }
}

void VehicleCockpitInstrumentation::RenderClusterCanvas(
    uint32_t*                       Dst,
    const CockpitTelemetryReading& T) const
{
    Canvas2D Cv { Dst, static_cast<int32_t>(ClusterWidthPx), static_cast<int32_t>(ClusterHeightPx) };
    Cv.Clear(PackRgba8(6, 10, 16, 255));
    Cv.PaintRectBorder(0, 0, Cv.W, Cv.H, 2, PackRgba8(34, 58, 86, 255));

    const float RpmFrac = std::clamp(T.EngineRpm / std::max(1000.0f, T.MaxRpm), 0.0f, 1.0f);

    // 1. Top Progressive F1/GT Shift-Light LED Strip
    constexpr int32_t LedCount = 18;
    constexpr int32_t LedX0    = 68;
    constexpr int32_t LedW     = 17;
    constexpr int32_t LedGap   = 4;
    for (int32_t I = 0; I < LedCount; ++I)
    {
        const float Thresh = 0.20f + 0.78f * (static_cast<float>(I) / static_cast<float>(LedCount - 1));
        const bool Lit = (RpmFrac >= Thresh);
        uint32_t Col = PackRgba8(24, 34, 46, 255);
        if (Lit)
        {
            if (I < 9)       Col = PackRgba8(48, 242, 152, 255);
            else if (I < 14) Col = PackRgba8(255, 192, 42, 255);
            else             Col = PackRgba8(255, 58, 58, 255);
        }
        const int32_t Lx = LedX0 + I * (LedW + LedGap);
        Cv.FillRect(Lx, 8, Lx + LedW, 17, Col);
    }

    // 2. Left Circular Tachometer Dial (Cx = 104, Cy = 108, R = 68)
    constexpr float TachCx = 104.0f;
    constexpr float TachCy = 110.0f;
    Cv.PaintGaugeArc(TachCx, TachCy, 52.0f, 68.0f, 135.0f, 385.0f, PackRgba8(22, 34, 48, 255));
    Cv.PaintGaugeArc(TachCx, TachCy, 52.0f, 68.0f, 345.0f, 385.0f, PackRgba8(120, 26, 32, 255)); // Redline track
    const float ActiveEndDeg = 135.0f + 250.0f * RpmFrac;
    const uint32_t TachCol = (RpmFrac > 0.84f)
        ? PackRgba8(255, 68, 62, 255)
        : ((RpmFrac > 0.65f) ? PackRgba8(255, 196, 48, 255) : PackRgba8(52, 224, 255, 255));
    Cv.PaintGaugeArc(TachCx, TachCy, 52.0f, 68.0f, 135.0f, ActiveEndDeg, TachCol);
    // Needle tick marks 0..8
    for (int32_t Tick = 0; Tick <= 8; ++Tick)
    {
        const float Deg = 135.0f + 250.0f * (static_cast<float>(Tick) / 8.0f);
        const float Rad = Deg * Frontier::Constants::DegToRadFloat;
        const float C = std::cos(Rad), S = std::sin(Rad);
        Cv.PaintLine(TachCx + C * 44.0f, TachCy + S * 44.0f, TachCx + C * 51.0f, TachCy + S * 51.0f, 1.5f,
            (Tick >= 7) ? PackRgba8(255, 76, 76, 255) : PackRgba8(170, 205, 232, 255));
    }
    char RpmBuf[16] {};
    std::snprintf(RpmBuf, sizeof(RpmBuf), "%d", static_cast<int>(std::lround(T.EngineRpm)));
    Cv.PaintTextCentered(static_cast<int32_t>(TachCx), 96, RpmBuf, 2, PackRgba8(240, 250, 255, 255));
    Cv.PaintTextCentered(static_cast<int32_t>(TachCx), 118, "RPM", 1, PackRgba8(116, 178, 212, 255));

    // 3. Center Digital Speedometer + Giant Gear Tower
    Cv.FillRect(190, 28, 322, 178, PackRgba8(12, 20, 32, 255));
    Cv.PaintRectBorder(190, 28, 322, 178, 1, PackRgba8(48, 108, 152, 255));

    char SpeedBuf[16] {};
    std::snprintf(SpeedBuf, sizeof(SpeedBuf), "%03d", static_cast<int>(std::clamp(std::lround(T.SpeedKph), 0l, 999l)));
    Cv.PaintTextCentered(256, 38, SpeedBuf, 4, PackRgba8(245, 252, 255, 255));
    Cv.PaintTextCentered(256, 72, "KM/H", 1, PackRgba8(84, 218, 255, 255));

    // Framed Gear Box
    Cv.FillRect(228, 88, 284, 134, PackRgba8(18, 38, 58, 255));
    Cv.PaintRectBorder(228, 88, 284, 134, 2, (RpmFrac > 0.85f) ? PackRgba8(255, 72, 68, 255) : PackRgba8(64, 232, 255, 255));
    Cv.PaintTextCentered(256, 97, FormatGearLabel(T.Gear), 4, PackRgba8(255, 255, 255, 255));

    // Throttle & Brake Vertical Micro-Bars flanking gear box
    const int32_t ThrH = static_cast<int32_t>(std::lround(std::clamp(T.Throttle01, 0.0f, 1.0f) * 44.0f));
    const int32_t BrkH = static_cast<int32_t>(std::lround(std::clamp(T.Brake01, 0.0f, 1.0f) * 44.0f));
    Cv.FillRect(204, 88, 216, 134, PackRgba8(20, 28, 38, 255));
    if (BrkH > 0) Cv.FillRect(205, 133 - BrkH, 215, 133, PackRgba8(255, 66, 62, 255));
    Cv.FillRect(296, 88, 308, 134, PackRgba8(20, 28, 38, 255));
    if (ThrH > 0) Cv.FillRect(297, 133 - ThrH, 307, 133, PackRgba8(58, 245, 146, 255));
    Cv.PaintText(207, 138, "B", 1, PackRgba8(248, 118, 114, 255));
    Cv.PaintText(299, 138, "T", 1, PackRgba8(102, 242, 168, 255));

    // Mode Status Chips at bottom of center tower
    Cv.FillRect(198, 152, 252, 170, T.StabilityAssistOn ? PackRgba8(22, 68, 56, 255) : PackRgba8(102, 54, 18, 255));
    Cv.PaintTextCentered(225, 158, T.StabilityAssistOn ? "ESC ON" : "DRIFT", 1, PackRgba8(240, 252, 248, 255));
    Cv.FillRect(260, 152, 314, 170, PackRgba8(24, 48, 72, 255));
    Cv.PaintTextCentered(287, 158, T.AutomaticTransmission ? "AUTO" : "MAN", 1, PackRgba8(230, 244, 255, 255));

    // 4. Right Circular Tire Slip Telemetry Pod (Cx = 408, Cy = 110, R = 68)
    constexpr float SlipCx = 408.0f;
    constexpr float SlipCy = 110.0f;
    const float SlipFrac = std::clamp(T.RearSlipRatio, 0.0f, 1.0f);
    Cv.PaintGaugeArc(SlipCx, SlipCy, 52.0f, 68.0f, 135.0f, 385.0f, PackRgba8(22, 34, 48, 255));
    const uint32_t SlipCol = (SlipFrac > 0.45f)
        ? PackRgba8(255, 108, 42, 255)
        : ((SlipFrac > 0.18f) ? PackRgba8(255, 206, 54, 255) : PackRgba8(58, 242, 178, 255));
    Cv.PaintGaugeArc(SlipCx, SlipCy, 52.0f, 68.0f, 135.0f, 135.0f + 250.0f * SlipFrac, SlipCol);

    // 4-Wheel Slip Schematic inside right dial
    auto WheelColor = [](float Slip) -> uint32_t
    {
        if (Slip > 0.45f) return PackRgba8(255, 76, 52, 255);
        if (Slip > 0.18f) return PackRgba8(255, 196, 52, 255);
        return PackRgba8(64, 232, 176, 255);
    };
    Cv.PaintRectBorder(394, 78, 422, 124, 1, PackRgba8(74, 126, 162, 255)); // Chassis outline
    Cv.FillRect(384, 82, 392, 96, WheelColor(T.WheelSlipRatios[0]));       // FL
    Cv.FillRect(424, 82, 432, 96, WheelColor(T.WheelSlipRatios[1]));       // FR
    Cv.FillRect(384, 106, 392, 122, WheelColor(T.WheelSlipRatios[2]));     // RL
    Cv.FillRect(424, 106, 432, 122, WheelColor(T.WheelSlipRatios[3]));     // RR

    char SlipBuf[24] {};
    std::snprintf(SlipBuf, sizeof(SlipBuf), "SLIP %d%%", static_cast<int>(std::lround(SlipFrac * 100.0f)));
    Cv.PaintTextCentered(static_cast<int32_t>(SlipCx), 132, SlipBuf, 1, PackRgba8(232, 244, 255, 255));
    Cv.PaintTextCentered(
        static_cast<int32_t>(SlipCx),
        146,
        (SlipFrac > 0.22f) ? "SLIP HIGH" : "GRIP OK",
        1,
        (SlipFrac > 0.22f) ? PackRgba8(255, 186, 68, 255) : PackRgba8(76, 238, 176, 255));
}

void VehicleCockpitInstrumentation::RenderWindshieldHudCanvas(
    uint32_t*                       Dst,
    const CockpitTelemetryReading& T) const
{
    Canvas2D Cv { Dst, static_cast<int32_t>(WindshieldWidthPx), static_cast<int32_t>(WindshieldHeightPx) };
    // Fully transparent background so ONLY collimated HUD optics glow on the windshield glass!
    Cv.Clear(PackRgba8(0, 0, 0, 0));

    const uint32_t HudCyan  = PackRgba8(58, 252, 206, 245);
    const uint32_t HudSoft  = PackRgba8(48, 212, 178, 185);
    const uint32_t HudAmber = PackRgba8(255, 182, 48, 250);
    const uint32_t HudRed   = PackRgba8(255, 68, 62, 250);

    // 1. Top Heading Compass Tape (scrolls cleanly with live vehicle Yaw)
    Cv.FillRect(96, 30, 416, 32, HudSoft);
    Cv.FillRect(255, 20, 257, 36, HudCyan); // Center heading lubber line
    float HeadingDeg = std::fmod(-T.VehicleYawDeg, 360.0f);
    if (HeadingDeg < 0.0f) HeadingDeg += 360.0f;
    char HdgText[16] {};
    std::snprintf(HdgText, sizeof(HdgText), "HDG %03d", static_cast<int>(std::lround(HeadingDeg)) % 360);
    Cv.PaintTextCentered(256, 8, HdgText, 1, HudCyan);

    // Compass ticks every 15 deg across +/- 60 deg window
    for (int32_t Offset = -60; Offset <= 60; Offset += 15)
    {
        const int32_t TickDeg = (static_cast<int32_t>(std::lround(HeadingDeg / 15.0f)) * 15 + Offset + 360) % 360;
        const float Diff = static_cast<float>(TickDeg) - HeadingDeg;
        float WrappedDiff = Diff;
        if (WrappedDiff > 180.0f) WrappedDiff -= 360.0f;
        if (WrappedDiff < -180.0f) WrappedDiff += 360.0f;
        const int32_t Tx = 256 + static_cast<int32_t>(std::lround(WrappedDiff * 2.5f));
        if (Tx < 102 || Tx > 410) continue;
        const bool Cardinal = (TickDeg % 90 == 0);
        Cv.FillRect(Tx, 32, Tx + 2, Cardinal ? 42 : 38, HudSoft);
        if (Cardinal)
        {
            const char* CLabel = (TickDeg == 0) ? "N" : ((TickDeg == 90) ? "E" : ((TickDeg == 180) ? "S" : "W"));
            Cv.PaintTextCentered(Tx + 1, 44, CLabel, 1, HudCyan);
        }
    }

    // 2. Center Collimated Road Reticle Brackets
    Cv.FillRect(160, 126, 204, 128, HudSoft);
    Cv.FillRect(308, 126, 352, 128, HudSoft);
    Cv.FillRect(160, 118, 162, 136, HudSoft);
    Cv.FillRect(350, 118, 352, 136, HudSoft);

    // 3. Giant Optical Digital Speed & Gear Readout
    char SpdStr[16] {};
    std::snprintf(SpdStr, sizeof(SpdStr), "%d", static_cast<int>(std::clamp(std::lround(T.SpeedKph), 0l, 999l)));
    Cv.PaintTextCentered(234, 88, SpdStr, 5, HudCyan);
    Cv.PaintText(298, 92, "KM/H", 1, HudSoft);
    Cv.PaintRectBorder(298, 106, 336, 144, 2, HudCyan);
    Cv.PaintTextCentered(317, 114, FormatGearLabel(T.Gear), 3, HudCyan);

    // 4. Curved Horizontal Optical RPM Shift Bar
    const float RpmFrac = std::clamp(T.EngineRpm / std::max(1000.0f, T.MaxRpm), 0.0f, 1.0f);
    constexpr int32_t Segs = 20;
    for (int32_t I = 0; I < Segs; ++I)
    {
        const float Frac = static_cast<float>(I) / static_cast<float>(Segs - 1);
        const int32_t Sx = 116 + I * 14;
        const bool Lit = (RpmFrac >= Frac * 0.96f);
        uint32_t Col = PackRgba8(48, 160, 136, 75);
        if (Lit)
        {
            Col = (I >= 16) ? HudRed : ((I >= 12) ? HudAmber : HudCyan);
        }
        Cv.FillRect(Sx, 164, Sx + 10, 174, Col);
    }

    // 5. Optical Slip / Drift Alert Line
    if (T.RearSlipRatio > 0.22f)
    {
        Cv.PaintRectBorder(168, 190, 344, 214, 2, HudAmber);
        Cv.PaintTextCentered(
            256,
            197,
            "REAR TRACTION SLIP",
            1,
            HudAmber);
    }
    else
    {
        char ModeLine[48] {};
        std::snprintf(
            ModeLine,
            sizeof(ModeLine),
            "%s  |  %s",
            T.StabilityAssistOn ? "ESC ACTIVE" : "DRIFT MODE",
            T.AutomaticTransmission ? "AUTO" : "SEQ");
        Cv.PaintTextCentered(256, 197, ModeLine, 1, HudSoft);
    }
}

void VehicleCockpitInstrumentation::RenderOnScreenOverlayCanvas(
    uint32_t*                       Dst,
    const CockpitTelemetryReading& T) const
{
    Canvas2D Cv { Dst, static_cast<int32_t>(OverlayWidthPx), static_cast<int32_t>(OverlayHeightPx) };
    // Fully transparent everywhere except sleek automotive telemetry pods
    Cv.Clear(PackRgba8(0, 0, 0, 0));

    // 1. Top-Left Frosted Mode & Telemetry Pill
    Cv.FillRect(14, 12, 314, 54, PackRgba8(10, 16, 26, 196));
    Cv.PaintRectBorder(14, 12, 314, 54, 1, PackRgba8(66, 186, 236, 210));
    Cv.FillRect(14, 12, 18, 54, PackRgba8(58, 232, 255, 255));
    char TopTitle[64] {};
    std::snprintf(
        TopTitle,
        sizeof(TopTitle),
        "L4 TRAINING · %s",
        RenderEngineShortLabel(T.MainRenderEngine));
    Cv.PaintText(26, 18, TopTitle, 1, PackRgba8(236, 248, 255, 255));

    char CamHint[64] {};
    std::snprintf(
        CamHint,
        sizeof(CamHint),
        "CAM:%s | V:CAM H:HMI U:HUD",
        T.ActiveCameraLabel.empty() ? "DRIVE" : T.ActiveCameraLabel.c_str());
    Cv.PaintText(26, 34, CamHint, 1, PackRgba8(146, 212, 240, 235));

    // 2. Bottom-Right Compact GT Racing Speed/Tach Pod (hidden only if desired; kept ultra-compact in exterior & cockpit)
    if (!T.InteriorViewActive)
    {
        constexpr int32_t Px0 = 448;
        constexpr int32_t Py0 = 246;
        constexpr int32_t Px1 = 626;
        constexpr int32_t Py1 = 346;
        Cv.FillRect(Px0, Py0, Px1, Py1, PackRgba8(9, 14, 22, 204));
        Cv.PaintRectBorder(Px0, Py0, Px1, Py1, 1, PackRgba8(58, 174, 224, 220));

        // Compact Tachometer Arc
        const float RpmFrac = std::clamp(T.EngineRpm / std::max(1000.0f, T.MaxRpm), 0.0f, 1.0f);
        Cv.PaintGaugeArc(502.0f, 300.0f, 30.0f, 40.0f, 135.0f, 385.0f, PackRgba8(28, 40, 56, 230));
        const uint32_t TachC = (RpmFrac > 0.85f) ? PackRgba8(255, 68, 62, 255) : PackRgba8(56, 228, 255, 255);
        Cv.PaintGaugeArc(502.0f, 300.0f, 30.0f, 40.0f, 135.0f, 135.0f + 250.0f * RpmFrac, TachC);
        Cv.PaintTextCentered(502, 289, FormatGearLabel(T.Gear), 3, PackRgba8(255, 255, 255, 255));

        char Spd[16] {};
        std::snprintf(Spd, sizeof(Spd), "%03d", static_cast<int>(std::clamp(std::lround(T.SpeedKph), 0l, 999l)));
        Cv.PaintText(554, 260, Spd, 3, PackRgba8(245, 252, 255, 255));
        Cv.PaintText(556, 286, "KM/H", 1, PackRgba8(98, 218, 252, 255));

        // Throttle & Brake Bars
        const int32_t ThrW = static_cast<int32_t>(std::lround(std::clamp(T.Throttle01, 0.0f, 1.0f) * 62.0f));
        const int32_t BrkW = static_cast<int32_t>(std::lround(std::clamp(T.Brake01, 0.0f, 1.0f) * 62.0f));
        Cv.FillRect(554, 302, 616, 309, PackRgba8(26, 36, 48, 235));
        if (ThrW > 0) Cv.FillRect(554, 302, 554 + ThrW, 309, PackRgba8(58, 245, 146, 255));
        Cv.FillRect(554, 313, 616, 320, PackRgba8(26, 36, 48, 235));
        if (BrkW > 0) Cv.FillRect(554, 313, 554 + BrkW, 320, PackRgba8(255, 68, 64, 255));

        // Status chips
        Cv.PaintText(
            554,
            328,
            T.StabilityAssistOn ? "ESC ON" : "DRIFT",
            1,
            PackRgba8(130, 226, 248, 255));
    }
}

void VehicleCockpitInstrumentation::PublishCockpitDisplaysToScene(
    Frontier::GeometricRaster::WorldSceneSpecification& Scene,
    const CockpitTelemetryReading&                     Telemetry,
    const HmiControlPanel&                          HmiPanel,
    const std::vector<uint8_t>&                         HmiFeedRgba8,
    uint32_t                                            HmiFeedW,
    uint32_t                                            HmiFeedH,
    const Axis3D&                                       VehiclePosWorld,
    float                                               VehicleYawDeg,
    bool                                                IncludeInCarDisplays)
{
    if (Scene.CockpitDisplayAtlasRgba8.size() != TotalTexels_)
    {
        Scene.CockpitDisplayAtlasRgba8.assign(TotalTexels_, 0u);
    }

    uint32_t* Base = Scene.CockpitDisplayAtlasRgba8.data();
    if (IncludeInCarDisplays)
    {
        RenderHmiCanvas(Base + HmiOffset_, Telemetry, HmiPanel, HmiFeedRgba8, HmiFeedW, HmiFeedH);
        RenderClusterCanvas(Base + ClusterOffset_, Telemetry);
        if (HmiPanel.WindshieldHudEnabled)
        {
            RenderWindshieldHudCanvas(Base + WindOffset_, Telemetry);
        }
    }
    if (HmiPanel.OnScreenHudEnabled)
    {
        RenderOnScreenOverlayCanvas(Base + OverlayOffset_, Telemetry);
    }

    Scene.CockpitDisplays              = BuildWorldDisplaySpecs(VehiclePosWorld, VehicleYawDeg, HmiPanel, IncludeInCarDisplays);
    Scene.CockpitDisplayAtlasGeneration = ++AtlasGen_;
}

} // namespace AutoSpace
