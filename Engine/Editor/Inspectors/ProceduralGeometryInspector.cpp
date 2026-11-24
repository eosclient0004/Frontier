/*============================================================================================================================================
                                                      PROCEDURALGEOMETRYINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Category 4 Procedural Geometry Inspector (8 tabs matching cube__0..7)

#include "Engine/Editor/Inspectors/ProceduralGeometryInspector.h"
#include "Engine/Editor/Inspectors/GeometryInspectorShared.h"

namespace Frontier
{

void ProceduralGeometryInspector::UpdateProceduralSurface(const ProceduralGeometryData& Instance)
{
    uint64_t Key = 0xA4040001ull ^ (static_cast<uint64_t>(Instance.GeneratorIndex) << 8u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.FeatureScaleMeters);

    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::GeometrySpecimenPreview, 3u,
        { static_cast<float>(Instance.GeneratorIndex), Instance.FeatureScaleMeters, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.68f, 0.64f, 0.56f, 1.0f }
    };
    ProceduralPreview.EnsureUpdatedGpu(Key, 96u, 96u, GpuSpec, [&](std::vector<uint8_t>& Pixels)
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

float ProceduralGeometryInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, ProceduralGeometryData& Instance,
    GeometricRaster::WorldSceneSpecification& /*Scene*/,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    UpdateProceduralSurface(Instance);
    GeometryUi::GeometrySummaryDesc Desc {};
    Desc.ShapeName = "Procedural Mesh";
    return GeometryUi::ConstructGeometryPages(Surface, LeftX, TopY, InnerWidth, Entry, Desc, ActivePage, ActiveDragId, Input, OutMutated, OutPageSwitched);
}

} // namespace Frontier
