/*============================================================================================================================================
                                                      ENVIRONMENTLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance EnvironmentLightInspector with own retained dome-lit hemisphere study

#include "Engine/Editor/Inspectors/EnvironmentLightInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;

struct EnvironmentPresetSpec
{
    const char*            Title;
    Mathematics::LinearRGB Overhead;
    Mathematics::LinearRGB Skyline;
    Mathematics::LinearRGB Ground;
    float                  ReferenceLux;
    float                  PatchAzimuthDeg;
    float                  PatchElevationDeg;
    Mathematics::LinearRGB PatchTint;
    float                  PatchShare;
};

constexpr std::array<EnvironmentPresetSpec, 5u> EnvironmentPresets = {{
    { "Sky atmosphere", { 0.25f, 0.50f, 0.88f }, { 0.74f, 0.84f, 0.95f }, { 0.35f, 0.32f, 0.28f }, 12000.0f, 140.0f, 38.0f, { 1.00f, 0.95f, 0.78f }, 0.55f },
    { "HDRI · studio",  { 0.17f, 0.17f, 0.17f }, { 0.44f, 0.44f, 0.44f }, { 0.12f, 0.12f, 0.12f },   900.0f,  60.0f, 34.0f, { 1.00f, 1.00f, 1.00f }, 0.45f },
    { "HDRI · overcast",{ 0.64f, 0.68f, 0.71f }, { 0.83f, 0.85f, 0.86f }, { 0.29f, 0.29f, 0.27f },  2500.0f,   0.0f, 70.0f, { 0.92f, 0.94f, 0.96f }, 0.10f },
    { "HDRI · sunset",  { 0.16f, 0.25f, 0.48f }, { 1.00f, 0.60f, 0.35f }, { 0.23f, 0.17f, 0.14f },  1400.0f, 250.0f,  6.0f, { 1.00f, 0.77f, 0.42f }, 0.60f },
    { "Uniform colour", { 0.62f, 0.71f, 0.80f }, { 0.62f, 0.71f, 0.80f }, { 0.32f, 0.36f, 0.40f },  1000.0f,   0.0f, 90.0f, { 1.00f, 1.00f, 1.00f }, 0.00f },
}};

inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}

inline Mathematics::LinearRGB ApplySat(Mathematics::LinearRGB C, float Sat) noexcept
{
    const float L = 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
    return {
        std::max(0.0f, L + (C.R - L) * Sat),
        std::max(0.0f, L + (C.G - L) * Sat),
        std::max(0.0f, L + (C.B - L) * Sat),
    };
}
} // namespace

void EnvironmentLightInspector::UpdateHemisphereSurface(const EnvironmentLightInstanceData& Instance)
{
    uint64_t Sig = 0xE1010002ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.SourcePresetIndex);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.SkyRadiance.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.SkyRadiance.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.SkyRadiance.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RotationDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.GainScale);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.ExposureEv);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Saturation);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.GroundBounce);

    const uint32_t W = 240u, H = 152u;
    HemispherePreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const auto& Preset = EnvironmentPresets[std::min<std::size_t>(Instance.SourcePresetIndex, EnvironmentPresets.size() - 1u)];
        const bool Uniform = (Instance.SourcePresetIndex == 4u);
        const Mathematics::LinearRGB Overhead = ApplySat(Uniform ? Instance.SkyRadiance : Preset.Overhead, Instance.Saturation);
        const Mathematics::LinearRGB Skyline  = ApplySat(Uniform ? Instance.SkyRadiance : Preset.Skyline,  Instance.Saturation);
        const Mathematics::LinearRGB Ground   = ApplySat(Preset.Ground, Instance.Saturation);
        const float Gain = Instance.GainScale * std::pow(2.0f, Instance.ExposureEv * 0.5f);

        const float AzRad = (Preset.PatchAzimuthDeg + Instance.RotationDegrees) * (PiF / 180.0f);
        const float ElRad = Preset.PatchElevationDeg * (PiF / 180.0f);
        const float SunX  = std::cos(ElRad) * std::sin(AzRad);
        const float SunY  = std::sin(ElRad);

        const float SphereCX = static_cast<float>(W) * 0.27f, SphereCY = static_cast<float>(H) * 0.48f, SphereR = 38.0f;
        const float CompassCX = static_cast<float>(W) * 0.74f, CompassCY = static_cast<float>(H) * 0.48f, CompassR = 38.0f;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX = static_cast<float>(X) + 0.5f;
                // Clean dark well background (#151618) with subtle centre divider
                float R = 0.017f, G = 0.018f, B = 0.021f;
                if (std::fabs(FX - static_cast<float>(W) * 0.5f) < 0.6f && FY > 10.0f && FY < static_cast<float>(H) - 10.0f)
                {
                    R += 0.022f; G += 0.023f; B += 0.026f;
                }

                // Left side-view sky hemisphere + ground half-dome
                const float SX = (FX - SphereCX) / SphereR;
                const float SY = (SphereCY - FY) / SphereR;
                const float S2 = SX * SX + SY * SY;
                if (S2 <= 1.0f)
                {
                    if (SY >= 0.0f)
                    {
                        const float T = std::pow(1.0f - SY, 0.75f);
                        R = (Overhead.R * (1.0f - T) + Skyline.R * T) * 0.92f * Gain;
                        G = (Overhead.G * (1.0f - T) + Skyline.G * T) * 0.92f * Gain;
                        B = (Overhead.B * (1.0f - T) + Skyline.B * T) * 0.92f * Gain;
                        const float SunSideX = std::clamp(SunX * 0.8f, -0.82f, 0.82f);
                        const float SunSideY = std::clamp(SunY * 0.85f, 0.08f, 0.88f);
                        const float SD = std::sqrt((SX - SunSideX) * (SX - SunSideX) + (SY - SunSideY) * (SY - SunSideY));
                        if (Preset.PatchShare > 0.01f && SD < 0.55f)
                        {
                            const float Glow = std::exp(-SD * SD * 18.0f) * Preset.PatchShare * 2.1f * Gain;
                            R += Preset.PatchTint.R * Glow; G += Preset.PatchTint.G * Glow; B += Preset.PatchTint.B * Glow;
                        }
                    }
                    else
                    {
                        const float Dn = std::clamp(-SY, 0.0f, 1.0f);
                        const float Bnc = (0.35f + 0.65f * Instance.GroundBounce) * (1.0f - Dn * 0.55f);
                        R = Ground.R * Bnc * Gain;
                        G = Ground.G * Bnc * Gain;
                        B = Ground.B * Bnc * Gain;
                    }
                    // Horizon line across equator
                    if (std::fabs(SY) < 0.028f)
                    {
                        R += 0.22f; G += 0.24f; B += 0.26f;
                    }
                }
                else if (std::fabs(std::sqrt(S2) - 1.0f) < 0.032f)
                {
                    R += 0.16f; G += 0.17f; B += 0.19f;
                }

                // Right top-down hemisphere polar compass showing rotated bright patch direction
                const float CDX = FX - CompassCX, CDY = FY - CompassCY;
                const float CD  = std::sqrt(CDX * CDX + CDY * CDY);
                if (CD <= CompassR + 1.1f)
                {
                    if (CD <= CompassR)
                    {
                        const float Fade = std::pow(CD / CompassR, 0.85f);
                        R = (Overhead.R * (1.0f - Fade) + Skyline.R * Fade) * 0.82f * Gain;
                        G = (Overhead.G * (1.0f - Fade) + Skyline.G * Fade) * 0.82f * Gain;
                        B = (Overhead.B * (1.0f - Fade) + Skyline.B * Fade) * 0.82f * Gain;
                        const float PX = CompassCX + std::sin(AzRad) * std::cos(ElRad) * (CompassR * 0.82f);
                        const float PY = CompassCY - std::cos(AzRad) * std::cos(ElRad) * (CompassR * 0.82f);
                        const float PD = std::sqrt((FX - PX) * (FX - PX) + (FY - PY) * (FY - PY));
                        if (Preset.PatchShare > 0.01f && PD < 14.0f)
                        {
                            const float Glow = std::exp(-PD * PD / 32.0f) * Preset.PatchShare * 2.6f * Gain;
                            R += Preset.PatchTint.R * Glow; G += Preset.PatchTint.G * Glow; B += Preset.PatchTint.B * Glow;
                        }
                        // Dashed 30° and 60° altitude rings + subtle crosshairs
                        const float Ring30 = std::fabs(CD - CompassR * 0.34f);
                        const float Ring60 = std::fabs(CD - CompassR * 0.67f);
                        if ((Ring30 < 0.65f || Ring60 < 0.65f) && ((static_cast<int>(X + Y) / 3) & 1) == 0)
                        {
                            R += 0.14f; G += 0.15f; B += 0.17f;
                        }
                        if ((std::fabs(CDX) < 0.55f || std::fabs(CDY) < 0.55f) && ((static_cast<int>(X + Y) / 4) & 1) == 0)
                        {
                            R += 0.08f; G += 0.09f; B += 0.10f;
                        }
                    }
                    if (std::fabs(CD - CompassR) < 0.95f)
                    {
                        R += 0.18f; G += 0.19f; B += 0.21f;
                    }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float EnvironmentLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, EnvironmentLightInstanceData& Instance,
    ViewportDisplaySettings* DisplaySettings,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Settings" }};
    static constexpr std::array<const char*, 5u> SourceLabels = {{
        "Sky atmosphere", "HDRI · studio", "HDRI · overcast", "HDRI · sunset", "Uniform colour"
    }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const auto& Preset = EnvironmentPresets[std::min<std::size_t>(Instance.SourcePresetIndex, EnvironmentPresets.size() - 1u)];
    const float EffectiveLux = Preset.ReferenceLux * Instance.GainScale * std::pow(2.0f, Instance.ExposureEv);

    if (ActivePage == 0u)
    {
        UpdateHemisphereSurface(Instance);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 332.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Sky dome");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { LeftX + PadX, Y }, 20.0f, WorkspaceSurfaceColors::PrimaryInk, Preset.Title);
        Y += 26.0f;
        char LuxSub[48]{};
        if (EffectiveLux >= 1000.0f) std::snprintf(LuxSub, sizeof(LuxSub), "%.0fk lx on a level surface", EffectiveLux * 0.001f);
        else                         std::snprintf(LuxSub, sizeof(LuxSub), "%.0f lx on a level surface", EffectiveLux);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + PadX, Y }, 10.5f, InspectorMetrics::MutedInk, LuxSub);
        Y += 20.0f;

        const Rectangle2D Stage { LeftX + PadX, Y, BodyW, 134.0f };
        HemispherePreview.Present(Surface, Stage, 10.0f);
        // Side-view & polar compass overlaid annotations
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.X + 7.0f, Stage.Y + Stage.Height * 0.43f }, 8.4f, ColorRgba{ 146u, 150u, 158u, 255u }, "W");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.X + Stage.Width * 0.45f, Stage.Y + Stage.Height * 0.43f }, 8.4f, ColorRgba{ 146u, 150u, 158u, 255u }, "E");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Stage.X + 12.0f, Stage.Bottom() - 15.0f }, 8.0f, ColorRgba{ 130u, 134u, 142u, 255u }, "side view · looking north");
        const float CompCX = Stage.X + Stage.Width * 0.74f;
        const float CompCY = Stage.Y + Stage.Height * 0.48f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { CompCX - 3.5f, Stage.Y + 4.0f }, 8.2f, ColorRgba{ 184u, 188u, 196u, 255u }, "N");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { CompCX + 40.0f, CompCY - 5.0f }, 8.0f, ColorRgba{ 146u, 150u, 158u, 255u }, "E");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { CompCX - 46.0f, CompCY - 5.0f }, 8.0f, ColorRgba{ 146u, 150u, 158u, 255u }, "W");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Stage.X + Stage.Width * 0.53f, Stage.Bottom() - 15.0f }, 8.0f, ColorRgba{ 130u, 134u, 142u, 255u }, "top view · rings 30\xC2\xB0 / 60\xC2\xB0");
        Y = Stage.Bottom() + 10.0f;

        // 3-column stat tiles inside hero section (KEY LIGHT / DIRECT / BOUNCE)
        static constexpr std::array<const char*, 8u> CompassDirs = {{ "N", "NE", "E", "SE", "S", "SW", "W", "NW" }};
        const float ModAz = std::fmod(std::fmod(Preset.PatchAzimuthDeg + Instance.RotationDegrees, 360.0f) + 360.0f, 360.0f);
        const std::size_t DirIdx = static_cast<std::size_t>(std::lround(ModAz / 45.0f)) % 8u;
        char KeyVal[24]{}, DirVal[16]{}, BncVal[16]{};
        std::snprintf(KeyVal, sizeof(KeyVal), "%s · %.0f\xC2\xB0", CompassDirs[DirIdx], Preset.PatchElevationDeg);
        std::snprintf(DirVal, sizeof(DirVal), "%.0f%%", Preset.PatchShare * 100.0f);
        std::snprintf(BncVal, sizeof(BncVal), "%.0f%%", Instance.GroundBounce * 100.0f);
        const float Gap = 6.0f;
        const float TileW = (BodyW - Gap * 2.0f) / 3.0f;
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX, Y, TileW, 46.0f }, "KEY LIGHT", KeyVal);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + TileW + Gap, Y, TileW, 46.0f }, "DIRECT", DirVal);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + (TileW + Gap) * 2.0f, Y, TileW, 46.0f }, "BOUNCE", BncVal);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        const bool SpecOn = DisplaySettings ? DisplaySettings->ReflectionsEnabled : true;
        const bool AoOn   = DisplaySettings ? DisplaySettings->XeGtaoEnabled      : true;
        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Enabled",           InspectorSwitchSymbol::Eye,   Entry.VisibilityEnabled, false },
            { "Diffuse",           InspectorSwitchSymbol::Orbit, Instance.IndirectGi,     false },
            { "Specular",          InspectorSwitchSymbol::Stars, SpecOn,                  false },
            { "Ambient occlusion", InspectorSwitchSymbol::Sun,   AoOn,                    false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.IndirectGi = !Instance.IndirectGi; OutMutated = true; }
        else if (Clicked == 2 && DisplaySettings) { DisplaySettings->ReflectionsEnabled = !DisplaySettings->ReflectionsEnabled; OutMutated = true; }
        else if (Clicked == 3 && DisplaySettings) { DisplaySettings->XeGtaoEnabled = !DisplaySettings->XeGtaoEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "sky-light", "ibl" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S1 { LeftX, CursorY, InnerWidth, 370.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S1, "Source", Preset.Title);
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        int32_t PickSrc = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Source", SourceLabels, Instance.SourcePresetIndex, 2u, Input, PickSrc);
        if (PickSrc >= 0) { Instance.SourcePresetIndex = static_cast<uint32_t>(PickSrc); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7181u, "Rotation", "deg", "%.0f", 0.0f, 360.0f, Instance.RotationDegrees, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 7180u, "Colour", Instance.SkyRadiance, ActiveDragId, Input, OutMutated);
        CursorY = S1.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D S2 { LeftX, CursorY, InnerWidth, 286.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S2, "Response");
        Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7190u, "Intensity", "x", "%.2f", 0.0f, 4.0f, Instance.GainScale, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7191u, "Exposure", "EV", "%+.1f", -4.0f, 4.0f, Instance.ExposureEv, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7192u, "Saturation", "", "%.2f", 0.0f, 2.0f, Instance.Saturation, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7193u, "Ground bounce", "", "%.2f", 0.0f, 1.0f, Instance.GroundBounce, ActiveDragId, Input, OutMutated);
        CursorY = S2.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
