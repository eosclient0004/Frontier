/*============================================================================================================================================
                                                    PROJECTEDITORSCENEREGISTRY.H
============================================================================================================================================*/
// 🧩 Decoupled project-to-editor scene & ProjectBrowser adapter registry enabling dynamic C++ projects to populate Frontier Editor

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ProjectSceneCameraEntry
{
    std::string                             Title    {}; // [-] - Outliner label for auxiliary project camera
    std::string                             Subtitle {}; // [-] - Outliner secondary caption
    GeometricRaster::PerspectiveCameraBasis Camera   {}; // [-] - Optical camera basis in world coordinates
};

struct ProjectBrowserDisplayDescriptor
{
    std::string                        SummaryCaption     {}; // [-] - ProjectBrowser card summary line
    std::string                        Description        {}; // [-] - ProjectBrowser details inspector text
    std::vector<std::string>           ViewTitles         {}; // [-] - Selectable scene/lobby variant titles in ProjectBrowser
    std::vector<std::filesystem::path> ViewThumbnailPaths {}; // [-] - Optional PNG preview paths per view variant
};

struct ProjectEditorSceneRequest
{
    std::filesystem::path ProjectSpacePath  {};   // [-]  - Selected .projectspace file opened in Frontier Editor
    std::filesystem::path RepositoryRoot    {};   // [-]  - Resolved Frontier repository root directory
    uint32_t              ViewportWidthPx   = 640u; // [px] - Initial editor viewport width
    uint32_t              ViewportHeightPx  = 384u; // [px] - Initial editor viewport height
    uint32_t              SceneVariantIndex = 0u;   // [idx]- Active project scene/lobby variant ordinal
};

struct ProjectEditorSceneResult
{
    std::string                          SceneFolderTitle         {}; // [-] - Root geometry folder title in Outliner
    std::string                          SceneFolderSubtitle      {}; // [-] - Root geometry folder subtitle in Outliner
    std::string                          ActiveVariantTitle       {}; // [-] - Human-readable active variant label
    GeometricRaster::UnifiedLightingMode PreferredLightingMode    = GeometricRaster::UnifiedLightingMode::UnifiedAll3;
    float                                PreferredExposureGain    = 1.0f; // [x] - Calibrated viewport exposure gain
    bool                                 DisableStaticClipmapBake = false; // [-] - Skip distant clipmap bake for studio interiors
    std::vector<ProjectSceneCameraEntry> ExtraCameras             {}; // [-] - Auxiliary project cameras registered in Outliner
};

struct ProjectEditorSceneAdapter
{
    std::string ProjectIdentifier {}; // [-] - Unique adapter identifier (e.g. "AutoSpace")

    std::function<bool(const std::filesystem::path& ProjectSpacePath)> MatchesProject {};

    std::function<bool(
        const std::filesystem::path&     ProjectSpacePath,
        const std::filesystem::path&     RepositoryRoot,
        ProjectBrowserDisplayDescriptor& OutDescriptor)> QueryBrowserDescriptor {};

    std::function<bool(
        const ProjectEditorSceneRequest&           Request,
        GeometricRaster::ShowcaseWorldSceneBundle& OutBundle,
        ProjectEditorSceneResult&                  OutResult,
        std::string&                               OutDiagnostic)> ConstructEditorScene {};

    std::function<bool(
        uint32_t                                   SceneVariantIndex,
        double                                     ElapsedSeconds,
        GeometricRaster::ShowcaseWorldSceneBundle& InOutBundle,
        bool&                                      OutMaterialsMutated)> TickEditorScene {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ProjectEditorSceneRegistry
{
public:
    static void RegisterAdapter(ProjectEditorSceneAdapter Adapter);

    [[nodiscard]] static const ProjectEditorSceneAdapter* FindMatchingAdapter(
        const std::filesystem::path& ProjectSpacePath);

    [[nodiscard]] static std::size_t QueryRegisteredCount() noexcept;
};

} // namespace Frontier::Lifecycle
