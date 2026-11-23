/*============================================================================================================================================
                                                          LENSFLAREINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LensFlareInspector

#include "Engine/Editor/Inspectors/LensFlareInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

void LensFlareInspector::UpdateFlareAtlas(const LensFlareInstanceData& Instance)
{
    const auto& Flare = Instance.Flare;
    uint64_t Key = 0xF1A2E001ull ^ static_cast<uint64_t>(Flare.Style);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Flare.FlareIntensity);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Flare.HaloIntensity);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Flare.GhostIntensity);
    const uint32_t W = 256u, H = 128u;
    FlareAtlas.EnsureUpdated(Key, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const uint32_t CellX = (X * 4u) / W;
                const uint32_t CellY = (Y * 2u) / H;
                const uint32_t Cell  = CellY * 4u + CellX;
                const float    U     = ((static_cast<float>(X % (W / 4u)) + 0.5f) / static_cast<float>(W / 4u)) * 2.0f - 1.0f;
                const float    V     = ((static_cast<float>(Y % (H / 2u)) + 0.5f) / static_cast<float>(H / 2u)) * 2.0f - 1.0f;
                const float    R2    = U * U + V * V;
                const float    Rad   = std::sqrt(R2);
                const float    Ang   = std::atan2(V, U);
                float R = 0.045f, G = 0.048f, B = 0.056f;
                if (Cell == 0u) // starburst peak
                {
                    const float Spike = std::pow(std::fabs(std::cos(Ang * 3.0f)), 18.0f) * std::exp(-Rad * 2.6f);
                    const float Peak  = std::exp(-R2 * 8.0f);
                    R += Peak * 1.1f + Spike * 0.85f; G += Peak * 0.96f + Spike * 0.78f; B += Peak * 0.80f + Spike * 0.62f;
                }
                else if (Cell == 1u) // anamorphic streak
                {
                    const float S = std::exp(-V * V * 46.0f) * std::exp(-U * U * 2.2f);
                    R += S * 0.42f; G += S * 0.74f; B += S * 1.15f;
                }
                else if (Cell == 2u) // chromatic halo ring
                {
                    const float Hr = std::exp(-std::pow((Rad - 0.62f) * 9.0f, 2.0f));
                    const float Hg = std::exp(-std::pow((Rad - 0.57f) * 9.0f, 2.0f));
                    const float Hb = std::exp(-std::pow((Rad - 0.52f) * 9.0f, 2.0f));
                    R += Hr * 0.85f; G += Hg * 0.80f; B += Hb * 0.95f;
                }
                else if (Cell == 3u) // warm hexagonal ghost
                {
                    const float Hex = std::max(std::fabs(U), std::fabs(U) * 0.5f + std::fabs(V) * 0.866f);
                    const float Disk = (Hex < 0.62f) ? (0.45f + 0.55f * (Hex / 0.62f)) : 0.0f;
                    R += Disk * 0.92f; G += Disk * 0.68f; B += Disk * 0.36f;
                }
                else if (Cell == 4u) // cyan soft ghost
                {
                    const float Disk = std::exp(-R2 * 4.5f) * 0.75f;
                    R += Disk * 0.36f; G += Disk * 0.84f; B += Disk * 1.05f;
                }
                else if (Cell == 5u) // magenta iris ring
                {
                    const float Ring = std::exp(-std::pow((Rad - 0.48f) * 8.0f, 2.0f));
                    R += Ring * 0.88f; G += Ring * 0.42f; B += Ring * 0.96f;
                }
                else if (Cell == 6u) // warm glow orb
                {
                    const float Glow = std::exp(-R2 * 5.2f);
                    R += Glow * 1.05f; G += Glow * 0.76f; B += Glow * 0.44f;
                }
                else // teal caustic
                {
                    const float Caustic = std::exp(-std::pow((Rad - 0.44f) * 6.5f, 2.0f)) * (0.6f + 0.4f * std::cos(Ang * 2.0f));
                    R += Caustic * 0.36f; G += Caustic * 0.92f; B += Caustic * 0.84f;
                }
                // Cell grid divider hairlines
                if ((X % (W / 4u)) == 0u || (Y % (H / 2u)) == 0u) { R += 0.09f; G += 0.10f; B += 0.12f; }
                const auto To8 = [](float V) noexcept -> uint8_t { return static_cast<uint8_t>(std::clamp(V * 255.0f, 0.0f, 255.0f)); };
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = To8(R); Pixels[Off + 1u] = To8(G); Pixels[Off + 2u] = To8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float LensFlareInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LensFlareInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 4u> Pages = {{ "Overview", "Optics", "Halo", "Baking" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    auto& Flare = Instance.Flare;

    if (ActivePage == 0u)
    {
        static constexpr std::array<const char*, 3u> Styles = {{ "Anamorphic", "Spherical", "Clean" }};
        const char* StyleName = Styles[std::min<std::size_t>(static_cast<std::size_t>(Flare.Style), 2u)];

        const Rectangle2D S { LeftX, CursorY, InnerWidth, 276.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Flare composite", StyleName);
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const long WholeF = static_cast<long>(std::floor(Flare.FlareIntensity));
        const int  FracF  = static_cast<int>(std::lround((Flare.FlareIntensity - static_cast<float>(WholeF)) * 100.0f)) % 100;
        char WholeBuf[16]{}, FracBuf[8]{}, SubBuf[64]{};
        std::snprintf(WholeBuf, sizeof(WholeBuf), "%ld", WholeF);
        std::snprintf(FracBuf,  sizeof(FracBuf),  ".%02d", FracF);
        std::snprintf(SubBuf, sizeof(SubBuf), "%s optics · halo %.2f\xC3\x97 · ghosts %.2f\xC3\x97", StyleName, Flare.HaloIntensity, Flare.GhostIntensity);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, WholeBuf, FracBuf, "\xC3\x97", SubBuf);
        Y += 6.0f;

        // Dark #151618 optical composite: starburst + chromatic halo + anamorphic streak + diagonal ghosts
        const Rectangle2D Stage { LeftX + PadX, Y, BodyW, 136.0f };
        Surface.FillRectangle(Stage, ColorRgba{ 18u, 19u, 23u, 255u }, 10.0f);
        Surface.PushClip(Stage);

        const float Gain = (Entry.VisibilityEnabled && Flare.Enabled) ? std::clamp(Flare.FlareIntensity, 0.0f, 2.5f) : 0.12f;
        const Point2D Src { Stage.X + Stage.Width * 0.30f, Stage.Y + Stage.Height * 0.44f };
        const Point2D Mid = Stage.Center();
        const float AxisX = Mid.X - Src.X, AxisY = Mid.Y - Src.Y;

        // Diagonal optical axis guide
        Surface.StrokeLine({ Src.X - AxisX * 0.6f, Src.Y - AxisY * 0.6f }, { Mid.X + AxisX * 1.4f, Mid.Y + AxisY * 1.4f }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);

        // Chromatic halo ring around source
        const float HaloA = std::clamp(Flare.HaloIntensity * Gain * 0.45f, 0.0f, 0.9f);
        if (HaloA > 0.02f)
        {
            Surface.StrokeCircle(Src, 42.0f, ColorRgba{ 242u, 174u, 96u,  static_cast<uint8_t>(HaloA * 120.0f) }, 2.2f);
            Surface.StrokeCircle(Src, 39.0f, ColorRgba{ 126u, 214u, 246u, static_cast<uint8_t>(HaloA * 110.0f) }, 1.8f);
            Surface.StrokeCircle(Src, 36.0f, ColorRgba{ 186u, 136u, 244u, static_cast<uint8_t>(HaloA * 85.0f)  }, 1.4f);
        }

        // Ghost reflections along optical axis
        const float GhostA = std::clamp(Flare.GhostIntensity * Gain * 0.42f, 0.0f, 0.85f);
        if (GhostA > 0.02f)
        {
            static constexpr std::array<float, 4u> GhostPos = {{ 0.55f, 1.15f, 1.72f, 2.18f }};
            static constexpr std::array<float, 4u> GhostRad = {{ 9.0f,  15.0f, 7.0f,  19.0f }};
            static constexpr std::array<ColorRgba, 4u> GhostCol = {{
                { 126u, 198u, 248u, 255u },
                { 196u, 148u, 244u, 255u },
                { 246u, 196u, 128u, 255u },
                { 112u, 224u, 198u, 255u },
            }};
            for (std::size_t I = 0u; I < 4u; ++I)
            {
                const Point2D GP { Src.X + AxisX * GhostPos[I], Src.Y + AxisY * GhostPos[I] };
                ColorRgba Fill = GhostCol[I]; Fill.Alpha = static_cast<uint8_t>(GhostA * 68.0f);
                ColorRgba Rim  = GhostCol[I]; Rim.Alpha  = static_cast<uint8_t>(GhostA * 155.0f);
                Surface.FillCircle(GP, GhostRad[I], Fill);
                Surface.StrokeCircle(GP, GhostRad[I], Rim, 1.0f);
            }
        }

        // Anamorphic horizontal streak
        if (Flare.Style == GeometricRaster::LensFlareStyle::Anamorphic)
        {
            const float StrA = std::clamp(Gain * 0.65f, 0.0f, 0.95f);
            Surface.StrokeLine({ Stage.X + 8.0f, Src.Y }, { Stage.Right() - 8.0f, Src.Y }, ColorRgba{ 118u, 186u, 252u, static_cast<uint8_t>(StrA * 125.0f) }, 3.2f);
            Surface.StrokeLine({ Stage.X + 24.0f, Src.Y }, { Stage.Right() - 24.0f, Src.Y }, ColorRgba{ 216u, 238u, 255u, static_cast<uint8_t>(StrA * 210.0f) }, 1.2f);
        }

        // 6-ray starburst diffraction spikes rotated by StreakRotationDegrees
        const float BaseRad = Flare.StreakRotationDegrees * (3.14159265f / 180.0f);
        for (int Ray = 0; Ray < 6; ++Ray)
        {
            const float Ang = BaseRad + static_cast<float>(Ray) * (3.14159265f / 3.0f);
            const float Len = (Ray % 2 == 0) ? 34.0f : 22.0f;
            const Point2D A { Src.X - std::cos(Ang) * Len, Src.Y - std::sin(Ang) * Len };
            const Point2D B { Src.X + std::cos(Ang) * Len, Src.Y + std::sin(Ang) * Len };
            Surface.StrokeLine(A, B, ColorRgba{ 255u, 240u, 214u, static_cast<uint8_t>(std::clamp(Gain * 140.0f, 20.0f, 220.0f)) }, 1.1f);
        }
        // Luminous core bloom
        Surface.FillCircle(Src, 18.0f, ColorRgba{ 255u, 214u, 152u, static_cast<uint8_t>(std::clamp(Gain * 54.0f, 12.0f, 90.0f)) });
        Surface.FillCircle(Src, 9.0f,  ColorRgba{ 255u, 238u, 206u, static_cast<uint8_t>(std::clamp(Gain * 145.0f, 24.0f, 200.0f)) });
        Surface.FillCircle(Src, 4.0f,  ColorRgba{ 255u, 252u, 246u, 255u });

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Stage.X + 8.0f, Stage.Bottom() - 14.0f }, 8.2f, InspectorMetrics::DimInk, "OPTICAL COMPOSITE");
        Surface.PopClip();
        Surface.StrokeRectangle(Stage, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const bool IsAnamorphic = (Flare.Style == GeometricRaster::LensFlareStyle::Anamorphic);
        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Enabled",    InspectorSwitchSymbol::Eye,    Entry.VisibilityEnabled && Flare.Enabled, false },
            { "Anamorphic", InspectorSwitchSymbol::PostFx, IsAnamorphic,                             false },
            { "Streaks",    InspectorSwitchSymbol::Sun,    Flare.FlareIntensity > 0.05f,             false },
            { "Starburst",  InspectorSwitchSymbol::Stars,  Flare.GhostIntensity > 0.05f,             false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Flare.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1)
        {
            Flare.Style = IsAnamorphic ? GeometricRaster::LensFlareStyle::Streaks : GeometricRaster::LensFlareStyle::Anamorphic;
            OutMutated = true;
        }
        else if (Clicked == 2) { Flare.FlareIntensity = (Flare.FlareIntensity > 0.05f) ? 0.0f : 1.0f; OutMutated = true; }
        else if (Clicked == 3) { Flare.GhostIntensity = (Flare.GhostIntensity > 0.05f) ? 0.0f : 0.7f; OutMutated = true; }

        const Rectangle2D SrcSec { LeftX, CursorY, InnerWidth, 206.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, SrcSec, "Flare source");
        float SY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        SY = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, SY, BodyW, 7051u, "Intensity", "x", "%.2f", 0.0f, 3.0f, Flare.FlareIntensity, ActiveDragId, Input, OutMutated);
        int32_t ChosenStyle = -1;
        (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, SY, BodyW, "Style", Styles, static_cast<uint32_t>(Flare.Style), 3u, Input, ChosenStyle);
        if (ChosenStyle >= 0 && ChosenStyle < 3) { Flare.Style = static_cast<GeometricRaster::LensFlareStyle>(ChosenStyle); OutMutated = true; }
        CursorY = SrcSec.Bottom() + InspectorMetrics::SectionGap;

        static constexpr std::array<const char*, 2u> Tags = {{ "lens-flare", "optics" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S1 { LeftX, CursorY, InnerWidth, 198.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S1, "Flare layers");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7054u, "Rotation", "deg", "%.0f", -90.0f, 90.0f, Flare.StreakRotationDegrees, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7051u, "Streak gain", "x", "%.2f", 0.0f, 3.0f, Flare.FlareIntensity, ActiveDragId, Input, OutMutated);
        CursorY = S1.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D S2 { LeftX, CursorY, InnerWidth, 210.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S2, "Lens ghosts");
        Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char GhBuf[16]{}; std::snprintf(GhBuf, sizeof(GhBuf), "%.2f", Flare.GhostIntensity);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, GhBuf, "x", "Aperture reflections along optical axis");
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7053u, "Ghost brightness", "x", "%.2f", 0.0f, 2.0f, Flare.GhostIntensity, ActiveDragId, Input, OutMutated);
        CursorY = S2.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 214.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Halo & optical response");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char HB[16]{}; std::snprintf(HB, sizeof(HB), "%.2f", Flare.HaloIntensity);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, HB, "x", "Circumferential spectral diffraction ring");
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7052u, "Halo brightness", "x", "%.2f", 0.0f, 2.0f, Flare.HaloIntensity, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        UpdateFlareAtlas(Instance);
        CursorY = InspectorPrimitives::ConstructBakedLutSection(
            Surface, LeftX, CursorY, InnerWidth,
            "Lens flare image", "4x2 sprites", "256x128",
            "Sprite sheet · starburst, streak, halo and chromatic ghosts.",
            FlareAtlas, 108.0f);
    }
    return CursorY;
}

} // namespace Frontier
