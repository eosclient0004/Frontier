/*============================================================================================================================================
                                                        NATIVEGEOMETRYINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Category 2 Native Engine Geometry Inspector (8 tabs matching cube__0..7)

#include "Engine/Editor/Inspectors/NativeGeometryInspector.h"
#include "Engine/Editor/Inspectors/GeometryInspectorShared.h"

namespace Frontier
{

void NativeGeometryInspector::UpdateSpecimenSurface(const NativeGeometryData& Instance)
{
    uint64_t Key = 0xA2020001ull ^ (static_cast<uint64_t>(Instance.TriangleCount) << 4u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.LodPixelError);

    SpecimenPreview.EnsureUpdated(Key, 96u, 96u, [&](std::vector<uint8_t>& Pixels)
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

float NativeGeometryInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, NativeGeometryData& Instance,
    GeometricRaster::WorldSceneSpecification& /*Scene*/,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    UpdateSpecimenSurface(Instance);
    GeometryUi::GeometrySummaryDesc Desc {};
    Desc.ShapeName     = "Native Mesh";
    Desc.TriangleCount = Instance.TriangleCount;
    Desc.ClusterCount  = Instance.ClusterCount;
    Desc.LodLevels     = std::max(1u, Instance.LodTierCount);
    return GeometryUi::ConstructGeometryPages(Surface, LeftX, TopY, InnerWidth, Entry, Desc, ActivePage, ActiveDragId, Input, OutMutated, OutPageSwitched);
}

} // namespace Frontier
