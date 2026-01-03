# Git version detection for Open Salamander
# Generates git_version.h with version info from git tags
# Format: x.y.z-shorthash (e.g., 5.0.1-9c98a4b)

find_package(Git QUIET)

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    # Get the latest tag
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_TAG_RESULT
    )

    # Get short commit hash
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_SHORT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_HASH_RESULT
    )

    if(GIT_TAG_RESULT EQUAL 0 AND GIT_HASH_RESULT EQUAL 0)
        # Strip leading 'v' if present (v5.0.1 -> 5.0.1)
        string(REGEX REPLACE "^v" "" GIT_TAG_VERSION "${GIT_TAG}")

        # Check if we're exactly on the tag
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_EXACT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE GIT_EXACT_RESULT
        )

        if(GIT_EXACT_RESULT EQUAL 0)
            # Exactly on tag: just use version (e.g., 5.0.1)
            set(GIT_VERSION "${GIT_TAG_VERSION}")
        else()
            # Ahead of tag: use version-hash (e.g., 5.0.1-9c98a4b)
            set(GIT_VERSION "${GIT_TAG_VERSION}-${GIT_COMMIT_SHORT}")
        endif()

        set(GIT_VERSION_AVAILABLE TRUE)
        message(STATUS "Git version: ${GIT_VERSION}")
    else()
        set(GIT_VERSION_AVAILABLE FALSE)
    endif()
else()
    set(GIT_VERSION_AVAILABLE FALSE)
endif()

if(NOT GIT_VERSION_AVAILABLE)
    set(GIT_VERSION "")
    set(GIT_COMMIT_SHORT "")
    message(STATUS "Git version: not available (using static version)")
endif()

# Configure the header file
configure_file(
    "${CMAKE_SOURCE_DIR}/src/git_version.h.in"
    "${CMAKE_SOURCE_DIR}/src/git_version.h"
    @ONLY
)
