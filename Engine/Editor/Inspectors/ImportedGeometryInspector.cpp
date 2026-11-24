/*============================================================================================================================================
                                                       IMPORTEDGEOMETRYINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Category 3 Imported Geometry Inspector (8 tabs matching cube__0..7)

#include "Engine/Editor/Inspectors/ImportedGeometryInspector.h"
#include "Engine/Editor/Inspectors/GeometryInspectorShared.h"

namespace Frontier
{

void ImportedGeometryInspector::UpdateExchangeSurface(const ImportedGeometryData& Instance)
{
    uint64_t Key = 0xA3030001ull ^ (static_cast<uint64_t>(Instance.ExchangeFormatIndex) << 8u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.ImportScaleFactor);

    ExchangePreview.EnsureUpdated(Key, 96u, 96u, [&](std::vector<uint8_t>& Pixels)
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

float ImportedGeometryInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, ImportedGeometryData& Instance,
    GeometricRaster::WorldSceneSpecification& /*Scene*/,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    UpdateExchangeSurface(Instance);
    GeometryUi::GeometrySummaryDesc Desc {};
    Desc.ShapeName = "Imported Mesh";
    return GeometryUi::ConstructGeometryPages(Surface, LeftX, TopY, InnerWidth, Entry, Desc, ActivePage, ActiveDragId, Input, OutMutated, OutPageSwitched);
}

} // namespace Frontier
