/*============================================================================================================================================
                                                         HEIGHTFOGINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance HeightFogInspector

#include "Engine/Editor/Inspectors/HeightFogInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float HeightFogInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, HeightFogInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Density", "Light" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Fog = Instance.HeightFog;
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 376.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Fog layer");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const float VisMeters = std::clamp(3.912f / std::max(0.0005f, Fog.Density), 10.0f, 9999.0f);
        char VisWhole[24]{}, SubBuf[64]{};
        std::snprintf(VisWhole, sizeof(VisWhole), "%.0f", VisMeters);
        std::snprintf(SubBuf, sizeof(SubBuf), "Visibility at datum · e-folding altitude %.0f m", Fog.FalloffHeightMeters);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, VisWhole, "", "m", SubBuf);
        Y += 6.0f;

        // Interactive altitude cross-section schematic with hills + exponential mist + falloff drag line
        const Rectangle2D Well { LeftX + PadX, Y, BodyW, 124.0f };
        Surface.FillRectangle(Well, ColorRgba{ 21u, 22u, 24u, 255u }, 10.0f);
        Surface.PushClip(Well);

        const float MaxAlt = 3000.0f;
        const float GroundY = Well.Bottom() - 18.0f;
        const float TopAltY = Well.Y + 14.0f;
        const float AltSpan = std::max(10.0f, GroundY - TopAltY);

        // Exponential mist density strata
        constexpr int BandCount = 16;
        for (int B = 0; B < BandCount; ++B)
        {
            const float T0 = static_cast<float>(B) / static_cast<float>(BandCount);
            const float T1 = static_cast<float>(B + 1) / static_cast<float>(BandCount);
            const float AltMid = (T0 + T1) * 0.5f * MaxAlt;
            const float ExpFactor = std::exp(-AltMid / std::max(10.0f, Fog.FalloffHeightMeters));
            const float DensBoost = std::clamp(Fog.Density * 18.0f, 0.15f, 1.0f);
            const uint8_t A = static_cast<uint8_t>(std::clamp(ExpFactor * DensBoost * 115.0f, 0.0f, 130.0f));
            const float YBot = GroundY - T0 * AltSpan;
            const float YTop = GroundY - T1 * AltSpan;
            Surface.FillRectangle({ Well.X, YTop, Well.Width, (YBot - YTop) + 1.0f }, ColorRgba{ 164u, 194u, 224u, A });
        }

        // Background & foreground dark hill silhouettes
        std::array<Point2D, 26u> HillPts{};
        for (std::size_t I = 0u; I < 24u; ++I)
        {
            const float U = static_cast<float>(I) / 23.0f;
            const float H = 10.0f + 22.0f * std::pow(std::sin(U * 3.14159f), 1.4f)
                          + 9.0f * std::sin(U * 11.2f + 0.8f) + 5.0f * std::cos(U * 19.0f);
            HillPts[I] = { Well.X + U * Well.Width, GroundY - std::max(2.0f, H) };
        }
        HillPts[24u] = { Well.Right(), Well.Bottom() };
        HillPts[25u] = { Well.X, Well.Bottom() };
        Surface.FillPolygon(HillPts, ColorRgba{ 14u, 16u, 20u, 230u });

        // Interactive falloff height dashed line + drag dot
        const float FalloffT = std::clamp(Fog.FalloffHeightMeters / MaxAlt, 0.03f, 0.96f);
        const float FalloffY = GroundY - FalloffT * AltSpan;
        for (float DX = Well.X + 8.0f; DX < Well.Right() - 8.0f; DX += 8.0f)
        {
            Surface.StrokeLine({ DX, FalloffY }, { std::min(Well.Right() - 8.0f, DX + 4.0f), FalloffY }, ColorRgba{ 196u, 222u, 248u, 190u }, 1.1f);
        }
        const Point2D HandlePt { Well.Right() - 26.0f, FalloffY };
        Surface.FillCircle(HandlePt, 4.5f, ColorRgba{ 226u, 238u, 250u, 255u });
        Surface.StrokeCircle(HandlePt, 4.5f, ColorRgba{ 21u, 22u, 24u, 255u }, 1.5f);

        const Point2D Ptr { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
        if (Input.PrimaryPointer.Down && Well.Contains(Ptr) && (ActiveDragId == 0u || ActiveDragId == 6190u))
        {
            if (Input.PrimaryPointer.Pressed) ActiveDragId = 6190u;
        }
        if (ActiveDragId == 6190u)
        {
            if (Input.PrimaryPointer.Down)
            {
                const float NewT = std::clamp((GroundY - Ptr.Y) / AltSpan, 0.02f, 1.0f);
                const float NewAlt = std::clamp(NewT * MaxAlt, 10.0f, 3000.0f);
                if (std::fabs(NewAlt - Fog.FalloffHeightMeters) > 0.5f)
                {
                    Fog.FalloffHeightMeters = NewAlt;
                    OutMutated = true;
                }
            }
            else
            {
                ActiveDragId = 0u;
            }
        }

        char FallLabel[48]{};
        std::snprintf(FallLabel, sizeof(FallLabel), "falloff %.0f m · density \xC3\xB7 e", Fog.FalloffHeightMeters);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Well.X + 10.0f, std::max(Well.Y + 6.0f, FalloffY - 14.0f) }, 8.6f, ColorRgba{ 212u, 228u, 244u, 245u }, FallLabel);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 8.0f, Well.Y + 6.0f }, 8.0f, InspectorMetrics::DimInk, "3 km");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 8.0f, Well.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, "0 m");
        Surface.PopClip();
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);

        Y = Well.Bottom() + 12.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6101u, "Density", "/m", "%.3f", 0.0f, 0.2f, Fog.Density, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6102u, "Falloff height", "m", "%.0f", 10.0f, 3000.0f, Fog.FalloffHeightMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Visible",     InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled, false },
            { "Follow wind", InspectorSwitchSymbol::Wind,    Fog.FollowWind,          false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Fog.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Fog.FollowWind = !Fog.FollowWind; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "fog", "height" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Height fog density");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6101u, "Density", "/m", "%.3f", 0.0f, 0.2f, Fog.Density, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6102u, "Falloff height", "m", "%.0f", 10.0f, 3000.0f, Fog.FalloffHeightMeters, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6103u, "Datum altitude", "m", "%.0f", -200.0f, 2000.0f, Fog.BaseAltitudeMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 214.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Height fog light");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6104u, "Sun scatter", "x", "%.2f", 0.0f, 2.0f, Fog.SunScatterIntensity, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 6110u, "Fog tint", Fog.HeightColour, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
