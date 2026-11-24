/*============================================================================================================================================
                                                         INSPECTORGPUBAKER.CPP
============================================================================================================================================*/
// 🧩 Implementation of pure-GPU Vulkan compute Inspector baker (zero per-bake memory churn, automatic fallback when headless)

#include "Engine/Editor/Inspectors/InspectorGpuBaker.h"
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

namespace Frontier
{

namespace
{

struct alignas(16) InspectorPreviewUniformStd140
{
    uint32_t DimensionsAndMode[4];
    float    ParamA[4];
    float    ParamB[4];
    float    ColourA[4];
    float    ColourB[4];
};

static_assert(sizeof(InspectorPreviewUniformStd140) == 80u, "InspectorPreviewUniformStd140 must be 80 bytes.");

struct InspectorGpuBakerState
{
    std::mutex                                Mutex {};
    GeometricRaster::VulkanInterchange*       Device = nullptr;
    GeometricRaster::ComputeDispatchEncoder   Encoder {};
    uint32_t                                  ProgramHandle = UINT32_MAX;
    GeometricRaster::DeviceBufferResource     UniformBuffer {};
    GeometricRaster::DeviceBufferResource     OutputBuffer {};
    uint64_t                                  OutputCapacityBytes = 0ull;
    std::atomic<uint64_t>                     GpuBakeCount { 0ull };
    std::atomic<uint64_t>                     CpuFallbackCount { 0ull };
    std::atomic<uint32_t>                     LastGpuBakeMicros { 0u };
};

InspectorGpuBakerState& AccessState() noexcept
{
    static InspectorGpuBakerState State {};
    return State;
}

void ReleaseStateBuffers(InspectorGpuBakerState& S) noexcept
{
    if (S.Device != nullptr && S.Device->Ready())
    {
        S.Device->ReleaseBuffer(S.UniformBuffer);
        S.Device->ReleaseBuffer(S.OutputBuffer);
    }
    S.UniformBuffer       = {};
    S.OutputBuffer        = {};
    S.OutputCapacityBytes = 0ull;
}

} // namespace

bool InspectorGpuBaker::AttachDevice(
    GeometricRaster::VulkanInterchange* Device,
    const std::filesystem::path&        ShaderDirectory)
{
    DetachDevice();
    if (Device == nullptr || !Device->Ready())
    {
        return false;
    }

    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);

    const std::filesystem::path SpvPath = ShaderDirectory / "InspectorPreviewCompute.comp.spv";
    if (!std::filesystem::exists(SpvPath))
    {
        return false;
    }

    if (!S.Encoder.InitializeEncoder(Device))
    {
        return false;
    }

    const std::array<VkDescriptorType, 2u> SlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    S.ProgramHandle = S.Encoder.RegisterProgram(SpvPath, SlotTypes, "ComputeEntry");
    if (S.ProgramHandle == UINT32_MAX)
    {
        S.Encoder.TeardownEncoder();
        return false;
    }

    S.UniformBuffer = S.Encoder.AllocateHostWritableBuffer(sizeof(InspectorPreviewUniformStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    if (S.UniformBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        S.Encoder.TeardownEncoder();
        S.ProgramHandle = UINT32_MAX;
        return false;
    }

    S.Device = Device;

    InspectorPreviewSurface::AssignGpuBakeHook(
        [](const InspectorGpuBakeSpec& Spec, uint32_t WidthPx, uint32_t HeightPx, std::vector<uint8_t>& OutRgba8) -> bool
        {
            return InspectorGpuBaker::DispatchBake(Spec, WidthPx, HeightPx, OutRgba8);
        });
    return true;
}

void InspectorGpuBaker::DetachDevice() noexcept
{
    InspectorPreviewSurface::ClearGpuBakeHook();
    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    ReleaseStateBuffers(S);
    if (S.ProgramHandle != UINT32_MAX)
    {
        S.Encoder.TeardownEncoder();
        S.ProgramHandle = UINT32_MAX;
    }
    S.Device = nullptr;
}

bool InspectorGpuBaker::QueryReady() noexcept
{
    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    return S.Device != nullptr && S.Device->Ready() && S.ProgramHandle != UINT32_MAX;
}

bool InspectorGpuBaker::DispatchBake(
    const InspectorGpuBakeSpec& Spec,
    uint32_t                    WidthPx,
    uint32_t                    HeightPx,
    std::vector<uint8_t>&       OutRgba8)
{
    if (WidthPx == 0u || HeightPx == 0u)
    {
        return false;
    }

    auto& S = AccessState();
    std::lock_guard<std::mutex> Lock(S.Mutex);
    if (S.Device == nullptr || !S.Device->Ready() || S.ProgramHandle == UINT32_MAX)
    {
        S.CpuFallbackCount.fetch_add(1ull, std::memory_order_relaxed);
        return false;
    }

    const auto T0 = std::chrono::steady_clock::now();
    const uint64_t OutBytes = static_cast<uint64_t>(WidthPx) * HeightPx * 4ull;
    if (S.OutputBuffer.BufferHandle == VK_NULL_HANDLE || S.OutputCapacityBytes < OutBytes)
    {
        S.Device->ReleaseBuffer(S.OutputBuffer);
        const uint64_t AllocBytes = std::max<uint64_t>(OutBytes, 512ull * 512ull * 4ull);
        S.OutputBuffer = S.Encoder.AllocateReadbackStagingBuffer(AllocBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        S.OutputCapacityBytes = (S.OutputBuffer.BufferHandle != VK_NULL_HANDLE) ? AllocBytes : 0ull;
    }
    if (S.OutputBuffer.BufferHandle == VK_NULL_HANDLE || S.UniformBuffer.HostAddress == nullptr)
    {
        S.CpuFallbackCount.fetch_add(1ull, std::memory_order_relaxed);
        return false;
    }

    InspectorPreviewUniformStd140 Uni {};
    Uni.DimensionsAndMode[0] = WidthPx;
    Uni.DimensionsAndMode[1] = HeightPx;
    Uni.DimensionsAndMode[2] = static_cast<uint32_t>(Spec.Mode);
    Uni.DimensionsAndMode[3] = Spec.Flags;
    for (uint32_t I = 0u; I < 4u; ++I)
    {
        Uni.ParamA[I]  = Spec.ParamA[I];
        Uni.ParamB[I]  = Spec.ParamB[I];
        Uni.ColourA[I] = Spec.ColourA[I];
        Uni.ColourB[I] = Spec.ColourB[I];
    }
    std::memcpy(S.UniformBuffer.HostAddress, &Uni, sizeof(Uni));

    bool Ok = false;
    S.Encoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = S.Device->BeginSingleTimeCommands();
    if (Cmd != VK_NULL_HANDLE)
    {
        const std::array<GeometricRaster::ComputeSlotDescriptor, 2u> Slots {{
            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, S.UniformBuffer.BufferHandle, sizeof(InspectorPreviewUniformStd140) },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, S.OutputBuffer.BufferHandle,  OutBytes },
        }};
        const uint32_t GX = (WidthPx + 7u) / 8u;
        const uint32_t GY = (HeightPx + 7u) / 8u;
        if (S.Encoder.EncodeComputeDispatch(Cmd, S.ProgramHandle, Slots, GX, GY, 1u))
        {
            VkMemoryBarrier Barrier {};
            Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            Barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0u, 1u, &Barrier, 0u, nullptr, 0u, nullptr);
            const uint64_t SyncPoint = S.Device->DispatchCommandBufferAsync(Cmd);
            Ok = S.Device->AwaitTimelineTick(SyncPoint, 60000000000ull);
        }
    }

    if (Ok && S.OutputBuffer.HostAddress != nullptr)
    {
        OutRgba8.resize(static_cast<std::size_t>(OutBytes));
        std::memcpy(OutRgba8.data(), S.OutputBuffer.HostAddress, static_cast<std::size_t>(OutBytes));
        const auto Micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - T0).count();
        S.LastGpuBakeMicros.store(static_cast<uint32_t>(std::max<int64_t>(0, Micros)), std::memory_order_relaxed);
        S.GpuBakeCount.fetch_add(1ull, std::memory_order_relaxed);
        return true;
    }

    S.CpuFallbackCount.fetch_add(1ull, std::memory_order_relaxed);
    return false;
}

uint64_t InspectorGpuBaker::QueryGpuBakeCount() noexcept
{
    return AccessState().GpuBakeCount.load(std::memory_order_relaxed);
}

uint64_t InspectorGpuBaker::QueryCpuFallbackCount() noexcept
{
    return AccessState().CpuFallbackCount.load(std::memory_order_relaxed);
}

float InspectorGpuBaker::QueryLastGpuBakeMs() noexcept
{
    return static_cast<float>(AccessState().LastGpuBakeMicros.load(std::memory_order_relaxed)) * 0.001f;
}

} // namespace Frontier
