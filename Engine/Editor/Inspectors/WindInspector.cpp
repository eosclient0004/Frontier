/*============================================================================================================================================
                                                             WINDINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance WindInspector (vector-summed atmospheric wind fields)

#include "Engine/Editor/Inspectors/WindInspector.h"

#include "Engine/Components/Controls/PolarDirectionDial.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
[[nodiscard]] const char* ResolveBeaufortLabel(float SpeedMs) noexcept
{
    if (SpeedMs < 0.3f)  return "Calm";
    if (SpeedMs < 1.6f)  return "Light air";
    if (SpeedMs < 3.4f)  return "Light breeze";
    if (SpeedMs < 5.5f)  return "Gentle breeze";
    if (SpeedMs < 8.0f)  return "Moderate breeze";
    if (SpeedMs < 10.8f) return "Fresh breeze";
    if (SpeedMs < 13.9f) return "Strong breeze";
    if (SpeedMs < 17.2f) return "Near gale";
    if (SpeedMs < 20.8f) return "Gale";
    return "Storm force";
}
} // namespace

float WindInspector::Construct(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    WindInstanceData&       Instance,
    const PaneInputReading& Input,
    bool&                   OutMutated,
    bool&                   OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Air" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Wind = Instance.Wind;
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Anemometer", "LAST 60 SECONDS");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const float MeanSpeed = std::max(0.0f, Wind.SpeedMetersPerSecond);
        const float GustAmp   = MeanSpeed * (0.20f + 0.55f * std::clamp(Wind.GustStrength, 0.0f, 1.0f));
        const float TurbAmp   = MeanSpeed * (0.08f + 0.28f * std::clamp(Wind.Turbulence,   0.0f, 1.0f));
        const float PeakGust  = MeanSpeed + GustAmp + TurbAmp * 0.45f;
        const float MinLull   = std::max(0.0f, MeanSpeed - GustAmp * 0.58f - TurbAmp * 0.35f);

        const long WholeSpd = static_cast<long>(std::floor(MeanSpeed));
        const int  FracSpd  = static_cast<int>(std::lround((MeanSpeed - static_cast<float>(WholeSpd)) * 10.0f)) % 10;
        char WholeBuf[16]{}, FracBuf[8]{}, SubBuf[64]{};
        std::snprintf(WholeBuf, sizeof(WholeBuf), "%ld", WholeSpd);
        std::snprintf(FracBuf,  sizeof(FracBuf),  ".%d", FracSpd);
        std::snprintf(SubBuf, sizeof(SubBuf), "%s · from %s (%.0f\xC2\xB0)",
            ResolveBeaufortLabel(MeanSpeed), PolarDirectionDial::ResolveCompassLabel(Wind.BearingDegrees), Wind.BearingDegrees);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, WholeBuf, FracBuf, "m/s", SubBuf);
        Y += 6.0f;

        // 60-second rolling anemometer trace inside dark #151618 plot well
        const Rectangle2D Plot { LeftX + PadX, Y, BodyW, 92.0f };
        Surface.FillRectangle(Plot, ColorRgba{ 21u, 22u, 24u, 255u }, 10.0f);
        Surface.StrokeRectangle(Plot, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);

        const float MaxScale = std::max(4.0f, PeakGust * 1.18f);
        const float PlotTop  = Plot.Y + 12.0f;
        const float PlotBot  = Plot.Bottom() - 20.0f;
        const float PlotH    = std::max(10.0f, PlotBot - PlotTop);
        const float PlotL    = Plot.X + 10.0f;
        const float PlotR    = Plot.Right() - 10.0f;
        const float PlotW    = std::max(10.0f, PlotR - PlotL);

        // Horizontal mean reference guide
        const float MeanY = PlotBot - (MeanSpeed / MaxScale) * PlotH;
        for (float DX = PlotL; DX < PlotR - 4.0f; DX += 8.0f)
        {
            Surface.StrokeLine({ DX, MeanY }, { std::min(PlotR, DX + 4.0f), MeanY }, ColorRgba{ 255u, 255u, 255u, 28u }, 1.0f);
        }

        // 60-second deterministic gust waveform with red peak accents above gust threshold
        constexpr std::size_t SampleCount = 60u;
        const float GustLine = MeanSpeed + GustAmp * 0.52f;
        std::array<Point2D, SampleCount> TracePts{};
        std::array<float, SampleCount>   TraceVals{};
        for (std::size_t I = 0u; I < SampleCount; ++I)
        {
            const float T = static_cast<float>(I) / static_cast<float>(SampleCount - 1u);
            const float Wave = std::sin(T * 11.4f + 0.6f) * 0.50f
                             + std::sin(T * 27.2f + 1.9f) * 0.28f
                             + std::cos(T * 43.0f + 0.3f) * 0.22f;
            const float GustBurst = std::exp(-std::pow((T - 0.34f) * 9.5f, 2.0f)) * 0.78f
                                  + std::exp(-std::pow((T - 0.74f) * 11.0f, 2.0f)) * 0.92f;
            const float Val = std::max(0.0f, MeanSpeed + Wave * (TurbAmp + GustAmp * 0.38f) + GustBurst * GustAmp);
            TraceVals[I] = Val;
            const float PX = PlotL + T * PlotW;
            const float PY = PlotBot - std::clamp(Val / MaxScale, 0.0f, 1.0f) * PlotH;
            TracePts[I] = { PX, PY };
        }
        for (std::size_t I = 1u; I < SampleCount; ++I)
        {
            const bool GustHot = (TraceVals[I] > GustLine) || (TraceVals[I - 1u] > GustLine);
            const ColorRgba SegCol = GustHot ? ColorRgba{ 238u, 96u, 88u, 240u } : ColorRgba{ 226u, 230u, 238u, 220u };
            Surface.StrokeLine(TracePts[I - 1u], TracePts[I], SegCol, GustHot ? 1.7f : 1.3f);
        }
        // Highlight current reading dot + gust crest dots
        Surface.FillCircle(TracePts.back(), 2.8f, ColorRgba{ 242u, 244u, 248u, 255u });
        for (std::size_t I = 2u; I + 2u < SampleCount; ++I)
        {
            if (TraceVals[I] > GustLine && TraceVals[I] >= TraceVals[I - 1u] && TraceVals[I] >= TraceVals[I + 1u])
            {
                Surface.FillCircle(TracePts[I], 2.5f, ColorRgba{ 242u, 102u, 92u, 255u });
            }
        }
        // Bottom time axis labels
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PlotL, Plot.Bottom() - 14.0f }, 8.5f, InspectorMetrics::DimInk, "-60s");
        const float Mid30W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, "-30s");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Plot.Center().X - Mid30W * 0.5f, Plot.Bottom() - 14.0f }, 8.5f, InspectorMetrics::DimInk, "-30s");
        const float NowW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, "now");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PlotR - NowW, Plot.Bottom() - 14.0f }, 8.5f, ColorRgba{ 196u, 200u, 208u, 255u }, "now");
        Y = Plot.Bottom() + 10.0f;

        // 2x2 stat tiles: MEAN / GUST / LULL / FROM
        const float Gap = 6.0f;
        const float TileW = (BodyW - Gap) * 0.5f;
        const float TileH = 44.0f;
        char MeanBuf[20]{}, GustBuf[20]{}, LullBuf[20]{}, FromBuf[28]{};
        std::snprintf(MeanBuf, sizeof(MeanBuf), "%.1f m/s", MeanSpeed);
        std::snprintf(GustBuf, sizeof(GustBuf), "%.1f m/s", PeakGust);
        std::snprintf(LullBuf, sizeof(LullBuf), "%.1f m/s", MinLull);
        std::snprintf(FromBuf, sizeof(FromBuf), "%s · %.0f\xC2\xB0", PolarDirectionDial::ResolveCompassLabel(Wind.BearingDegrees), Wind.BearingDegrees);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX,               Y,               TileW, TileH }, "MEAN", MeanBuf);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + TileW + Gap, Y,               TileW, TileH }, "GUST", GustBuf);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX,               Y + TileH + Gap, TileW, TileH }, "LULL", LullBuf);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + TileW + Gap, Y + TileH + Gap, TileW, TileH }, "FROM", FromBuf);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Visible",   InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled, false },
            { "Air shear", InspectorSwitchSymbol::Wind,    Wind.AirShearAdvection,  false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Wind.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Wind.AirShearAdvection = !Wind.AirShearAdvection; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "wind", "advection" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 408.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Wind vector descriptors");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6001u, "Wind speed", "m/s", "%.1f", 0.0f, 40.0f, Wind.SpeedMetersPerSecond, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6002u, "Bearing", "deg", "%.0f", 0.0f, 360.0f, Wind.BearingDegrees, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6003u, "Vertical shear", "/km", "%.2f", 0.0f, 2.0f, Wind.ShearPerKilometer, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6004u, "Ekman veer", "deg", "%.1f", -60.0f, 60.0f, Wind.VeerDegreesPerKm, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6005u, "Gust strength", "", "%.2f", 0.0f, 1.0f, Wind.GustStrength, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6006u, "Turbulence", "", "%.2f", 0.0f, 1.0f, Wind.Turbulence, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
