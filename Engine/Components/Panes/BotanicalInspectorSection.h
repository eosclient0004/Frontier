/*============================================================================================================================================
                                                      BOTANICALINSPECTORSECTION.H
============================================================================================================================================*/
// 🧩 Full 8-Tab Native Frontier Botanical Inspector matching Frontier's Geometry Inspector (`Overview | Shape | Detail | Material | UV | SDF | LOD | Wind`)
//    and exposing 100% of the HTML botanical generator parameters (Trees, Palms, Ferns) via Frontier's native `ScalarSlider::ConstructWithReadout`,
//    `CapsuleButton::ConstructSegmented`, `ExclusiveSelector::Construct`, and `InspectorPrimitives::{UV, SDF, LOD}` workspace sections!

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/GeometrySpace/Procedural/Botanical/BotanicalSpeciesSchema.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class BotanicalInspectorTab : uint32_t
{
    Overview = 0u, // Welded manifold checks (V-E+F=2), footprint volume, quick switches
    Shape    = 1u, // Full HTML-demo Shape tab (Architecture / Limbs / Roots / Fronds)
    Detail   = 2u, // Full HTML-demo Detail tab (Bezier Collars / Canopy Leaves / Fruits / Venation)
    Material = 3u, // Bark, leaf base/tip, fruit pigments, translucency & roughness
    UV       = 4u, // Dual UDIM 1001 (Wood) + 1002 (Foliage) UV island packing & UV workspace links
    SDF      = 5u, // 2.5D Thin-Leaf 64x64 EDT + Noisy Wood Capsule SDF + 16^3 Canopy Bricks
    LOD      = 6u, // 4-Tier Cluster / Botanical LOD Pyramid + Cone-Width Beer-Lambert Extinction
    Wind     = 7u, // WorldPartition 16m C1 GPU<->CPU + 4m Falling/Sleeping/Waking Leaf Subcells
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct BotanicalInspectorRuntimeStats
{
    uint32_t TriangleCount     = 0u;
    uint32_t ClusterCount      = 0u;
    uint32_t OccupiedSdfBricks = 0u;
    uint32_t TotalSdfBricks    = 4096u;
    uint32_t WeldedJunctions   = 0u;
    int32_t  EulerChar         = 2;
    uint32_t BoundaryEdges     = 0u;
    uint32_t UvIslandCount     = 24u;
};

struct BotanicalInspectorEditState
{
    BotanicalInspectorTab ActiveTab         = BotanicalInspectorTab::Shape;
    int32_t               ActiveSliderIndex = -1;
    float                 SdfSliceDepthPct  = 50.0f;
    float                 PreviewLodTier    = 0.0f;
    bool                  Changed           = false;
    bool                  TabChanged        = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace BotanicalInspectorSection
{

/// Renders the full 8-tab Frontier Botanical Inspector using native Frontier controls (`ScalarSlider::ConstructWithReadout`,
/// `CapsuleButton::ConstructSegmented`, `ExclusiveSelector::Construct`) and Geometry-matching UV / SDF / LOD tabs.
/// Returns the final vertical cursor Y after the last constructed section.
[[nodiscard]] float Construct(
    PixelSpace&                                                 Surface,
    Rectangle2D                                                 PaneBounds,
    GeometrySpace::Botanical::BotanicalSpeciesDefinition&       Species,
    const BotanicalInspectorRuntimeStats&                       Stats,
    BotanicalInspectorEditState&                                State,
    const ControlPointer&                                       Pointer,
    float                                                       Opacity = 1.0f) noexcept;

} // namespace BotanicalInspectorSection

} // namespace Frontier
