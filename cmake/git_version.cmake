# Git version detection for Sally
# Generates git_version.h with version info from git tags
# Format: x.y.z-shorthash (e.g., 1.0.0-9c98a4b)

find_package(Git QUIET)

# Sally local development version base.
#
# Deliberately NOT a CACHE entry. `set(... CACHE ...)` only applies when the entry does
# not already exist, so once a build directory had been configured, bumping this line did
# nothing there - the stale cached value won and every build was stamped with the previous
# release's version. That is silent and survives rebuilds, so it is only noticed when
# someone reads the About box.
#
# As a normal variable it is re-evaluated on every configure, while an explicit
# -DSALLY_LOCAL_DEV_VERSION=x.y.z on the command line still wins because that defines the
# variable before this file runs.
# Sally local development version base. The SINGLE SOURCE OF TRUTH is
# src/plugins/shared/spl_vers.h; this derives from it and must not hardcode a version.
#
# Keep spl_vers.h AHEAD of the newest released tag: v1.0.24 is published, so leaving it
# at 1.0.24 would stamp unreleased builds with a version already shipped.
#
# It is deliberately neither a CACHE entry nor merely `if(NOT DEFINED)`-guarded, because
# both let a value cached by an EARLIER configure win, and that has now shipped wrong
# versions twice:
#
#   * `set(... CACHE STRING ...)` is skipped entirely when the entry already exists, so a
#     build directory configured before a bump kept emitting the old version forever.
#   * `if(NOT DEFINED ...)` is satisfied by a stale cache entry just as well as by a real
#     -D, so sibling build trees (the private test suite configures Sally sub-builds of its
#     own) silently kept their old value.
#
# The second one bit harder when generation still happened in the source tree:
# sibling build directories could stamp different cached versions into one shared
# header. Generation is now build-local, and this unconditional value also keeps
# each independently configured tree current.
#
# If you genuinely need a different value, pass -DSALLY_VERSION_OVERRIDE=x.y.z. That is a
# distinct name, so it cannot be confused with a leftover cache entry.
if(SALLY_VERSION_OVERRIDE)
    set(SALLY_LOCAL_DEV_VERSION "${SALLY_VERSION_OVERRIDE}")
else()
    # Read the version from the header the RESOURCES already use, instead of keeping a
    # second copy here. Two copies is what went wrong: this file said 1.0.25 while
    # spl_vers.h still said 1,0,0, so the About box and the file-properties dialog
    # disagreed about what the build was.
    #
    # The direction is deliberate. rc.exe has no __has_include, so it cannot safely
    # include a CMake-generated header that does not exist in a fresh clone before
    # configure. CMake, by contrast, can always read a plain header. So the header is the
    # source and this is the consumer.
    set(_sally_vers_header "${CMAKE_SOURCE_DIR}/src/plugins/shared/spl_vers.h")
    if(NOT EXISTS "${_sally_vers_header}")
        message(FATAL_ERROR "Sally version header not found: ${_sally_vers_header}")
    endif()
    file(READ "${_sally_vers_header}" _sally_vers_txt)

    # Anchored on "#define" so the macro BODIES below the definitions, which mention the
    # same names inside VERSINFO_xstr(...), cannot be picked up instead.
    foreach(_part MAJOR MINORA MINORB)
        string(REGEX MATCH "#define[ 	]+VERSINFO_SALAMANDER_${_part}[ 	]+([0-9]+)"
               _sally_vers_match "${_sally_vers_txt}")
        if(NOT _sally_vers_match)
            message(FATAL_ERROR
                "Could not parse VERSINFO_SALAMANDER_${_part} from ${_sally_vers_header}. "
                "Refusing to guess - a wrong version here ships silently.")
        endif()
        set(_sally_v_${_part} "${CMAKE_MATCH_1}")
    endforeach()

    set(SALLY_LOCAL_DEV_VERSION "${_sally_v_MAJOR}.${_sally_v_MINORA}.${_sally_v_MINORB}")
    message(STATUS "Sally version (from spl_vers.h): ${SALLY_LOCAL_DEV_VERSION}")
endif()

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    # Get the latest tag
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --match "v*" --abbrev=0
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
        if(SALLY_LOCAL_DEV_VERSION)
            set(GIT_TAG_VERSION "${SALLY_LOCAL_DEV_VERSION}")
        else()
            # Strip leading 'v' if present (v1.0.0 -> 1.0.0)
            string(REGEX REPLACE "^v" "" GIT_TAG_VERSION "${GIT_TAG}")
        endif()

        # Check if we're exactly on the tag
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --match "v*" --exact-match
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_EXACT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE GIT_EXACT_RESULT
        )

        if(GIT_EXACT_RESULT EQUAL 0)
            # Exactly on tag: just use version (e.g., 1.0.0)
            set(GIT_VERSION "${GIT_TAG_VERSION}")
        else()
            # Ahead of tag: use version-hash (e.g., 1.0.0-9c98a4b)
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

# Configure the header in this build tree. Multiple test and production builds
# may configure Sally concurrently, so generated files must never be shared
# through the source checkout.
set(SALLY_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${SALLY_GENERATED_INCLUDE_DIR}")
configure_file(
    "${CMAKE_SOURCE_DIR}/src/git_version.h.in"
    "${SALLY_GENERATED_INCLUDE_DIR}/git_version.h"
    @ONLY
)
