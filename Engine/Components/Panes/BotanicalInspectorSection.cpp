/*============================================================================================================================================
                                                     BOTANICALINSPECTORSECTION.CPP
============================================================================================================================================*/
// 🧩 Full 8-Tab Native Frontier Botanical Inspector (`Overview | Shape | Detail | Material | UV | SDF | LOD | Wind`)
//    with 100% HTML-demo parameter parity (Trees, Palms, Ferns) and Frontier Geometry-style UV, SDF, and LOD tabs.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/BotanicalInspectorSection.h"
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ExclusiveSelector.h"
#include "Engine/Components/Controls/ScalarSlider.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace BotanicalInspectorSection
{

float Construct(
    PixelSpace&                                           Surface,
    const Rectangle2D                                     PaneBounds,
    GeometrySpace::Botanical::BotanicalSpeciesDefinition& Species,
    const BotanicalInspectorRuntimeStats&                 Stats,
    BotanicalInspectorEditState&                          State,
    const ControlPointer&                                 Pointer,
    const float                                           Opacity) noexcept
{
    using namespace GeometrySpace::Botanical;

    State.Changed    = false;
    State.TabChanged = false;
    const auto& Colors = ControlAppearance::Colors();

    if (!Pointer.Down)
    {
        State.ActiveSliderIndex = -1;
    }

    const float padX = ControlMetrics::SectionPadding;
    const float x    = PaneBounds.X + padX;
    const float w    = std::max(160.0f, PaneBounds.Width - 2.0f * padX);
    float       y    = PaneBounds.Y + 12.0f;

    // 1. Header + Exact 8-Tab Geometry-Style Segmented Page Strip (Overview | Shape | Detail | Material | UV | SDF | LOD | Wind)
    y = ControlAppearance::ConstructSectionHeading(
        Surface, x, y, w,
        Species.CommonName.empty() ? "Botanical Asset" : Species.CommonName.c_str(),
        "Welded genus-0 botanical manifold  |  .botanical",
        Colors.Text, Colors.TextDim, Opacity);
    y += 8.0f;

    {
        static constexpr const char* kTopTabs[4] = { "1 Overview", "2 Shape", "3 Detail", "4 Material" };
        static constexpr const char* kBotTabs[4] = { "5 UV", "6 SDF", "7 LOD", "8 Wind" };
        const uint32_t curTab = static_cast<uint32_t>(State.ActiveTab);

        uint32_t pickTop = (curTab < 4u) ? curTab : 99u;
        const Rectangle2D row0 { x, y, w, 30.0f };
        if (CapsuleButton::ConstructSegmented(Surface, row0, kTopTabs, 4u, pickTop, Pointer, pickTop, Opacity).Pressed && pickTop < 4u)
        {
            State.ActiveTab  = static_cast<BotanicalInspectorTab>(pickTop);
            State.TabChanged = true;
        }
        y = row0.Bottom() + 6.0f;

        uint32_t pickBot = (curTab >= 4u) ? (curTab - 4u) : 99u;
        const Rectangle2D row1 { x, y, w, 30.0f };
        if (CapsuleButton::ConstructSegmented(Surface, row1, kBotTabs, 4u, pickBot, Pointer, pickBot, Opacity).Pressed && pickBot < 4u)
        {
            State.ActiveTab  = static_cast<BotanicalInspectorTab>(4u + pickBot);
            State.TabChanged = true;
        }
        y = row1.Bottom() + 14.0f;
    }

    auto emitFloat = [&](const int32_t id, const char* label, const char* fmt, const char* unit,
                         float& value, const float minV, const float maxV) noexcept
    {
        const Rectangle2D row = ControlAppearance::ConstructLabeledRow(Surface, x, y, w, label, Colors.TextDim, Opacity);
        char numBuf[32] {};
        std::snprintf(numBuf, sizeof(numBuf), fmt, static_cast<double>(value));
        const bool dragging = (State.ActiveSliderIndex == id);
        float updated = value;
        const auto c = ScalarSlider::ConstructWithReadout(
            Surface, row, numBuf, unit, minV, maxV, value, dragging, Pointer, updated, Opacity);
        if (c.Pressed) State.ActiveSliderIndex = id;
        if ((c.Pressed || dragging) && updated != value)
        {
            value = updated;
            State.Changed = true;
        }
        y = row.Bottom() + 12.0f;
    };

    auto emitUint = [&](const int32_t id, const char* label, const char* unit,
                        uint32_t& value, const uint32_t minV, const uint32_t maxV) noexcept
    {
        float fVal = static_cast<float>(value);
        emitFloat(id, label, "%.0f", unit, fVal, static_cast<float>(minV), static_cast<float>(maxV));
        const uint32_t clamped = static_cast<uint32_t>(std::clamp(std::lround(fVal), static_cast<long>(minV), static_cast<long>(maxV)));
        if (clamped != value)
        {
            value = clamped;
            State.Changed = true;
        }
    };

    auto emitToggle = [&](const char* label, bool& value) noexcept
    {
        const float toggleX = x + w - ControlMetrics::SwitchWidth;
        const Rectangle2D labelRect { x, y, w - ControlMetrics::SwitchWidth - 10.0f, ControlMetrics::SwitchHeight };
        ControlAppearance::TextLeading(Surface, labelRect, 0.0f, Colors.TextDim.Faded(Opacity), label, 12.5f);
        if (ExclusiveSelector::Construct(Surface, toggleX, y, value, Pointer, Opacity).Pressed)
        {
            value = !value;
            State.Changed = true;
        }
        y += ControlMetrics::SwitchHeight + 12.0f;
    };

    switch (State.ActiveTab)
    {
        case BotanicalInspectorTab::Overview:
        {
            const Rectangle2D row = ControlAppearance::ConstructLabeledRow(Surface, x, y, w, "Growth Form", Colors.TextDim, Opacity);
            static constexpr const char* kForms[3] = { "Fern", "Palm", "Tree" };
            const uint32_t curForm = static_cast<uint32_t>(Species.GrowthForm);
            uint32_t pick = curForm;
            if (CapsuleButton::ConstructSegmented(Surface, row, kForms, 3u, curForm, Pointer, pick, Opacity).Pressed && pick < 3u && pick != curForm)
            {
                Species.GrowthForm = static_cast<BotanicalGrowthForm>(pick);
                State.Changed = true;
            }
            y = row.Bottom() + 14.0f;

            char summary[128] {};
            std::snprintf(summary, sizeof(summary), "Manifold V-E+F=%d  |  Open=%u  |  Welds=%u  |  Tris=%u",
                          Stats.EulerChar, Stats.BoundaryEdges, Stats.WeldedJunctions, Stats.TriangleCount);
            y = ControlAppearance::ConstructSectionHeading(Surface, x, y, w, "Welded Manifold Status", summary, Colors.Text, Colors.TextDim, Opacity);
            break;
        }

        case BotanicalInspectorTab::Shape:
        {
            if (Species.GrowthForm == BotanicalGrowthForm::Tree)
            {
                // FULL HTML-demo Tree Shape tab (13 controls — Welded Tree Architecture + Limbs/Forks/Crown + Terrain-Welded Roots!)
                emitUint (100, "Mesh Detail Tier",      "",    Species.Tree.DetailTier,      1u, 4u);
                emitFloat(101, "Tree Height",           "%.2f","m",   Species.Trunk.HeightMeters,   4.0f,  24.0f);
                emitFloat(102, "Trunk Base Radius",     "%.3f","m",   Species.Trunk.BaseRadiusMeters, 0.12f, 1.40f);
                emitFloat(103, "Basal Trunk Flare",     "%.2f","x",   Species.Trunk.FlareRatio,     1.00f,  2.80f);
                emitUint (104, "Branch Recursion",      "",    Species.Tree.BranchLevels,    2u, 4u);
                emitUint (105, "Scaffold Limbs",        "",    Species.Tree.LimbCount,       5u, 28u);
                emitFloat(106, "Emergence Angle",       "%.0f","deg", Species.Tree.BranchAngleDeg,  25.0f, 82.0f);
                emitFloat(107, "Y-Fork Tendency",       "%.2f","",    Species.Tree.ForkTendency,    0.00f, 0.85f);
                emitFloat(108, "Canopy Spread",         "%.2f","",    Species.Tree.CanopyFlatten,   0.00f, 1.00f);
                emitUint (109, "Primary Surface Roots", "",    Species.Roots.Count,          0u, 9u);
                emitFloat(110, "Root Radial Spread",    "%.2f","m",   Species.Roots.SpreadMeters,   0.80f, 6.00f);
                emitFloat(111, "Buttress Lobe Flutes",  "%.2f","",    Species.Tree.ButtressRelief,  0.00f, 0.75f);
            }
            else if (Species.GrowthForm == BotanicalGrowthForm::Palm)
            {
                // FULL HTML-demo Palm Shape tab (11 controls — Trunk Architecture + Phyllotactic Frond Crown!)
                emitUint (200, "Mesh Detail Tier",      "",    Species.Palm.DetailTier,        1u, 4u);
                emitFloat(201, "Trunk Height",          "%.2f","m",   Species.Trunk.HeightMeters,     3.0f,  16.0f);
                emitFloat(202, "Trunk Radius",          "%.2f","m",   Species.Trunk.BaseRadiusMeters, 0.12f,  0.45f);
                emitFloat(203, "Trade-Wind Lean",       "%.0f","deg", Species.Palm.TrunkLeanDeg,      0.0f,  32.0f);
                emitFloat(204, "Phototropic Sweep",     "%.0f","deg", Species.Palm.TrunkCurveDeg,     0.0f,  42.0f);
                emitUint (205, "Aerial Y-Fork Levels",  "",    Species.Palm.ForkLevels,        0u, 2u);
                emitUint (206, "Fronds per Crown",      "",    Species.Palm.FrondCount,       10u, 56u);
                emitFloat(207, "Frond Length",          "%.2f","m",   Species.Palm.FrondLengthMeters, 1.4f,   5.2f);
                emitFloat(208, "Frond Span Width",      "%.2f","m",   Species.Palm.FrondWidthMeters,  0.6f,   1.8f);
                emitFloat(209, "Rachis Arch Droop",     "%.0f","deg", Species.Palm.FrondArchDeg,     20.0f, 115.0f);
                emitFloat(210, "Dry Petticoat Skirt",   "%.2f","",    Species.Palm.SkirtFraction,    0.00f,  0.48f);
            }
            else
            {
                // FULL HTML-demo Fern Shape tab (16 controls — Rosette, Frond Stature, Pinnae & Rhizome Crown!)
                emitUint (300, "Mesh Detail Tier",      "",    Species.Fern.DetailTier,          1u, 4u);
                emitUint (301, "Frond Count",           "",    Species.Fern.FrondCount,          6u, 40u);
                emitFloat(302, "Frond Length",          "%.2f","m",   Species.Fern.FrondLengthMeters,   0.30f, 1.80f);
                emitFloat(303, "Length Variance",       "%.2f","",    Species.Fern.FrondLengthVar,      0.00f, 0.40f);
                emitFloat(304, "Frond Width",           "%.2f","m",   Species.Fern.FrondWidthMeters,    0.08f, 0.45f);
                emitFloat(305, "Stipe Fraction",        "%.2f","",    Species.Fern.StipeFraction,       0.05f, 0.55f);
                emitFloat(306, "Rosette Angle",         "%.0f","deg", Species.Fern.RosetteAngleDeg,    15.0f, 72.0f);
                emitFloat(307, "Arch Droop",            "%.0f","deg", Species.Fern.ArchDroopDeg,       15.0f,120.0f);
                emitFloat(308, "Arch Power",            "%.2f","",    Species.Fern.ArchPower,           0.80f, 2.50f);
                emitFloat(309, "Rachis Twist",          "%.0f","deg", Species.Fern.RachisTwistDeg,      0.0f, 35.0f);
                emitFloat(310, "Rachis Wave",           "%.0f","deg", Species.Fern.RachisWaveDeg,       0.0f, 25.0f);
                emitUint (311, "Pinna Pairs",           "",    Species.Fern.PinnaPairs,          8u, 48u);
                emitUint (312, "Pinnule Pairs",         "",    Species.Fern.PinnulePairs,        4u, 24u);
                emitFloat(313, "Pinna Angle",           "%.0f","deg", Species.Fern.PinnaAngleDeg,      30.0f, 90.0f);
                emitFloat(314, "Crown Radius",          "%.3f","m",   Species.Fern.CrownRadiusMeters,   0.02f, 0.25f);
                emitFloat(315, "Caudex Height",         "%.2f","m",   Species.Fern.CaudexHeightMeters,  0.01f, 0.60f);
            }
            break;
        }

        case BotanicalInspectorTab::Detail:
        {
            if (Species.GrowthForm == BotanicalGrowthForm::Tree)
            {
                // FULL HTML-demo Tree Detail tab (Bezier Collars & Pipe Law + 3D Sculpted Canopy Foliage!)
                emitFloat (400, "Collar Fillet Curve",  "%.2f","",  Species.Tree.CollarFillet,      0.25f, 0.98f);
                emitFloat (401, "Da Vinci Pipe Law",    "%.2f","",  Species.Tree.PipeConservation,  0.00f, 1.00f);
                emitUint  (402, "Lateral Rootlets",     "",         Species.Tree.RootLaterals,      0u,    4u);
                emitFloat (403, "Bark 3D FBM Relief",   "%.2f","",  Species.Trunk.BarkNoiseAmp,     0.00f, 0.12f);
                emitToggle("Show Canopy Leaves",                    Species.Tree.ShowLeaves);
                emitFloat (404, "Foliage Density",      "%.2f","x", Species.Tree.LeafDensity,       0.25f, 1.80f);
                emitFloat (405, "Leaf Blade Length",    "%.2f","m", Species.Tree.LeafSizeMeters,    0.08f, 0.28f);
            }
            else if (Species.GrowthForm == BotanicalGrowthForm::Palm)
            {
                // FULL HTML-demo Palm Detail tab (3D Infructescence + Pinnae, Crownshaft & Bole Relief!)
                emitToggle("Show Fruiting Bunches",                 Species.Palm.ShowFruits);
                emitUint  (410, "Axil / Spadix Bunches","",         Species.Palm.FruitBunches,      0u, 12u);
                emitFloat (411, "Fruit Abundance",      "%.2f","x", Species.Palm.FruitAbundance,    0.30f, 1.60f);
                emitFloat (412, "Fruit Ripeness",       "%.2f","",  Species.Palm.FruitRipeness,     0.00f, 1.00f);
                emitUint  (413, "Pinna Pairs / Pleats", "",         Species.Palm.LeafletPairs,     18u, 60u);
                emitFloat (414, "Plumose Multi-Plane",  "%.2f","",  Species.Palm.PlumoseSplay,      0.00f, 1.00f);
                emitFloat (415, "Crownshaft Ratio",     "%.2f","",  Species.Palm.CrownshaftRatio,   0.00f, 0.28f);
                emitFloat (416, "Basal Root Bole",      "%.2f","",  Species.Palm.BasalBole,         0.00f, 0.90f);
                emitUint  (417, "Welded Root Spurs",    "",         Species.Palm.RootButtresses,    0u,  8u);
                emitFloat (418, "Boot / Ring Relief",   "%.2f","",  Species.Palm.TrunkRelief,       0.00f, 1.00f);
            }
            else
            {
                // FULL HTML-demo Fern Detail tab (Venation, Margins/Auricles, Crosiers & Sori!)
                emitFloat(420, "Lamina Thickness",      "%.2f","mm",Species.Fern.LaminaThicknessMm, 0.20f, 1.40f);
                emitFloat(421, "Midrib Relief",         "%.2f","",  Species.Fern.MidribRelief,      0.00f, 1.50f);
                emitFloat(422, "Secondary Vein Relief", "%.2f","",  Species.Fern.VeinRelief,        0.00f, 1.50f);
                emitUint (423, "Vein Pairs",            "",         Species.Fern.VeinCount,         4u,   28u);
                emitFloat(424, "Mesophyll Bullation",   "%.2f","",  Species.Fern.Bullation,         0.00f, 1.00f);
                emitFloat(425, "Lamina Camber",         "%.2f","",  Species.Fern.LaminaCamber,     -0.50f, 0.80f);
                emitFloat(426, "Edge Undulation",       "%.2f","",  Species.Fern.EdgeUndulation,    0.00f, 1.00f);
                emitFloat(427, "Tip Curl",              "%.2f","",  Species.Fern.TipCurl,           0.00f, 1.00f);
                emitFloat(428, "Basal Auricle",         "%.2f","",  Species.Fern.Auricle,           0.00f, 1.00f);
                emitFloat(429, "Serration Depth",       "%.2f","",  Species.Fern.SerrationDepth,    0.00f, 0.45f);
                emitUint (430, "Crosier Count",         "",         Species.Fern.CrosierCount,      0u,   10u);
                emitFloat(431, "Crosier Turns",         "%.2f","",  Species.Fern.CrosierTurns,      1.00f, 4.00f);
                emitFloat(432, "Abaxial Sori Coverage", "%.2f","",  Species.Fern.SoriCoverage,      0.00f, 1.00f);
                emitFloat(433, "Outer Senescence",      "%.2f","",  Species.Fern.Senescence,        0.00f, 1.00f);
            }
            break;
        }

        case BotanicalInspectorTab::Material:
        {
            emitFloat(500, "Leaf Translucency (SSS)", "%.2f", "",  Species.Materials.Translucency,   0.00f, 1.00f);
            emitFloat(501, "Rest-Space Bark Freq",    "%.2f", "Hz",Species.Trunk.BarkNoiseFreq,      1.00f, 16.0f);
            emitFloat(502, "Blade Camber X",          "%.2f", "",  Species.Leaves.CamberX,           0.00f, 0.60f);
            emitFloat(503, "Blade Camber Z",          "%.2f", "",  Species.Leaves.CamberZ,           0.00f, 0.60f);
            break;
        }

        case BotanicalInspectorTab::UV:
        {
            // Geometry-matching UV Workspace Section (Dual UDIM 1001 Wood + 1002 Foliage)
            char uvSub[128] {};
            std::snprintf(uvSub, sizeof(uvSub), "Dual UDIM 1001 Wood + 1002 Foliage  |  %u islands  |  60 deg unwrap",
                          Stats.UvIslandCount);
            y = ControlAppearance::ConstructSectionHeading(Surface, x, y, w, "UV Preview", uvSub, Colors.Text, Colors.TextDim, Opacity);
            y += 10.0f;
            const Rectangle2D btnOpen { x, y, w, ControlMetrics::ControlHeight };
            (void)CapsuleButton::Construct(Surface, btnOpen, ButtonDescriptor{ "Open in UV editor", ButtonTone::Primary }, Pointer, Opacity);
            y = btnOpen.Bottom() + 8.0f;
            const Rectangle2D btnEdit { x, y, w, ControlMetrics::ControlHeight };
            (void)CapsuleButton::Construct(Surface, btnEdit, ButtonDescriptor{ "Edit in UV workspace", ButtonTone::Secondary }, Pointer, Opacity);
            y = btnEdit.Bottom() + 10.0f;
            break;
        }

        case BotanicalInspectorTab::SDF:
        {
            // Geometry-matching SDF Workspace Section (2.5D Thin-Leaf 64x64 EDT + 16^3 Canopy Bricks)
            char boundsBuf[128] {};
            std::snprintf(boundsBuf, sizeof(boundsBuf), "%u / %u 16^3 canopy bricks  |  64x64 EDT  |  GPU SDF active",
                          Stats.OccupiedSdfBricks, Stats.TotalSdfBricks);
            y = ControlAppearance::ConstructSectionHeading(Surface, x, y, w, "SDF Preview", boundsBuf, Colors.Text, Colors.TextDim, Opacity);
            y += 10.0f;
            emitFloat (600, "SDF Slice Depth",      "%.0f", "%",  State.SdfSliceDepthPct,     0.0f, 100.0f);
            emitFloat (601, "Cone-Width LOD Bias",  "%.2f", "x",  Species.Sdf.LodConeBias,    0.25f,  2.5f);
            emitFloat (602, "Thin-Leaf Half-Thick", "%.2f", "mm", Species.Leaves.HalfThicknessMm, 0.25f, 1.50f);
            emitToggle("Noisy Bark Capsules",                     Species.Sdf.UseNoisyCapsules);
            break;
        }

        case BotanicalInspectorTab::LOD:
        {
            // Geometry-matching LOD Workspace Section (4-Tier Botanical Cluster Pyramid + Beer-Lambert Far)
            char lodBuf[128] {};
            std::snprintf(lodBuf, sizeof(lodBuf), "4 Cluster Levels  |  %u LOD0 tris  |  %u clusters  |  C1 streaming",
                          Stats.TriangleCount, std::max(1u, Stats.ClusterCount));
            y = ControlAppearance::ConstructSectionHeading(Surface, x, y, w, "LOD Preview", lodBuf, Colors.Text, Colors.TextDim, Opacity);
            y += 10.0f;
            emitFloat(700, "Preview LOD Tier",      "%.0f", "",   State.PreviewLodTier,       0.0f, 3.0f);
            break;
        }

        case BotanicalInspectorTab::Wind:
        {
            emitFloat (800, "Wind Sensitivity",     "%.2f", "x",   Species.WindAndMotion.WindSensitivity,   0.05f, 2.00f);
            emitFloat (801, "Stem Stiffness",       "%.1f", "Nm",  Species.WindAndMotion.StemStiffness,     4.0f, 120.0f);
            emitFloat (802, "Stem Damping",         "%.2f", "",    Species.WindAndMotion.StemDamping,       0.20f, 8.00f);
            emitFloat (803, "Rustle Frequency",     "%.2f", "Hz",  Species.WindAndMotion.RustleFrequencyHz, 0.20f, 5.00f);
            emitFloat (804, "Detach Wind Speed",    "%.1f", "m/s", Species.Leaves.DetachWindMps,            6.0f, 30.0f);
            emitToggle("Detached Leaf Jolt Physics",               Species.Leaves.CanDetach);
            break;
        }
    }

    return y;
}

} // namespace BotanicalInspectorSection

} // namespace Frontier
