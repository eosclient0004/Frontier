#============================================================================================================================================
#                                                      STAMPGITCOMMITHEADER.CMAKE
#============================================================================================================================================
# Build-time generator for FrontierBuildInfo.h: resolves the active 8-char git HEAD revision on every build and only
# touches FrontierBuildInfo.h when the revision actually changes (preserving incremental builds & sccache hit rates).

set(_STAMP_HASH "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=8 HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE _GIT_OUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_GIT_OUT)
        set(_STAMP_HASH "${_GIT_OUT}")
    endif()
endif()

set(_NEXT_CONTENT "#pragma once\n#define FRONTIER_GIT_COMMIT_HASH \"${_STAMP_HASH}\"\n")
set(_PREV_CONTENT "")
if(EXISTS "${OUTPUT_HEADER}")
    file(READ "${OUTPUT_HEADER}" _PREV_CONTENT)
endif()

if(NOT "${_PREV_CONTENT}" STREQUAL "${_NEXT_CONTENT}")
    get_filename_component(_OUT_DIR "${OUTPUT_HEADER}" DIRECTORY)
    file(MAKE_DIRECTORY "${_OUT_DIR}")
    file(WRITE "${OUTPUT_HEADER}" "${_NEXT_CONTENT}")
endif()
