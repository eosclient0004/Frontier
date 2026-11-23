/*============================================================================================================================================
                                                         POINTLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance PointLightInspector with own retained radial falloff diagram

#include "Engine/Editor/Inspectors/PointLightInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;

inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}
} // namespace

void PointLightInspector::UpdateFalloffSurface(const PointLightData& Instance)
{
    uint64_t Sig = 0x1A010001ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.IntensityCandela);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.KelvinTemperature);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.ReachMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.DecayExponent);

    const uint32_t W = 240u, H = 152u;
    FalloffPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const float TR = Instance.RadiantTint.R * K.R;
        const float TG = Instance.RadiantTint.G * K.G;
        const float TB = Instance.RadiantTint.B * K.B;
        const float CX = static_cast<float>(W) * 0.5f;
        const float CY = static_cast<float>(H) * 0.5f;
        const float MaxR = 62.0f;
        const float Gain = std::clamp(Instance.IntensityCandela / 24.0f, 0.2f, 2.2f);
        const float Dec  = std::clamp(Instance.DecayExponent, 0.3f, 4.0f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float DY = static_cast<float>(Y) + 0.5f - CY;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX;
                const float D  = std::sqrt(DX * DX + DY * DY);
                float R = 0.012f, G = 0.013f, B = 0.015f;

                // Omnidirectional inverse-power falloff glow
                const float NormD = D / MaxR;
                const float Falloff = std::pow(1.0f / (1.0f + NormD * 2.8f), Dec * 0.95f) * Gain * 0.36f;
                R += TR * Falloff * 0.55f; G += TG * Falloff * 0.62f; B += TB * Falloff * 0.95f;

                // 4 concentric hairline reach rings
                for (int Ring = 1; Ring <= 4; ++Ring)
                {
                    const float RR = MaxR * (static_cast<float>(Ring) * 0.25f);
                    if (std::fabs(D - RR) < 0.65f)
                    {
                        R += 0.055f; G += 0.060f; B += 0.072f;
                    }
                }

                // Subtle 4-axis dotted crosshair
                if ((std::fabs(DX) < 0.6f || std::fabs(DY) < 0.6f) && D > 10.0f && D < MaxR && (static_cast<int>(D) % 6) < 2)
                {
                    R += 0.035f; G += 0.038f; B += 0.044f;
                }

                // Crisp central luminous bulb dot (matching HTML indigo/tint core)
                if (D < 5.5f)
                {
                    const float Core = 1.0f - D / 5.5f;
                    R += 0.42f + TR * Core * 0.65f;
                    G += 0.46f + TG * Core * 0.68f;
                    B += 0.92f + TB * Core * 0.95f;
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float PointLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, PointLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Source" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const float Lux5m = Instance.IntensityCandela / std::pow(5.0f, std::max(0.01f, Instance.DecayExponent));

    if (ActivePage == 0u)
    {
        UpdateFalloffSurface(Instance);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 342.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Radial falloff");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const long WholeCd = static_cast<long>(std::floor(Instance.IntensityCandela));
        const int  FracCd  = static_cast<int>(std::lround((Instance.IntensityCandela - static_cast<float>(WholeCd)) * 10.0f)) % 10;
        char WholeBuf[24]{}, FracBuf[8]{};
        std::snprintf(WholeBuf, sizeof(WholeBuf), "%ld", WholeCd);
        std::snprintf(FracBuf,  sizeof(FracBuf),  ".%d", FracCd);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, WholeBuf, FracBuf, "cd", "Authored luminous intensity");

        Surface.StrokeLine({ LeftX + PadX, Y + 4.0f }, { LeftX + PadX + BodyW, Y + 4.0f }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);
        Y += 14.0f;

        char CapBuf[64]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "Omnidirectional / decay %.1f", Instance.DecayExponent);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, Y }, BodyW, 10.5f, ColorRgba{ 204u, 208u, 214u, 255u }, CapBuf);
        Y += 20.0f;

        const Rectangle2D Stage { LeftX + PadX, Y, BodyW, 166.0f };
        FalloffPreview.Present(Surface, Stage, 10.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.X + 8.0f, Stage.Y + 8.0f }, 8.4f, ColorRgba{ 126u, 130u, 138u, 255u }, "OMNIDIRECTIONAL");
        char DecTag[24]{};
        std::snprintf(DecTag, sizeof(DecTag), "DECAY %.1f", Instance.DecayExponent);
        const float DTW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.4f, DecTag);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.Right() - 8.0f - DTW, Stage.Y + 8.0f }, 8.4f, ColorRgba{ 126u, 130u, 138u, 255u }, DecTag);

        // Reach guide labels along right radius (8 m, 15 m, 23 m, 30 m)
        char ReachLine[48]{};
        std::snprintf(ReachLine, sizeof(ReachLine), "%.0f m  %.0f m  %.0f m",
            Instance.ReachMeters * 0.25f, Instance.ReachMeters * 0.55f, Instance.ReachMeters);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Stage.Center().X + 16.0f, Stage.Center().Y - 5.0f }, 8.2f, ColorRgba{ 140u, 144u, 152u, 255u }, ReachLine);
        const char* GuideFoot = "Reach guides · free space";
        const float GFW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.6f, GuideFoot);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Stage.Center().X - GFW * 0.5f, Stage.Bottom() - 14.0f }, 8.6f, ColorRgba{ 126u, 130u, 138u, 255u }, GuideFoot);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Visible",      InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled, false },
            { "Show glow",    InspectorSwitchSymbol::Sun,     Instance.ShowGlow,       false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.ShowGlow = !Instance.ShowGlow; OutMutated = true; }

        static constexpr std::array<const char*, 1u> Tags = {{ "lighting" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S1 { LeftX, CursorY, InnerWidth, 354.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S1, "Source & colour");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char CdBuf[20]{}; std::snprintf(CdBuf, sizeof(CdBuf), "%.1f", Instance.IntensityCandela);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, CdBuf, "cd", "Authored luminous intensity");
        const float HalfW = (BodyW - 8.0f) * 0.5f;
        char LuxBuf[24]{}, ReachBuf[24]{};
        std::snprintf(LuxBuf, sizeof(LuxBuf), "%.2f lx", Lux5m);
        std::snprintf(ReachBuf, sizeof(ReachBuf), "%.0f m", Instance.ReachMeters);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX, Y, HalfW, 50.0f }, "AT 5 M · ESTIMATE", LuxBuf);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + HalfW + 8.0f, Y, HalfW, 50.0f }, "REACH", ReachBuf);
        Y += 58.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8110u, "Intensity", "cd", "%.1f", 0.0f, 60.0f, Instance.IntensityCandela, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8111u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8112u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S1.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D S2 { LeftX, CursorY, InnerWidth, 444.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S2, "Attenuation", "OPTICS");
        Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        std::array<float, 32u> Curve {};
        for (std::size_t I = 0u; I < Curve.size(); ++I)
        {
            const float Dist = 1.0f + (static_cast<float>(I) / 31.0f) * 9.0f;
            Curve[I] = std::clamp(1.0f / std::pow(Dist, std::max(0.05f, Instance.DecayExponent)), 0.0f, 1.0f);
        }
        InspectorPrimitives::ConstructCurvePlot(
            Surface, { LeftX + PadX, Y, BodyW, 74.0f }, Curve,
            ColorRgba{ 246u, 206u, 128u, 255u }, ColorRgba{ 246u, 206u, 128u, 32u }, "1 m", "10 m");
        Y += 84.0f;
        static constexpr std::array<float, 4u> SampleDists = {{ 1.0f, 2.0f, 5.0f, 10.0f }};
        for (float Dist : SampleDists)
        {
            const float Ratio = 1.0f / std::pow(Dist, std::max(0.05f, Instance.DecayExponent));
            char DLabel[16]{}, ValBuf[40]{};
            std::snprintf(DLabel, sizeof(DLabel), "%.0f m", Dist);
            std::snprintf(ValBuf, sizeof(ValBuf), "%.2f lx  ·  %.1f%%", Instance.IntensityCandela * Ratio, Ratio * 100.0f);
            Y = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, Y, BodyW, DLabel, ValBuf);
        }
        Y += 6.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8120u, "Reach", "m", "%.0f", 1.0f, 120.0f, Instance.ReachMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8121u, "Decay exponent", "", "%.2f", 0.0f, 4.0f, Instance.DecayExponent, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8122u, "Source radius", "m", "%.2f", 0.01f, 1.0f, Instance.SourceRadiusMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S2.Bottom() + InspectorMetrics::SectionGap;

        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8140u, ActiveDragId, Input, OutMutated);
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, false, "Show glow", Instance.ShowGlow, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
