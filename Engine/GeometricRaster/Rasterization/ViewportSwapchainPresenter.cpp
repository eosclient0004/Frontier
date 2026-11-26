/*============================================================================================================================================
                                                   VIEWPORTSWAPCHAINPRESENTER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ViewportSwapchainPresenter driving ViewportSwapchainBlit.slang over OS VkSwapchainKHR or offscreen targets

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Rasterization/ViewportSwapchainPresenter.h"

#if !defined(__ANDROID__)
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Frontier::GeometricRaster
{

ViewportSwapchainPresenter::~ViewportSwapchainPresenter()
{
    TeardownPresenter();
}

bool ViewportSwapchainPresenter::InitializePresenter(
    VulkanInterchange*                TargetDevice,
    const std::filesystem::path&      CompiledShaderDirectory,
    Windowing::DesktopPlatformWindow* OptionalDesktopWindow,
    uint32_t                          InitialWidthPx,
    uint32_t                          InitialHeightPx,
    bool                              EnableVerticalSync,
    std::string*                      OptionalDiagnostic)
{
    TeardownPresenter();
    if (TargetDevice == nullptr || !TargetDevice->Ready())
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "ViewportSwapchainPresenter requires an initialized VulkanInterchange.";
        }
        return false;
    }

    BoundDevice                   = TargetDevice;
    DesktopWindow                 = OptionalDesktopWindow;
    ShaderDirectory               = CompiledShaderDirectory;
    Telemetry                     = {};
    Telemetry.VerticalSyncEnabled = EnableVerticalSync;
    const VkDevice LogicalVk      = BoundDevice->AccessDevice();

#if !defined(__ANDROID__)
    if (DesktopWindow != nullptr &&
        DesktopWindow->PlatformCategory() != Windowing::WindowPlatformCategory::GlfwNull &&
        DesktopWindow->PlatformCategory() != Windowing::WindowPlatformCategory::HeadlessStub &&
        DesktopWindow->NativeGlfwHandle() != nullptr &&
        vkDestroySurfaceKHR != nullptr &&
        vkCreateSwapchainKHR != nullptr)
    {
        auto* GlfwWin = static_cast<GLFWwindow*>(DesktopWindow->NativeGlfwHandle());
        if (glfwCreateWindowSurface(BoundDevice->AccessInstance(), GlfwWin, nullptr, &WindowSurface) == VK_SUCCESS)
        {
            VkBool32 PresentSupported = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR != nullptr)
            {
                vkGetPhysicalDeviceSurfaceSupportKHR(
                    BoundDevice->AccessPhysicalDevice(),
                    BoundDevice->QueryQueueOrdinal(),
                    WindowSurface,
                    &PresentSupported);
            }
            if (PresentSupported == VK_TRUE)
            {
                Telemetry.LiveWindowSwapchain = true;
            }
            else
            {
                vkDestroySurfaceKHR(BoundDevice->AccessInstance(), WindowSurface, nullptr);
                WindowSurface = VK_NULL_HANDLE;
            }
        }
    }
#endif

    ActiveColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    if (Telemetry.LiveWindowSwapchain && vkGetPhysicalDeviceSurfaceFormatsKHR != nullptr)
    {
        uint32_t FormatCount = 0u;
        vkGetPhysicalDeviceSurfaceFormatsKHR(BoundDevice->AccessPhysicalDevice(), WindowSurface, &FormatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> Formats(FormatCount);
        if (FormatCount > 0u)
        {
            vkGetPhysicalDeviceSurfaceFormatsKHR(BoundDevice->AccessPhysicalDevice(), WindowSurface, &FormatCount, Formats.data());
            ActiveColorFormat = Formats.front().format;
            for (const auto& Candidate : Formats)
            {
                if (Candidate.format == VK_FORMAT_B8G8R8A8_UNORM || Candidate.format == VK_FORMAT_R8G8B8A8_UNORM)
                {
                    ActiveColorFormat = Candidate.format;
                    break;
                }
            }
        }
    }

    if (Telemetry.LiveWindowSwapchain)
    {
        VkSemaphoreCreateInfo SemSpec {};
        SemSpec.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo SignalSpec {};
        SignalSpec.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        for (std::size_t Slot = 0u; Slot < PresentSlotCount; ++Slot)
        {
            (void)vkCreateSemaphore(LogicalVk, &SemSpec, nullptr, &AcquireSemaphores[Slot]);
            (void)vkCreateFence(LogicalVk, &SignalSpec, nullptr, &PresentSyncSignals[Slot]);
        }
    }

    if (!ConstructBlitResources(ActiveColorFormat, OptionalDiagnostic))
    {
        TeardownPresenter();
        return false;
    }

    if (!RebuildTargetAttachments(std::max(64u, InitialWidthPx), std::max(64u, InitialHeightPx)))
    {
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "Failed to initialize ViewportSwapchainPresenter target attachments.";
        }
        TeardownPresenter();
        return false;
    }

    return true;
}

bool ViewportSwapchainPresenter::ConstructBlitResources(VkFormat TargetFormat, std::string* OptionalDiagnostic)
{
    const VkDevice LogicalVk = BoundDevice->AccessDevice();

    VkSamplerCreateInfo SamplerSpec {};
    SamplerSpec.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SamplerSpec.magFilter    = VK_FILTER_LINEAR;
    SamplerSpec.minFilter    = VK_FILTER_LINEAR;
    SamplerSpec.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerSpec.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerSpec.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerSpec.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(LogicalVk, &SamplerSpec, nullptr, &LinearSampler) != VK_SUCCESS)
    {
        return false;
    }

    VkDescriptorSetLayoutBinding ImageSlot {};
    ImageSlot.binding         = 0u;
    ImageSlot.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ImageSlot.descriptorCount = 1u;
    ImageSlot.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo LayoutSpec {};
    LayoutSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    LayoutSpec.bindingCount = 1u;
    LayoutSpec.pBindings    = &ImageSlot;
    if (vkCreateDescriptorSetLayout(LogicalVk, &LayoutSpec, nullptr, &DescriptorLayout) != VK_SUCCESS)
    {
        return false;
    }

    VkDescriptorPoolSize AllocSize {};
    AllocSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    AllocSize.descriptorCount = static_cast<uint32_t>(PresentSlotCount);

    VkDescriptorPoolCreateInfo AllocSpec {};
    AllocSpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    AllocSpec.maxSets       = static_cast<uint32_t>(PresentSlotCount);
    AllocSpec.poolSizeCount = 1u;
    AllocSpec.pPoolSizes    = &AllocSize;
    if (vkCreateDescriptorPool(LogicalVk, &AllocSpec, nullptr, &DescriptorAllocator) != VK_SUCCESS)
    {
        return false;
    }

    std::array<VkDescriptorSetLayout, PresentSlotCount> SetLayouts {};
    SetLayouts.fill(DescriptorLayout);
    VkDescriptorSetAllocateInfo SetAlloc {};
    SetAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAlloc.descriptorPool     = DescriptorAllocator;
    SetAlloc.descriptorSetCount = static_cast<uint32_t>(PresentSlotCount);
    SetAlloc.pSetLayouts        = SetLayouts.data();
    if (vkAllocateDescriptorSets(LogicalVk, &SetAlloc, BlitDescriptors.data()) != VK_SUCCESS)
    {
        return false;
    }

    VkPipelineLayoutCreateInfo UnitLayoutSpec {};
    UnitLayoutSpec.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    UnitLayoutSpec.setLayoutCount = 1u;
    UnitLayoutSpec.pSetLayouts    = &DescriptorLayout;
    if (vkCreatePipelineLayout(LogicalVk, &UnitLayoutSpec, nullptr, &BlitLayout) != VK_SUCCESS)
    {
        return false;
    }

    VkAttachmentDescription ColorAttach {};
    ColorAttach.format         = TargetFormat;
    ColorAttach.samples        = VK_SAMPLE_COUNT_1_BIT;
    ColorAttach.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColorAttach.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    ColorAttach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ColorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ColorAttach.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    ColorAttach.finalLayout    = Telemetry.LiveWindowSwapchain
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference ColorRef {};
    ColorRef.attachment = 0u;
    ColorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription Subunit {};
    Subunit.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subunit.colorAttachmentCount = 1u;
    Subunit.pColorAttachments    = &ColorRef;

    VkRenderPassCreateInfo RenderSpec {};
    RenderSpec.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    RenderSpec.attachmentCount = 1u;
    RenderSpec.pAttachments    = &ColorAttach;
    RenderSpec.subpassCount    = 1u;
    RenderSpec.pSubpasses      = &Subunit;
    if (vkCreateRenderPass(LogicalVk, &RenderSpec, nullptr, &BlitRenderUnit) != VK_SUCCESS)
    {
        return false;
    }

    const VkShaderModule VertexSpirv   = BoundDevice->LoadShaderBinary(ShaderDirectory / "ViewportSwapchainBlit.vert.spv");
    const VkShaderModule FragmentSpirv = BoundDevice->LoadShaderBinary(ShaderDirectory / "ViewportSwapchainBlit.frag.spv");
    if (VertexSpirv == VK_NULL_HANDLE || FragmentSpirv == VK_NULL_HANDLE)
    {
        if (VertexSpirv != VK_NULL_HANDLE) vkDestroyShaderModule(LogicalVk, VertexSpirv, nullptr);
        if (FragmentSpirv != VK_NULL_HANDLE) vkDestroyShaderModule(LogicalVk, FragmentSpirv, nullptr);
        if (OptionalDiagnostic != nullptr)
        {
            *OptionalDiagnostic = "Missing ViewportSwapchainBlit.{vert,frag}.spv shader files in " + ShaderDirectory.string();
        }
        return false;
    }

    VkPipelineShaderStageCreateInfo ShaderUnits[2]{};
    ShaderUnits[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderUnits[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    ShaderUnits[0].module = VertexSpirv;
    ShaderUnits[0].pName  = "VertexEntry";

    ShaderUnits[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderUnits[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    ShaderUnits[1].module = FragmentSpirv;
    ShaderUnits[1].pName  = "FragmentEntry";

    VkPipelineVertexInputStateCreateInfo VertexInput {};
    VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo InputTopo {};
    InputTopo.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    InputTopo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo ViewportSpec {};
    ViewportSpec.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ViewportSpec.viewportCount = 1u;
    ViewportSpec.scissorCount  = 1u;

    VkPipelineRasterizationStateCreateInfo RasterSpec {};
    RasterSpec.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RasterSpec.polygonMode = VK_POLYGON_MODE_FILL;
    RasterSpec.cullMode    = VK_CULL_MODE_NONE;
    RasterSpec.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    RasterSpec.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample {};
    Multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState BlendAttach {};
    BlendAttach.blendEnable    = VK_FALSE;
    BlendAttach.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo ColorBlend {};
    ColorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ColorBlend.attachmentCount = 1u;
    ColorBlend.pAttachments    = &BlendAttach;

    const VkDynamicState DynamicOpts[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynamicSpec {};
    DynamicSpec.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynamicSpec.dynamicStateCount = 2u;
    DynamicSpec.pDynamicStates    = DynamicOpts;

    VkGraphicsPipelineCreateInfo UnitCreate {};
    UnitCreate.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    UnitCreate.stageCount          = 2u;
    UnitCreate.pStages             = ShaderUnits;
    UnitCreate.pVertexInputState   = &VertexInput;
    UnitCreate.pInputAssemblyState = &InputTopo;
    UnitCreate.pViewportState      = &ViewportSpec;
    UnitCreate.pRasterizationState = &RasterSpec;
    UnitCreate.pMultisampleState   = &Multisample;
    UnitCreate.pColorBlendState    = &ColorBlend;
    UnitCreate.pDynamicState       = &DynamicSpec;
    UnitCreate.layout              = BlitLayout;
    UnitCreate.renderPass          = BlitRenderUnit;

    const VkResult CreateStatus = vkCreateGraphicsPipelines(LogicalVk, VK_NULL_HANDLE, 1u, &UnitCreate, nullptr, &BlitRasterUnit);
    vkDestroyShaderModule(LogicalVk, VertexSpirv, nullptr);
    vkDestroyShaderModule(LogicalVk, FragmentSpirv, nullptr);
    return CreateStatus == VK_SUCCESS;
}

void ViewportSwapchainPresenter::DestroyTargetAttachments() noexcept
{
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return;
    }
    const VkDevice LogicalVk = BoundDevice->AccessDevice();
    for (std::size_t Slot = 0u; Slot < PresentSlotCount; ++Slot)
    {
        if (InFlightCommands[Slot] != VK_NULL_HANDLE)
        {
            if (PresentSyncSignals[Slot] != VK_NULL_HANDLE)
            {
                (void)vkWaitForFences(LogicalVk, 1u, &PresentSyncSignals[Slot], VK_TRUE, 5000000000ull);
                (void)vkResetFences(LogicalVk, 1u, &PresentSyncSignals[Slot]);
            }
            vkFreeCommandBuffers(LogicalVk, BoundDevice->AccessCommandAllocator(), 1u, &InFlightCommands[Slot]);
            InFlightCommands[Slot] = VK_NULL_HANDLE;
        }
    }
    ImageSyncSignals.clear();
    vkDeviceWaitIdle(LogicalVk);

    for (VkSemaphore Sem : RenderDoneSemaphores)
    {
        if (Sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(LogicalVk, Sem, nullptr);
        }
    }
    RenderDoneSemaphores.clear();

    for (VkFramebuffer TargetFb : TargetAttachments)
    {
        if (TargetFb != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(LogicalVk, TargetFb, nullptr);
        }
    }
    TargetAttachments.clear();

    for (VkImageView View : SwapchainViews)
    {
        if (View != VK_NULL_HANDLE)
        {
            vkDestroyImageView(LogicalVk, View, nullptr);
        }
    }
    SwapchainViews.clear();
    SwapchainImages.clear();

    if (SwapchainHandle != VK_NULL_HANDLE && vkDestroySwapchainKHR != nullptr)
    {
        vkDestroySwapchainKHR(LogicalVk, SwapchainHandle, nullptr);
        SwapchainHandle = VK_NULL_HANDLE;
    }

    BoundDevice->ReleaseImage2D(OffscreenTarget);
}

bool ViewportSwapchainPresenter::RebuildTargetAttachments(uint32_t WidthPx, uint32_t HeightPx)
{
    DestroyTargetAttachments();
    const VkDevice LogicalVk = BoundDevice->AccessDevice();
    WidthPx                  = std::max(16u, WidthPx);
    HeightPx                 = std::max(16u, HeightPx);

    if (Telemetry.LiveWindowSwapchain && WindowSurface != VK_NULL_HANDLE)
    {
        VkSurfaceCapabilitiesKHR Caps {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(BoundDevice->AccessPhysicalDevice(), WindowSurface, &Caps);
        if (Caps.currentExtent.width != 0xFFFFFFFFu && Caps.currentExtent.width > 0u)
        {
            WidthPx  = Caps.currentExtent.width;
            HeightPx = Caps.currentExtent.height;
        }
        else
        {
            WidthPx  = std::clamp(WidthPx, Caps.minImageExtent.width, Caps.maxImageExtent.width);
            HeightPx = std::clamp(HeightPx, Caps.minImageExtent.height, Caps.maxImageExtent.height);
        }

        uint32_t ImageCount = Caps.minImageCount + 1u;
        if (Caps.maxImageCount > 0u && ImageCount > Caps.maxImageCount)
        {
            ImageCount = Caps.maxImageCount;
        }

        VkPresentModeKHR ChosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (!Telemetry.VerticalSyncEnabled && vkGetPhysicalDeviceSurfacePresentModesKHR != nullptr)
        {
            uint32_t ModeCount = 0u;
            vkGetPhysicalDeviceSurfacePresentModesKHR(BoundDevice->AccessPhysicalDevice(), WindowSurface, &ModeCount, nullptr);
            if (ModeCount > 0u)
            {
                std::vector<VkPresentModeKHR> Modes(ModeCount);
                vkGetPhysicalDeviceSurfacePresentModesKHR(BoundDevice->AccessPhysicalDevice(), WindowSurface, &ModeCount, Modes.data());
                for (VkPresentModeKHR Mode : Modes)
                {
                    if (Mode == VK_PRESENT_MODE_MAILBOX_KHR)
                    {
                        ChosenPresentMode = Mode;
                        break;
                    }
                    if (Mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
                    {
                        ChosenPresentMode = Mode;
                    }
                }
            }
        }

        VkSwapchainCreateInfoKHR ScSpec {};
        ScSpec.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ScSpec.surface          = WindowSurface;
        ScSpec.minImageCount    = ImageCount;
        ScSpec.imageFormat      = ActiveColorFormat;
        ScSpec.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        ScSpec.imageExtent      = { WidthPx, HeightPx };
        ScSpec.imageArrayLayers = 1u;
        ScSpec.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            ((Caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                 ? static_cast<VkImageUsageFlags>(VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                 : static_cast<VkImageUsageFlags>(0u));
        ScSpec.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ScSpec.preTransform     = Caps.currentTransform;
        ScSpec.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ScSpec.presentMode      = ChosenPresentMode;
        ScSpec.clipped          = VK_TRUE;

        if (vkCreateSwapchainKHR(LogicalVk, &ScSpec, nullptr, &SwapchainHandle) != VK_SUCCESS)
        {
            return false;
        }

        uint32_t RetrievedCount = 0u;
        vkGetSwapchainImagesKHR(LogicalVk, SwapchainHandle, &RetrievedCount, nullptr);
        SwapchainImages.resize(RetrievedCount);
        vkGetSwapchainImagesKHR(LogicalVk, SwapchainHandle, &RetrievedCount, SwapchainImages.data());

        SwapchainViews.resize(RetrievedCount, VK_NULL_HANDLE);
        TargetAttachments.resize(RetrievedCount, VK_NULL_HANDLE);
        RenderDoneSemaphores.resize(RetrievedCount, VK_NULL_HANDLE);
        ImageSyncSignals.assign(RetrievedCount, VK_NULL_HANDLE);

        VkSemaphoreCreateInfo SemSpec {};
        SemSpec.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (uint32_t Idx = 0u; Idx < RetrievedCount; ++Idx)
        {
            VkImageViewCreateInfo ViewSpec {};
            ViewSpec.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ViewSpec.image            = SwapchainImages[Idx];
            ViewSpec.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            ViewSpec.format           = ActiveColorFormat;
            ViewSpec.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
            if (vkCreateImageView(LogicalVk, &ViewSpec, nullptr, &SwapchainViews[Idx]) != VK_SUCCESS)
            {
                return false;
            }

            VkFramebufferCreateInfo FbSpec {};
            FbSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            FbSpec.renderPass      = BlitRenderUnit;
            FbSpec.attachmentCount = 1u;
            FbSpec.pAttachments    = &SwapchainViews[Idx];
            FbSpec.width           = WidthPx;
            FbSpec.height          = HeightPx;
            FbSpec.layers          = 1u;
            if (vkCreateFramebuffer(LogicalVk, &FbSpec, nullptr, &TargetAttachments[Idx]) != VK_SUCCESS)
            {
                return false;
            }

            if (vkCreateSemaphore(LogicalVk, &SemSpec, nullptr, &RenderDoneSemaphores[Idx]) != VK_SUCCESS)
            {
                return false;
            }
        }
    }
    else
    {
        OffscreenTarget = BoundDevice->AllocateImage2D(
            WidthPx,
            HeightPx,
            ActiveColorFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        if (OffscreenTarget.ViewHandle == VK_NULL_HANDLE)
        {
            return false;
        }
        TargetAttachments.resize(1u, VK_NULL_HANDLE);
        VkFramebufferCreateInfo FbSpec {};
        FbSpec.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        FbSpec.renderPass      = BlitRenderUnit;
        FbSpec.attachmentCount = 1u;
        FbSpec.pAttachments    = &OffscreenTarget.ViewHandle;
        FbSpec.width           = WidthPx;
        FbSpec.height          = HeightPx;
        FbSpec.layers          = 1u;
        if (vkCreateFramebuffer(LogicalVk, &FbSpec, nullptr, &TargetAttachments[0]) != VK_SUCCESS)
        {
            return false;
        }
    }

    Telemetry.SurfaceWidthPx  = WidthPx;
    Telemetry.SurfaceHeightPx = HeightPx;
    return true;
}

bool ViewportSwapchainPresenter::EnsureUploadImage(uint32_t WidthPx, uint32_t HeightPx)
{
    if (UploadedColorImage.ImageHandle != VK_NULL_HANDLE &&
        UploadedColorImage.WidthPx == WidthPx &&
        UploadedColorImage.HeightPx == HeightPx)
    {
        return true;
    }

    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }

    vkDeviceWaitIdle(BoundDevice->AccessDevice());
    BoundDevice->ReleaseImage2D(UploadedColorImage);
    UploadedColorImage = BoundDevice->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    return UploadedColorImage.ViewHandle != VK_NULL_HANDLE;
}

bool ViewportSwapchainPresenter::PresentImageView(
    VkImageView SourceImageView,
    uint32_t    TargetWidthPx,
    uint32_t    TargetHeightPx)
{
    if (!QueryReady() || SourceImageView == VK_NULL_HANDLE)
    {
        LastDiagnostic = "ViewportSwapchainPresenter is not initialized or received a null VkImageView.";
        return false;
    }

    TargetWidthPx  = std::max(16u, TargetWidthPx);
    TargetHeightPx = std::max(16u, TargetHeightPx);
    if (TargetWidthPx != Telemetry.SurfaceWidthPx ||
        TargetHeightPx != Telemetry.SurfaceHeightPx ||
        TargetAttachments.empty())
    {
        if (!RebuildTargetAttachments(TargetWidthPx, TargetHeightPx))
        {
            LastDiagnostic = "Unable to rebuild ViewportSwapchainPresenter target attachments.";
            return false;
        }
    }

    const VkDevice    LogicalVk   = BoundDevice->AccessDevice();
    const std::size_t Slot        = static_cast<std::size_t>(ActiveSlotIndex % PresentSlotCount);

    if (Telemetry.LiveWindowSwapchain && InFlightCommands[Slot] != VK_NULL_HANDLE)
    {
        if (PresentSyncSignals[Slot] != VK_NULL_HANDLE)
        {
            (void)vkWaitForFences(LogicalVk, 1u, &PresentSyncSignals[Slot], VK_TRUE, 5000000000ull);
            (void)vkResetFences(LogicalVk, 1u, &PresentSyncSignals[Slot]);
        }
        vkFreeCommandBuffers(LogicalVk, BoundDevice->AccessCommandAllocator(), 1u, &InFlightCommands[Slot]);
        InFlightCommands[Slot] = VK_NULL_HANDLE;
    }

    uint32_t ImageIndex = 0u;
    if (Telemetry.LiveWindowSwapchain)
    {
        const VkResult AcquireResult = vkAcquireNextImageKHR(
            LogicalVk,
            SwapchainHandle,
            1000000000ull,
            AcquireSemaphores[Slot],
            VK_NULL_HANDLE,
            &ImageIndex);
        if (AcquireResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            if (!RebuildTargetAttachments(TargetWidthPx, TargetHeightPx))
            {
                LastDiagnostic = "Swapchain became out of date and could not be rebuilt.";
                return false;
            }
            return true;
        }
        if (AcquireResult != VK_SUCCESS && AcquireResult != VK_SUBOPTIMAL_KHR)
        {
            LastDiagnostic = "vkAcquireNextImageKHR failed with VkResult " + std::to_string(static_cast<int32_t>(AcquireResult));
            return false;
        }
        if (ImageIndex < ImageSyncSignals.size() &&
            ImageSyncSignals[ImageIndex] != VK_NULL_HANDLE &&
            ImageSyncSignals[ImageIndex] != PresentSyncSignals[Slot])
        {
            (void)vkWaitForFences(LogicalVk, 1u, &ImageSyncSignals[ImageIndex], VK_TRUE, 5000000000ull);
        }
    }

    VkDescriptorImageInfo ImgDesc {};
    ImgDesc.sampler     = LinearSampler;
    ImgDesc.imageView   = SourceImageView;
    ImgDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet WriteDesc {};
    WriteDesc.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    WriteDesc.dstSet          = BlitDescriptors[Slot];
    WriteDesc.dstBinding      = 0u;
    WriteDesc.descriptorCount = 1u;
    WriteDesc.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    WriteDesc.pImageInfo      = &ImgDesc;
    vkUpdateDescriptorSets(LogicalVk, 1u, &WriteDesc, 0u, nullptr);

    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        LastDiagnostic = "Unable to allocate Vulkan command buffer for swapchain blit.";
        return false;
    }

    VkClearValue ClearVal {};
    ClearVal.color = { { 0.02f, 0.02f, 0.025f, 1.0f } };

    VkRenderPassBeginInfo BeginSpec {};
    BeginSpec.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    BeginSpec.renderPass      = BlitRenderUnit;
    BeginSpec.framebuffer     = TargetAttachments[ImageIndex];
    BeginSpec.renderArea      = { { 0, 0 }, { Telemetry.SurfaceWidthPx, Telemetry.SurfaceHeightPx } };
    BeginSpec.clearValueCount = 1u;
    BeginSpec.pClearValues    = &ClearVal;

    vkCmdBeginRenderPass(Cmd, &BeginSpec, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, BlitRasterUnit);

    VkViewport Vp {};
    Vp.x        = 0.0f;
    Vp.y        = 0.0f;
    Vp.width    = static_cast<float>(Telemetry.SurfaceWidthPx);
    Vp.height   = static_cast<float>(Telemetry.SurfaceHeightPx);
    Vp.minDepth = 0.0f;
    Vp.maxDepth = 1.0f;
    vkCmdSetViewport(Cmd, 0u, 1u, &Vp);

    VkRect2D Scissor { { 0, 0 }, { Telemetry.SurfaceWidthPx, Telemetry.SurfaceHeightPx } };
    vkCmdSetScissor(Cmd, 0u, 1u, &Scissor);

    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, BlitLayout, 0u, 1u, &BlitDescriptors[Slot], 0u, nullptr);
    vkCmdDraw(Cmd, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(Cmd);

    if (Telemetry.LiveWindowSwapchain)
    {
        if (vkEndCommandBuffer(Cmd) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(LogicalVk, BoundDevice->AccessCommandAllocator(), 1u, &Cmd);
            LastDiagnostic = "vkEndCommandBuffer failed during swapchain presentation.";
            return false;
        }

        const VkSemaphore          DoneSem  = RenderDoneSemaphores[ImageIndex];
        const VkPipelineStageFlags WaitMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo               SubmitSpec {};
        SubmitSpec.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        SubmitSpec.waitSemaphoreCount   = 1u;
        SubmitSpec.pWaitSemaphores      = &AcquireSemaphores[Slot];
        SubmitSpec.pWaitDstStageMask    = &WaitMask;
        SubmitSpec.commandBufferCount   = 1u;
        SubmitSpec.pCommandBuffers      = &Cmd;
        SubmitSpec.signalSemaphoreCount = 1u;
        SubmitSpec.pSignalSemaphores    = &DoneSem;

        if (vkQueueSubmit(BoundDevice->AccessQueue(), 1u, &SubmitSpec, PresentSyncSignals[Slot]) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(LogicalVk, BoundDevice->AccessCommandAllocator(), 1u, &Cmd);
            LastDiagnostic = "vkQueueSubmit failed during swapchain presentation.";
            return false;
        }

        if (ImageIndex < ImageSyncSignals.size())
        {
            ImageSyncSignals[ImageIndex] = PresentSyncSignals[Slot];
        }
        LastPresentedImageIndex = ImageIndex;
        InFlightCommands[Slot]  = Cmd;

        VkPresentInfoKHR PresentSpec {};
        PresentSpec.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        PresentSpec.waitSemaphoreCount = 1u;
        PresentSpec.pWaitSemaphores    = &DoneSem;
        PresentSpec.swapchainCount     = 1u;
        PresentSpec.pSwapchains        = &SwapchainHandle;
        PresentSpec.pImageIndices      = &ImageIndex;

        const VkResult PresentResult = vkQueuePresentKHR(BoundDevice->AccessQueue(), &PresentSpec);
        ActiveSlotIndex              = (ActiveSlotIndex + 1u) % static_cast<uint32_t>(PresentSlotCount);

        if (PresentResult == VK_ERROR_OUT_OF_DATE_KHR || PresentResult == VK_SUBOPTIMAL_KHR)
        {
            if (!RebuildTargetAttachments(TargetWidthPx, TargetHeightPx))
            {
                LastDiagnostic = "Unable to rebuild swapchain after suboptimal/out-of-date presentation.";
                return false;
            }
        }
        else if (PresentResult != VK_SUCCESS)
        {
            LastDiagnostic = "vkQueuePresentKHR failed with VkResult " + std::to_string(static_cast<int32_t>(PresentResult));
            return false;
        }
    }
    else
    {
        const uint64_t SyncPoint     = BoundDevice->DispatchCommandBufferAsync(Cmd);
        const uint64_t SyncTimeoutNs = BoundDevice->QueryHardwareAccelerated() ? 5000000000ull : 60000000000ull;
        const bool     SyncOk        = BoundDevice->AwaitTimelineTick(SyncPoint, SyncTimeoutNs);
        BoundDevice->FreeCommandBuffer(Cmd);
        if (!SyncOk)
        {
            LastDiagnostic = "Timeline synchronization failed during offscreen viewport presentation.";
            return false;
        }
        LastPresentedImageIndex = 0u;
    }

    ++Telemetry.PresentedImageCount;
    LastDiagnostic.clear();
    return true;
}

bool ViewportSwapchainPresenter::PresentRgba8Buffer(
    std::span<const uint8_t> PixelsRgba8,
    uint32_t                 SourceWidthPx,
    uint32_t                 SourceHeightPx,
    uint32_t                 TargetWidthPx,
    uint32_t                 TargetHeightPx)
{
    if (!QueryReady() || SourceWidthPx == 0u || SourceHeightPx == 0u)
    {
        return false;
    }

    const uint64_t ExpectedBytes = static_cast<uint64_t>(SourceWidthPx) * SourceHeightPx * 4ull;
    if (PixelsRgba8.size() < ExpectedBytes)
    {
        LastDiagnostic = "PresentRgba8Buffer received fewer bytes than SourceWidthPx * SourceHeightPx * 4.";
        return false;
    }

    if (!EnsureUploadImage(SourceWidthPx, SourceHeightPx))
    {
        LastDiagnostic = "Unable to allocate RGBA8 upload image for ViewportSwapchainPresenter.";
        return false;
    }

    if (TransferBuffer.ByteCapacity < ExpectedBytes)
    {
        vkDeviceWaitIdle(BoundDevice->AccessDevice());
        BoundDevice->ReleaseBuffer(TransferBuffer);
        TransferBuffer = BoundDevice->AllocateBuffer(
            ExpectedBytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (TransferBuffer.HostAddress == nullptr)
    {
        LastDiagnostic = "Unable to allocate host-visible transfer buffer for RGBA8 upload.";
        return false;
    }

    std::memcpy(TransferBuffer.HostAddress, PixelsRgba8.data(), static_cast<std::size_t>(ExpectedBytes));

    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    VkImageMemoryBarrier ToDst {};
    ToDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToDst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    ToDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToDst.image               = UploadedColorImage.ImageHandle;
    ToDst.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
    ToDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToDst);

    VkBufferImageCopy CopyRegion {};
    CopyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    CopyRegion.imageExtent      = { SourceWidthPx, SourceHeightPx, 1u };
    vkCmdCopyBufferToImage(Cmd, TransferBuffer.BufferHandle, UploadedColorImage.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &CopyRegion);

    VkImageMemoryBarrier ToSample = ToDst;
    ToSample.oldLayout            = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToSample.newLayout            = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ToSample.srcAccessMask        = VK_ACCESS_TRANSFER_WRITE_BIT;
    ToSample.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToSample);

    const uint64_t SyncPoint     = BoundDevice->DispatchCommandBufferAsync(Cmd);
    const uint64_t SyncTimeoutNs = BoundDevice->QueryHardwareAccelerated() ? 5000000000ull : 60000000000ull;
    const bool     SyncOk        = BoundDevice->AwaitTimelineTick(SyncPoint, SyncTimeoutNs);
    BoundDevice->FreeCommandBuffer(Cmd);
    if (!SyncOk)
    {
        return false;
    }

    return PresentImageView(UploadedColorImage.ViewHandle, TargetWidthPx, TargetHeightPx);
}

bool ViewportSwapchainPresenter::CapturePresentedRgba8(
    std::vector<uint8_t>& OutRgbaPixels,
    uint32_t&             OutWidthPx,
    uint32_t&             OutHeightPx)
{
    if (BoundDevice == nullptr || !BoundDevice->Ready() ||
        Telemetry.SurfaceWidthPx == 0u || Telemetry.SurfaceHeightPx == 0u)
    {
        return false;
    }

    const VkDevice LogicalVk = BoundDevice->AccessDevice();
    for (std::size_t Slot = 0u; Slot < PresentSlotCount; ++Slot)
    {
        if (InFlightCommands[Slot] != VK_NULL_HANDLE)
        {
            if (PresentSyncSignals[Slot] != VK_NULL_HANDLE)
            {
                (void)vkWaitForFences(LogicalVk, 1u, &PresentSyncSignals[Slot], VK_TRUE, 5000000000ull);
                (void)vkResetFences(LogicalVk, 1u, &PresentSyncSignals[Slot]);
            }
            vkFreeCommandBuffers(LogicalVk, BoundDevice->AccessCommandAllocator(), 1u, &InFlightCommands[Slot]);
            InFlightCommands[Slot] = VK_NULL_HANDLE;
        }
    }
    for (VkFence& ImgSignal : ImageSyncSignals)
    {
        ImgSignal = VK_NULL_HANDLE;
    }

    VkImage SourceImage = OffscreenTarget.ImageHandle;
    if (Telemetry.LiveWindowSwapchain && !SwapchainImages.empty())
    {
        const uint32_t SafeIdx = std::min<uint32_t>(LastPresentedImageIndex, static_cast<uint32_t>(SwapchainImages.size() - 1u));
        SourceImage            = SwapchainImages[SafeIdx];
    }
    if (SourceImage == VK_NULL_HANDLE)
    {
        return false;
    }

    const uint64_t TotalBytes = static_cast<uint64_t>(Telemetry.SurfaceWidthPx) * Telemetry.SurfaceHeightPx * 4ull;
    if (TotalBytes > TransferBuffer.ByteCapacity)
    {
        BoundDevice->ReleaseBuffer(TransferBuffer);
        TransferBuffer = BoundDevice->AllocateBuffer(
            TotalBytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (TransferBuffer.HostAddress == nullptr)
    {
        return false;
    }

    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    if (Telemetry.LiveWindowSwapchain)
    {
        VkImageMemoryBarrier ToSrc {};
        ToSrc.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ToSrc.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        ToSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        ToSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ToSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ToSrc.image               = SourceImage;
        ToSrc.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
        ToSrc.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToSrc);
    }

    VkBufferImageCopy CopyRegion {};
    CopyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    CopyRegion.imageExtent      = { Telemetry.SurfaceWidthPx, Telemetry.SurfaceHeightPx, 1u };
    vkCmdCopyImageToBuffer(Cmd, SourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, TransferBuffer.BufferHandle, 1u, &CopyRegion);

    const uint64_t SyncPoint     = BoundDevice->DispatchCommandBufferAsync(Cmd);
    const uint64_t SyncTimeoutNs = BoundDevice->QueryHardwareAccelerated() ? 5000000000ull : 60000000000ull;
    const bool     SyncOk        = BoundDevice->AwaitTimelineTick(SyncPoint, SyncTimeoutNs);
    BoundDevice->FreeCommandBuffer(Cmd);
    if (!SyncOk)
    {
        return false;
    }

    OutWidthPx  = Telemetry.SurfaceWidthPx;
    OutHeightPx = Telemetry.SurfaceHeightPx;
    OutRgbaPixels.resize(static_cast<std::size_t>(TotalBytes));
    std::memcpy(OutRgbaPixels.data(), TransferBuffer.HostAddress, static_cast<std::size_t>(TotalBytes));

    if (ActiveColorFormat == VK_FORMAT_B8G8R8A8_UNORM)
    {
        for (std::size_t Off = 0u; Off + 3u < OutRgbaPixels.size(); Off += 4u)
        {
            std::swap(OutRgbaPixels[Off + 0u], OutRgbaPixels[Off + 2u]);
        }
    }
    return true;
}

void ViewportSwapchainPresenter::TeardownPresenter() noexcept
{
    if (BoundDevice != nullptr && BoundDevice->Ready())
    {
        const VkDevice LogicalVk = BoundDevice->AccessDevice();
        DestroyTargetAttachments();

        BoundDevice->ReleaseImage2D(UploadedColorImage);
        BoundDevice->ReleaseBuffer(TransferBuffer);

        if (BlitRasterUnit != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(LogicalVk, BlitRasterUnit, nullptr);
            BlitRasterUnit = VK_NULL_HANDLE;
        }
        if (BlitLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(LogicalVk, BlitLayout, nullptr);
            BlitLayout = VK_NULL_HANDLE;
        }
        if (BlitRenderUnit != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(LogicalVk, BlitRenderUnit, nullptr);
            BlitRenderUnit = VK_NULL_HANDLE;
        }
        if (DescriptorAllocator != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(LogicalVk, DescriptorAllocator, nullptr);
            DescriptorAllocator = VK_NULL_HANDLE;
        }
        BlitDescriptors.fill(VK_NULL_HANDLE);
        if (DescriptorLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(LogicalVk, DescriptorLayout, nullptr);
            DescriptorLayout = VK_NULL_HANDLE;
        }
        if (LinearSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(LogicalVk, LinearSampler, nullptr);
            LinearSampler = VK_NULL_HANDLE;
        }

        for (std::size_t Slot = 0u; Slot < PresentSlotCount; ++Slot)
        {
            if (AcquireSemaphores[Slot] != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(LogicalVk, AcquireSemaphores[Slot], nullptr);
                AcquireSemaphores[Slot] = VK_NULL_HANDLE;
            }
            if (PresentSyncSignals[Slot] != VK_NULL_HANDLE)
            {
                vkDestroyFence(LogicalVk, PresentSyncSignals[Slot], nullptr);
                PresentSyncSignals[Slot] = VK_NULL_HANDLE;
            }
        }

        if (WindowSurface != VK_NULL_HANDLE && vkDestroySurfaceKHR != nullptr)
        {
            vkDestroySurfaceKHR(BoundDevice->AccessInstance(), WindowSurface, nullptr);
            WindowSurface = VK_NULL_HANDLE;
        }
    }

    BoundDevice     = nullptr;
    DesktopWindow   = nullptr;
    ActiveSlotIndex = 0u;
    Telemetry       = {};
}

} // namespace Frontier::GeometricRaster
