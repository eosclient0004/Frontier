/*============================================================================================================================================
                                                       CAMERASETTINGSINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Standalone multi-instance CameraSettingsInspector wired end-to-end to ViewportDisplaySettings -> WorldRenderConfiguration -> .slang shaders

#include "Engine/Editor/Inspectors/CameraSettingsInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

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

[[nodiscard]] float ComputeSampleBudget(const CameraSettingsData& S) noexcept
{
    const float ScaleMul = (S.RenderScalePercent / 100.0f) * (S.RenderScalePercent / 100.0f);
    const float GtaoWork = S.XeGtaoEnabled ? (S.XeGtaoSliceCount * S.XeGtaoStepsPerSide * 0.125f) : 0.0f;
    float CoreSamples = 1.0f;
    switch (S.RendererIndex)
    {
        case 0u:
            CoreSamples = (S.PbrShadowIndex == 2u ? (S.SmrtShadowRays * 0.5f) : 2.0f) + GtaoWork;
            break;
        case 1u:
            CoreSamples = (S.SdfConeSteps * 0.8f) + GtaoWork + 2.0f;
            break;
        case 2u:
            CoreSamples = (S.RayTracerIndex >= 1u)
                ? (S.PathTraceBatchSpp * S.PathTraceMaxBounces)
                : (S.SmrtShadowRays + S.PathTraceMaxBounces + GtaoWork + 2.0f);
            break;
        default:
            CoreSamples = 1.0f + (S.MatcapShadowIndex > 0u ? 1.0f : 0.0f);
            break;
    }
    return std::max(1.0f, CoreSamples * ScaleMul);
}

void ConstructXeGtaoSection(
    PixelSpace&             Surface,
    float                   LeftX,
    float&                  CursorY,
    float                   InnerWidth,
    CameraSettingsData&     Instance,
    uint32_t&               ActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept
{
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
    InspectorPrimitives::ConstructSectionSurface(Surface, S, "XeGTAO screen-space AO");
    InspectorPrimitives::ConstructTooltipTrigger(
        Surface, { LeftX + InnerWidth - PadX - 8.0f, CursorY + InspectorMetrics::SectionPadTop + 8.0f },
        "XeGTAO (.slang)",
        "Horizon-based cosine visibility integral in WorldViewportRadiometricCompute.slang. Controls slice count, horizon steps per side, world radius, and strength.",
        Input);

    float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
    Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable XeGTAO", Instance.XeGtaoEnabled, Input, OutMutated);
    const bool Disabled = !Instance.XeGtaoEnabled;
    Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8860u, "AO strength", "x", "%.2f", 0.0f, 2.0f, Instance.XeGtaoStrength, ActiveDragId, Input, OutMutated, Disabled);
    Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8861u, "AO slices", "", "%.0f", 2.0f, 8.0f, Instance.XeGtaoSliceCount, ActiveDragId, Input, OutMutated, Disabled);
    Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8862u, "AO steps / side", "", "%.0f", 2.0f, 8.0f, Instance.XeGtaoStepsPerSide, ActiveDragId, Input, OutMutated, Disabled);
    (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8863u, "Search radius", "m", "%.2f", 0.25f, 3.0f, Instance.XeGtaoSearchRadiusM, ActiveDragId, Input, OutMutated, Disabled);
    CursorY = S.Bottom() + InspectorMetrics::SectionGap;
}
} // namespace

void CameraSettingsInspector::UpdateRendererHeroSurface(const CameraSettingsData& Instance)
{
    uint64_t Sig = 0xC5010002ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.RendererIndex);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.MatcapChannelIndex);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.MatcapLightAngleDeg);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.ExposureGain);

    const uint32_t W = 240u, H = 148u;
    RendererHeroPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float CX = static_cast<float>(W) * 0.5f, CY = 68.0f, SphereR = 40.0f, FloorY = 120.0f;
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX = static_cast<float>(X) + 0.5f;
                float R = 0.028f, G = 0.030f, B = 0.035f;

                if (std::fabs(FY - FloorY) < 0.7f && FX > 16.0f && FX < 224.0f)
                {
                    R += 0.09f; G += 0.10f; B += 0.11f;
                }

                if (Instance.RendererIndex == 0u)
                {
                    const float EX = (FX - (CX + 18.0f)) / 42.0f, EY = (FY - FloorY) / 8.0f;
                    const float E2 = EX * EX + EY * EY;
                    if (E2 < 1.0f) { const float A = (1.0f - E2) * 0.65f; R *= (1.0f - A); G *= (1.0f - A); B *= (1.0f - A); }
                }
                else if (Instance.RendererIndex == 1u)
                {
                    const float EX = (FX - (CX + 12.0f)) / 48.0f, EY = (FY - FloorY) / 9.5f;
                    const float E  = std::sqrt(EX * EX + EY * EY);
                    if (std::fabs(E - 1.0f) < 0.08f || std::fabs(E - 0.66f) < 0.08f)
                    {
                        R += 0.14f; G += 0.42f; B += 0.28f;
                    }
                }
                else if (Instance.RendererIndex == 2u)
                {
                    const float D1 = std::fabs((FY - 22.0f) - 0.55f * (FX - 36.0f));
                    const float D2 = std::fabs((FY - 58.0f) + 0.62f * (FX - 102.0f));
                    if ((D1 < 1.0f && FX > 36.0f && FX < 104.0f && (static_cast<int>(X) % 7) < 4) ||
                        (D2 < 1.0f && FX > 100.0f && FX < 180.0f && (static_cast<int>(X) % 7) < 4))
                    {
                        R += 0.48f; G += 0.36f; B += 0.72f;
                    }
                }
                else
                {
                    const float EX = (FX - CX) / 36.0f, EY = (FY - FloorY) / 6.5f;
                    const float E2 = EX * EX + EY * EY;
                    if (E2 < 1.0f) { const float A = (1.0f - E2) * 0.45f; R *= (1.0f - A); G *= (1.0f - A); B *= (1.0f - A); }
                }

                const float NX = (FX - CX) / SphereR, NY = (CY - FY) / SphereR;
                const float S2 = NX * NX + NY * NY;
                if (S2 <= 1.0f)
                {
                    const float NZ = std::sqrt(std::max(0.0f, 1.0f - S2));
                    const float Key  = std::max(0.0f, -0.45f * NX + 0.58f * NY + 0.68f * NZ);
                    const float Spec = std::pow(std::max(0.0f, -0.30f * NX + 0.46f * NY + 0.83f * NZ), 26.0f);
                    const float Rim  = std::pow(1.0f - NZ, 3.0f);
                    if (Instance.RendererIndex == 0u)
                    {
                        R = 0.08f + Key * 0.56f + Spec * 0.95f;
                        G = 0.14f + Key * 0.72f + Spec * 0.98f;
                        B = 0.24f + Key * 0.96f + Spec * 1.00f;
                    }
                    else if (Instance.RendererIndex == 1u)
                    {
                        R = 0.08f + Key * 0.46f + Spec * 0.85f + Rim * 0.12f;
                        G = 0.22f + Key * 0.86f + Spec * 0.95f + Rim * 0.34f;
                        B = 0.16f + Key * 0.64f + Spec * 0.90f + Rim * 0.22f;
                    }
                    else if (Instance.RendererIndex == 2u)
                    {
                        R = 0.16f + Key * 0.66f + Spec * 1.05f + Rim * 0.30f;
                        G = 0.12f + Key * 0.54f + Spec * 0.95f + Rim * 0.20f;
                        B = 0.26f + Key * 0.94f + Spec * 1.15f + Rim * 0.48f;
                    }
                    else
                    {
                        R = 0.22f + Key * 0.64f + Rim * 0.28f;
                        G = 0.20f + Key * 0.59f + Rim * 0.26f;
                        B = 0.17f + Key * 0.52f + Rim * 0.23f;
                    }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float CameraSettingsInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, CameraSettingsData& Instance,
    ViewportDisplaySettings* DisplaySettings,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 4u> RendererLabels = {{ "A · PBR", "B · SDF", "C · Ray", "D · MatCap" }};
    static constexpr std::array<const char*, 4u> RendererNames  = {{ "OpenPBR Direct", "SDF Cone GI", "Ray / Path Tracer", "MatCap Clay" }};
    static constexpr std::array<const char*, 4u> RendererLines  = {{
        "OpenPBR direct + LTC + shadow lab · XeGTAO",
        "Global distance field + voxel cone GI + SDF shadows",
        "WideTree BVH4 GI & deterministic path tracer",
        "MatCap normal/roughness shading for surface form",
    }};
    static constexpr std::array<const char*, 5u> QualityLabels  = {{ "Low", "Med", "Std", "High", "Ultra" }};
    static constexpr std::array<const char*, 5u> ShadowAlgs     = {{ "Contact", "Plane PCSS", "SMRT", "PCF", "RayQuery" }};
    static constexpr std::array<const char*, 2u> ReflSources    = {{ "BVH4 Raytrace", "Sky Env" }};
    static constexpr std::array<const char*, 3u> TraceResLabels = {{ "Full", "1/2 Trace", "1/4 Trace" }};

    std::vector<const char*> DynamicPages {};
    switch (Instance.RendererIndex)
    {
        case 0u: DynamicPages = { "Overview", "Shadows", "Lighting", "AO", "Output" }; break;
        case 1u: DynamicPages = { "Overview", "Shadows", "GI", "AO", "Output" }; break;
        case 2u: DynamicPages = { "Overview", "Tracer", "GI", "Shadows", "AO", "Reflections", "Output" }; break;
        default: DynamicPages = { "Overview", "Shadows", "Lighting", "MatCap", "Output" }; break;
    }
    if (ActivePage >= DynamicPages.size()) ActivePage = 0u;

    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, DynamicPages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const uint32_t RIdx          = std::min<uint32_t>(Instance.RendererIndex, 3u);
    const float    BudgetSpp     = ComputeSampleBudget(Instance);
    const uint32_t OutputPageIdx = static_cast<uint32_t>(DynamicPages.size() - 1u);

    if (ActivePage == 0u)
    {
        UpdateRendererHeroSurface(Instance);
        const Rectangle2D HeroSec { LeftX, CursorY, InnerWidth, 326.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, HeroSec, "Render type", RendererNames[RIdx]);
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        RendererHeroPreview.Present(Surface, { LeftX + PadX, Y, BodyW, 148.0f }, 12.0f);
        Y += 156.0f;
        int32_t PickRen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Active render engine", RendererLabels, Instance.RendererIndex, 2u, Input, PickRen);
        if (PickRen >= 0 && PickRen < 4)
        {
            Instance.RendererIndex = static_cast<uint32_t>(PickRen);
            OutMutated = true;
        }
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, HeroSec.Bottom() - 24.0f }, BodyW, 10.0f, InspectorMetrics::MutedInk, RendererLines[RIdx]);
        CursorY = HeroSec.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D QualSec { LeftX, CursorY, InnerWidth, 232.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, QualSec, "Quality budget", QualityLabels[std::min<uint32_t>(Instance.QualityTierIndex, 4u)]);
        float QY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char SppBuf[20]{};
        std::snprintf(SppBuf, sizeof(SppBuf), "%.1f", BudgetSpp);
        QY = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, QY, SppBuf, "spp", "Live GPU shader sample budget");
        int32_t PickTier = -1;
        (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, QY, BodyW, "Master quality tier", QualityLabels, Instance.QualityTierIndex, 3u, Input, PickTier);
        if (PickTier >= 0 && PickTier < 5) { Instance.QualityTierIndex = static_cast<uint32_t>(PickTier); OutMutated = true; }
        CursorY = QualSec.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D SumSec { LeftX, CursorY, InnerWidth, 168.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, SumSec, RendererNames[RIdx], "Live .slang");
        float SY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char AoLine[48]{}, ScaleLine[48]{};
        std::snprintf(AoLine, sizeof(AoLine), "%s · %.0fx%.0f · %.2f m", Instance.XeGtaoEnabled ? "XeGTAO" : "Off", Instance.XeGtaoSliceCount, Instance.XeGtaoStepsPerSide, Instance.XeGtaoSearchRadiusM);
        std::snprintf(ScaleLine, sizeof(ScaleLine), "%.0f %% · exp %.2fx", Instance.RenderScalePercent, Instance.ExposureGain);
        SY = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, SY, BodyW, "Engine", RendererNames[RIdx]);
        SY = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, SY, BodyW, "AO",     AoLine);
        SY = InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, SY, BodyW, "Output", ScaleLine);
        (void)InspectorPrimitives::ConstructMetadataRow(Surface, LeftX + PadX, SY, BodyW, "Cluster LOD", Instance.ClusterLodEnabled ? "Active" : "Bypass");
        CursorY = SumSec.Bottom() + InspectorMetrics::SectionGap;

        static constexpr std::array<const char*, 2u> Tags = {{ "camera-settings", "shader-live" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == OutputPageIdx)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 312.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Camera output");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8810u, "Render scale", "%", "%.0f", 50.0f, 200.0f, Instance.RenderScalePercent, ActiveDragId, Input, OutMutated);
        int32_t PickRes = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Trace resolution", TraceResLabels, Instance.TraceResolutionIndex, 3u, Input, PickRes);
        if (PickRes >= 0) { Instance.TraceResolutionIndex = static_cast<uint32_t>(PickRes); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8811u, "Exposure gain", "x", "%.2f", 0.1f, 4.0f, Instance.ExposureGain, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Spatial shadow / AA denoise", Instance.SpatialAntiAliasing, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D LodS { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, LodS, "Cluster-LOD & debug");
        InspectorPrimitives::ConstructTooltipTrigger(
            Surface, { LeftX + InnerWidth - PadX - 8.0f, CursorY + InspectorMetrics::SectionPadTop + 8.0f },
            "Cluster-LOD (.slang)",
            "Drives WorldRenderConfiguration::NaniteLodEnabled, NanitePixelError, NaniteWireframeOverlay, and shader DebugChannel (0..22).",
            Input);
        float LY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        LY = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, LY, BodyW, "Cluster-LOD cut", Instance.ClusterLodEnabled, Input, OutMutated);
        LY = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, LY, BodyW, 8812u, "LOD pixel error", "px", "%.2f", 0.25f, 8.0f, Instance.ClusterLodPixelError, ActiveDragId, Input, OutMutated, !Instance.ClusterLodEnabled);
        LY = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, LY, BodyW, "Triangle wireframe overlay", Instance.ClusterWireframeOverlay, Input, OutMutated);
        float DebugF = static_cast<float>(Instance.DebugChannelIndex);
        bool DebugChanged = false;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, LY, BodyW, 8813u, "Shader debug channel", "", "%.0f", 0.0f, 22.0f, DebugF, ActiveDragId, Input, DebugChanged);
        if (DebugChanged) { Instance.DebugChannelIndex = static_cast<uint32_t>(std::clamp(std::lround(DebugF), 0l, 22l)); OutMutated = true; }
        CursorY = LodS.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (Instance.RendererIndex == 0u) // PBR pages: Shadows(1), Lighting(2), AO(3)
    {
        if (ActivePage == 1u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 342.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Solar shadow lab");
            InspectorPrimitives::ConstructTooltipTrigger(
                Surface, { LeftX + InnerWidth - PadX - 8.0f, CursorY + InspectorMetrics::SectionPadTop + 8.0f },
                "Shadow Lab (.slang)",
                "Selects SolarShadowAlgorithm in WorldViewportRadiometricCompute.slang. SMRT rays & march steps directly control Algorithm 2 quadratic raymarching.",
                Input);
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Shadow algorithm", ShadowAlgs, Instance.PbrShadowIndex, 3u, Input, Pick);
            if (Pick >= 0) { Instance.PbrShadowIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            const bool SmrtDisabled = (Instance.PbrShadowIndex != 2u);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8820u, "SMRT shadow rays", "", "%.0f", 1.0f, 8.0f, Instance.SmrtShadowRays, ActiveDragId, Input, OutMutated, SmrtDisabled);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8821u, "SMRT march steps", "", "%.0f", 4.0f, 16.0f, Instance.SmrtMarchSteps, ActiveDragId, Input, OutMutated, SmrtDisabled);
            (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Bilateral shadow filter", Instance.SpatialAntiAliasing, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 256.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "OpenPBR direct & reflections");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Specular reflections", Instance.PbrReflectionsEnabled, Input, OutMutated);
            int32_t PickRefl = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Reflection source", ReflSources, Instance.ReflectionSourceIndex, 2u, Input, PickRefl);
            if (PickRefl >= 0) { Instance.ReflectionSourceIndex = static_cast<uint32_t>(PickRefl); OutMutated = true; }
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8822u, "Sky fill gain", "x", "%.2f", 0.0f, 3.0f, Instance.PbrFillGain, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            ConstructXeGtaoSection(Surface, LeftX, CursorY, InnerWidth, Instance, ActiveDragId, Input, OutMutated);
        }
    }
    else if (Instance.RendererIndex == 1u) // SDF pages: Shadows(1), GI(2), AO(3)
    {
        if (ActivePage == 1u)
        {
            static constexpr std::array<const char*, 6u> SdfModes = {{ "Point SDF", "Cone", "Clipmap", "Cone GI", "Hybrid", "MC Ref" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 286.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "SDF penumbra & trace");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "SDF penumbra mode", SdfModes, Instance.SdfPenumbraIndex, 3u, Input, Pick);
            if (Pick >= 0) { Instance.SdfPenumbraIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            int32_t PickRes = -1;
            (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Trace resolution", TraceResLabels, Instance.TraceResolutionIndex, 3u, Input, PickRes);
            if (PickRes >= 0) { Instance.TraceResolutionIndex = static_cast<uint32_t>(PickRes); OutMutated = true; }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            static constexpr std::array<const char*, 2u> GiCands = {{ "Stokes Contour", "Surfel Lattice" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 304.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "GDF voxel cone GI");
            InspectorPrimitives::ConstructTooltipTrigger(
                Surface, { LeftX + InnerWidth - PadX - 8.0f, CursorY + InspectorMetrics::SectionPadTop + 8.0f },
                "Voxel Cone GI (.slang)",
                "Drives EvaluateGdfVoxelConeGi & EvaluateGdfGlossyCone step count and indirect bounce intensity in WorldViewportRadiometricCompute.slang.",
                Input);
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t PickCand = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "GI candidate", GiCands, Instance.GiCandidateIndex, 2u, Input, PickCand);
            if (PickCand >= 0) { Instance.GiCandidateIndex = static_cast<uint32_t>(PickCand); OutMutated = true; }
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8831u, "Cone march steps", "", "%.0f", 3.0f, 12.0f, Instance.SdfConeSteps, ActiveDragId, Input, OutMutated);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8832u, "GI indirect gain", "x", "%.2f", 0.0f, 2.5f, Instance.SdfGiIntensity, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8833u, "Sky fill gain", "x", "%.2f", 0.0f, 3.0f, Instance.SdfFillGain, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            ConstructXeGtaoSection(Surface, LeftX, CursorY, InnerWidth, Instance, ActiveDragId, Input, OutMutated);
        }
    }
    else if (Instance.RendererIndex == 2u) // Ray traced pages: Tracer(1), GI(2), Shadows(3), AO(4), Reflections(5)
    {
        if (ActivePage == 1u)
        {
            static constexpr std::array<const char*, 3u> Tracers = {{ "BVH4 GI", "PT Realtime", "PT Final" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 370.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Ray & path tracer");
            InspectorPrimitives::ConstructTooltipTrigger(
                Surface, { LeftX + InnerWidth - PadX - 8.0f, CursorY + InspectorMetrics::SectionPadTop + 8.0f },
                "Path Tracer (.slang)",
                "Controls WorldRenderEngine (3=HighEnd BVH4, 4=PT Realtime, 5=PT Final), samples/frame, convergence target spp (0=continuous), and max bounces in EvaluateProgressiveViewportPath.",
                Input);
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Tracer engine", Tracers, Instance.RayTracerIndex, 3u, Input, Pick);
            if (Pick >= 0) { Instance.RayTracerIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable BVH4 raytracing", Instance.RaytracingEnabled, Input, OutMutated);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8840u, "Samples / frame", "spp", "%.0f", 1.0f, 8.0f, Instance.PathTraceBatchSpp, ActiveDragId, Input, OutMutated);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8841u, "Target samples", "spp", "%.0f", 0.0f, 1024.0f, Instance.PathTraceTargetSpp, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8842u, "Max bounces", "", "%.0f", 1.0f, 8.0f, Instance.PathTraceMaxBounces, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            static constexpr std::array<const char*, 2u> GiCands = {{ "Stokes Contour", "Surfel Lattice" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 248.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Traced indirect GI");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t PickCand = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Dual-cascade GI", GiCands, Instance.GiCandidateIndex, 2u, Input, PickCand);
            if (PickCand >= 0) { Instance.GiCandidateIndex = static_cast<uint32_t>(PickCand); OutMutated = true; }
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8843u, "Max bounces", "", "%.0f", 1.0f, 8.0f, Instance.PathTraceMaxBounces, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8844u, "GI indirect gain", "x", "%.2f", 0.0f, 2.5f, Instance.SdfGiIntensity, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 3u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 304.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "BVH & SMRT shadows");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Shadow algorithm", ShadowAlgs, Instance.RayShadowIndex, 3u, Input, Pick);
            if (Pick >= 0) { Instance.RayShadowIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8845u, "SMRT shadow rays", "", "%.0f", 1.0f, 8.0f, Instance.SmrtShadowRays, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8846u, "SMRT march steps", "", "%.0f", 4.0f, 16.0f, Instance.SmrtMarchSteps, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 4u)
        {
            ConstructXeGtaoSection(Surface, LeftX, CursorY, InnerWidth, Instance, ActiveDragId, Input, OutMutated);
        }
        else
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 256.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Traced reflections");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable reflections", Instance.RayReflectionsEnabled, Input, OutMutated);
            int32_t PickRefl = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Reflection source", ReflSources, Instance.ReflectionSourceIndex, 2u, Input, PickRefl);
            if (PickRefl >= 0) { Instance.ReflectionSourceIndex = static_cast<uint32_t>(PickRefl); OutMutated = true; }
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8847u, "Max bounces", "", "%.0f", 1.0f, 8.0f, Instance.PathTraceMaxBounces, ActiveDragId, Input, OutMutated, !Instance.RayReflectionsEnabled);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else // MatCap pages: Shadows(1), Lighting(2), MatCap(3)
    {
        if (ActivePage == 1u)
        {
            static constexpr std::array<const char*, 3u> MSh = {{ "None", "Contact", "Soft" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 164.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "MatCap floor shadow");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Shadow mode", MSh, Instance.MatcapShadowIndex, 3u, Input, Pick);
            if (Pick >= 0) { Instance.MatcapShadowIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 192.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Clay key & rim");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8850u, "Key angle", "deg", "%.0f", 0.0f, 360.0f, Instance.MatcapLightAngleDeg, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8851u, "Rim highlight", "", "%.2f", 0.0f, 1.0f, Instance.MatcapRimGain, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            static constexpr std::array<const char*, 4u> Channels = {{ "Clay", "Ceramic", "Metallic", "Normal" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 224.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "MatCap channel");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Shading sphere", Channels, Instance.MatcapChannelIndex, 2u, Input, Pick);
            if (Pick >= 0) { Instance.MatcapChannelIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Triangle wireframe overlay", Instance.ClusterWireframeOverlay, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }

    if (OutMutated)
    {
        InspectorInstanceRegister::SyncCameraSettingsToDisplay(Instance, DisplaySettings);
    }

    return CursorY;
}

} // namespace Frontier
