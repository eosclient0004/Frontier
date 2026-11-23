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

namespace
{
inline float LocalHash2D(int X, int Y) noexcept
{
    uint32_t N = static_cast<uint32_t>(X) * 374761393u + static_cast<uint32_t>(Y) * 668265263u;
    N = (N ^ (N >> 13u)) * 1274126177u;
    return static_cast<float>((N ^ (N >> 16u)) & 0xFFFFu) / 65535.0f;
}

inline float LocalNoise2D(float X, float Y) noexcept
{
    const int IX = static_cast<int>(std::floor(X));
    const int IY = static_cast<int>(std::floor(Y));
    const float FX = X - static_cast<float>(IX);
    const float FY = Y - static_cast<float>(IY);
    const float UX = FX * FX * (3.0f - 2.0f * FX);
    const float UY = FY * FY * (3.0f - 2.0f * FY);
    const float A = LocalHash2D(IX, IY);
    const float B = LocalHash2D(IX + 1, IY);
    const float C = LocalHash2D(IX, IY + 1);
    const float D = LocalHash2D(IX + 1, IY + 1);
    return (A + (B - A) * UX) + ((C - A) + (A - B - C + D) * UX) * UY;
}

inline float LocalFbm(float X, float Y) noexcept
{
    float Sum = 0.0f, Amp = 0.52f, Freq = 1.0f;
    for (int Oct = 0; Oct < 4; ++Oct)
    {
        Sum += LocalNoise2D(X * Freq, Y * Freq) * Amp;
        Freq *= 2.05f;
        Amp  *= 0.50f;
    }
    return Sum;
}
} // namespace

void LocalCloudInspector::UpdateVolumePreviewSurface(const LocalCloudInstanceData& Instance)
{
    const auto& Vol = Instance.Volume;
    uint64_t Sig = 0x10CA7C10ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Vol.CloudFraction);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Vol.ExtinctionMultiplier);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Vol.EdgeSoftness);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Vol.HalfExtentMeters.X);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Vol.HalfExtentMeters.Y);

    const uint32_t W = 240u, H = 178u;
    VolumePreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float Aspect = std::clamp(Vol.HalfExtentMeters.X / std::max(1.0f, Vol.HalfExtentMeters.Y), 0.5f, 2.0f);
        const float HalfBW = (Aspect >= 1.0f) ? 0.30f : (0.30f * Aspect);
        const float HalfBH = (Aspect >= 1.0f) ? (0.28f / Aspect) : 0.28f;
        const float Cov    = std::clamp(Vol.CloudFraction, 0.0f, 1.0f);
        const float Soft   = std::clamp(0.06f + Vol.EdgeSoftness * 0.24f, 0.04f, 0.35f);
        const float Dens   = std::clamp(Vol.ExtinctionMultiplier * 0.45f, 0.15f, 1.15f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float V = static_cast<float>(Y) / static_cast<float>(H);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float U = static_cast<float>(X) / static_cast<float>(W);
                float R = 0.055f + (1.0f - V) * 0.022f;
                float G = 0.082f + (1.0f - V) * 0.032f;
                float B = 0.125f + (1.0f - V) * 0.048f;
                if ((X > 4u && X < W - 4u && (X % 40u) == 0u) || (Y > 4u && Y < H - 4u && (Y % 44u) == 0u))
                {
                    R += 0.028f; G += 0.042f; B += 0.058f;
                }

                // Signed distance to rounded bounded cloud volume footprint
                const float DX = std::fabs(U - 0.50f) - HalfBW;
                const float DY = std::fabs(V - 0.44f) - HalfBH;
                const float BoxDist = std::max(DX, DY);
                const float Mask = std::clamp((-BoxDist) / Soft, 0.0f, 1.0f);
                if (Mask > 0.005f)
                {
                    const float N  = LocalFbm(U * 7.2f + 3.1f, V * 5.6f + 1.9f);
                    const float N2 = LocalFbm(U * 7.2f + 2.98f, V * 5.6f + 1.78f);
                    const float Thresh = 0.88f - Cov * 0.65f;
                    const float Cloud = std::clamp(((N - Thresh) / 0.28f) * Mask, 0.0f, 1.0f);
                    if (Cloud > 0.005f)
                    {
                        const float Smooth = Cloud * Cloud * (3.0f - 2.0f * Cloud);
                        const float Lit    = std::clamp(0.55f + (N - N2) * 3.8f, 0.25f, 1.0f);
                        const float Opac   = std::clamp(Smooth * (0.50f + 0.45f * Dens), 0.0f, 0.94f);
                        R = R * (1.0f - Opac) + (0.68f + 0.28f * Lit) * Opac;
                        G = G * (1.0f - Opac) + (0.76f + 0.22f * Lit) * Opac;
                        B = B * (1.0f - Opac) + (0.86f + 0.13f * Lit) * Opac;
                    }
                }
                // Crisp cyan bounding footprint outline
                if (std::fabs(BoxDist) < 0.006f && DX < 0.006f && DY < 0.006f)
                {
                    R = 0.55f; G = 0.78f; B = 0.98f;
                }
                if (V > 0.68f)
                {
                    const float Scrim = ((V - 0.68f) / 0.32f) * 0.55f;
                    R *= (1.0f - Scrim); G *= (1.0f - Scrim); B *= (1.0f - Scrim);
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = static_cast<uint8_t>(std::clamp(std::lround(R * 255.0f), 0l, 255l));
                Pixels[Off + 1u] = static_cast<uint8_t>(std::clamp(std::lround(G * 255.0f), 0l, 255l));
                Pixels[Off + 2u] = static_cast<uint8_t>(std::clamp(std::lround(B * 255.0f), 0l, 255l));
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

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
        UpdateVolumePreviewSurface(Instance);
        // 1:1 HTML Local cloud blueprint volumetric fBM schematic + integrated 2x2 stat tiles
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 336.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud volume", "LOCAL VOLUME · SCHEMATIC");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;

        const Rectangle2D Swath { LeftX + PadX, Y, BodyW, 178.0f };
        VolumePreview.Present(Surface, Swath, 12.0f);
        Surface.PushClip(Swath);

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
        UpdateVolumePreviewSurface(Instance);
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Volume coverage");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Pct[16]{}; std::snprintf(Pct, sizeof(Pct), "%.0f", Vol.CloudFraction * 100.0f);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Pct, "%", "Bounded cloud fill fraction");
        const Rectangle2D PreviewBox { LeftX + PadX, Y + 4.0f, BodyW, 128.0f };
        VolumePreview.Present(Surface, PreviewBox, 10.0f);
        Surface.StrokeRectangle(PreviewBox, ColorRgba{ 138u, 184u, 232u, 32u }, 10.0f, 1.0f);
        Y = PreviewBox.Bottom() + 12.0f;
        float PctVal = Vol.CloudFraction * 100.0f; bool Changed = false;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3601u, "Coverage", "%", "%.0f", 0.0f, 100.0f, PctVal, ActiveDragId, Input, Changed);
        if (Changed) { Vol.CloudFraction = PctVal * 0.01f; OutMutated = true; }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S1 { LeftX, CursorY, InnerWidth, 356.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S1, "Local bounds");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        // Dark vertical/footprint bounds cross-section diagram
        const Rectangle2D BoxWell { LeftX + PadX, Y, BodyW, 104.0f };
        Surface.FillRectangle(BoxWell, ColorRgba{ 19u, 21u, 26u, 255u }, 10.0f);
        Surface.PushClip(BoxWell);
        const float FloorY = BoxWell.Bottom() - 20.0f;
        Surface.StrokeLine({ BoxWell.X + 12.0f, FloorY }, { BoxWell.Right() - 12.0f, FloorY }, ColorRgba{ 255u, 255u, 255u, 42u }, 1.0f);
        const float MaxH = std::max(1000.0f, (Vol.VolumeCenterMeters.Z + Vol.HalfExtentMeters.Z) * 1.35f);
        const float TopY2 = FloorY - std::clamp((Vol.VolumeCenterMeters.Z + Vol.HalfExtentMeters.Z) / MaxH, 0.20f, 0.88f) * (BoxWell.Height - 30.0f);
        const float BotY2 = FloorY - std::clamp(std::max(0.0f, Vol.VolumeCenterMeters.Z - Vol.HalfExtentMeters.Z) / MaxH, 0.05f, 0.72f) * (BoxWell.Height - 30.0f);
        const float BoxW2 = std::clamp(Vol.HalfExtentMeters.X / 400.0f, 0.25f, 0.72f) * (BoxWell.Width - 28.0f);
        const Rectangle2D VolR { BoxWell.Center().X - BoxW2 * 0.5f, TopY2, BoxW2, std::max(14.0f, BotY2 - TopY2) };
        Surface.FillRectangle(VolR, ColorRgba{ 184u, 214u, 246u, 52u }, 6.0f);
        Surface.StrokeRectangle(VolR, ColorRgba{ 168u, 212u, 252u, 195u }, 6.0f, 1.2f);
        char AltLab[32]{};
        std::snprintf(AltLab, sizeof(AltLab), "Base %.0f m · Depth %.0f m", std::max(0.0f, Vol.VolumeCenterMeters.Z - Vol.HalfExtentMeters.Z), Vol.HalfExtentMeters.Z * 2.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BoxWell.X + 10.0f, BoxWell.Bottom() - 14.0f }, 8.0f, InspectorMetrics::DimInk, AltLab);
        Surface.PopClip();
        Surface.StrokeRectangle(BoxWell, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
        Y = BoxWell.Bottom() + 12.0f;

        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3610u, "Extent X", "m", "%.0f", 10.0f, 4000.0f, Vol.HalfExtentMeters.X, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3611u, "Extent Y", "m", "%.0f", 10.0f, 4000.0f, Vol.HalfExtentMeters.Y, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3612u, "Extent Z", "m", "%.0f", 10.0f, 2000.0f, Vol.HalfExtentMeters.Z, ActiveDragId, Input, OutMutated);
        CursorY = S1.Bottom() + InspectorMetrics::SectionGap;
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
