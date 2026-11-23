/*============================================================================================================================================
                                                       PRECIPITATIONINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance PrecipitationInspector

#include "Engine/Editor/Inspectors/PrecipitationInspector.h"

#include <array>
#include <cstdio>

namespace Frontier
{

float PrecipitationInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, PrecipitationInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Particles", "Simulation" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    static constexpr std::array<const char*, 5u> Modes = {{ "Rain", "Drizzle", "Hail", "Snow", "Sleet" }};

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 214.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Precipitation type");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Rate[16]{}; std::snprintf(Rate, sizeof(Rate), "%.0f", Instance.RateMultiplier * 100.0f);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Rate, "%", Modes[Instance.ModeIndex % 5u]);
        int32_t Chosen = -1;
        (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y + 4.0f, BodyW, "Hydrometeor", Modes, Instance.ModeIndex % 5u, 3u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 5) { Instance.ModeIndex = static_cast<uint32_t>(Chosen); OutMutated = true; }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Enabled",          InspectorSwitchSymbol::Cloud, Entry.VisibilityEnabled,    false },
            { "Follow wind",      InspectorSwitchSymbol::Wind,  Instance.WindSlantScale > 0.05f, false },
            { "Spawn from clouds",InspectorSwitchSymbol::Orbit, true,                       false },
            { "Ground collision", InspectorSwitchSymbol::Cube,  true,                       false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Instance.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.WindSlantScale = (Instance.WindSlantScale > 0.05f) ? 0.0f : 0.75f; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "precipitation", "weather" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Fall + density");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6501u, "Fall rate", "x", "%.2f", 0.0f, 2.0f, Instance.RateMultiplier, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6502u, "Drop diameter", "mm", "%.1f", 0.3f, 8.0f, Instance.DropSizeMm, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6503u, "Wind slant", "x", "%.2f", 0.0f, 2.0f, Instance.WindSlantScale, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 168.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Simulation + settling");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char VelBuf[24]{};
        const float TerminalMs = 1.6f + Instance.DropSizeMm * 1.1f;
        std::snprintf(VelBuf, sizeof(VelBuf), "%.1f m/s", TerminalMs);
        Y = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Hydrometeor class", Modes[Instance.ModeIndex % 5u]);
        Y = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Terminal speed", VelBuf);
        (void)InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Wind advection", Instance.WindSlantScale > 0.05f ? "Coupled" : "Vertical");
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
