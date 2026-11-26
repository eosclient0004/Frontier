# ============================================================================================================================================
#                                                     PROJECTS/AUTOSPACE/CMAKELISTS.TXT
# ============================================================================================================================================
# 🧩 Standalone project build target for AutoSpace (isolated from Engine/; domain-organized single-level Studio Lobbies)

add_library(AutoSpaceProject STATIC
    Source/Policies/LobbyPolicy.cpp
    Source/Policies/AutoSpacePolicies.cpp
    Source/Geometry/StudioProceduralSurface.cpp
    Source/Levels/StudioLobbyWorldBuilder.cpp
    Source/Levels/Lobby1CycloramaLevel.cpp
    Source/Levels/Lobby2ForestStageLevel.cpp
    Source/Levels/Lobby3RacingGarageLevel.cpp
    Source/Gameplay/LobbyLifecycle.cpp
    Source/Gameplay/AutoSpaceLifecycle.cpp
)
target_include_directories(AutoSpaceProject PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}"
)
target_link_libraries(AutoSpaceProject PUBLIC
    FrontierEngine
    FrontierCompileSettings
)
target_compile_definitions(AutoSpaceProject PUBLIC
    AUTOSPACE_PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(AutoSpaceProject PUBLIC cxx_std_20)

add_executable(AutoSpace
    Source/AutoSpaceLaunch.cpp
)
target_link_libraries(AutoSpace PRIVATE AutoSpaceProject)
target_compile_features(AutoSpace PRIVATE cxx_std_20)
