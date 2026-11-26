/*============================================================================================================================================
                                                    VIEWPORTSWAPCHAINPRESENTER.H
============================================================================================================================================*/
// 🧩 Standalone zero-ImGui Vulkan 1.2+ swapchain & offscreen presenter blitting rendered world viewports via ViewportSwapchainBlit.slang

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Windowing/PlatformWindow.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SwapchainPresentTelemetry
{
    uint32_t SurfaceWidthPx      = 0u;    // [px]    - Active swapchain or offscreen surface width
    uint32_t SurfaceHeightPx     = 0u;    // [px]    - Active swapchain or offscreen surface height
    uint64_t PresentedImageCount = 0ull;  // [count] - Total viewport images presented to swapchain or offscreen target
    bool     LiveWindowSwapchain = false; // [-]     - True when presenting directly to OS VkSwapchainKHR
    bool     VerticalSyncEnabled = true;  // [-]     - True when FIFO vertical sync is requested
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ViewportSwapchainPresenter
{
public:
    ViewportSwapchainPresenter() = default;
    ~ViewportSwapchainPresenter();

    ViewportSwapchainPresenter(const ViewportSwapchainPresenter&)            = delete;
    ViewportSwapchainPresenter& operator=(const ViewportSwapchainPresenter&) = delete;

    [[nodiscard]] bool InitializePresenter(
        VulkanInterchange*                TargetDevice,
        const std::filesystem::path&      CompiledShaderDirectory,
        Windowing::DesktopPlatformWindow* OptionalDesktopWindow,
        uint32_t                          InitialWidthPx      = 1280u,
        uint32_t                          InitialHeightPx     = 720u,
        bool                              EnableVerticalSync  = true,
        std::string*                      OptionalDiagnostic  = nullptr);

    void TeardownPresenter() noexcept;

    [[nodiscard]] bool PresentImageView(
        VkImageView SourceImageView,
        uint32_t    TargetWidthPx,
        uint32_t    TargetHeightPx);

    [[nodiscard]] bool PresentRgba8Buffer(
        std::span<const uint8_t> PixelsRgba8,
        uint32_t                 SourceWidthPx,
        uint32_t                 SourceHeightPx,
        uint32_t                 TargetWidthPx,
        uint32_t                 TargetHeightPx);

    [[nodiscard]] bool CapturePresentedRgba8(
        std::vector<uint8_t>& OutRgbaPixels,
        uint32_t&             OutWidthPx,
        uint32_t&             OutHeightPx);

    [[nodiscard]] bool QueryReady() const noexcept
    {
        return BoundDevice != nullptr && BlitRasterUnit != VK_NULL_HANDLE;
    }

    [[nodiscard]] const SwapchainPresentTelemetry& QueryTelemetry() const noexcept
    {
        return Telemetry;
    }

    [[nodiscard]] const std::string& QueryLastDiagnostic() const noexcept
    {
        return LastDiagnostic;
    }

private:
    static constexpr std::size_t PresentSlotCount = 2u;

    [[nodiscard]] bool ConstructBlitResources(VkFormat TargetFormat, std::string* OptionalDiagnostic);
    [[nodiscard]] bool RebuildTargetAttachments(uint32_t WidthPx, uint32_t HeightPx);
    void               DestroyTargetAttachments() noexcept;
    [[nodiscard]] bool EnsureUploadImage(uint32_t WidthPx, uint32_t HeightPx);

    VulkanInterchange*                BoundDevice        = nullptr;
    Windowing::DesktopPlatformWindow* DesktopWindow      = nullptr;
    std::filesystem::path             ShaderDirectory    {};

    VkSurfaceKHR                      WindowSurface      = VK_NULL_HANDLE;
    VkSwapchainKHR                    SwapchainHandle    = VK_NULL_HANDLE;
    VkFormat                          ActiveColorFormat  = VK_FORMAT_R8G8B8A8_UNORM;
    std::vector<VkImage>              SwapchainImages    {};
    std::vector<VkImageView>          SwapchainViews     {};
    std::vector<VkFramebuffer>        TargetAttachments  {};
    DeviceImageResource               OffscreenTarget    {};

    VkRenderPass                      BlitRenderUnit     = VK_NULL_HANDLE;
    VkDescriptorSetLayout             DescriptorLayout   = VK_NULL_HANDLE;
    VkDescriptorPool                  DescriptorAllocator = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, PresentSlotCount> BlitDescriptors { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkPipelineLayout                  BlitLayout         = VK_NULL_HANDLE;
    VkPipeline                        BlitRasterUnit     = VK_NULL_HANDLE;
    VkSampler                         LinearSampler      = VK_NULL_HANDLE;

    DeviceBufferResource              TransferBuffer     {};
    DeviceImageResource               UploadedColorImage {};

    std::array<VkSemaphore, PresentSlotCount>     AcquireSemaphores  { VK_NULL_HANDLE, VK_NULL_HANDLE };
    std::vector<VkSemaphore>                      RenderDoneSemaphores {};
    std::array<VkFence, PresentSlotCount>         PresentSyncSignals { VK_NULL_HANDLE, VK_NULL_HANDLE };
    std::array<VkCommandBuffer, PresentSlotCount> InFlightCommands   { VK_NULL_HANDLE, VK_NULL_HANDLE };
    std::vector<VkFence>                          ImageSyncSignals   {};

    uint32_t                  ActiveSlotIndex         = 0u;
    uint32_t                  LastPresentedImageIndex = 0u;
    SwapchainPresentTelemetry Telemetry               {};
    std::string               LastDiagnostic          {};
};

} // namespace Frontier::GeometricRaster
