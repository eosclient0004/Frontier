/*============================================================================================================================================
                                                   PROJECTEDITORSCENEREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Implementation of ProjectEditorSceneRegistry storing project-provided Frontier Editor scene & ProjectBrowser adapters

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/ProjectEditorSceneRegistry.h"

#include <mutex>
#include <utility>

namespace Frontier::Lifecycle
{

namespace
{

struct AdapterRegistryStorage
{
    std::mutex                             Guard    {};
    std::vector<ProjectEditorSceneAdapter> Adapters {};
};

[[nodiscard]] AdapterRegistryStorage& AccessRegistryStorage() noexcept
{
    static AdapterRegistryStorage Storage {};
    return Storage;
}

} // namespace

void ProjectEditorSceneRegistry::RegisterAdapter(ProjectEditorSceneAdapter Adapter)
{
    if (Adapter.ProjectIdentifier.empty() || !Adapter.MatchesProject)
    {
        return;
    }

    auto& Storage = AccessRegistryStorage();
    std::scoped_lock Lock(Storage.Guard);

    for (auto& Existing : Storage.Adapters)
    {
        if (Existing.ProjectIdentifier == Adapter.ProjectIdentifier)
        {
            Existing = std::move(Adapter);
            return;
        }
    }
    Storage.Adapters.push_back(std::move(Adapter));
}

const ProjectEditorSceneAdapter* ProjectEditorSceneRegistry::FindMatchingAdapter(
    const std::filesystem::path& ProjectSpacePath)
{
    if (ProjectSpacePath.empty())
    {
        return nullptr;
    }

    auto& Storage = AccessRegistryStorage();
    std::scoped_lock Lock(Storage.Guard);

    for (const auto& Candidate : Storage.Adapters)
    {
        if (Candidate.MatchesProject && Candidate.MatchesProject(ProjectSpacePath))
        {
            return &Candidate;
        }
    }
    return nullptr;
}

std::size_t ProjectEditorSceneRegistry::QueryRegisteredCount() noexcept
{
    auto& Storage = AccessRegistryStorage();
    std::scoped_lock Lock(Storage.Guard);
    return Storage.Adapters.size();
}

} // namespace Frontier::Lifecycle
