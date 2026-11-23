/*============================================================================================================================================
                                                         POSTPROCESSINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance PostProcessInspector

#include "Engine/Editor/Inspectors/PostProcessInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float PostProcessInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, PostProcessInstanceData& Instance,
    ViewportDisplaySettings* DisplaySettings,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Settings" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        // 1:1 HTML Post-process hero: Exposure & tone filmic transfer curve + inline Exposure slider
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 276.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Exposure & tone", "ACES Filmic");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        char WholeEv[16]{}, SubBuf[64]{};
        std::snprintf(WholeEv, sizeof(WholeEv), "%+.2f", Instance.ExposureEv);
        std::snprintf(SubBuf, sizeof(SubBuf), "Contrast %.2f\xC3\x97 · saturation %.2f\xC3\x97 · vignette %.0f%%",
            Instance.ContrastGain, Instance.SaturationGain, Instance.VignetteStrength * 100.0f);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, WholeEv, "", "EV", SubBuf);
        Y += 6.0f;

        std::array<float, 28u> Curve{};
        const float Gain = std::pow(2.0f, Instance.ExposureEv);
        for (std::size_t I = 0u; I < Curve.size(); ++I)
        {
            const float X = (static_cast<float>(I) / 27.0f) * 3.0f * Gain;
            Curve[I] = std::clamp(std::pow(X / (1.0f + X), Instance.ContrastGain), 0.0f, 1.0f);
        }
        InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 84.0f }, Curve, ColorRgba{ 232u, 234u, 240u, 255u }, ColorRgba{ 188u, 198u, 218u, 36u }, "Toe", "Shoulder");
        Y += 96.0f;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7201u, "Exposure", "EV", "%.2f", -4.0f, 4.0f, Instance.ExposureEv, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const bool AaOn   = DisplaySettings ? DisplaySettings->AntiAliasingEnabled     : true;
        const bool ReflOn = DisplaySettings ? DisplaySettings->ReflectionsEnabled      : true;
        const bool AoOn   = DisplaySettings ? DisplaySettings->XeGtaoEnabled           : false;
        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Visible",       InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled && Instance.Enabled, false },
            { "Anti-aliasing", InspectorSwitchSymbol::PostFx,  AaOn,   false },
            { "Reflections",   InspectorSwitchSymbol::Stars,   ReflOn, false },
            { "XeGTAO",        InspectorSwitchSymbol::Orbit,   AoOn,   false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Instance.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1 && DisplaySettings) { DisplaySettings->AntiAliasingEnabled = !DisplaySettings->AntiAliasingEnabled; OutMutated = true; }
        else if (Clicked == 2 && DisplaySettings) { DisplaySettings->ReflectionsEnabled = !DisplaySettings->ReflectionsEnabled; OutMutated = true; }
        else if (Clicked == 3 && DisplaySettings) { DisplaySettings->XeGtaoEnabled = !DisplaySettings->XeGtaoEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "post-process", "grading" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S1 { LeftX, CursorY, InnerWidth, 344.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S1, "Exposure & tone curve");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        std::array<float, 28u> Curve{};
        const float Gain = std::pow(2.0f, Instance.ExposureEv);
        for (std::size_t I = 0u; I < Curve.size(); ++I)
        {
            const float X = (static_cast<float>(I) / 27.0f) * 3.0f * Gain;
            Curve[I] = std::clamp(std::pow(X / (1.0f + X), Instance.ContrastGain), 0.0f, 1.0f);
        }
        InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 74.0f }, Curve, ColorRgba{ 232u, 232u, 238u, 255u }, ColorRgba{ 188u, 198u, 218u, 36u }, "Toe", "Shoulder");
        Y += 86.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7201u, "Exposure", "EV", "%.2f", -4.0f, 4.0f, Instance.ExposureEv, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7202u, "Vignette", "", "%.2f", 0.0f, 1.0f, Instance.VignetteStrength, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7203u, "Chromatic shift", "", "%.2f", 0.0f, 1.0f, Instance.ChromaticShift, ActiveDragId, Input, OutMutated);
        CursorY = S1.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D S2 { LeftX, CursorY, InnerWidth, 212.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S2, "Colour grading");
        Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7204u, "Contrast", "x", "%.2f", 0.5f, 1.8f, Instance.ContrastGain, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7205u, "Saturation", "x", "%.2f", 0.0f, 2.0f, Instance.SaturationGain, ActiveDragId, Input, OutMutated);
        CursorY = S2.Bottom() + InspectorMetrics::SectionGap;
    }
    return CursorY;
}

} // namespace Frontier
