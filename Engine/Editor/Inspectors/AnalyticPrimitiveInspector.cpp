/*============================================================================================================================================
                                                      ANALYTICPRIMITIVEINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Category 1 Analytical Primitive Inspector (8 tabs matching cube__0..7)

#include "Engine/Editor/Inspectors/AnalyticPrimitiveInspector.h"
#include "Engine/Editor/Inspectors/GeometryInspectorShared.h"

#include <array>

namespace Frontier
{

void AnalyticPrimitiveInspector::UpdatePrimitiveSurface(const AnalyticPrimitiveData& Instance)
{
    uint64_t Key = 0xA1010001ull ^ (static_cast<uint64_t>(Instance.FormIndex) << 8u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.RadiusMeters);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.HeightMeters);

    PrimitivePreview.EnsureUpdated(Key, 96u, 96u, [&](std::vector<uint8_t>& Pixels)
    {
        for (uint32_t Y = 0u; Y < 96u; ++Y)
        {
            for (uint32_t X = 0u; X < 96u; ++X)
            {
                const std::size_t Off = (static_cast<std::size_t>(Y) * 96u + X) * 4u;
                Pixels[Off + 0u] = 36u; Pixels[Off + 1u] = 38u; Pixels[Off + 2u] = 44u; Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float AnalyticPrimitiveInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, AnalyticPrimitiveData& Instance,
    GeometricRaster::WorldSceneSpecification& /*Scene*/,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    UpdatePrimitiveSurface(Instance);
    static constexpr std::array<const char*, 7u> ShapeNames = {{
        "Sphere", "Cube", "Cylinder", "Cone", "Torus", "Capsule", "Plane"
    }};
    GeometryUi::GeometrySummaryDesc Desc {};
    Desc.ShapeName    = ShapeNames[std::min(Instance.FormIndex, 6u)];
    Desc.WidthMeters  = std::max(0.1f, Instance.RadiusMeters * 2.0f);
    Desc.HeightMeters = std::max(0.1f, Instance.HeightMeters);
    Desc.DepthMeters  = std::max(0.1f, Instance.RadiusMeters * 2.0f);
    return GeometryUi::ConstructGeometryPages(Surface, LeftX, TopY, InnerWidth, Entry, Desc, ActivePage, ActiveDragId, Input, OutMutated, OutPageSwitched);
}

} // namespace Frontier
