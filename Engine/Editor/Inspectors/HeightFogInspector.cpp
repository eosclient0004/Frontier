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

void HeightFogInspector::UpdateScatterSurface(const HeightFogInstanceData& Instance)
{
    const auto& Fog = Instance.HeightFog;
    uint64_t Key = 0xF0611671ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Fog.SunScatterIntensity);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Fog.HeightColour.R);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Fog.HeightColour.G);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Fog.HeightColour.B);

    const uint32_t W = 240u, H = 118u;
    ScatterPreview.EnsureUpdated(Key, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float FR = std::clamp(Fog.HeightColour.R, 0.14f, 1.0f);
        const float FG = std::clamp(Fog.HeightColour.G, 0.14f, 1.0f);
        const float FB = std::clamp(Fog.HeightColour.B, 0.14f, 1.0f);
        const float GlowGain = std::clamp(Fog.SunScatterIntensity * 0.72f, 0.12f, 1.25f);
        const float SunX = static_cast<float>(W) * 0.50f;
        const float SunY = static_cast<float>(H) * 0.42f;
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float V = static_cast<float>(Y) / static_cast<float>(H);
            const float MistT = std::clamp((V - 0.34f) / 0.66f, 0.0f, 1.0f);
            const float MistA = MistT * MistT * 0.46f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) - SunX;
                const float DY = static_cast<float>(Y) - SunY;
                const float Dist = std::sqrt(DX * DX + DY * DY);
                // Smooth dark twilight sky -> horizon mist blend
                float R = 0.080f * (1.0f - MistA) + FR * MistA;
                float G = 0.086f * (1.0f - MistA) + FG * MistA;
                float B = 0.104f * (1.0f - MistA) + FB * MistA;
                // Smooth Henyey-Greenstein Mie forward scatter aureole + crisp solar disc
                const float Aureole   = std::exp(-Dist * 0.036f) * GlowGain * 0.48f;
                const float SolarGlow = std::exp(-Dist * Dist * 0.012f) * (0.45f + GlowGain * 0.45f);
                const float Disc      = std::clamp((8.5f - Dist) * 0.7f, 0.0f, 1.0f);
                R += Aureole * 1.00f + SolarGlow * 1.00f + Disc * 0.42f;
                G += Aureole * 0.86f + SolarGlow * 0.92f + Disc * 0.38f;
                B += Aureole * 0.66f + SolarGlow * 0.78f + Disc * 0.32f;
                const auto To8 = [](float Val) noexcept -> uint8_t
                {
                    return static_cast<uint8_t>(std::clamp(std::lround(Val * 255.0f), 0l, 255l));
                };
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = To8(R); Pixels[Off + 1u] = To8(G); Pixels[Off + 2u] = To8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

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
        // 1:1 HTML Density tab: Density by altitude (vertical exponential profile + 3 visibility/transmission readouts)
        const float SafeDensity = std::max(0.0001f, Fog.Density);
        const float FalloffM    = std::max(10.0f, Fog.FalloffHeightMeters);
        const float VisGround   = std::min(99999.0f, 3.0f / SafeDensity);
        const float VisFalloff  = std::min(99999.0f, 3.0f / (SafeDensity * std::exp(-1.0f)));
        const float ZenithTrans = std::exp(-SafeDensity * FalloffM) * 100.0f;

        const Rectangle2D S { LeftX, CursorY, InnerWidth, 356.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Density by altitude");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;

        // Exponential altitude profile well (vertical axis = altitude 0..3 km, horizontal = density)
        const Rectangle2D Well { LeftX + PadX, Y, BodyW, 132.0f };
        Surface.FillRectangle(Well, ColorRgba{ 21u, 22u, 24u, 255u }, 10.0f);
        Surface.PushClip(Well);
        const float PadL = 12.0f, PadR = 12.0f, PadT = 14.0f, PadB = 20.0f;
        const float PlotW = Well.Width - PadL - PadR, PlotH = Well.Height - PadT - PadB;
        const float MaxAlt = std::max(600.0f, FalloffM * 3.0f);
        std::array<Point2D, 28u> CurvePts{};
        for (std::size_t I = 0u; I < CurvePts.size(); ++I)
        {
            const float T = static_cast<float>(I) / 27.0f;
            const float Alt = T * MaxAlt;
            const float RelD = std::exp(-Alt / FalloffM);
            const float PX = Well.X + PadL + RelD * PlotW * 0.88f;
            const float PY = Well.Bottom() - PadB - T * PlotH;
            CurvePts[I] = { PX, PY };
            Surface.StrokeLine({ Well.X + PadL, PY }, { PX, PY }, ColorRgba{ 168u, 198u, 226u, static_cast<uint8_t>(26.0f + RelD * 44.0f) }, 3.5f);
        }
        Surface.StrokePolyline(CurvePts, ColorRgba{ 196u, 220u, 244u, 235u }, 1.8f, false);
        const float FallY = Well.Bottom() - PadB - std::clamp(FalloffM / MaxAlt, 0.08f, 0.92f) * PlotH;
        for (float DX = Well.X + PadL; DX < Well.Right() - PadR; DX += 7.0f)
        {
            Surface.StrokeLine({ DX, FallY }, { std::min(Well.Right() - PadR, DX + 3.5f), FallY }, ColorRgba{ 232u, 186u, 118u, 185u }, 1.0f);
        }
        char FallTag[32]{};
        std::snprintf(FallTag, sizeof(FallTag), "1/e at %.0f m", FalloffM);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Well.Right() - 88.0f, FallY - 12.0f }, 8.2f, ColorRgba{ 232u, 186u, 118u, 240u }, FallTag);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + PadL, Well.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, "0 m ground");
        Surface.PopClip();
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
        Y = Well.Bottom() + 12.0f;

        auto FmtVis = [](float M, char* Out, std::size_t N)
        {
            if (M >= 1000.0f) std::snprintf(Out, N, "%.1f km", M * 0.001f);
            else              std::snprintf(Out, N, "%.0f m", M);
        };
        char V0[24]{}, VF[24]{}, FLabel[32]{}, ZVal[28]{};
        FmtVis(VisGround, V0, sizeof(V0));
        FmtVis(VisFalloff, VF, sizeof(VF));
        std::snprintf(FLabel, sizeof(FLabel), "Visibility at %.0f m", FalloffM);
        std::snprintf(ZVal, sizeof(ZVal), "%.0f%% of the light", std::clamp(ZenithTrans, 0.0f, 100.0f));
        Y = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Visibility at 0 m", V0);
        Y = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, FLabel, VF);
        Y = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, "Looking straight up", ZVal);
        Y += 6.0f;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6103u, "Datum altitude", "m", "%.0f", -200.0f, 2000.0f, Fog.BaseAltitudeMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // 1:1 HTML Light tab: Light in the fog (Sun Scatter KPI + smooth per-pixel Mie sun-in-fog horizon study + sliders)
        UpdateScatterSurface(Instance);
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 356.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Light in the fog");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;
        char ScatW[16]{};
        std::snprintf(ScatW, sizeof(ScatW), "%.2f", Fog.SunScatterIntensity);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, ScatW, "", "\xC3\x97", "Forward glow around the sun");
        Y += 6.0f;

        const Rectangle2D Well { LeftX + PadX, Y, BodyW, 118.0f };
        ScatterPreview.Present(Surface, Well, 10.0f);
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
        Y = Well.Bottom() + 12.0f;

        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6104u, "Sun scatter", "x", "%.2f", 0.0f, 2.0f, Fog.SunScatterIntensity, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 6110u, "Colour", Fog.HeightColour, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
