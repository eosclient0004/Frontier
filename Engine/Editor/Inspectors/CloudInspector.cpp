/*============================================================================================================================================
                                                            CLOUDINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance CloudInspector (KPI metrics, Coverage, CloudDeck cross-section, Body, Wind, Quality)

#include "Engine/Editor/Inspectors/CloudInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;

inline void PutPixel(std::vector<uint8_t>& Buf, uint32_t W, uint32_t X, uint32_t Y, uint8_t R, uint8_t G, uint8_t B) noexcept
{
    const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
    Buf[Off + 0u] = R; Buf[Off + 1u] = G; Buf[Off + 2u] = B; Buf[Off + 3u] = 255u;
}
} // namespace

void CloudInspector::UpdateCloudDeckSurface(const GeometricRaster::CloudDeckConfiguration& Clouds)
{
    uint64_t Sig = 0xC1007007ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Clouds.CloudFloorMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Clouds.LayerThicknessMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Clouds.ExtinctionMultiplier);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Clouds.CloudFraction);

    const uint32_t W = 224u, H = 176u;
    CloudDeckPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float SpanAlt = std::max(400.0f, std::ceil(((Clouds.CloudFloorMeters + Clouds.LayerThicknessMeters) * 1.25f) / 500.0f) * 500.0f);
        auto AltToY = [&](float Alt) noexcept -> float
        {
            return static_cast<float>(H) - 18.0f - (Alt / SpanAlt) * (static_cast<float>(H) - 44.0f);
        };
        const float DatumY  = AltToY(Clouds.CloudFloorMeters);
        const float DepthPx = (Clouds.LayerThicknessMeters / SpanAlt) * (static_cast<float>(H) - 44.0f);
        const float Dens01  = std::clamp(Clouds.ExtinctionMultiplier / 4.0f, 0.12f, 1.0f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX   = static_cast<float>(X);
                const float TopY = DatumY - DepthPx * (0.65f + 0.28f * std::sin(FX * 0.08f) + 0.12f * std::sin(FX * 0.31f));
                float R = 6.0f / 255.0f, G = 7.0f / 255.0f, B = 9.0f / 255.0f;
                if (FY >= TopY && FY <= DatumY + 4.0f)
                {
                    const float T = std::clamp((FY - TopY) / std::max(1.0f, (DatumY + 4.0f) - TopY), 0.0f, 1.0f);
                    const float A = (T < 0.35f) ? (0.08f + (T / 0.35f) * 0.68f * Dens01) : (0.74f * Dens01);
                    R = R * (1.0f - A) + ((1.0f - T) * 0.93f + T * 0.36f) * A;
                    G = G * (1.0f - A) + ((1.0f - T) * 0.95f + T * 0.42f) * A;
                    B = B * (1.0f - A) + ((1.0f - T) * 0.97f + T * 0.50f) * A;
                }
                for (int32_t Line = 0; Line <= 4; ++Line)
                {
                    if (std::fabs(FY - AltToY(SpanAlt * static_cast<float>(Line) * 0.25f)) < 0.6f)
                    {
                        R += 0.06f; G += 0.06f; B += 0.07f;
                    }
                }
                if (std::fabs(FY - DatumY) < 0.8f && ((X / 4u) % 2u == 0u)) { R = 0.82f; G = 0.84f; B = 0.88f; }
                PutPixel(Pixels, W, X, Y,
                    static_cast<uint8_t>(std::clamp(std::lround(R * 255.0f), 0l, 255l)),
                    static_cast<uint8_t>(std::clamp(std::lround(G * 255.0f), 0l, 255l)),
                    static_cast<uint8_t>(std::clamp(std::lround(B * 255.0f), 0l, 255l)));
            }
        }
    });
}

float CloudInspector::Construct(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    CloudInstanceData&      Instance,
    const PaneInputReading& Input,
    bool&                   OutMutated,
    bool&                   OutPageSwitched)
{
    static constexpr std::array<const char*, 6u> Pages = {{
        "Overview", "Coverage", "Layer", "Body", "Wind", "Quality",
    }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Clouds = Instance.Clouds;
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        // 1:1 HTML Blueprint radar swath hero section + integrated 2x2 stat tiles
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 336.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud deck", "12 KM SWATH");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const Rectangle2D Swath { LeftX + PadX, Y, BodyW, 178.0f };
        Surface.FillRectangle(Swath, ColorRgba{ 16u, 22u, 32u, 255u }, 12.0f);
        Surface.PushClip(Swath);

        // Blueprint coordinate grid
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
        Surface.StrokeCircle(Swath.Center(), 54.0f, ColorRgba{ 132u, 182u, 232u, 28u }, 1.0f);
        Surface.StrokeCircle(Swath.Center(), 27.0f, ColorRgba{ 132u, 182u, 232u, 22u }, 1.0f);

        // Soft cloud cells weighted by CloudFraction
        const int CellCount = static_cast<int>(std::clamp(Clouds.CloudFraction * 26.0f, 0.0f, 26.0f));
        const uint8_t CloudA = static_cast<uint8_t>(std::clamp(32.0f + Clouds.ExtinctionMultiplier * 28.0f, 24.0f, 118.0f));
        for (int C = 0; C < CellCount; ++C)
        {
            const float HX = std::fabs(std::fmod(std::sin(static_cast<float>(C + 1) * 41.73f) * 43758.5f, 1.0f));
            const float HY = std::fabs(std::fmod(std::sin(static_cast<float>(C + 1) * 19.31f) * 19283.7f, 1.0f));
            const float Rad = 14.0f + 18.0f * std::fabs(std::fmod(std::sin(static_cast<float>(C + 3) * 13.7f) * 9123.3f, 1.0f));
            const Point2D Pt { Swath.X + 20.0f + HX * (Swath.Width - 40.0f), Swath.Y + 26.0f + HY * (Swath.Height - 64.0f) };
            Surface.FillCircle(Pt, Rad, ColorRgba{ 212u, 228u, 246u, static_cast<uint8_t>(CloudA / 2u) });
            Surface.FillCircle(Pt, Rad * 0.62f, ColorRgba{ 232u, 242u, 255u, CloudA });
        }

        // Top-left north compass indicator & drift vector arrow
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { Swath.X + 10.0f, Swath.Y + 8.0f }, 9.0f, ColorRgba{ 162u, 206u, 248u, 235u }, "N");
        const float DriftRad = Clouds.WindBearingDegrees * (PiF / 180.0f);
        const Point2D ArrowOrg { Swath.Right() - 28.0f, Swath.Y + 24.0f };
        const Point2D ArrowTip { ArrowOrg.X + std::sin(DriftRad) * 13.0f, ArrowOrg.Y - std::cos(DriftRad) * 13.0f };
        Surface.StrokeCircle(ArrowOrg, 14.0f, ColorRgba{ 132u, 182u, 232u, 44u }, 1.0f);
        Surface.StrokeLine(ArrowOrg, ArrowTip, ColorRgba{ 194u, 224u, 252u, 235u }, 1.5f);
        Surface.FillCircle(ArrowTip, 2.2f, ColorRgba{ 222u, 240u, 255u, 255u });

        // Bottom overlay strip: sky state + oktas + optical depth + base altitude tag
        const int Oktas = static_cast<int>(std::clamp(std::lround(Clouds.CloudFraction * 8.0f), 0l, 8l));
        const char* SkyClass = (Oktas == 0) ? "Clear" : (Oktas <= 2) ? "Few" : (Oktas <= 4) ? "Scattered" : (Oktas <= 6) ? "Broken" : "Overcast";
        char OktaBuf[40]{}, BaseBuf[24]{};
        std::snprintf(OktaBuf, sizeof(OktaBuf), "%d/8 oktas · tau %.1f", Oktas, Clouds.ExtinctionMultiplier * 3.0f);
        std::snprintf(BaseBuf, sizeof(BaseBuf), "BASE %.0f M", Clouds.CloudFloorMeters);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { Swath.X + 12.0f, Swath.Bottom() - 34.0f }, 13.0f, ColorRgba{ 238u, 244u, 252u, 255u }, SkyClass);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,  { Swath.X + 12.0f, Swath.Bottom() - 18.0f }, 9.2f, ColorRgba{ 156u, 186u, 216u, 235u }, OktaBuf);
        const float BaseW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.6f, BaseBuf);
        const Rectangle2D BaseTag { Swath.Right() - 12.0f - BaseW - 12.0f, Swath.Bottom() - 24.0f, BaseW + 12.0f, 16.0f };
        Surface.FillRectangle(BaseTag, ColorRgba{ 26u, 42u, 62u, 215u }, 8.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { BaseTag.X + 6.0f, BaseTag.Y + 3.0f }, 8.6f, ColorRgba{ 196u, 224u, 252u, 255u }, BaseBuf);
        Surface.PopClip();
        Surface.StrokeRectangle(Swath, ColorRgba{ 138u, 184u, 232u, 36u }, 12.0f, 1.0f);

        // 2x2 stat tiles integrated inside the hero section
        Y = Swath.Bottom() + 10.0f;
        const float Gap = 6.0f;
        const float TileW = (BodyW - Gap) * 0.5f;
        const float TileH = 44.0f;
        char Cov[16]{}, Opt[16]{}, Flr[16]{}, Drf[16]{};
        std::snprintf(Cov, sizeof(Cov), "%.0f%%", Clouds.CloudFraction * 100.0f);
        std::snprintf(Opt, sizeof(Opt), "%.1f",   Clouds.ExtinctionMultiplier * 3.0f);
        std::snprintf(Flr, sizeof(Flr), "%.0f m", Clouds.CloudFloorMeters);
        std::snprintf(Drf, sizeof(Drf), "%.1f m/s", Clouds.WindSpeedMetersPerSecond);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX,               Y,               TileW, TileH }, "COVERAGE", Cov);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + TileW + Gap, Y,               TileW, TileH }, "OPTICAL",  Opt);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX,               Y + TileH + Gap, TileW, TileH }, "BASE",     Flr);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + TileW + Gap, Y + TileH + Gap, TileW, TileH }, "DRIFT",    Drf);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Visible", InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "clouds", "weather" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 210.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud coverage");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Pct[16]{}; std::snprintf(Pct, sizeof(Pct), "%.0f", Clouds.CloudFraction * 100.0f);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Pct, "%", "Horizontal sky cloud fraction");
        float PctVal = Clouds.CloudFraction * 100.0f;
        bool Changed = false;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y + 4.0f, BodyW, 3101u, "Fraction", "%", "%.0f", 0.0f, 100.0f, PctVal, ActiveDragId, Input, Changed);
        if (Changed) { Clouds.CloudFraction = PctVal * 0.01f; OutMutated = true; }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        UpdateCloudDeckSurface(Clouds);
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 390.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud altitude");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char Km[16]{}; std::snprintf(Km, sizeof(Km), "%.2f", Clouds.CloudFloorMeters * 0.001f);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Km, "km", "World Z altitude  ·  vertical section");
            const Rectangle2D DeckBox { LeftX + PadX, Y + 4.0f, BodyW, 176.0f };
            CloudDeckPreview.Present(Surface, DeckBox, 8.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DeckBox.X + 8.0f, DeckBox.Y + 6.0f }, 8.5f, InspectorMetrics::MutedInk, "CLOUD DECK  ·  VERTICAL SECTION");
            Y = DeckBox.Bottom() + 14.0f;
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3201u, "Floor altitude", "m", "%.0f", 100.0f, 12000.0f, Clouds.CloudFloorMeters, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 276.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Layer thickness");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char Km[16]{}; std::snprintf(Km, sizeof(Km), "%.2f", Clouds.LayerThicknessMeters * 0.001f);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Km, "km", "Vertical development  ·  density profile");
            std::array<float, 24u> Profile{};
            for (std::size_t I = 0u; I < Profile.size(); ++I)
            {
                const float H = static_cast<float>(I) / 23.0f;
                Profile[I] = std::clamp(std::sin(H * PiF) * (1.0f - 0.35f * H) + Clouds.AnvilTopSpread * std::pow(H, 4.0f) * 0.55f, 0.0f, 1.0f);
            }
            InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 74.0f }, Profile, ColorRgba{ 222u, 232u, 246u, 255u }, ColorRgba{ 196u, 212u, 232u, 42u }, "Floor", "Top");
            Y += 86.0f;
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3202u, "Thickness", "m", "%.0f", 100.0f, 6500.0f, Clouds.LayerThicknessMeters, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else if (ActivePage == 3u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 444.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud body");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3301u, "Density", "x", "%.2f", 0.0f, 4.0f, Clouds.ExtinctionMultiplier, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3302u, "Feature Scale", "x", "%.2f", 0.2f, 3.0f, Clouds.FeatureScaleMultiplier, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3303u, "Anisotropy", "g", "%.2f", -0.9f, 0.9f, Clouds.ForwardScatterAnisotropy, ActiveDragId, Input, OutMutated);
        static constexpr std::array<const char*, 6u> Types = {{ "Cumulus", "Stratus", "Stratocumulus", "Cumulonimbus", "Cirrus", "Altostratus" }};
        static constexpr std::array<GeometricRaster::CloudCategoryType, 6u> TypeEnums = {{
            GeometricRaster::CloudCategoryType::Cumulus,
            GeometricRaster::CloudCategoryType::Stratus,
            GeometricRaster::CloudCategoryType::Stratocumulus,
            GeometricRaster::CloudCategoryType::Cumulonimbus,
            GeometricRaster::CloudCategoryType::Cirrus,
            GeometricRaster::CloudCategoryType::Altostratus,
        }};
        uint32_t ActiveType = 0u;
        for (uint32_t I = 0u; I < TypeEnums.size(); ++I) if (Clouds.Category == TypeEnums[I]) { ActiveType = I; break; }
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Preset", Types, ActiveType, 2u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 6) { GeometricRaster::VolumetricCloudSequence::ApplyCloudCategoryPreset(Clouds, TypeEnums[static_cast<std::size_t>(Chosen)]); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3304u, "Anvil", "", "%.2f", 0.0f, 1.0f, Clouds.AnvilTopSpread, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3305u, "Ceiling", "m", "%.0f", 4000.0f, 22000.0f, Clouds.DeckCeilingMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 4u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 306.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud wind");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Follow wind", Clouds.FollowWindEnabled, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3401u, "Wind speed", "m/s", "%.1f", 0.0f, 40.0f, Clouds.WindSpeedMetersPerSecond, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3402u, "Wind bearing", "deg", "%.0f", 0.0f, 360.0f, Clouds.WindBearingDegrees, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3403u, "Wind shear", "/km", "%.2f", 0.0f, 2.0f, Clouds.WindShearPerKilometer, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3404u, "Turbulence", "", "%.2f", 0.0f, 1.0f, Clouds.WindTurbulence, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 272.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Quality & cloud shadows");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        float Steps = static_cast<float>(Clouds.CloudMarchSteps);
        bool StepsChanged = false;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3501u, "March steps", "", "%.0f", 16.0f, 128.0f, Steps, ActiveDragId, Input, StepsChanged);
        if (StepsChanged) { Clouds.CloudMarchSteps = static_cast<uint32_t>(std::lround(Steps)); OutMutated = true; }
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Cloud shadows", Clouds.CloudShadow.ShadowEnabled, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3502u, "Shadow density", "x", "%.2f", 0.0f, 3.0f, Clouds.CloudShadow.ShadowDensityMultiplier, ActiveDragId, Input, OutMutated, !Clouds.CloudShadow.ShadowEnabled);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3503u, "Shadow fraction", "", "%.2f", 0.0f, 1.0f, Clouds.CloudShadow.ShadowFraction, ActiveDragId, Input, OutMutated, !Clouds.CloudShadow.ShadowEnabled);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
