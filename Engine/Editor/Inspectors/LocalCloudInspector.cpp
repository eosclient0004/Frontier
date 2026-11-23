/*============================================================================================================================================
                                                         LOCALCLOUDINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LocalCloudInspector (bounded cloud volume extents, density & edge softness)

#include "Engine/Editor/Inspectors/LocalCloudInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float LocalCloudInspector::Construct(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    LocalCloudInstanceData& Instance,
    const PaneInputReading& Input,
    bool&                   OutMutated,
    bool&                   OutPageSwitched)
{
    static constexpr std::array<const char*, 4u> Pages = {{ "Overview", "Coverage", "Layer", "Body" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Vol = Instance.Volume;
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        // 1:1 HTML Local cloud blueprint schematic + integrated 2x2 stat tiles
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 336.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud volume", "LOCAL VOLUME · SCHEMATIC");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const Rectangle2D Swath { LeftX + PadX, Y, BodyW, 178.0f };
        Surface.FillRectangle(Swath, ColorRgba{ 16u, 22u, 32u, 255u }, 12.0f);
        Surface.PushClip(Swath);

        for (int G = 1; G <= 5; ++G)
        {
            const float GX = Swath.X + Swath.Width * (static_cast<float>(G) / 6.0f);
            Surface.StrokeLine({ GX, Swath.Y }, { GX, Swath.Bottom() }, ColorRgba{ 118u, 166u, 218u, 18u }, 1.0f);
        }
        for (int G = 1; G <= 3; ++G)
        {
            const float GY = Swath.Y + Swath.Height * (static_cast<float>(G) / 4.0f);
            Surface.StrokeLine({ Swath.X, GY }, { Swath.Right(), GY }, ColorRgba{ 118u, 166u, 218u, 18u }, 1.0f);
        }

        // Bounded footprint box proportional to HalfExtentMeters X/Y
        const float Aspect = std::clamp(Vol.HalfExtentMeters.X / std::max(1.0f, Vol.HalfExtentMeters.Y), 0.45f, 2.2f);
        const float BoxW = (Aspect >= 1.0f) ? 116.0f : (116.0f * Aspect);
        const float BoxH = (Aspect >= 1.0f) ? (116.0f / Aspect) : 84.0f;
        const Rectangle2D Bounds { Swath.Center().X - BoxW * 0.5f, Swath.Center().Y - BoxH * 0.5f - 8.0f, BoxW, std::min(BoxH, 92.0f) };
        Surface.StrokeRectangle(Bounds, ColorRgba{ 152u, 202u, 252u, 130u }, 10.0f, 1.2f);

        // Soft cloud puffs clipped to bounded volume
        const int CellCount = static_cast<int>(std::clamp(Vol.CloudFraction * 18.0f, 2.0f, 18.0f));
        const uint8_t CloudA = static_cast<uint8_t>(std::clamp(38.0f + Vol.ExtinctionMultiplier * 32.0f, 30.0f, 130.0f));
        for (int C = 0; C < CellCount; ++C)
        {
            const float HX = std::fabs(std::fmod(std::sin(static_cast<float>(C + 5) * 31.17f) * 29183.5f, 1.0f));
            const float HY = std::fabs(std::fmod(std::sin(static_cast<float>(C + 2) * 17.83f) * 18273.3f, 1.0f));
            const float Rad = 12.0f + 12.0f * (1.0f - Vol.EdgeSoftness * 0.35f);
            const Point2D Pt { Bounds.X + 14.0f + HX * std::max(4.0f, Bounds.Width - 28.0f), Bounds.Y + 14.0f + HY * std::max(4.0f, Bounds.Height - 28.0f) };
            Surface.FillCircle(Pt, Rad, ColorRgba{ 214u, 232u, 250u, static_cast<uint8_t>(CloudA / 2u) });
            Surface.FillCircle(Pt, Rad * 0.6f, ColorRgba{ 236u, 244u, 255u, CloudA });
        }

        char DimTop[40]{}, BaseBuf[24]{};
        std::snprintf(DimTop, sizeof(DimTop), "%.0f \xC3\x97 %.0f m", Vol.HalfExtentMeters.X * 2.0f, Vol.HalfExtentMeters.Y * 2.0f);
        std::snprintf(BaseBuf, sizeof(BaseBuf), "ALT %.0f M", Vol.VolumeCenterMeters.Z);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Swath.X + 10.0f, Swath.Y + 8.0f }, 8.6f, ColorRgba{ 162u, 206u, 248u, 230u }, DimTop);

        const int Oktas = static_cast<int>(std::clamp(std::lround(Vol.CloudFraction * 8.0f), 0l, 8l));
        const char* SkyClass = (Oktas <= 2) ? "Sparse" : (Oktas <= 5) ? "Scattered" : "Dense";
        char SubBuf[40]{};
        std::snprintf(SubBuf, sizeof(SubBuf), "%.0f%% fill · softness %.2f", Vol.CloudFraction * 100.0f, Vol.EdgeSoftness);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { Swath.X + 12.0f, Swath.Bottom() - 34.0f }, 13.0f, ColorRgba{ 238u, 244u, 252u, 255u }, SkyClass);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,  { Swath.X + 12.0f, Swath.Bottom() - 18.0f }, 9.2f, ColorRgba{ 156u, 186u, 216u, 235u }, SubBuf);
        const float BaseW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.6f, BaseBuf);
        const Rectangle2D BaseTag { Swath.Right() - 12.0f - BaseW - 12.0f, Swath.Bottom() - 24.0f, BaseW + 12.0f, 16.0f };
        Surface.FillRectangle(BaseTag, ColorRgba{ 26u, 42u, 62u, 215u }, 8.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { BaseTag.X + 6.0f, BaseTag.Y + 3.0f }, 8.6f, ColorRgba{ 196u, 224u, 252u, 255u }, BaseBuf);
        Surface.PopClip();
        Surface.StrokeRectangle(Swath, ColorRgba{ 138u, 184u, 232u, 36u }, 12.0f, 1.0f);

        Y = Swath.Bottom() + 10.0f;
        const float Gap = 6.0f;
        const float TileW = (BodyW - Gap) * 0.5f;
        const float TileH = 44.0f;
        char Cov[16]{}, Opt[16]{}, Alt[16]{}, Soft[16]{};
        std::snprintf(Cov,  sizeof(Cov),  "%.0f%%", Vol.CloudFraction * 100.0f);
        std::snprintf(Opt,  sizeof(Opt),  "%.1fx",  Vol.ExtinctionMultiplier);
        std::snprintf(Alt,  sizeof(Alt),  "%.0f m", Vol.VolumeCenterMeters.Z);
        std::snprintf(Soft, sizeof(Soft), "%.2f",   Vol.EdgeSoftness);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX,               Y,               TileW, TileH }, "FILL",     Cov);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + TileW + Gap, Y,               TileW, TileH }, "OPTICAL",  Opt);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX,               Y + TileH + Gap, TileW, TileH }, "ALTITUDE", Alt);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + TileW + Gap, Y + TileH + Gap, TileW, TileH }, "SOFTNESS", Soft);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Visible", InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "local-cloud", "volume" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 210.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Volume fill");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Pct[16]{}; std::snprintf(Pct, sizeof(Pct), "%.0f", Vol.CloudFraction * 100.0f);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Pct, "%", "Bounded cloud fill fraction");
        float PctVal = Vol.CloudFraction * 100.0f; bool Changed = false;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y + 4.0f, BodyW, 3601u, "Fill", "%", "%.0f", 0.0f, 100.0f, PctVal, ActiveDragId, Input, Changed);
        if (Changed) { Vol.CloudFraction = PctVal * 0.01f; OutMutated = true; }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Volume half-extents");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3610u, "Extent X", "m", "%.0f", 10.0f, 4000.0f, Vol.HalfExtentMeters.X, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3611u, "Extent Y", "m", "%.0f", 10.0f, 4000.0f, Vol.HalfExtentMeters.Y, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3612u, "Extent Z", "m", "%.0f", 10.0f, 2000.0f, Vol.HalfExtentMeters.Z, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 212.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Volume density");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3620u, "Extinction", "x", "%.2f", 0.0f, 4.0f, Vol.ExtinctionMultiplier, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3621u, "Edge softness", "", "%.2f", 0.0f, 1.0f, Vol.EdgeSoftness, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
