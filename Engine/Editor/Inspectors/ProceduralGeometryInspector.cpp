/*============================================================================================================================================
                                                     PROCEDURALGEOMETRYINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone ProceduralGeometryInspector (Category 4 Geometry: Sweep, Terrain, Boolean, Spline, Voronoi generator)

#include "Engine/Editor/Inspectors/ProceduralGeometryInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr std::array<const char*, 5u> GeneratorLabels = {{
    "Parametric Sweep", "Terrain Field", "Boolean Solid", "Spline Surface", "Voronoi Partition",
}};

inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}

void SyncMaterialToScene(const GeometrySurfaceBinding& Binding, GeometricRaster::WorldSceneSpecification& Scene) noexcept
{
    if (Scene.MaterialRegister.QuerySlotCount() == 0u) return;
    const uint16_t Slot = std::min<uint16_t>(Binding.MaterialSlotOrdinal, static_cast<uint16_t>(Scene.MaterialRegister.QuerySlotCount() - 1u));
    auto Authored = Scene.MaterialRegister.InspectAuthoredMaterial(Slot);
    Authored.Diffuse.BaseColor  = Binding.AlbedoTint;
    Authored.Specular.Roughness = Binding.Roughness;
    Authored.Specular.Metalness = Binding.Metallic;
    Scene.MaterialRegister.UpdateMaterialSlot(Slot, Authored);
}
} // namespace

void ProceduralGeometryInspector::UpdateProceduralSurface(const ProceduralGeometryData& Instance)
{
    uint64_t Sig = 0x90CE0004ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.GeneratorIndex);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.RandomSeed);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.SubdivisionLevel);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.BuildRevision);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.FeatureScaleMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.AmplitudeMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.AlbedoTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.AlbedoTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.AlbedoTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.Roughness);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Surface.Metallic);

    const uint32_t W = 224u, H = 148u;
    ProceduralPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float CX = static_cast<float>(W) * 0.5f, CY = static_cast<float>(H) * 0.54f;
        const float SeedPhase = static_cast<float>(Instance.RandomSeed % 360u) * 0.08f;
        const float Freq      = std::clamp(2.2f / std::max(0.4f, Instance.FeatureScaleMeters), 0.4f, 4.5f);
        const float Amp       = std::clamp(Instance.AmplitudeMeters, 0.15f, 3.0f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX = static_cast<float>(X) + 0.5f;
                const float DX = FX - CX, DY = FY - CY;
                const float RadDist = std::sqrt(DX * DX + DY * DY) / (static_cast<float>(W) * 0.65f);
                float OutR = std::max(0.03f, 0.11f - RadDist * 0.07f);
                float OutG = std::max(0.03f, 0.13f - RadDist * 0.07f);
                float OutB = std::max(0.04f, 0.16f - RadDist * 0.08f);

                if (Instance.GeneratorIndex == 1u) // Terrain heightfield ridges
                {
                    const float U = (FX / static_cast<float>(W)) * 6.28f * Freq + SeedPhase;
                    const float Ridge = 78.0f - (std::sin(U) * 18.0f + std::sin(U * 2.1f + 0.8f) * 9.0f + std::cos(U * 3.7f) * 4.5f) * std::min(1.6f, Amp);
                    if (FY >= Ridge)
                    {
                        const float Depth = std::clamp((FY - Ridge) / 68.0f, 0.0f, 1.0f);
                        const float Slope = 0.55f + 0.45f * std::cos(U);
                        OutR = Instance.Surface.AlbedoTint.R * (0.24f + 0.58f * Slope) * (1.0f - Depth * 0.45f);
                        OutG = Instance.Surface.AlbedoTint.G * (0.28f + 0.60f * Slope) * (1.0f - Depth * 0.40f);
                        OutB = Instance.Surface.AlbedoTint.B * (0.26f + 0.52f * Slope) * (1.0f - Depth * 0.35f);
                        if (std::fabs(FY - Ridge) < 1.3f) { OutR += 0.20f; OutG += 0.36f; OutB += 0.28f; }
                    }
                }
                else // Parametric sweep / boolean / spline / voronoi shaded solid with contour lines
                {
                    const float Angle = std::atan2(DY, DX);
                    const float ModR  = 40.0f + std::sin(Angle * static_cast<float>(3u + (Instance.GeneratorIndex % 4u)) + SeedPhase) * (7.5f * std::min(1.4f, Amp));
                    const float Dist  = std::sqrt(DX * DX + DY * DY);
                    if (Dist <= ModR)
                    {
                        const float NX = (DX / ModR) * 0.85f;
                        const float NY = (DY / ModR) * 0.85f;
                        const float NZ = std::sqrt(std::max(0.06f, 1.0f - NX * NX - NY * NY));
                        const float NDotL = std::max(0.0f, -0.50f * NX - 0.56f * NY + 0.66f * NZ);
                        const float Rim   = std::pow(1.0f - NZ, 2.5f) * 0.38f;
                        OutR = Instance.Surface.AlbedoTint.R * (0.20f + 0.80f * NDotL) + Rim * 0.40f;
                        OutG = Instance.Surface.AlbedoTint.G * (0.20f + 0.80f * NDotL) + Rim * 0.78f;
                        OutB = Instance.Surface.AlbedoTint.B * (0.20f + 0.80f * NDotL) + Rim * 0.58f;
                        const float IsoStep = std::max(6.0f, 18.0f - static_cast<float>(Instance.SubdivisionLevel) * 1.4f);
                        if (std::fabs(std::fmod(Dist, IsoStep)) < 0.9f)
                        {
                            OutR += 0.09f; OutG += 0.16f; OutB += 0.14f;
                        }
                    }
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(OutR); Pixels[Off + 1u] = ToneSrgb8(OutG); Pixels[Off + 2u] = ToneSrgb8(OutB); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float ProceduralGeometryInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, ProceduralGeometryData& Instance,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Generator", "Transform", "Material", "Physics" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    const char* GenLabel = GeneratorLabels[std::min<std::size_t>(Instance.GeneratorIndex, GeneratorLabels.size() - 1u)];

    if (ActivePage == 0u)
    {
        CursorY = InspectorPrimitives::ConstructDualStatusBoxes(
            Surface, LeftX, CursorY, InnerWidth,
            "Checks passed", "2", ColorRgba{ 112u, 212u, 142u, 255u }, true,
            "Warnings",      "0", InspectorMetrics::MutedInk, true);

        const float DimX = std::max(0.01f, Instance.FeatureScaleMeters * 2.0f * std::fabs(Entry.ScaleX));
        const float DimY = std::max(0.01f, Instance.AmplitudeMeters    * 2.0f * std::fabs(Entry.ScaleY));
        const float DimZ = std::max(0.01f, Instance.FeatureScaleMeters * 2.0f * std::fabs(Entry.ScaleZ));
        const float VolM3 = DimX * DimY * DimZ;
        const Rectangle2D FootSec { LeftX, CursorY, InnerWidth, 168.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, FootSec, "Footprint", GenLabel);
        float Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;
        const long WholeV = static_cast<long>(std::floor(VolM3));
        const int  FracV  = static_cast<int>(std::lround((VolM3 - static_cast<float>(WholeV)) * 100.0f)) % 100;
        char VWhole[20]{}, VFrac[8]{};
        std::snprintf(VWhole, sizeof(VWhole), "%ld", WholeV);
        std::snprintf(VFrac,  sizeof(VFrac),  ".%02d", FracV);
        Y = InspectorPrimitives::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, VWhole, VFrac, "m\xC2\xB3", "Generated world bounding volume");
        Y += 6.0f;
        const float Gap = 6.0f;
        const float TileW = (BodyW - Gap * 2.0f) / 3.0f;
        char WBuf[16]{}, HBuf[16]{}, DBuf[16]{};
        std::snprintf(WBuf, sizeof(WBuf), "%.2f m", DimX);
        std::snprintf(HBuf, sizeof(HBuf), "%.2f m", DimY);
        std::snprintf(DBuf, sizeof(DBuf), "%.2f m", DimZ);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX,                      Y, TileW, 46.0f }, "WIDTH",  WBuf);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + TileW + Gap,        Y, TileW, 46.0f }, "HEIGHT", HBuf);
        InspectorPrimitives::ConstructStatTile(Surface, { LeftX + PadX + (TileW + Gap) * 2.0f, Y, TileW, 46.0f }, "DEPTH",  DBuf);
        CursorY = FootSec.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D CompSec { LeftX, CursorY, InnerWidth, 174.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, CompSec, "Components", "3");
        Y = CursorY + InspectorMetrics::SectionPadTop + 26.0f;
        char GenMeta[40]{};
        std::snprintf(GenMeta, sizeof(GenMeta), "Seed %u · subdiv %u", Instance.RandomSeed, Instance.SubdivisionLevel);
        bool HitGen = false, HitMat = false, HitPhys = false;
        Y = InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, Y, BodyW, "Procedural generator", GenMeta, true, Input, HitGen);
        Y = InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, Y, BodyW, "OpenPBR surface", Instance.Surface.Metallic > 0.5f ? "Metallic" : "Dielectric", true, Input, HitMat);
        (void)InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, Y, BodyW, "Rigid body & collision", Entry.DynamicMotion ? "Dynamic" : "Static", false, Input, HitPhys);
        if (HitGen) { ActivePage = 1u; OutPageSwitched = true; }
        else if (HitMat) { ActivePage = 3u; OutPageSwitched = true; }
        else if (HitPhys) { ActivePage = 4u; OutPageSwitched = true; }
        CursorY = CompSec.Bottom() + InspectorMetrics::SectionGap;

        UpdateProceduralSurface(Instance);
        const Rectangle2D IconSec { LeftX, CursorY, InnerWidth, 102.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, IconSec, "Icon capture");
        const float IconY = CursorY + InspectorMetrics::SectionPadTop + 26.0f;
        ProceduralPreview.Present(Surface, { LeftX + PadX, IconY, 54.0f, 44.0f }, 8.0f);
        const Rectangle2D CapBtn { LeftX + PadX + 64.0f, IconY + 6.0f, BodyW - 64.0f, 32.0f };
        Surface.FillRectangle(CapBtn, ColorRgba{ 32u, 33u, 37u, 255u }, 10.0f);
        Surface.StrokeRectangle(CapBtn, ColorRgba{ 255u, 255u, 255u, 20u }, 10.0f, 1.0f);
        const char* CapLabel = "Capture viewport icon";
        const float CLW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.5f, CapLabel);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { CapBtn.Center().X - CLW * 0.5f, CapBtn.Y + 9.0f }, 10.5f, WorkspaceSurfaceColors::PrimaryInk, CapLabel);
        CursorY = IconSec.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Visible",        InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled, false },
            { "Cast shadows",   InspectorSwitchSymbol::Shadow,  Entry.CastShadows,       false },
            { "Auto rebuild",   InspectorSwitchSymbol::Cycle,   Instance.AutoRebuild,    false },
            { "Dynamic motion", InspectorSwitchSymbol::Physics, Entry.DynamicMotion,     false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Entry.CastShadows = !Entry.CastShadows; OutMutated = true; }
        else if (Clicked == 2) { Instance.AutoRebuild = !Instance.AutoRebuild; OutMutated = true; }
        else if (Clicked == 3) { Entry.DynamicMotion = !Entry.DynamicMotion; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "procedural", "parametric" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        char RevBuf[24]{};
        std::snprintf(RevBuf, sizeof(RevBuf), "Rev %u", Instance.BuildRevision);
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 536.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Procedural generator", RevBuf);
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        bool ParamChanged = false;
        int32_t PickGen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Generator mode", GeneratorLabels, Instance.GeneratorIndex, 2u, Input, PickGen);
        if (PickGen >= 0) { Instance.GeneratorIndex = static_cast<uint32_t>(PickGen); ParamChanged = true; }
        float SeedF = static_cast<float>(Instance.RandomSeed);
        bool SeedChanged = false;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7801u, "Random seed", "", "%.0f", 0.0f, 999.0f, SeedF, ActiveDragId, Input, SeedChanged);
        if (SeedChanged) { Instance.RandomSeed = static_cast<uint32_t>(std::clamp(std::lround(SeedF), 0l, 999l)); ParamChanged = true; }
        float SubF = static_cast<float>(Instance.SubdivisionLevel);
        bool SubChanged = false;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7802u, "Subdivision", "", "%.0f", 1.0f, 8.0f, SubF, ActiveDragId, Input, SubChanged);
        if (SubChanged) { Instance.SubdivisionLevel = static_cast<uint32_t>(std::clamp(std::lround(SubF), 1l, 8l)); ParamChanged = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7803u, "Chord tolerance", "mm", "%.2f", 0.05f, 10.0f, Instance.ChordToleranceMm,   ActiveDragId, Input, ParamChanged);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7804u, "Feature scale",   "m",  "%.2f", 0.20f, 50.0f, Instance.FeatureScaleMeters, ActiveDragId, Input, ParamChanged);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7805u, "Amplitude",       "m",  "%.2f", 0.05f, 20.0f, Instance.AmplitudeMeters,    ActiveDragId, Input, ParamChanged);
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Auto rebuild", Instance.AutoRebuild, Input, OutMutated);
        if (ParamChanged)
        {
            if (Instance.AutoRebuild) ++Instance.BuildRevision;
            OutMutated = true;
        }
        if (InspectorPrimitives::ConstructSectionActionButton(Surface, LeftX + PadX, Y + 4.0f, BodyW, "Rebuild procedural geometry", Input, true))
        {
            ++Instance.BuildRevision;
            OutMutated = true;
        }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 7820u, ActiveDragId, Input, OutMutated);
    }
    else if (ActivePage == 3u)
    {
        bool MatChanged = false;
        CursorY = InspectorPrimitives::ConstructMaterialSection(Surface, LeftX, CursorY, InnerWidth, Instance.Surface, 7840u, ActiveDragId, Input, MatChanged);
        if (MatChanged) { SyncMaterialToScene(Instance.Surface, Scene); OutMutated = true; }
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructPhysicsSection(Surface, LeftX, CursorY, InnerWidth, Entry, "Procedural SDF", Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
