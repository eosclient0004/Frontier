/*============================================================================================================================================
                                                           RAINBOWINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance RainbowInspector

#include "Engine/Editor/Inspectors/RainbowInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

void SpectralRgb(float T, float& OutR, float& OutG, float& OutB) noexcept
{
    const float U = std::clamp(T, 0.0f, 1.0f); // 0 = violet, 1 = red
    OutR = std::clamp(std::exp(-std::pow((U - 0.85f) * 3.4f, 2.0f)) + 0.35f * std::exp(-std::pow((U - 0.05f) * 5.0f, 2.0f)), 0.0f, 1.0f);
    OutG = std::clamp(std::exp(-std::pow((U - 0.50f) * 3.8f, 2.0f)), 0.0f, 1.0f);
    OutB = std::clamp(std::exp(-std::pow((U - 0.16f) * 4.2f, 2.0f)), 0.0f, 1.0f);
}

} // namespace

void RainbowInspector::UpdateRainbowSurfaces(const RainbowInstanceData& Instance)
{
    uint64_t Key = 0x5A1AB001ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.PrimaryIntensity);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.SecondaryIntensity);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.SupernumeraryWidth);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.DropletRadiusUm);

    const uint32_t W1 = 240u, H1 = 136u;
    ArcPreview.EnsureUpdated(Key, W1, H1, [&](std::vector<uint8_t>& Pixels)
    {
        const uint32_t W = W1, H = H1;
        const float CX = static_cast<float>(W) * 0.50f;
        const float CY = static_cast<float>(H) * 1.12f;
        const float R1 = static_cast<float>(H) * 0.78f;
        const float R2 = static_cast<float>(H) * 1.02f;
        const float BandW = static_cast<float>(H) * 0.12f;
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) / static_cast<float>(H);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) - CX;
                const float DY = static_cast<float>(Y) - CY;
                const float Dist = std::sqrt(DX * DX + DY * DY);
                // Deep twilight rain-sky backdrop + subtle Alexander dark band between R1 and R2
                float R = 0.055f + FY * 0.045f;
                float G = 0.072f + FY * 0.060f;
                float B = 0.115f + FY * 0.075f;
                if (Dist > R1 + BandW * 0.45f && Dist < R2 - BandW * 0.45f)
                {
                    R *= 0.72f; G *= 0.72f; B *= 0.76f;
                }
                else if (Dist < R1 - BandW * 0.45f)
                {
                    R += 0.035f; G += 0.042f; B += 0.055f;
                }
                // Primary 42° arc (violet inside -> red outside)
                const float U1 = (Dist - (R1 - BandW * 0.5f)) / BandW;
                if (U1 >= 0.0f && U1 <= 1.0f)
                {
                    float SR, SG, SB;
                    SpectralRgb(U1, SR, SG, SB);
                    const float Env = std::sin(U1 * 3.14159265f) * std::clamp(Instance.PrimaryIntensity * 0.85f, 0.0f, 1.1f);
                    R += SR * Env; G += SG * Env; B += SB * Env;
                }
                // Secondary 51° arc (reversed: red inside -> violet outside)
                const float U2 = (Dist - (R2 - BandW * 0.55f)) / (BandW * 1.1f);
                if (U2 >= 0.0f && U2 <= 1.0f)
                {
                    float SR, SG, SB;
                    SpectralRgb(1.0f - U2, SR, SG, SB);
                    const float Env = std::sin(U2 * 3.14159265f) * std::clamp(Instance.SecondaryIntensity * 0.55f, 0.0f, 0.75f);
                    R += SR * Env; G += SG * Env; B += SB * Env;
                }
                const auto To8 = [](float V) noexcept -> uint8_t { return static_cast<uint8_t>(std::clamp(V * 255.0f, 0.0f, 255.0f)); };
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = To8(R); Pixels[Off + 1u] = To8(G); Pixels[Off + 2u] = To8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });

    const uint32_t W2 = 256u, H2 = 32u;
    DispersionLut.EnsureUpdated(Key, W2, H2, [&](std::vector<uint8_t>& Pixels)
    {
        const uint32_t W = W2, H = H2;
        for (uint32_t X = 0u; X < W; ++X)
        {
            const float U = static_cast<float>(X) / static_cast<float>(W - 1u);
            float R = 0.05f, G = 0.06f, B = 0.09f;
            if (U > 0.12f && U < 0.44f)
            {
                const float T = (U - 0.12f) / 0.32f;
                float SR, SG, SB; SpectralRgb(T, SR, SG, SB);
                const float E = std::sin(T * 3.14159265f) * std::clamp(Instance.PrimaryIntensity * 0.9f, 0.0f, 1.0f);
                R += SR * E; G += SG * E; B += SB * E;
            }
            else if (U > 0.62f && U < 0.88f)
            {
                const float T = 1.0f - (U - 0.62f) / 0.26f;
                float SR, SG, SB; SpectralRgb(T, SR, SG, SB);
                const float E = std::sin(T * 3.14159265f) * std::clamp(Instance.SecondaryIntensity * 0.6f, 0.0f, 0.8f);
                R += SR * E; G += SG * E; B += SB * E;
            }
            const auto To8 = [](float V) noexcept -> uint8_t { return static_cast<uint8_t>(std::clamp(V * 255.0f, 0.0f, 255.0f)); };
            for (uint32_t Y = 0u; Y < H; ++Y)
            {
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = To8(R); Pixels[Off + 1u] = To8(G); Pixels[Off + 2u] = To8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float RainbowInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, RainbowInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Bow", "Baking" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        UpdateRainbowSurfaces(Instance);
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 272.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Optical preview");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;
        char Buf[16]{}; std::snprintf(Buf, sizeof(Buf), "%.0f", Instance.DropletRadiusUm);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, Buf, "", "um", "Primary 42\xC2\xB0 & secondary 51\xC2\xB0 caustic");
        Y += 6.0f;

        const Rectangle2D ArcWell { LeftX + PadX, Y, BodyW, 134.0f };
        ArcPreview.Present(Surface, ArcWell, 10.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { ArcWell.X + 8.0f, ArcWell.Bottom() - 14.0f }, 8.0f, ColorRgba{ 196u, 206u, 224u, 210u }, "42\xC2\xB0 PRIMARY · 51\xC2\xB0 SECONDARY");
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",          InspectorSwitchSymbol::Sun,   Entry.VisibilityEnabled,          false },
            { "Alexander's band", InspectorSwitchSymbol::Orbit, Instance.SecondaryIntensity > 0.05f, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Instance.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.SecondaryIntensity = (Instance.SecondaryIntensity > 0.05f) ? 0.0f : 0.42f; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "rainbow", "optics" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 290.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Bow response");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6601u, "Primary bow", "x", "%.2f", 0.0f, 2.0f, Instance.PrimaryIntensity, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6602u, "Secondary bow", "x", "%.2f", 0.0f, 1.0f, Instance.SecondaryIntensity, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6603u, "Supernumerary", "", "%.2f", 0.0f, 1.0f, Instance.SupernumeraryWidth, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6604u, "Droplet radius", "um", "%.0f", 50.0f, 1500.0f, Instance.DropletRadiusUm, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        UpdateRainbowSurfaces(Instance);
        CursorY = InspectorPrimitives::ConstructBakedLutSection(
            Surface, LeftX, CursorY, InnerWidth,
            "Dispersion LUT", "40-54 deg", "256x32",
            "Angular spectral dispersion · primary 42 deg and reversed 51 deg secondary.",
            DispersionLut, 64.0f);
    }

    return CursorY;
}

} // namespace Frontier
