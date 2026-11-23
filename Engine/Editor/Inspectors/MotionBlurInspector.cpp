/*============================================================================================================================================
                                                         MOTIONBLURINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance MotionBlurInspector

#include "Engine/Editor/Inspectors/MotionBlurInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float MotionBlurInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, MotionBlurInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Settings" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Shutter study");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const float ShutterDeg = std::clamp(Instance.ShutterAngleDegrees, 15.0f, 360.0f);
        const float InvSec     = 24.0f * (360.0f / ShutterDeg);
        char ExpWhole[24]{}, SubBuf[64]{};
        std::snprintf(ExpWhole, sizeof(ExpWhole), "1/%.0f", InvSec);
        std::snprintf(SubBuf, sizeof(SubBuf), "%.0f\xC2\xB0 rotary shutter · 24 fps cinema base", ShutterDeg);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, ExpWhole, "", "s exposure", SubBuf);
        Y += 6.0f;

        // Dark #151618 rotary shutter + frame duty timeline + motion streak well
        const Rectangle2D Well { LeftX + PadX, Y, BodyW, 134.0f };
        Surface.FillRectangle(Well, ColorRgba{ 21u, 22u, 24u, 255u }, 10.0f);
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);

        // Left: amber rotary shutter disc
        const Point2D DiscC { Well.X + 54.0f, Well.Y + 62.0f };
        const float   DiscR = 38.0f;
        Surface.FillCircle(DiscC, DiscR, ColorRgba{ 32u, 33u, 38u, 255u });
        const float OpenRad = ShutterDeg * (3.14159265f / 180.0f);
        const float StartA  = -3.14159265f * 0.5f - OpenRad * 0.5f;
        constexpr std::size_t WedgeSteps = 22u;
        std::array<Point2D, WedgeSteps + 2u> Wedge{};
        Wedge[0u] = DiscC;
        for (std::size_t I = 0u; I <= WedgeSteps; ++I)
        {
            const float A = StartA + (static_cast<float>(I) / static_cast<float>(WedgeSteps)) * OpenRad;
            Wedge[I + 1u] = { DiscC.X + std::cos(A) * DiscR, DiscC.Y + std::sin(A) * DiscR };
        }
        Surface.FillPolygon(Wedge, ColorRgba{ 232u, 168u, 76u, 190u });
        Surface.StrokeCircle(DiscC, DiscR, ColorRgba{ 255u, 255u, 255u, 32u }, 1.2f);
        Surface.FillCircle(DiscC, 7.5f, ColorRgba{ 21u, 22u, 24u, 255u });
        Surface.StrokeCircle(DiscC, 7.5f, ColorRgba{ 232u, 168u, 76u, 210u }, 1.2f);
        char DegFoot[16]{};
        std::snprintf(DegFoot, sizeof(DegFoot), "%.0f\xC2\xB0", ShutterDeg);
        const float DFW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.0f, DegFoot);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { DiscC.X - DFW * 0.5f, Well.Bottom() - 18.0f }, 9.0f, ColorRgba{ 232u, 178u, 96u, 255u }, DegFoot);

        // Right top: FRAMES · 24 FPS duty cycle bars
        const float RightX = Well.X + 108.0f;
        const float RightW = std::max(60.0f, Well.Right() - RightX - 12.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { RightX, Well.Y + 14.0f }, 8.2f, InspectorMetrics::DimInk, "FRAMES · 24 FPS");
        const float BarY = Well.Y + 30.0f;
        const float Duty = std::clamp(ShutterDeg / 360.0f, 0.04f, 1.0f);
        const float SegW = (RightW - 8.0f) / 3.0f;
        for (int F = 0; F < 3; ++F)
        {
            const float FX = RightX + static_cast<float>(F) * (SegW + 4.0f);
            Surface.FillRectangle({ FX, BarY, SegW, 14.0f }, ColorRgba{ 36u, 38u, 44u, 255u }, 3.0f);
            Surface.FillRectangle({ FX, BarY, std::max(3.0f, SegW * Duty), 14.0f }, ColorRgba{ 232u, 168u, 76u, 225u }, 3.0f);
        }

        // Right bottom: motion streak smear preview
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { RightX, Well.Y + 62.0f }, 8.2f, InspectorMetrics::DimInk, "BLUR STREAK");
        const float StreakY = Well.Y + 84.0f;
        const float StreakLen = std::clamp((ShutterDeg / 360.0f) * Instance.VelocityScale * RightW * 0.78f, 8.0f, RightW - 14.0f);
        for (int Step = 4; Step >= 1; --Step)
        {
            const float Frac = static_cast<float>(Step) / 4.0f;
            const uint8_t A = static_cast<uint8_t>(26 + (5 - Step) * 28);
            Surface.FillRectangle({ RightX, StreakY - 6.0f, StreakLen * Frac + 10.0f, 12.0f }, ColorRgba{ 232u, 178u, 96u, A }, 6.0f);
        }
        Surface.FillCircle({ RightX + StreakLen + 4.0f, StreakY }, 6.0f, ColorRgba{ 244u, 226u, 196u, 255u });
        Y = Well.Bottom() + 10.0f;

        // 3-column stat tiles inside hero section (EXPOSURE / STREAK / VELOCITY)
        const float Gap = 6.0f;
        const float TileW = (BodyW - Gap * 2.0f) / 3.0f;
        char ExpVal[20]{}, StrVal[20]{}, VelVal[20]{};
        std::snprintf(ExpVal, sizeof(ExpVal), "1/%.0f s", InvSec);
        std::snprintf(StrVal, sizeof(StrVal), "%.0f px", Instance.MaxSampleRadiusPx * Duty);
        std::snprintf(VelVal, sizeof(VelVal), "%.2fx",   Instance.VelocityScale);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX,                      Y, TileW, 46.0f }, "EXPOSURE", ExpVal);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + TileW + Gap,        Y, TileW, 46.0f }, "STREAK",   StrVal);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + (TileW + Gap) * 2.0f, Y, TileW, 46.0f }, "VELOCITY", VelVal);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Enabled",       InspectorSwitchSymbol::Eye,        Entry.VisibilityEnabled && Instance.Enabled, false },
            { "Object motion", InspectorSwitchSymbol::Cube,       true,  false },
            { "Camera motion", InspectorSwitchSymbol::CameraLens, true,  false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Instance.Enabled = Entry.VisibilityEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "motion-blur", "shutter" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S1 { LeftX, CursorY, InnerWidth, 152.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S1, "Shutter");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7061u, "Shutter angle", "deg", "%.0f", 0.0f, 360.0f, Instance.ShutterAngleDegrees, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Reference rate", "24 fps");
        CursorY = S1.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D S2 { LeftX, CursorY, InnerWidth, 186.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S2, "Quality");
        Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7063u, "Max blur", "px", "%.0f", 2.0f, 64.0f, Instance.MaxSampleRadiusPx, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7062u, "Preview speed", "x", "%.2f", 0.0f, 3.0f, Instance.VelocityScale, ActiveDragId, Input, OutMutated);
        CursorY = S2.Bottom() + InspectorMetrics::SectionGap;
    }
    return CursorY;
}

} // namespace Frontier
