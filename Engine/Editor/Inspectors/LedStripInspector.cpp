/*============================================================================================================================================
                                                         LEDSTRIPINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LedStripInspector with own retained SMD ribbon / opal diffuser preview

#include "Engine/Editor/Inspectors/LedStripInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}
} // namespace

void LedStripInspector::UpdateRibbonSurface(const LedStripData& Instance)
{
    uint64_t Sig = 0x20010001ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.OpalDiffuser ? 1u : 0u);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.LumensPerMetre);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Dimmer);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.KelvinTemperature);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.LengthMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.DiodesPerMetre);

    const uint32_t W = 240u, H = 152u;
    RibbonPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const float TR = Instance.RadiantTint.R * K.R;
        const float TG = Instance.RadiantTint.G * K.G;
        const float TB = Instance.RadiantTint.B * K.B;
        const float CY = static_cast<float>(H) * 0.5f;
        const float LeftEnd = 22.0f, RightEnd = 218.0f;
        const float RunW = RightEnd - LeftEnd;
        const float Dim = std::clamp(Instance.Dimmer, 0.0f, 1.0f);
        const int   Diodes = std::clamp(static_cast<int>(std::lround(Instance.LengthMeters * Instance.DiodesPerMetre / 4.0f)), 8, 32);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            const float DY = std::fabs(FY - CY);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX = static_cast<float>(X) + 0.5f;
                float R = 0.017f, G = 0.018f, B = 0.021f;

                const bool AlongRun = (FX >= LeftEnd && FX <= RightEnd);

                // Soft radiant spill above & below ribbon
                if (AlongRun && DY < 38.0f)
                {
                    const float Spill = std::exp(-DY * 0.09f) * (Instance.OpalDiffuser ? 0.32f : 0.20f) * Dim;
                    R += TR * Spill; G += TG * Spill; B += TB * Spill;
                }

                // Extruded aluminium profile channel
                if (AlongRun && DY <= 10.5f)
                {
                    R = 0.19f; G = 0.19f; B = 0.17f;
                    if (DY <= 6.8f) { R = 0.10f; G = 0.11f; B = 0.10f; }
                }

                if (AlongRun && Instance.OpalDiffuser && DY <= 5.2f)
                {
                    // Smooth opal polycarbonate diffuser bar
                    const float Core = 1.0f - (DY / 5.2f) * 0.35f;
                    const float Lum  = (0.28f + 1.05f * Dim) * Core;
                    R = 0.08f + TR * Lum; G = 0.08f + TG * Lum; B = 0.08f + TB * Lum;
                }
                else if (AlongRun && !Instance.OpalDiffuser && DY <= 5.5f)
                {
                    // Discrete exposed SMD LED packages & copper cut pads
                    const float U = (FX - LeftEnd) / RunW * static_cast<float>(Diodes - 1);
                    const float Nearest = std::round(U);
                    const float DistX = std::fabs(FX - (LeftEnd + (Nearest / static_cast<float>(Diodes - 1)) * RunW));
                    if (DistX < 2.2f && DY < 2.3f)
                    {
                        const float Lum = 0.25f + 1.25f * Dim;
                        R = 0.05f + TR * Lum; G = 0.05f + TG * Lum; B = 0.05f + TB * Lum;
                    }
                    else if ((static_cast<int>(Nearest) % 6) == 0 && DistX < 3.6f && DY < 4.5f && (DistX > 2.8f || DY > 3.6f))
                    {
                        R = 0.56f; G = 0.48f; B = 0.24f; // Copper solder/cut pad outline
                    }
                }

                // Bottom dimension line
                if (std::fabs(FY - 128.0f) < 0.7f && AlongRun)
                {
                    R += 0.22f; G += 0.24f; B += 0.26f;
                }
                if (std::fabs(FY - 128.0f) < 3.5f && (std::fabs(FX - LeftEnd) < 0.8f || std::fabs(FX - RightEnd) < 0.8f))
                {
                    R += 0.28f; G += 0.30f; B += 0.32f;
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float LedStripInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LedStripData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Source" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const float TotalLumens = std::max(0.0f, Instance.LumensPerMetre * Instance.LengthMeters * Instance.Dimmer);
    const float TotalWatts  = std::max(0.0f, Instance.WattsPerMetre  * Instance.LengthMeters);
    const float TotalLeds   = std::max(1.0f, std::round(Instance.LengthMeters * Instance.DiodesPerMetre));
    const float LoadAmps    = TotalWatts / std::max(1.0f, Instance.SupplyVolts);

    if (ActivePage == 0u)
    {
        UpdateRibbonSurface(Instance);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 342.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Linear LED strip");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const long WholeLm = static_cast<long>(std::floor(TotalLumens));
        const int  FracLm  = static_cast<int>(std::lround((TotalLumens - static_cast<float>(WholeLm)) * 10.0f)) % 10;
        char WholeBuf[24]{}, FracBuf[8]{};
        std::snprintf(WholeBuf, sizeof(WholeBuf), "%ld", WholeLm);
        std::snprintf(FracBuf,  sizeof(FracBuf),  ".%d", FracLm);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, WholeBuf, FracBuf, "lm", "Estimated ribbon flux");

        Surface.StrokeLine({ LeftX + PadX, Y + 4.0f }, { LeftX + PadX + BodyW, Y + 4.0f }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);
        Y += 14.0f;

        char CapBuf[72]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "%.1f m run / %.0f V DC / %s",
            Instance.LengthMeters, Instance.SupplyVolts, Instance.OpalDiffuser ? "Opal diffuser" : "Exposed SMD");
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, Y }, BodyW, 10.5f, ColorRgba{ 204u, 208u, 214u, 255u }, CapBuf);
        Y += 20.0f;

        const Rectangle2D Stage { LeftX + PadX, Y, BodyW, 166.0f };
        RibbonPreview.Present(Surface, Stage, 10.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.X + 8.0f, Stage.Y + 8.0f }, 8.4f, ColorRgba{ 126u, 130u, 138u, 255u },
            Instance.OpalDiffuser ? "OPAL DIFFUSER" : "SMD RIBBON");
        char DensTag[28]{};
        std::snprintf(DensTag, sizeof(DensTag), "%.0f LED/M", Instance.DiodesPerMetre);
        const float DTW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.4f, DensTag);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.Right() - 8.0f - DTW, Stage.Y + 8.0f }, 8.4f, ColorRgba{ 126u, 130u, 138u, 255u }, DensTag);

        char RunFoot[48]{};
        std::snprintf(RunFoot, sizeof(RunFoot), "%.1f M · %.0f LEDS · %.1f W", Instance.LengthMeters, TotalLeds, TotalWatts);
        const float RFW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.4f, RunFoot);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.Center().X - RFW * 0.5f, Stage.Bottom() - 14.0f }, 8.4f, ColorRgba{ 168u, 172u, 180u, 255u }, RunFoot);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;
        (void)LoadAmps;

        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Visible",       InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled, false },
            { "Opal diffuser", InspectorSwitchSymbol::Orbit,   Instance.OpalDiffuser,   false },
            { "Show ribbon",   InspectorSwitchSymbol::Cube,    Instance.ShowFixture,    false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.OpalDiffuser = !Instance.OpalDiffuser; OutMutated = true; }
        else if (Clicked == 2) { Instance.ShowFixture = !Instance.ShowFixture; OutMutated = true; }

        static constexpr std::array<const char*, 1u> Tags = {{ "lighting" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S1 { LeftX, CursorY, InnerWidth, 452.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S1, "Output per metre");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char LmBuf[20]{}; std::snprintf(LmBuf, sizeof(LmBuf), "%.0f", TotalLumens);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, LmBuf, "lm", "Estimated ribbon flux");
        const float HalfW = (BodyW - 8.0f) * 0.5f;
        char LoadStat[24]{}, CountStat[24]{};
        std::snprintf(LoadStat, sizeof(LoadStat), "%.1f W", TotalWatts);
        std::snprintf(CountStat, sizeof(CountStat), "%.0f LEDs", TotalLeds);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX, Y, HalfW, 50.0f }, "CONNECTED LOAD", LoadStat);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + HalfW + 8.0f, Y, HalfW, 50.0f }, "EMITTER COUNT", CountStat);
        Y += 58.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8710u, "Flux per metre", "lm/m", "%.0f", 10.0f, 4000.0f, Instance.LumensPerMetre, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8711u, "Load per metre", "W/m", "%.1f", 1.0f, 50.0f, Instance.WattsPerMetre, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8712u, "Dimmer", "", "%.2f", 0.0f, 1.0f, Instance.Dimmer, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8713u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8714u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S1.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D S2 { LeftX, CursorY, InnerWidth, 306.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S2, "Layout & segments", "OPTICS");
        Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char AmpBuf[20]{}; std::snprintf(AmpBuf, sizeof(AmpBuf), "%.2f", LoadAmps);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, AmpBuf, "A", "Ideal full-load current");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8720u, "Strip length", "m", "%.1f", 0.1f, 20.0f, Instance.LengthMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8721u, "Emitter density", "/m", "%.0f", 10.0f, 240.0f, Instance.DiodesPerMetre, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8722u, "Supply voltage", "V", "%.0f", 5.0f, 48.0f, Instance.SupplyVolts, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Opal diffuser", Instance.OpalDiffuser, Input, OutMutated);
        CursorY = S2.Bottom() + InspectorMetrics::SectionGap;

        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8740u, ActiveDragId, Input, OutMutated);
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, false, "Show emitter", Instance.ShowFixture, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
