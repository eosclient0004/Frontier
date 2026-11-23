/*============================================================================================================================================
                                                          LENSFLAREINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LensFlareInspector

#include "Engine/Editor/Inspectors/LensFlareInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float LensFlareInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LensFlareInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Optics" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    auto& Flare = Instance.Flare;

    if (ActivePage == 0u)
    {
        static constexpr std::array<const char*, 3u> Styles = {{ "Anamorphic", "Spherical", "Clean" }};
        const char* StyleName = Styles[std::min<std::size_t>(static_cast<std::size_t>(Flare.Style), 2u)];

        const Rectangle2D S { LeftX, CursorY, InnerWidth, 276.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Flare composite", StyleName);
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const long WholeF = static_cast<long>(std::floor(Flare.FlareIntensity));
        const int  FracF  = static_cast<int>(std::lround((Flare.FlareIntensity - static_cast<float>(WholeF)) * 100.0f)) % 100;
        char WholeBuf[16]{}, FracBuf[8]{}, SubBuf[64]{};
        std::snprintf(WholeBuf, sizeof(WholeBuf), "%ld", WholeF);
        std::snprintf(FracBuf,  sizeof(FracBuf),  ".%02d", FracF);
        std::snprintf(SubBuf, sizeof(SubBuf), "%s optics · halo %.2f\xC3\x97 · ghosts %.2f\xC3\x97", StyleName, Flare.HaloIntensity, Flare.GhostIntensity);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, WholeBuf, FracBuf, "\xC3\x97", SubBuf);
        Y += 6.0f;

        // Dark #151618 optical composite: starburst + chromatic halo + anamorphic streak + diagonal ghosts
        const Rectangle2D Stage { LeftX + PadX, Y, BodyW, 136.0f };
        Surface.FillRectangle(Stage, ColorRgba{ 18u, 19u, 23u, 255u }, 10.0f);
        Surface.PushClip(Stage);

        const float Gain = (Entry.VisibilityEnabled && Flare.Enabled) ? std::clamp(Flare.FlareIntensity, 0.0f, 2.5f) : 0.12f;
        const Point2D Src { Stage.X + Stage.Width * 0.30f, Stage.Y + Stage.Height * 0.44f };
        const Point2D Mid = Stage.Center();
        const float AxisX = Mid.X - Src.X, AxisY = Mid.Y - Src.Y;

        // Diagonal optical axis guide
        Surface.StrokeLine({ Src.X - AxisX * 0.6f, Src.Y - AxisY * 0.6f }, { Mid.X + AxisX * 1.4f, Mid.Y + AxisY * 1.4f }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);

        // Chromatic halo ring around source
        const float HaloA = std::clamp(Flare.HaloIntensity * Gain * 0.45f, 0.0f, 0.9f);
        if (HaloA > 0.02f)
        {
            Surface.StrokeCircle(Src, 42.0f, ColorRgba{ 242u, 174u, 96u,  static_cast<uint8_t>(HaloA * 120.0f) }, 2.2f);
            Surface.StrokeCircle(Src, 39.0f, ColorRgba{ 126u, 214u, 246u, static_cast<uint8_t>(HaloA * 110.0f) }, 1.8f);
            Surface.StrokeCircle(Src, 36.0f, ColorRgba{ 186u, 136u, 244u, static_cast<uint8_t>(HaloA * 85.0f)  }, 1.4f);
        }

        // Ghost reflections along optical axis
        const float GhostA = std::clamp(Flare.GhostIntensity * Gain * 0.42f, 0.0f, 0.85f);
        if (GhostA > 0.02f)
        {
            static constexpr std::array<float, 4u> GhostPos = {{ 0.55f, 1.15f, 1.72f, 2.18f }};
            static constexpr std::array<float, 4u> GhostRad = {{ 9.0f,  15.0f, 7.0f,  19.0f }};
            static constexpr std::array<ColorRgba, 4u> GhostCol = {{
                { 126u, 198u, 248u, 255u },
                { 196u, 148u, 244u, 255u },
                { 246u, 196u, 128u, 255u },
                { 112u, 224u, 198u, 255u },
            }};
            for (std::size_t I = 0u; I < 4u; ++I)
            {
                const Point2D GP { Src.X + AxisX * GhostPos[I], Src.Y + AxisY * GhostPos[I] };
                ColorRgba Fill = GhostCol[I]; Fill.Alpha = static_cast<uint8_t>(GhostA * 68.0f);
                ColorRgba Rim  = GhostCol[I]; Rim.Alpha  = static_cast<uint8_t>(GhostA * 155.0f);
                Surface.FillCircle(GP, GhostRad[I], Fill);
                Surface.StrokeCircle(GP, GhostRad[I], Rim, 1.0f);
            }
        }

        // Anamorphic horizontal streak
        if (Flare.Style == GeometricRaster::LensFlareStyle::Anamorphic)
        {
            const float StrA = std::clamp(Gain * 0.65f, 0.0f, 0.95f);
            Surface.StrokeLine({ Stage.X + 8.0f, Src.Y }, { Stage.Right() - 8.0f, Src.Y }, ColorRgba{ 118u, 186u, 252u, static_cast<uint8_t>(StrA * 125.0f) }, 3.2f);
            Surface.StrokeLine({ Stage.X + 24.0f, Src.Y }, { Stage.Right() - 24.0f, Src.Y }, ColorRgba{ 216u, 238u, 255u, static_cast<uint8_t>(StrA * 210.0f) }, 1.2f);
        }

        // 6-ray starburst diffraction spikes rotated by StreakRotationDegrees
        const float BaseRad = Flare.StreakRotationDegrees * (3.14159265f / 180.0f);
        for (int Ray = 0; Ray < 6; ++Ray)
        {
            const float Ang = BaseRad + static_cast<float>(Ray) * (3.14159265f / 3.0f);
            const float Len = (Ray % 2 == 0) ? 34.0f : 22.0f;
            const Point2D A { Src.X - std::cos(Ang) * Len, Src.Y - std::sin(Ang) * Len };
            const Point2D B { Src.X + std::cos(Ang) * Len, Src.Y + std::sin(Ang) * Len };
            Surface.StrokeLine(A, B, ColorRgba{ 255u, 240u, 214u, static_cast<uint8_t>(std::clamp(Gain * 140.0f, 20.0f, 220.0f)) }, 1.1f);
        }
        // Luminous core bloom
        Surface.FillCircle(Src, 18.0f, ColorRgba{ 255u, 214u, 152u, static_cast<uint8_t>(std::clamp(Gain * 54.0f, 12.0f, 90.0f)) });
        Surface.FillCircle(Src, 9.0f,  ColorRgba{ 255u, 238u, 206u, static_cast<uint8_t>(std::clamp(Gain * 145.0f, 24.0f, 200.0f)) });
        Surface.FillCircle(Src, 4.0f,  ColorRgba{ 255u, 252u, 246u, 255u });

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.X + 8.0f, Stage.Bottom() - 14.0f }, 8.2f, InspectorMetrics::DimInk, "OPTICAL COMPOSITE");
        Surface.PopClip();
        Surface.StrokeRectangle(Stage, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Visible", InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled && Flare.Enabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Flare.Enabled = Entry.VisibilityEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lens-flare", "optics" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 326.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Optical lens flare");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        static constexpr std::array<const char*, 3u> Styles = {{ "Anamorphic", "Spherical", "Clean" }};
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Optical style", Styles, static_cast<uint32_t>(Flare.Style), 3u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 3) { Flare.Style = static_cast<GeometricRaster::LensFlareStyle>(Chosen); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7051u, "Intensity", "x", "%.2f", 0.0f, 3.0f, Flare.FlareIntensity, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7052u, "Halo glow", "x", "%.2f", 0.0f, 2.0f, Flare.HaloIntensity, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7053u, "Ghost elements", "x", "%.2f", 0.0f, 2.0f, Flare.GhostIntensity, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7054u, "Streak angle", "deg", "%.0f", -90.0f, 90.0f, Flare.StreakRotationDegrees, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    return CursorY;
}

} // namespace Frontier
