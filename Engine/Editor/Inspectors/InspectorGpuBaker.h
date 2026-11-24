/*============================================================================================================================================
                                                          INSPECTORGPUBAKER.H
============================================================================================================================================*/
// 🧩 Pure-GPU Vulkan compute baker for Inspector previews & LUTs (InspectorPreviewCompute.comp.spv) with persistent zero-churn buffers

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Frontier
{

class InspectorGpuBaker
{
public:
    static bool     AttachDevice(GeometricRaster::VulkanInterchange* Device, const std::filesystem::path& ShaderDirectory);
    static void     DetachDevice() noexcept;

    [[nodiscard]] static bool     QueryReady() noexcept;
    [[nodiscard]] static bool     DispatchBake(
        const InspectorGpuBakeSpec& Spec,
        uint32_t                    WidthPx,
        uint32_t                    HeightPx,
        std::vector<uint8_t>&       OutRgba8);

    [[nodiscard]] static uint64_t QueryGpuBakeCount() noexcept;
    [[nodiscard]] static uint64_t QueryCpuFallbackCount() noexcept;
    [[nodiscard]] static float    QueryLastGpuBakeMs() noexcept;
};

} // namespace Frontier
