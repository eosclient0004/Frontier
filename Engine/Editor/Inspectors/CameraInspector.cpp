/*============================================================================================================================================
                                                           CAMERAINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance CameraInspector with own retained optical lens & F78 Mount (On tripod / Floating) diagrams

#include "Engine/Editor/Inspectors/CameraInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}

[[nodiscard]] float SegmentDistance(float PX, float PY, float AX, float AY, float BX, float BY) noexcept
{
    const float VX = BX - AX, VY = BY - AY;
    const float Den = VX * VX + VY * VY;
    const float T = (Den > 1e-5f) ? std::clamp(((PX - AX) * VX + (PY - AY) * VY) / Den, 0.0f, 1.0f) : 0.0f;
    const float DX = PX - (AX + VX * T), DY = PY - (AY + VY * T);
    return std::sqrt(DX * DX + DY * DY);
}
} // namespace

void CameraInspector::UpdateLensSurface(const CameraInstanceData& Instance)
{
    uint64_t Sig = 0xCA010001ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.FocalLengthMm);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.ApertureFStop);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.LensFlare.Enabled ? 1ull : 0ull);

    const uint32_t W = 224u, H = 142u;
    LensPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float CX = static_cast<float>(W) * 0.5f, CY = static_cast<float>(H) * 0.5f, OuterR = 48.0f;
        const float ApertureR = std::clamp(32.0f / std::max(1.2f, Instance.ApertureFStop), 6.0f, 34.0f);
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX, DY = static_cast<float>(Y) + 0.5f - CY;
                const float Dist = std::sqrt(DX * DX + DY * DY);
                float R = 0.045f, G = 0.048f, B = 0.058f;
                if (Dist < OuterR)
                {
                    const float Norm = Dist / OuterR;
                    if (Dist > OuterR - 6.0f) { R = 0.14f; G = 0.145f; B = 0.16f; }
                    else if (Dist > ApertureR)
                    {
                        const float Blade = 0.08f + 0.04f * std::sin(std::atan2(DY, DX) * 7.0f);
                        R = Blade; G = Blade * 1.03f; B = Blade * 1.12f;
                    }
                    else
                    {
                        const float Coat = std::exp(-std::pow((Norm - 0.35f) * 3.5f, 2.0f));
                        const float Glint = std::exp(-((DX + 14.0f) * (DX + 14.0f) + (DY + 14.0f) * (DY + 14.0f)) / 90.0f);
                        R = 0.05f + Coat * 0.12f + Glint * 0.72f + (Instance.LensFlare.Enabled ? Coat * 0.18f : 0.0f);
                        G = 0.09f + Coat * 0.24f + Glint * 0.85f;
                        B = 0.16f + Coat * 0.42f + Glint * 1.05f;
                    }
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

void CameraInspector::UpdateMountSurface(const CameraInstanceData& Instance)
{
    uint64_t Sig = 0xCA020001ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.TripodMounted ? 1ull : 0ull);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.MountHeightMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.CameraRollDegrees);

    const uint32_t W = 240u, H = 148u;
    MountPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const bool  Floating = !Instance.TripodMounted;
        const float GroundY  = 126.0f;
        const float CamX     = 118.0f;
        const float Scale    = Floating ? 12.0f : 38.0f;
        const float CamY     = GroundY - std::clamp(Instance.MountHeightMeters * Scale, 26.0f, 96.0f);
        const float RollRad  = Floating ? (Instance.CameraRollDegrees * 3.14159265f / 180.0f) : 0.0f;
        const float CosR     = std::cos(RollRad), SinR = std::sin(RollRad);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX = static_cast<float>(X) + 0.5f;
                float R = 0.028f, G = 0.030f, B = 0.035f;

                // Ground horizon line
                if (std::fabs(FY - GroundY) < 0.8f && FX > 14.0f && FX < 226.0f)
                {
                    R += 0.16f; G += 0.17f; B += 0.19f;
                }

                // Mount support (Tripod legs OR Floating drop-line + ground ellipse + motion arc)
                if (!Floating)
                {
                    const float DLeft  = SegmentDistance(FX, FY, CamX, CamY + 10.0f, CamX - 38.0f, GroundY);
                    const float DRight = SegmentDistance(FX, FY, CamX, CamY + 10.0f, CamX + 38.0f, GroundY);
                    const float DMid   = SegmentDistance(FX, FY, CamX, CamY + 10.0f, CamX + 6.0f,  GroundY);
                    if (DLeft < 1.5f || DRight < 1.5f) { R = 0.44f; G = 0.42f; B = 0.39f; }
                    else if (DMid < 1.3f) { R = 0.28f; G = 0.27f; B = 0.25f; }
                }
                else
                {
                    // Vertical dashed drop-line to ground
                    if (std::fabs(FX - CamX) < 0.9f && FY > CamY + 12.0f && FY < GroundY && (static_cast<int>(Y) % 8) < 4)
                    {
                        R += 0.36f; G += 0.54f; B += 0.72f;
                    }
                    // Ground projection ellipse
                    const float EX = (FX - CamX) / 24.0f, EY = (FY - GroundY) / 5.5f;
                    if (std::fabs(std::sqrt(EX * EX + EY * EY) - 1.0f) < 0.14f)
                    {
                        R += 0.36f; G += 0.54f; B += 0.72f;
                    }
                    // Curved crane/drone approach path from left
                    if (FX > 24.0f && FX < CamX - 20.0f)
                    {
                        const float T = (FX - 24.0f) / std::max(1.0f, (CamX - 44.0f));
                        const float ArcY = (CamY + 26.0f) * (1.0f - T) + CamY * T - std::sin(T * 3.14159f) * 22.0f;
                        if (std::fabs(FY - ArcY) < 1.0f && (static_cast<int>(X) % 7) < 4)
                        {
                            R += 0.28f; G += 0.44f; B += 0.62f;
                        }
                    }
                }

                // Height gauge bar on right
                if (std::fabs(FX - 208.0f) < 0.8f && FY >= CamY && FY <= GroundY)
                {
                    R = 0.72f; G = 0.56f; B = 0.28f;
                }
                if (FX >= 203.0f && FX <= 213.0f && (std::fabs(FY - CamY) < 0.8f || std::fabs(FY - GroundY) < 0.8f))
                {
                    R = 0.72f; G = 0.56f; B = 0.28f;
                }

                // Cinema camera body (rotated by Roll when Floating)
                const float DX = FX - CamX, DY = FY - CamY;
                const float LX =  DX * CosR + DY * SinR;
                const float LY = -DX * SinR + DY * CosR;
                if (std::fabs(LX) <= 16.0f && std::fabs(LY) <= 10.0f)
                {
                    R = 0.68f; G = 0.65f; B = 0.60f;
                }
                else if (LX > 16.0f && LX <= 29.0f && std::fabs(LY) <= 6.0f)
                {
                    R = 0.40f; G = 0.38f; B = 0.35f; // Lens matte box
                }
                else
                {
                    // Dual cinema film reels on top
                    const float R1 = std::sqrt((LX + 5.0f) * (LX + 5.0f) + (LY + 14.5f) * (LY + 14.5f));
                    const float R2 = std::sqrt((LX - 7.0f) * (LX - 7.0f) + (LY + 14.5f) * (LY + 14.5f));
                    if (std::fabs(R1 - 5.2f) < 1.5f || std::fabs(R2 - 5.2f) < 1.5f)
                    {
                        R = 0.68f; G = 0.65f; B = 0.60f;
                    }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float CameraInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, CameraInstanceData& Instance,
    ViewportDisplaySettings* DisplaySettings,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 6u> Pages = {{ "Overview", "Lens", "Body", "Mount", "Motion blur", "Lens flare" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        // 1:1 HTML Overview: Lens + field of view purple optical schematic + Focal length slider
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 316.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Lens + field of view", Instance.TripodMounted ? "Tripod" : "Floating");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const float SensorW = std::max(10.0f, Instance.SensorWidthMm);
        const float FocalMm = std::max(8.0f,  Instance.FocalLengthMm);
        const float HfovDeg = 2.0f * std::atan(SensorW / (2.0f * FocalMm)) * (180.0f / 3.14159265f);
        const long WholeFl = static_cast<long>(std::floor(FocalMm));
        const int  FracFl  = static_cast<int>(std::lround((FocalMm - static_cast<float>(WholeFl)) * 10.0f)) % 10;
        char WholeBuf[16]{}, FracBuf[8]{}, SubBuf[64]{};
        std::snprintf(WholeBuf, sizeof(WholeBuf), "%ld", WholeFl);
        std::snprintf(FracBuf,  sizeof(FracBuf),  ".%d", FracFl);
        std::snprintf(SubBuf, sizeof(SubBuf), "Horizontal FOV %.1f\xC2\xB0 · f/%.1f · %.0f mm gate", HfovDeg, Instance.ApertureFStop, SensorW);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, WholeBuf, FracBuf, "mm", SubBuf);
        Y += 6.0f;

        // Purple optical schematic: sensor plane -> biconvex lens & iris -> FOV ray cone -> focus plane
        const Rectangle2D Stage { LeftX + PadX, Y, BodyW, 128.0f };
        Surface.FillRectangle(Stage, ColorRgba{ 21u, 22u, 24u, 255u }, 10.0f);
        Surface.PushClip(Stage);

        const float AxisY   = Stage.Center().Y;
        const float SensorX = Stage.X + 26.0f;
        const float LensX   = Stage.X + 76.0f;
        const float ConeR   = Stage.Right() - 16.0f;
        const float HalfSensorH = std::clamp(SensorW * 0.56f, 12.0f, 24.0f);
        const float HalfConeEnd = std::clamp(std::tan(HfovDeg * 3.14159265f / 360.0f) * (ConeR - LensX) * 0.62f, 14.0f, Stage.Height * 0.44f);
        const float ApertureGap = std::clamp(30.0f / std::max(1.2f, Instance.ApertureFStop), 4.0f, 22.0f);

        // Optical axis dashed centreline
        for (float DX = SensorX; DX < ConeR; DX += 8.0f)
        {
            Surface.StrokeLine({ DX, AxisY }, { std::min(ConeR, DX + 4.0f), AxisY }, ColorRgba{ 168u, 134u, 248u, 55u }, 1.0f);
        }

        // Translucent purple FOV cone polygon (lens -> right boundary)
        const std::array<Point2D, 4u> ConePoly = {{
            { LensX, AxisY - ApertureGap },
            { ConeR, AxisY - HalfConeEnd },
            { ConeR, AxisY + HalfConeEnd },
            { LensX, AxisY + ApertureGap },
        }};
        Surface.FillPolygon(ConePoly, ColorRgba{ 156u, 118u, 246u, 34u });
        Surface.StrokeLine({ LensX, AxisY - ApertureGap }, { ConeR, AxisY - HalfConeEnd }, ColorRgba{ 182u, 146u, 255u, 215u }, 1.3f);
        Surface.StrokeLine({ LensX, AxisY + ApertureGap }, { ConeR, AxisY + HalfConeEnd }, ColorRgba{ 182u, 146u, 255u, 215u }, 1.3f);

        // Converging sensor-to-lens optical chamber
        const std::array<Point2D, 4u> BackPoly = {{
            { SensorX, AxisY - HalfSensorH },
            { LensX,   AxisY - ApertureGap },
            { LensX,   AxisY + ApertureGap },
            { SensorX, AxisY + HalfSensorH },
        }};
        Surface.FillPolygon(BackPoly, ColorRgba{ 156u, 118u, 246u, 24u });
        Surface.StrokeLine({ SensorX, AxisY - HalfSensorH }, { LensX, AxisY - ApertureGap }, ColorRgba{ 168u, 134u, 248u, 150u }, 1.1f);
        Surface.StrokeLine({ SensorX, AxisY + HalfSensorH }, { LensX, AxisY + ApertureGap }, ColorRgba{ 168u, 134u, 248u, 150u }, 1.1f);

        // Sensor bar + lens element + iris blades
        Surface.StrokeLine({ SensorX, AxisY - HalfSensorH - 4.0f }, { SensorX, AxisY + HalfSensorH + 4.0f }, ColorRgba{ 216u, 198u, 255u, 240u }, 2.6f);
        Surface.FillRectangle({ LensX - 3.0f, AxisY - 30.0f, 6.0f, 60.0f }, ColorRgba{ 138u, 108u, 224u, 64u }, 3.0f);
        Surface.StrokeRectangle({ LensX - 3.0f, AxisY - 30.0f, 6.0f, 60.0f }, ColorRgba{ 196u, 172u, 255u, 180u }, 3.0f, 1.1f);
        // Iris blade stop bars
        Surface.StrokeLine({ LensX + 5.0f, AxisY - 28.0f }, { LensX + 5.0f, AxisY - ApertureGap }, ColorRgba{ 222u, 206u, 255u, 220u }, 2.0f);
        Surface.StrokeLine({ LensX + 5.0f, AxisY + ApertureGap }, { LensX + 5.0f, AxisY + 28.0f }, ColorRgba{ 222u, 206u, 255u, 220u }, 2.0f);

        // Focus distance vertical dashed plane
        const float FocusU = std::clamp(std::log2(1.0f + Instance.FocusDistanceMeters) / std::log2(121.0f), 0.14f, 0.90f);
        const float FocusX = LensX + 18.0f + FocusU * (ConeR - LensX - 24.0f);
        for (float DY = Stage.Y + 14.0f; DY < Stage.Bottom() - 18.0f; DY += 6.0f)
        {
            Surface.StrokeLine({ FocusX, DY }, { FocusX, std::min(Stage.Bottom() - 18.0f, DY + 3.0f) }, ColorRgba{ 216u, 194u, 255u, 165u }, 1.0f);
        }
        Surface.FillCircle({ FocusX, AxisY }, 3.2f, ColorRgba{ 216u, 194u, 255u, 255u });

        // Schematic captions
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.X + 8.0f, Stage.Y + 7.0f }, 8.0f, ColorRgba{ 176u, 154u, 224u, 220u }, "SENSOR");
        char FovTag[28]{};
        std::snprintf(FovTag, sizeof(FovTag), "FOV %.1f\xC2\xB0", HfovDeg);
        const float FTW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.4f, FovTag);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.Right() - 8.0f - FTW, Stage.Y + 7.0f }, 8.4f, ColorRgba{ 204u, 182u, 255u, 240u }, FovTag);
        char FocTag[28]{};
        std::snprintf(FocTag, sizeof(FocTag), "Focus %.1f m", Instance.FocusDistanceMeters);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { std::min(Stage.Right() - 62.0f, FocusX - 18.0f), Stage.Bottom() - 14.0f }, 8.2f, ColorRgba{ 182u, 164u, 226u, 220u }, FocTag);
        Surface.PopClip();
        Surface.StrokeRectangle(Stage, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);

        Y = Stage.Bottom() + 12.0f;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7001u, "Focal length", "mm", "%.0f", 14.0f, 200.0f, Instance.FocalLengthMm, ActiveDragId, Input, OutMutated);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        const bool ReflOn = DisplaySettings ? DisplaySettings->ReflectionsEnabled : true;
        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Visible",     InspectorSwitchSymbol::CameraLens, Entry.VisibilityEnabled,    false },
            { "Lens flare",  InspectorSwitchSymbol::PostFx,     Instance.LensFlare.Enabled, false },
            { "Motion blur", InspectorSwitchSymbol::Cycle,      Instance.MotionBlurEnabled, false },
            { "Reflections", InspectorSwitchSymbol::Stars,      ReflOn,                     false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.LensFlare.Enabled = !Instance.LensFlare.Enabled; OutMutated = true; }
        else if (Clicked == 2) { Instance.MotionBlurEnabled = !Instance.MotionBlurEnabled; OutMutated = true; }
        else if (Clicked == 3 && DisplaySettings) { DisplaySettings->ReflectionsEnabled = !DisplaySettings->ReflectionsEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "camera", "optics" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 356.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Camera lens & gate");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Fl[16]{}; std::snprintf(Fl, sizeof(Fl), "%.0f", Instance.FocalLengthMm);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Fl, "mm", "Rectilinear optical projection");
        static constexpr std::array<const char*, 4u> Gates = {{ "Full 36mm", "Super 35", "Med 44mm", "APS-C" }};
        static constexpr std::array<float, 4u> GateW = {{ 36.0f, 24.89f, 43.8f, 23.5f }};
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Sensor gate", Gates, Instance.SensorGateIndex, 2u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 4) { Instance.SensorGateIndex = static_cast<uint32_t>(Chosen); Instance.SensorWidthMm = GateW[static_cast<std::size_t>(Chosen)]; OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7001u, "Focal length", "mm", "%.0f", 14.0f, 200.0f, Instance.FocalLengthMm, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7002u, "Aperture", "f/", "%.1f", 1.2f, 22.0f, Instance.ApertureFStop, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7003u, "Focus distance", "m", "%.1f", 0.3f, 120.0f, Instance.FocusDistanceMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 184.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Camera body");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        static constexpr std::array<const char*, 3u> Bodies = {{ "Cinema", "Rig", "Compact" }};
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Body rig", Bodies, Instance.RigPresetIndex, 3u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 3) { Instance.RigPresetIndex = static_cast<uint32_t>(Chosen); OutMutated = true; }
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "On tripod mount", Instance.TripodMounted, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        // 1:1 HTML Mount page (02-cine-mount.png): dark hero stage with illustrated camera + amber height bracket + 2 visual tiles + Tripod height + Position + angle
        UpdateMountSurface(Instance);
        const bool Floating = !Instance.TripodMounted;
        const Point2D Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
        const bool Pressed = Input.PrimaryPointer.Pressed;

        const float MountSecH = Floating ? 476.0f : 424.0f;
        const Rectangle2D MountSec { LeftX, CursorY, InnerWidth, MountSecH };
        InspectorPrimitives::ConstructSectionSurface(Surface, MountSec, "Mount");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;

        // Dark glossy #15171c hero stage containing camera illustration + centered A · On tripod / B · Floating + subtitle
        const Rectangle2D Stage { LeftX + PadX, Y, BodyW, 174.0f };
        Surface.FillRectangle(Stage, ColorRgba{ 21u, 23u, 28u, 255u }, 14.0f);
        Surface.StrokeRectangle(Stage, ColorRgba{ 255u, 255u, 255u, 14u }, 14.0f, 1.0f);

        const float FloorY = Stage.Y + 98.0f;
        const float CamCx  = Stage.Center().X;
        const float CamCy  = Stage.Y + 42.0f;
        Surface.StrokeLine({ Stage.X + 26.0f, FloorY }, { Stage.Right() - 26.0f, FloorY }, ColorRgba{ 255u, 255u, 255u, 42u }, 1.0f);

        if (!Floating)
        {
            // Three extending tripod legs from camera head to ground line
            const Point2D Head { CamCx, CamCy + 14.0f };
            Surface.StrokeLine(Head, { CamCx - 24.0f, FloorY }, ColorRgba{ 168u, 174u, 184u, 220u }, 1.6f);
            Surface.StrokeLine(Head, { CamCx + 24.0f, FloorY }, ColorRgba{ 168u, 174u, 184u, 220u }, 1.6f);
            Surface.StrokeLine(Head, { CamCx + 2.0f,  FloorY }, ColorRgba{ 120u, 126u, 136u, 190u }, 1.3f);
            Surface.StrokeLine({ CamCx - 12.0f, CamCy + 34.0f }, { CamCx + 12.0f, CamCy + 34.0f }, ColorRgba{ 140u, 146u, 156u, 180u }, 1.1f);
            Surface.FillRectangle({ CamCx - 8.0f, CamCy + 11.0f, 16.0f, 5.0f }, ColorRgba{ 170u, 176u, 188u, 255u }, 2.0f);
        }
        else
        {
            // Dashed drop line + cyan ground target ring
            for (float Dy = CamCy + 18.0f; Dy < FloorY - 4.0f; Dy += 7.0f)
            {
                Surface.StrokeLine({ CamCx, Dy }, { CamCx, std::min(FloorY - 2.0f, Dy + 3.5f) }, ColorRgba{ 98u, 206u, 214u, 160u }, 1.2f);
            }
            std::array<Point2D, 24u> Ring {};
            for (uint32_t R = 0u; R < 24u; ++R)
            {
                const float A = (6.2831853f * static_cast<float>(R)) / 24.0f;
                Ring[R] = { CamCx + std::cos(A) * 18.0f, FloorY + std::sin(A) * 5.0f };
            }
            Surface.StrokePolyline(Ring, ColorRgba{ 98u, 206u, 214u, 185u }, 1.2f, true);
        }

        // Illustrated cinema camera silhouette (dual film magazines + body + lens matte box)
        Surface.FillCircle({ CamCx - 9.0f, CamCy - 9.0f }, 6.5f, ColorRgba{ 194u, 198u, 206u, 255u });
        Surface.FillCircle({ CamCx + 5.0f, CamCy - 9.0f }, 6.5f, ColorRgba{ 194u, 198u, 206u, 255u });
        Surface.FillRectangle({ CamCx - 14.0f, CamCy - 4.0f, 26.0f, 15.0f }, ColorRgba{ 212u, 216u, 224u, 255u }, 3.0f);
        Surface.FillRectangle({ CamCx + 12.0f, CamCy - 2.0f, 10.0f, 10.0f }, ColorRgba{ 156u, 162u, 172u, 255u }, 2.0f);

        // Amber vertical height bracket + label on right side (1:1 HTML)
        const float BrX = CamCx + 42.0f;
        const ColorRgba Amber { 228u, 174u, 76u, 235u };
        Surface.StrokeLine({ BrX, CamCy + 6.0f }, { BrX, FloorY }, Amber, 1.3f);
        Surface.StrokeLine({ BrX - 4.0f, CamCy + 6.0f }, { BrX + 4.0f, CamCy + 6.0f }, Amber, 1.3f);
        Surface.StrokeLine({ BrX - 4.0f, FloorY }, { BrX + 4.0f, FloorY }, Amber, 1.3f);
        char HBuf[24]{};
        std::snprintf(HBuf, sizeof(HBuf), "%.2f m", Instance.MountHeightMeters);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { BrX + 7.0f, (CamCy + FloorY) * 0.5f - 4.0f }, 9.5f, Amber, HBuf);

        const char* HeroTitle = Floating ? "B · Floating" : "A · On tripod";
        const char* HeroSub   = Floating ? "Free crane / drone mount · full roll & height" : "A fixed camera · the tripod's legs extend to the camera's height";
        const float HTW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 13.0f, HeroTitle);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.Center().X - HTW * 0.5f, Stage.Y + 114.0f }, 13.0f, ColorRgba{ 242u, 244u, 248u, 255u }, HeroTitle);
        const float HSW = std::min(Stage.Width - 16.0f, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.2f, HeroSub));
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Stage.Center().X - HSW * 0.5f, Stage.Y + 134.0f }, Stage.Width - 16.0f, 9.2f, ColorRgba{ 146u, 150u, 158u, 255u }, HeroSub);
        Y = Stage.Bottom() + 10.0f;

        // 2 side-by-side visual tiles (A On tripod | B Floating) matching 02-cine-mount.png
        const float TileGap = 8.0f;
        const float TileW   = std::floor((BodyW - TileGap) * 0.5f);
        const float TileH   = 74.0f;
        for (uint32_t I = 0u; I < 2u; ++I)
        {
            const Rectangle2D T { LeftX + PadX + static_cast<float>(I) * (TileW + TileGap), Y, TileW, TileH };
            const bool Sel = (I == 0u ? !Floating : Floating);
            const bool Hov = T.Contains(Cur);
            Surface.FillRectangle(T, Sel ? ColorRgba{ 36u, 38u, 43u, 255u } : (Hov ? ColorRgba{ 28u, 30u, 34u, 255u } : ColorRgba{ 22u, 23u, 26u, 255u }), 11.0f);
            Surface.StrokeRectangle(T, Sel ? ColorRgba{ 235u, 238u, 244u, 185u } : ColorRgba{ 255u, 255u, 255u, 18u }, 11.0f, Sel ? 1.2f : 1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { T.X + 8.0f, T.Y + 6.0f }, 9.6f,
                Sel ? ColorRgba{ 239u, 68u, 68u, 255u } : ColorRgba{ 118u, 122u, 130u, 255u }, I == 0u ? "A" : "B");

            // Mini vector icon inside tile
            const float Ix = T.Center().X, Iy = T.Y + 28.0f;
            Surface.FillRectangle({ Ix - 8.0f, Iy - 5.0f, 14.0f, 8.0f }, ColorRgba{ 186u, 192u, 202u, 255u }, 2.0f);
            if (I == 0u)
            {
                Surface.StrokeLine({ Ix - 1.0f, Iy + 3.0f }, { Ix - 11.0f, Iy + 21.0f }, ColorRgba{ 156u, 162u, 172u, 220u }, 1.2f);
                Surface.StrokeLine({ Ix - 1.0f, Iy + 3.0f }, { Ix + 9.0f,  Iy + 21.0f }, ColorRgba{ 156u, 162u, 172u, 220u }, 1.2f);
                Surface.StrokeLine({ Ix - 14.0f, Iy + 21.0f }, { Ix + 12.0f, Iy + 21.0f }, ColorRgba{ 255u, 255u, 255u, 44u }, 1.0f);
            }
            else
            {
                Surface.StrokeLine({ Ix - 1.0f, Iy + 6.0f }, { Ix - 1.0f, Iy + 18.0f }, ColorRgba{ 98u, 206u, 214u, 165u }, 1.1f);
                std::array<Point2D, 18u> MiniRing {};
                for (uint32_t R = 0u; R < 18u; ++R)
                {
                    const float A = (6.2831853f * static_cast<float>(R)) / 18.0f;
                    MiniRing[R] = { (Ix - 1.0f) + std::cos(A) * 10.0f, (Iy + 20.0f) + std::sin(A) * 3.0f };
                }
                Surface.StrokePolyline(MiniRing, ColorRgba{ 98u, 206u, 214u, 185u }, 1.1f, true);
            }
            const char* TLab = (I == 0u) ? "On tripod" : "Floating";
            const float TLW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, TLab);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { T.Center().X - TLW * 0.5f, T.Bottom() - 16.0f }, 9.6f, Sel ? ColorRgba{ 238u, 240u, 244u, 255u } : ColorRgba{ 148u, 152u, 160u, 255u }, TLab);
            if (Hov && Pressed)
            {
                Instance.TripodMounted = (I == 0u);
                if (Instance.TripodMounted && Instance.MountHeightMeters > 2.2f) Instance.MountHeightMeters = 2.2f;
                Entry.PositionY = Instance.MountHeightMeters;
                OutMutated = true;
            }
        }
        Y += TileH + 14.0f;

        const float MaxH = Floating ? 8.0f : 2.2f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(
            Surface, LeftX + PadX, Y, BodyW, 7010u, Floating ? "Height" : "Tripod height", "m", "%.2f",
            Floating ? 0.1f : 0.45f, MaxH, Instance.MountHeightMeters, ActiveDragId, Input, OutMutated);
        Entry.PositionY = Instance.MountHeightMeters;
        if (Floating)
        {
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7011u, "Roll", "deg", "%.0f", -45.0f, 45.0f, Instance.CameraRollDegrees, ActiveDragId, Input, OutMutated);
        }
        char DescBuf[96]{};
        std::snprintf(DescBuf, sizeof(DescBuf), Floating ? "Free floating mount at %.2f m with %.0f deg roll." : "The tripod legs extend to %.2f m and feet stay on ground (0.4–2.2 m).", Instance.MountHeightMeters, Instance.CameraRollDegrees);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, Y }, BodyW, 9.2f, ColorRgba{ 138u, 142u, 150u, 255u }, DescBuf);
        CursorY = MountSec.Bottom() + InspectorMetrics::SectionGap;

        // 1:1 HTML Position + angle KPI summary card (02-cine-mount.png)
        const Rectangle2D PosSec { LeftX, CursorY, InnerWidth, 168.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PosSec, "Position + angle");
        float PY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char HighBuf[16]{};
        std::snprintf(HighBuf, sizeof(HighBuf), "%.2f", Instance.MountHeightMeters);
        PY = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, PY, HighBuf, "m high", "On its arc in front of the Showcase");
        PY = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, PY, BodyW, "Aim at Showcase", Instance.AimAtShowcase, Input, OutMutated);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + PadX, PY + 2.0f }, 10.0f, ColorRgba{ 138u, 142u, 150u, 255u }, "Camera");
        const char* AimTag = Instance.AimAtShowcase ? "AIMED AT SHOWCASE" : "MANUAL ORIENTATION";
        const float AW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.4f, AimTag);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + PadX + BodyW - AW, PY + 2.0f }, 9.4f, ColorRgba{ 138u, 142u, 150u, 255u }, AimTag);
        CursorY = PosSec.Bottom() + InspectorMetrics::SectionGap;

        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 7020u, ActiveDragId, Input, OutMutated);
    }
    else if (ActivePage == 4u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 212.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Motion blur");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable motion blur", Instance.MotionBlurEnabled, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7004u, "Shutter angle", "deg", "%.0f", 15.0f, 360.0f, Instance.ShutterAngleDegrees, ActiveDragId, Input, OutMutated, !Instance.MotionBlurEnabled);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        auto& Flare = Instance.LensFlare;
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Optical lens flare");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Lens flare", Flare.Enabled, Input, OutMutated);
        static constexpr std::array<const char*, 3u> Styles = {{ "Anamorphic", "Spherical", "Clean" }};
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Optical style", Styles, static_cast<uint32_t>(Flare.Style), 3u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 3) { Flare.Style = static_cast<GeometricRaster::LensFlareStyle>(Chosen); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7005u, "Intensity", "x", "%.2f", 0.0f, 3.0f, Flare.FlareIntensity, ActiveDragId, Input, OutMutated, !Flare.Enabled);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7006u, "Halo glow", "x", "%.2f", 0.0f, 2.0f, Flare.HaloIntensity, ActiveDragId, Input, OutMutated, !Flare.Enabled);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7007u, "Ghost elements", "x", "%.2f", 0.0f, 2.0f, Flare.GhostIntensity, ActiveDragId, Input, OutMutated, !Flare.Enabled);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7008u, "Streak angle", "deg", "%.0f", -90.0f, 90.0f, Flare.StreakRotationDegrees, ActiveDragId, Input, OutMutated, !Flare.Enabled);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
