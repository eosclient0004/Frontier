/*============================================================================================================================================
                                                         SPOTLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance SpotLightInspector with own retained beam-envelope diagram

#include "Engine/Editor/Inspectors/SpotLightInspector.h"

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

void SpotLightInspector::UpdateEnvelopeSurface(const SpotLightData& Instance)
{
    uint64_t Sig = 0x1B010001ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.IntensityCandela);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.KelvinTemperature);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.FullConeDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Penumbra);

    const uint32_t W = 240u, H = 152u;
    EnvelopePreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const float TR = Instance.RadiantTint.R * K.R;
        const float TG = Instance.RadiantTint.G * K.G;
        const float TB = Instance.RadiantTint.B * K.B;
        const float CX = static_cast<float>(W) * 0.5f;
        const float ApexY = 30.0f;
        const float ConeH = 108.0f;
        const float HalfBottom = std::clamp(std::tan(Instance.FullConeDegrees * PiF / 360.0f) * ConeH * 1.45f, 12.0f, 96.0f);
        const float InnerScale = std::clamp(1.0f - Instance.Penumbra, 0.05f, 1.0f);
        const float Gain = std::clamp(Instance.IntensityCandela / 90.0f, 0.25f, 2.0f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX;
                const float AX = std::fabs(DX);
                float R = 0.017f, G = 0.018f, B = 0.021f;

                // Subtle apex glow / fixture head at top
                const float ApexDist = std::sqrt(DX * DX + (FY - ApexY) * (FY - ApexY));
                if (ApexDist < 10.0f)
                {
                    const float Glow = std::exp(-ApexDist * ApexDist / 26.0f);
                    R += TR * Glow * 0.85f + Glow * 0.25f;
                    G += TG * Glow * 0.85f + Glow * 0.25f;
                    B += TB * Glow * 0.85f + Glow * 0.25f;
                }

                // Beam envelope cone
                if (FY > ApexY && FY <= ApexY + ConeH)
                {
                    const float T = (FY - ApexY) / ConeH;
                    const float OuterW = std::max(2.0f, HalfBottom * T);
                    const float InnerW = OuterW * InnerScale;
                    if (AX <= OuterW)
                    {
                        float Soft = 1.0f;
                        if (AX > InnerW && OuterW > InnerW)
                        {
                            const float U = (AX - InnerW) / (OuterW - InnerW);
                            Soft = (1.0f - U) * (1.0f - U);
                        }
                        const float DepthFade = (0.14f + 0.50f * (1.0f - 0.55f * T)) * Gain * 0.42f;
                        R += TR * DepthFade * Soft;
                        G += TG * DepthFade * Soft;
                        B += TB * DepthFade * Soft;

                        // Outer solid cone edge
                        if (std::fabs(AX - OuterW) < 1.0f)
                        {
                            R += TR * 0.28f; G += TG * 0.28f; B += TB * 0.28f;
                        }
                        // Inner dashed penumbra edge
                        const bool Dash = (static_cast<int>(FY) % 7) < 4;
                        if (Dash && std::fabs(AX - InnerW) < 0.95f && Instance.Penumbra > 0.02f)
                        {
                            R += TR * 0.42f; G += TG * 0.42f; B += TB * 0.42f;
                        }
                    }
                    // Center optical axis dashed line
                    if (AX < 0.65f && (static_cast<int>(FY) % 8) < 4)
                    {
                        R += 0.09f; G += 0.09f; B += 0.10f;
                    }
                }

                // Illuminated floor pool ellipse at bottom of cone
                const float Ey = (FY - (ApexY + ConeH)) / 7.5f;
                const float Ex = DX / std::max(10.0f, HalfBottom);
                const float Er2 = Ex * Ex + Ey * Ey;
                if (Er2 < 1.25f)
                {
                    const float Pool = std::exp(-Er2 * 2.2f) * 0.16f * Gain;
                    R += TR * Pool; G += TG * Pool; B += TB * Pool;
                    const float Ring = std::fabs(std::sqrt(Er2) - 1.0f);
                    if (Ring < 0.055f)
                    {
                        const float Edge = (1.0f - Ring / 0.055f) * 0.22f;
                        R += TR * Edge; G += TG * Edge; B += TB * Edge;
                    }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float SpotLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, SpotLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Source" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const float BeamDiam5m = 2.0f * 5.0f * std::tan(Instance.FullConeDegrees * PiF / 360.0f);

    if (ActivePage == 0u)
    {
        UpdateEnvelopeSurface(Instance);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 342.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Beam envelope");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const long WholeCd = static_cast<long>(std::floor(Instance.IntensityCandela));
        const int  FracCd  = static_cast<int>(std::lround((Instance.IntensityCandela - static_cast<float>(WholeCd)) * 10.0f)) % 10;
        char WholeBuf[24]{}, FracBuf[8]{};
        std::snprintf(WholeBuf, sizeof(WholeBuf), "%ld", WholeCd);
        std::snprintf(FracBuf,  sizeof(FracBuf),  ".%d", FracCd);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, WholeBuf, FracBuf, "cd", "Authored luminous intensity");

        Surface.StrokeLine({ LeftX + PadX, Y + 4.0f }, { LeftX + PadX + BodyW, Y + 4.0f }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);
        Y += 14.0f;

        char CapBuf[72]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "%.1f\xC2\xB0 cone / penumbra %.0f%% / reach %.0f m",
            Instance.FullConeDegrees, Instance.Penumbra * 100.0f, Instance.ReachMeters);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, Y }, BodyW, 10.5f, ColorRgba{ 204u, 208u, 214u, 255u }, CapBuf);
        Y += 20.0f;

        const Rectangle2D Stage { LeftX + PadX, Y, BodyW, 166.0f };
        EnvelopePreview.Present(Surface, Stage, 10.0f);
        char PenTag[28]{}, DecTag[24]{};
        std::snprintf(PenTag, sizeof(PenTag), "PENUMBRA %.0f%%", Instance.Penumbra * 100.0f);
        std::snprintf(DecTag, sizeof(DecTag), "REACH %.0f M", Instance.ReachMeters);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.X + 8.0f, Stage.Y + 8.0f }, 8.4f, ColorRgba{ 126u, 130u, 138u, 255u }, PenTag);
        const float DTW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.4f, DecTag);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.Right() - 8.0f - DTW, Stage.Y + 8.0f }, 8.4f, ColorRgba{ 126u, 130u, 138u, 255u }, DecTag);

        // Bottom full-cone bracket caliper
        const float HalfSpan = std::clamp(std::tan(Instance.FullConeDegrees * PiF / 360.0f) * 108.0f * 1.45f, 18.0f, BodyW * 0.38f);
        const float CalY = Stage.Bottom() - 12.0f;
        const float MidX = Stage.Center().X;
        const ColorRgba CalStroke { 142u, 146u, 154u, 170u };
        Surface.StrokeLine({ MidX - HalfSpan, CalY - 3.0f }, { MidX - HalfSpan, CalY + 3.0f }, CalStroke, 1.0f);
        Surface.StrokeLine({ MidX + HalfSpan, CalY - 3.0f }, { MidX + HalfSpan, CalY + 3.0f }, CalStroke, 1.0f);
        Surface.StrokeLine({ MidX - HalfSpan, CalY }, { MidX - 44.0f, CalY }, CalStroke, 1.0f);
        Surface.StrokeLine({ MidX + 44.0f, CalY }, { MidX + HalfSpan, CalY }, CalStroke, 1.0f);
        char ConeFoot[32]{};
        std::snprintf(ConeFoot, sizeof(ConeFoot), "%.1f\xC2\xB0 FULL CONE", Instance.FullConeDegrees);
        const float CFW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.4f, ConeFoot);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { MidX - CFW * 0.5f, CalY - 5.0f }, 8.4f, ColorRgba{ 168u, 172u, 180u, 255u }, ConeFoot);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Visible",      InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled, false },
            { "Cast shadows", InspectorSwitchSymbol::Shadow,  Entry.CastShadows,       false },
            { "Show cone",    InspectorSwitchSymbol::Sun,     Instance.ShowCone,       false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Entry.CastShadows = !Entry.CastShadows; OutMutated = true; }
        else if (Clicked == 2) { Instance.ShowCone = !Instance.ShowCone; OutMutated = true; }

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
        char ConeBuf[20]{}, SoftBuf[20]{};
        std::snprintf(ConeBuf, sizeof(ConeBuf), "%.1f\xC2\xB0", Instance.FullConeDegrees);
        std::snprintf(SoftBuf, sizeof(SoftBuf), "%.0f %%", Instance.Penumbra * 100.0f);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX, Y, HalfW, 50.0f }, "FULL CONE", ConeBuf);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + HalfW + 8.0f, Y, HalfW, 50.0f }, "SOFT EDGE", SoftBuf);
        Y += 58.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8210u, "Intensity", "cd", "%.1f", 0.0f, 200.0f, Instance.IntensityCandela, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8211u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8212u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S1.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D S2 { LeftX, CursorY, InnerWidth, 386.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S2, "Beam shaping", "OPTICS");
        Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char DiamBuf[20]{}; std::snprintf(DiamBuf, sizeof(DiamBuf), "%.2f", BeamDiam5m);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, DiamBuf, "m", "Beam diameter on a perpendicular plane at 5 m");
        std::array<float, 32u> BeamCurve {};
        const float InnerFrac = std::clamp(1.0f - Instance.Penumbra, 0.05f, 1.0f);
        for (std::size_t I = 0u; I < BeamCurve.size(); ++I)
        {
            const float U = std::fabs((static_cast<float>(I) / 31.0f) * 2.0f - 1.0f);
            BeamCurve[I] = (U <= InnerFrac) ? 1.0f : std::clamp(1.0f - (U - InnerFrac) / std::max(0.05f, 1.0f - InnerFrac), 0.0f, 1.0f);
        }
        InspectorPrimitives::ConstructCurvePlot(
            Surface, { LeftX + PadX, Y, BodyW, 60.0f }, BeamCurve,
            ColorRgba{ 246u, 206u, 128u, 255u }, ColorRgba{ 246u, 206u, 128u, 36u }, "-cone/2", "+cone/2");
        Y += 72.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8220u, "Full cone angle", "deg", "%.1f", 2.0f, 80.0f, Instance.FullConeDegrees, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8221u, "Penumbra", "", "%.2f", 0.0f, 1.0f, Instance.Penumbra, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8222u, "Reach", "m", "%.0f", 1.0f, 150.0f, Instance.ReachMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8223u, "Lens radius", "m", "%.2f", 0.02f, 0.8f, Instance.SourceRadiusMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S2.Bottom() + InspectorMetrics::SectionGap;

        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8240u, ActiveDragId, Input, OutMutated);
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, true, "Show cone", Instance.ShowCone, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
