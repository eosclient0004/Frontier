/*============================================================================================================================================
                                                       ATMOSPHERICFOGINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance AtmosphericFogInspector

#include "Engine/Editor/Inspectors/AtmosphericFogInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float AtmosphericFogInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, AtmosphericFogInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Spectrum" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Fog = Instance.AerialFog;
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 376.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Aerial perspective");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const float AerialRangeKm = std::clamp(4.0f / std::max(0.15f, Fog.DensityMultiplier), 0.5f, 32.0f);
        const long WholeKm = static_cast<long>(std::floor(AerialRangeKm));
        const int  FracKm  = static_cast<int>(std::lround((AerialRangeKm - static_cast<float>(WholeKm)) * 10.0f)) % 10;
        char WholeBuf[16]{}, FracBuf[8]{}, SubBuf[64]{};
        std::snprintf(WholeBuf, sizeof(WholeBuf), "%ld", WholeKm);
        std::snprintf(FracBuf,  sizeof(FracBuf),  ".%d", FracKm);
        std::snprintf(SubBuf, sizeof(SubBuf), "Onset %.0f m · scale height %.0f m", Fog.StartDistanceMeters, Fog.ScaleHeightMeters);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, WholeBuf, FracBuf, "km", SubBuf);
        Y += 6.0f;

        // 5-ridge mountain depth profile inside dark #151618 well
        const Rectangle2D Well { LeftX + PadX, Y, BodyW, 124.0f };
        Surface.FillRectangle(Well, ColorRgba{ 21u, 22u, 24u, 255u }, 10.0f);
        Surface.PushClip(Well);

        // Subtle atmospheric horizon sky gradient
        for (int Row = 0; Row < 8; ++Row)
        {
            const float T = static_cast<float>(Row) / 7.0f;
            const uint8_t A = static_cast<uint8_t>(std::clamp(18.0f + T * 40.0f * std::clamp(Fog.DensityMultiplier, 0.2f, 2.0f), 8.0f, 72.0f));
            Surface.FillRectangle({ Well.X, Well.Y + T * Well.Height * 0.58f, Well.Width, Well.Height * 0.10f }, ColorRgba{ 118u, 152u, 198u, A });
        }

        // 5 receding mountain ridges from far (back) to near (front)
        for (int Ridge = 4; Ridge >= 0; --Ridge)
        {
            const float DepthFrac = static_cast<float>(Ridge) / 4.0f; // 1.0 = farthest
            const float DistM = 1200.0f + DepthFrac * 10800.0f;
            const float EffM  = std::max(0.0f, DistM - Fog.StartDistanceMeters);
            const float Haze  = 1.0f - std::exp(-EffM * 0.00022f * std::max(0.05f, Fog.DensityMultiplier));
            const uint8_t R = static_cast<uint8_t>(std::clamp(18.0f + Haze * 124.0f, 0.0f, 255.0f));
            const uint8_t G = static_cast<uint8_t>(std::clamp(22.0f + Haze * 144.0f, 0.0f, 255.0f));
            const uint8_t B = static_cast<uint8_t>(std::clamp(28.0f + Haze * 176.0f, 0.0f, 255.0f));

            std::array<Point2D, 26u> Pts{};
            const float BaseY = Well.Bottom() - 16.0f - DepthFrac * 34.0f;
            const float Phase = static_cast<float>(Ridge) * 1.45f;
            for (std::size_t I = 0u; I < 24u; ++I)
            {
                const float U = static_cast<float>(I) / 23.0f;
                const float Peak = 16.0f * (0.45f + 0.55f * DepthFrac)
                                 * (0.55f + 0.45f * std::sin(U * 6.8f + Phase) + 0.25f * std::cos(U * 14.5f - Phase));
                Pts[I] = { Well.X + U * Well.Width, BaseY - std::max(2.0f, Peak) };
            }
            Pts[24u] = { Well.Right(), Well.Bottom() };
            Pts[25u] = { Well.X, Well.Bottom() };
            Surface.FillPolygon(Pts, ColorRgba{ R, G, B, 240u });
        }

        // Vertical dashed onset distance marker (interactive drag)
        const float StartT = std::clamp(Fog.StartDistanceMeters / 2000.0f, 0.05f, 0.92f);
        const float StartX = Well.X + 14.0f + StartT * (Well.Width - 28.0f);
        for (float DY = Well.Y + 12.0f; DY < Well.Bottom() - 18.0f; DY += 7.0f)
        {
            Surface.StrokeLine({ StartX, DY }, { StartX, std::min(Well.Bottom() - 18.0f, DY + 3.5f) }, ColorRgba{ 182u, 212u, 248u, 200u }, 1.1f);
        }
        Surface.FillCircle({ StartX, Well.Y + 26.0f }, 4.0f, ColorRgba{ 216u, 232u, 252u, 255u });

        const float Trans5k = std::exp(-std::max(0.0f, 5000.0f - Fog.StartDistanceMeters) * 0.00022f * std::max(0.05f, Fog.DensityMultiplier)) * 100.0f;
        char TransBuf[32]{};
        std::snprintf(TransBuf, sizeof(TransBuf), "T(5 km) %.0f%%", Trans5k);
        const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.4f, TransBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Well.Right() - 8.0f - TW, Well.Y + 7.0f }, 8.4f, ColorRgba{ 194u, 216u, 242u, 235u }, TransBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { StartX + 6.0f, Well.Y + 19.0f }, 8.2f, ColorRgba{ 208u, 224u, 246u, 235u }, "Start");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 8.0f, Well.Bottom() - 13.0f }, 8.0f, InspectorMetrics::DimInk, "0 km");
        const float FarW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.0f, "12 km");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.Right() - 8.0f - FarW, Well.Bottom() - 13.0f }, 8.0f, InspectorMetrics::DimInk, "12 km");
        Surface.PopClip();
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);

        Y = Well.Bottom() + 12.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6201u, "Density", "x", "%.2f", 0.0f, 4.0f, Fog.DensityMultiplier, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6202u, "Start", "m", "%.0f", 0.0f, 2000.0f, Fog.StartDistanceMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Visible",     InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled, false },
            { "Follow wind", InspectorSwitchSymbol::Wind,    Fog.FollowWind,          false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Fog.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Fog.FollowWind = !Fog.FollowWind; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "fog", "aerial" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        // 1:1 HTML Spectrum tab: Spectral transmission (aerosol % KPI + RGB Rayleigh-vs-Mie spectral curve + Mie Blend)
        const float Mie = std::clamp(Fog.MieBlend, 0.0f, 1.0f);
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 368.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Spectral transmission");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        char AerBuf[16]{};
        std::snprintf(AerBuf, sizeof(AerBuf), "%.0f", Mie * 100.0f);
        const char* HazeDesc = (Mie < 0.35f) ? "Blue, clean-air haze" : (Mie > 0.65f ? "Grey-white, dusty haze" : "Mixed Rayleigh & aerosol");
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, AerBuf, "", "% aerosol", HazeDesc);
        Y += 6.0f;

        // Dark #151618 RGB spectral transmission well (400..700 nm, R/G/B curves converging as Mie -> 1)
        const Rectangle2D Well { LeftX + PadX, Y, BodyW, 128.0f };
        Surface.FillRectangle(Well, ColorRgba{ 21u, 22u, 24u, 255u }, 10.0f);
        Surface.PushClip(Well);
        const float PadL = 12.0f, PadR = 12.0f, PadT = 16.0f, PadB = 22.0f;
        const float PW = Well.Width - PadL - PadR, PH = Well.Height - PadT - PadB;
        const float ExtScale = std::clamp(Fog.DensityMultiplier * 0.45f, 0.12f, 2.0f);
        const std::array<float, 3u> LambdaPow = {{ 0.42f, 0.72f, 1.25f }}; // R, G, B relative Rayleigh extinction
        const std::array<ColorRgba, 3u> BandCol = {{
            { 238u, 114u, 108u, 235u },
            { 138u, 214u, 146u, 235u },
            { 112u, 178u, 248u, 235u },
        }};
        for (std::size_t C = 0u; C < 3u; ++C)
        {
            const float K = LambdaPow[C] * (1.0f - Mie) + 0.80f * Mie;
            std::array<Point2D, 24u> Pts{};
            for (std::size_t I = 0u; I < 24u; ++I)
            {
                const float U = static_cast<float>(I) / 23.0f;
                const float Trans = std::exp(-U * 2.2f * ExtScale * K);
                Pts[I] = { Well.X + PadL + U * PW, Well.Bottom() - PadB - Trans * PH };
            }
            Surface.StrokePolyline(Pts, BandCol[C], 1.7f, false);
        }
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Well.X + PadL, Well.Y + 6.0f }, 8.0f, ColorRgba{ 238u, 114u, 108u, 220u }, "R 650nm");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Well.X + PadL + 52.0f, Well.Y + 6.0f }, 8.0f, ColorRgba{ 138u, 214u, 146u, 220u }, "G 550nm");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Well.X + PadL + 104.0f, Well.Y + 6.0f }, 8.0f, ColorRgba{ 112u, 178u, 248u, 220u }, "B 450nm");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + PadL, Well.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, "0 km");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.Right() - 36.0f, Well.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, "12 km");
        Surface.PopClip();
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
        Y = Well.Bottom() + 12.0f;

        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6204u, "Mie blend", "", "%.2f", 0.0f, 1.0f, Fog.MieBlend, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6203u, "Scale height", "m", "%.0f", 200.0f, 8000.0f, Fog.ScaleHeightMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
