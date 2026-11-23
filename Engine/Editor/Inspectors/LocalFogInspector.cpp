/*============================================================================================================================================
                                                          LOCALFOGINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LocalFogInspector

#include "Engine/Editor/Inspectors/LocalFogInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float LocalFogInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LocalFogInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Medium", "Shape" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Fog = Instance.LocalFog;
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 368.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Visibility through fog", "LIVE AUTHORING PROBE");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const float ProbeDistM = std::clamp(Fog.HalfExtentMeters.X * 2.0f, 10.0f, 2000.0f);
        const float Sigma = std::max(0.0001f, Fog.Density * 0.0022f);
        const float TransPct = std::exp(-Sigma * ProbeDistM) * 100.0f;
        const long WholePct = static_cast<long>(std::floor(TransPct));
        const int  FracPct  = static_cast<int>(std::lround((TransPct - static_cast<float>(WholePct)) * 100.0f)) % 100;
        char WholeBuf[16]{}, FracBuf[8]{}, SubBuf[64]{};
        std::snprintf(WholeBuf, sizeof(WholeBuf), "%ld", WholePct);
        std::snprintf(FracBuf,  sizeof(FracBuf),  ".%02d", FracPct);
        std::snprintf(SubBuf, sizeof(SubBuf), "Beer-Lambert transmittance across %.0f m path", ProbeDistM);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, WholeBuf, FracBuf, "%", SubBuf);
        Y += 6.0f;

        // Beer-Lambert attenuation curve 0..2000 m with live probe line & dot
        const Rectangle2D Well { LeftX + PadX, Y, BodyW, 114.0f };
        Surface.FillRectangle(Well, ColorRgba{ 21u, 22u, 24u, 255u }, 10.0f);
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);

        const float PlotL = Well.X + 10.0f, PlotR = Well.Right() - 10.0f;
        const float PlotT = Well.Y + 14.0f, PlotB = Well.Bottom() - 20.0f;
        const float PlotW = std::max(10.0f, PlotR - PlotL);
        const float PlotH = std::max(10.0f, PlotB - PlotT);

        constexpr std::size_t SampleN = 32u;
        std::array<Point2D, SampleN> LinePts{};
        std::array<Point2D, SampleN + 2u> FillPts{};
        for (std::size_t I = 0u; I < SampleN; ++I)
        {
            const float U = static_cast<float>(I) / static_cast<float>(SampleN - 1u);
            const float Dist = U * 2000.0f;
            const float Val = std::exp(-Sigma * Dist);
            const Point2D Pt { PlotL + U * PlotW, PlotB - Val * PlotH };
            LinePts[I] = Pt;
            FillPts[I] = Pt;
        }
        FillPts[SampleN]      = { PlotR, PlotB };
        FillPts[SampleN + 1u] = { PlotL, PlotB };
        Surface.FillPolygon(FillPts, ColorRgba{ 138u, 178u, 224u, 36u });
        Surface.StrokePolyline(LinePts, ColorRgba{ 182u, 214u, 248u, 240u }, 1.6f);

        const float ProbeU = std::clamp(ProbeDistM / 2000.0f, 0.0f, 1.0f);
        const float ProbeX = PlotL + ProbeU * PlotW;
        const float ProbeY = PlotB - std::exp(-Sigma * ProbeDistM) * PlotH;
        for (float DY = PlotT; DY < PlotB; DY += 6.0f)
        {
            Surface.StrokeLine({ ProbeX, DY }, { ProbeX, std::min(PlotB, DY + 3.0f) }, ColorRgba{ 226u, 236u, 248u, 160u }, 1.0f);
        }
        Surface.FillCircle({ ProbeX, ProbeY }, 4.2f, ColorRgba{ 232u, 242u, 255u, 255u });
        Surface.StrokeCircle({ ProbeX, ProbeY }, 4.2f, ColorRgba{ 21u, 22u, 24u, 255u }, 1.4f);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PlotL, Well.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, "0 m");
        const float MidW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.0f, "1000 m");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.Center().X - MidW * 0.5f, Well.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, "1000 m");
        const float EndW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.0f, "2000 m");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PlotR - EndW, Well.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, "2000 m");

        Y = Well.Bottom() + 12.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6301u, "Density", "x", "%.2f", 0.0f, 4.0f, Fog.Density, ActiveDragId, Input, OutMutated);
        float PathMeters = ProbeDistM;
        bool PathChanged = false;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6303u, "Distance", "m", "%.0f", 10.0f, 2000.0f, PathMeters, ActiveDragId, Input, PathChanged);
        if (PathChanged) { Fog.HalfExtentMeters.X = PathMeters * 0.5f; OutMutated = true; }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Visible", InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Fog.Enabled = Entry.VisibilityEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "fog", "local-volume" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        // 1:1 HTML Medium tab: Medium sliders + FogBeamChamber volumetric beam scattering study
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 356.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Medium");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;

        const Rectangle2D Chamber { LeftX + PadX, Y, BodyW, 114.0f };
        Surface.FillRectangle(Chamber, ColorRgba{ 16u, 18u, 22u, 255u }, 10.0f);
        Surface.PushClip(Chamber);
        const uint8_t TR = static_cast<uint8_t>(std::clamp(Fog.FogTint.R * 255.0f, 55.0f, 255.0f));
        const uint8_t TG = static_cast<uint8_t>(std::clamp(Fog.FogTint.G * 255.0f, 55.0f, 255.0f));
        const uint8_t TB = static_cast<uint8_t>(std::clamp(Fog.FogTint.B * 255.0f, 55.0f, 255.0f));
        const Point2D EmitP { Chamber.X + 22.0f, Chamber.Y + 20.0f };
        const float ScatA = std::clamp(Fog.Density * 0.35f, 0.10f, 0.85f);
        for (int Layer = 0; Layer < 4; ++Layer)
        {
            const float Spread = 18.0f + static_cast<float>(Layer) * 18.0f;
            const std::array<Point2D, 3u> Cone = {{
                EmitP,
                { Chamber.Right() - 12.0f, Chamber.Bottom() - 8.0f - Spread * 0.4f },
                { Chamber.X + 48.0f + Spread * 1.4f, Chamber.Bottom() },
            }};
            Surface.FillPolygon(Cone, ColorRgba{ TR, TG, TB, static_cast<uint8_t>(ScatA * (42.0f - static_cast<float>(Layer) * 8.0f)) });
        }
        Surface.FillCircle(EmitP, 6.0f, ColorRgba{ 255u, 242u, 214u, 240u });
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Chamber.X + 10.0f, Chamber.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, "BEAM CHAMBER · IN-SCATTERING");
        Surface.PopClip();
        Surface.StrokeRectangle(Chamber, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
        Y = Chamber.Bottom() + 12.0f;

        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6301u, "Density", "x", "%.2f", 0.0f, 4.0f, Fog.Density, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6302u, "Edge softness", "", "%.2f", 0.0f, 1.0f, Fog.EdgeSoftness, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 6310u, "Volume tint", Fog.FogTint, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // 1:1 HTML Shape tab: FogShapePanel wireframe bounding box diagram + Half Extents X / Y / Z
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 374.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Volume shape");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;

        const Rectangle2D Well { LeftX + PadX, Y, BodyW, 114.0f };
        Surface.FillRectangle(Well, ColorRgba{ 19u, 20u, 24u, 255u }, 10.0f);
        Surface.PushClip(Well);
        const Point2D C = Well.Center();
        const float MaxE = std::max({ 1.0f, Fog.HalfExtentMeters.X, Fog.HalfExtentMeters.Y, Fog.HalfExtentMeters.Z });
        const float BX = (Fog.HalfExtentMeters.X / MaxE) * 58.0f;
        const float BY = (Fog.HalfExtentMeters.Y / MaxE) * 32.0f;
        const float OffX = 16.0f, OffY = -10.0f;
        const Rectangle2D Front { C.X - BX * 0.5f - OffX * 0.3f, C.Y - BY * 0.5f + 6.0f, BX, BY };
        const Rectangle2D Back  { Front.X + OffX, Front.Y + OffY, BX, BY };
        Surface.FillRectangle(Front, ColorRgba{ 156u, 188u, 218u, 36u }, 2.0f);
        Surface.StrokeRectangle(Back,  ColorRgba{ 156u, 188u, 218u, 96u }, 0.0f, 1.0f);
        Surface.StrokeRectangle(Front, ColorRgba{ 196u, 220u, 246u, 210u }, 0.0f, 1.3f);
        Surface.StrokeLine({ Front.X, Front.Y }, { Back.X, Back.Y }, ColorRgba{ 156u, 188u, 218u, 130u }, 1.0f);
        Surface.StrokeLine({ Front.Right(), Front.Y }, { Back.Right(), Back.Y }, ColorRgba{ 156u, 188u, 218u, 130u }, 1.0f);
        Surface.StrokeLine({ Front.X, Front.Bottom() }, { Back.X, Back.Bottom() }, ColorRgba{ 156u, 188u, 218u, 130u }, 1.0f);
        Surface.StrokeLine({ Front.Right(), Front.Bottom() }, { Back.Right(), Back.Bottom() }, ColorRgba{ 156u, 188u, 218u, 130u }, 1.0f);
        char DimBuf[48]{};
        std::snprintf(DimBuf, sizeof(DimBuf), "%.0f \xC3\x97 %.0f \xC3\x97 %.0f m", Fog.HalfExtentMeters.X * 2.0f, Fog.HalfExtentMeters.Y * 2.0f, Fog.HalfExtentMeters.Z * 2.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Well.X + 10.0f, Well.Bottom() - 14.0f }, 8.2f, InspectorMetrics::DimInk, DimBuf);
        Surface.PopClip();
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
        Y = Well.Bottom() + 12.0f;

        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6303u, "Half extent X", "m", "%.1f", 1.0f, 400.0f, Fog.HalfExtentMeters.X, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6304u, "Half extent Y", "m", "%.1f", 1.0f, 200.0f, Fog.HalfExtentMeters.Y, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6305u, "Half extent Z", "m", "%.1f", 1.0f, 400.0f, Fog.HalfExtentMeters.Z, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
