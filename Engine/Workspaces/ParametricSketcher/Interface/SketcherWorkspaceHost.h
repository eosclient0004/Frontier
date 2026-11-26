/*============================================================================================================================================
                                                        SKETCHERWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Top-level dockable host for Parametric Sketcher: Vulkan viewport rasterizer, ThorVG SVG icon atlas, Outliner/Viewport/DrawingSheet/Construct/Inspector panes

#pragma once

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherInterface.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherOptions.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherRasterization.h"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace Frontier::Workspaces
{

class SketcherWorkspaceHost
{
public:
    SketcherWorkspaceHost() = default;
    ~SketcherWorkspaceHost() noexcept;

    SketcherWorkspaceHost(const SketcherWorkspaceHost&)            = delete;
    SketcherWorkspaceHost& operator=(const SketcherWorkspaceHost&) = delete;

    bool Initialize(
        GeometricRaster::VulkanInterchange* Interchange,
        const std::filesystem::path&        CompiledShaderDir,
        const std::filesystem::path&        IconsRoot,
        const std::string&                  InitialDocumentPath = {},
        std::string*                        OutDiagnostic       = nullptr);

    void Finalize() noexcept;

    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept;
    void AttachImGuiPresenter(GeometricRaster::ImGuiOverlayRasterization* Presenter) noexcept;
    void SynchronizeHostScale(float ScaleFactor) noexcept;

    void RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer);

    void BeginFrame(double DeltaSeconds);
    void RenderFullscreenViewport(float WidthPx, float HeightPx);
    void EndFrame(double DeltaSeconds, bool DrawFloatingOverlays = true, bool HostedOverlayMode = true);
    bool OpenDocumentPath(const std::string& DocumentPath, std::string* OutDiagnostic = nullptr);
    bool IntakeExternalFileDrop(const std::string& FilePath, std::string* OutDiagnostic = nullptr);

    [[nodiscard]] bool QueryViewportMaximized() const noexcept { return State.ViewportMaximizedEnabled; }
    [[nodiscard]] Sketcher::SketcherWorkspaceStatus&       AccessState() noexcept        { return State; }
    [[nodiscard]] const Sketcher::SketcherWorkspaceStatus& InspectState() const noexcept { return State; }
    [[nodiscard]] uint32_t QueryRegisteredTabCount() const noexcept { return RegisteredTabCount; }

private:
    void RenderViewportTarget();
    void UploadViewportPixels(const uint32_t* Pixels, uint32_t Width, uint32_t Height);
    [[nodiscard]] ImTextureRef QueryViewportTextureRef() noexcept;

    GeometricRaster::VulkanInterchange*            Device            = nullptr;
    GeometricRaster::ImGuiOverlayRasterization*    ImGuiPresenter    = nullptr;
    Sketcher::SketcherWorkspaceStatus              State             {};
    Sketcher::SketcherOptions                      Options           {};
    Sketcher::SketcherRasterization                Rasterizer        {};
    ImTextureData                                  ViewportTexture   {};
    uint64_t                                       DirectGpuTextureId = 0ull;
    uint64_t                                       UploadedMatcapGen  = 0u;
    uint64_t                                       LastViewportRenderSignature = 0ull;
    int                                            LastProbeX         = -1;
    int                                            LastProbeY         = -1;
    bool                                           Initialized        = false;
    bool                                           RasterReady        = false;
    ContentBrowserPanel*             BoundBrowser       = nullptr;
    float                            HostScaleFactor    = 1.0f;
    uint32_t                         RegisteredTabCount = 0u;
    double                           LastDeltaSeconds   = 1.0 / 60.0;
};

} // namespace Frontier::Workspaces
