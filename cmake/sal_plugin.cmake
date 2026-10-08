# Plugin building utilities for Sally
# Provides sal_add_plugin() function to create plugin targets

include_guard(GLOBAL)

# Ensure common settings are loaded
if(NOT DEFINED SAL_ROOT)
  include("${CMAKE_CURRENT_LIST_DIR}/sal_common.cmake")
endif()

# sal_add_plugin_languages(TARGET <plugin_target> LANG_DIR <dir>)
#   Compiles the plugin's built-in languages into the plugin DLL: <dir>/languages.rc includes
#   English (<dir>/lang.rc) and every translation (<dir>/<tag>/lang.rc), each under its own
#   LANGUAGE.
function(sal_add_plugin_languages)
  cmake_parse_arguments(PARSE_ARGV 0 LANG "" "TARGET;LANG_DIR" "")

  if(NOT LANG_TARGET OR NOT LANG_LANG_DIR)
    message(FATAL_ERROR "sal_add_plugin_languages: TARGET and LANG_DIR are required")
  endif()
  if(NOT EXISTS "${LANG_LANG_DIR}/languages.rc")
    return()  # No language resources for this plugin
  endif()

  set(LANG_RC "${LANG_LANG_DIR}/languages.rc")
  sal_set_rc_platform_defines(${LANG_RC})
  target_sources(${LANG_TARGET} PRIVATE ${LANG_RC})
  get_filename_component(LANG_PARENT "${LANG_LANG_DIR}" DIRECTORY)
  target_include_directories(${LANG_TARGET} PRIVATE
    "${LANG_PARENT}"
    "${LANG_LANG_DIR}"
    "${SAL_SRC}"
    "${SAL_SHARED}"
  )
endfunction()

# sal_add_plugin(NAME <plugin_name>
#   SOURCES <source_files>...
#   [SOURCE_DIR <plugin_root>]
#   [INCLUDES <include_dirs>...]
#   [DEFINES <preprocessor_defines>...]
#   [LIBS <libraries>...]
#   [RC <resource_file>]
#   [DEF <module_definition_file>]
#   [PCH <precomp_header>]  # Precompiled header (default: precomp.h)
#   [NO_SHARED]  # Don't include shared plugin sources
#   [NO_PCH]     # Disable precompiled headers
#   [NO_LANG]    # Don't add lang/ languages (for custom lang paths)
# )
function(sal_add_plugin)
  cmake_parse_arguments(PARSE_ARGV 0 PLUGIN
    "NO_SHARED;NO_PCH;NO_LANG"
    "NAME;SOURCE_DIR;RC;DEF;PCH"
    "SOURCES;INCLUDES;DEFINES;LIBS"
  )

  if(NOT PLUGIN_NAME)
    message(FATAL_ERROR "sal_add_plugin: NAME is required")
  endif()

  set(TARGET_NAME "plugin_${PLUGIN_NAME}")
  if(PLUGIN_SOURCE_DIR)
    get_filename_component(PLUGIN_DIR "${PLUGIN_SOURCE_DIR}" ABSOLUTE)
  else()
    set(PLUGIN_DIR "${SAL_PLUGINS}/${PLUGIN_NAME}")
  endif()

  # Default PCH header
  if(NOT PLUGIN_PCH)
    set(PLUGIN_PCH "precomp.h")
  endif()

  # Collect sources
  set(ALL_SOURCES ${PLUGIN_SOURCES})

  # Add shared plugin sources unless NO_SHARED is specified
  if(NOT PLUGIN_NO_SHARED)
    list(APPEND ALL_SOURCES
      "${SAL_SHARED}/auxtools.cpp"
      "${SAL_SHARED}/dbg.cpp"
      "${SAL_SHARED}/plugindarkmode.cpp"
      "${SAL_SRC}/combo_dark_paint.cpp"
      "${SAL_SHARED}/winliblt.cpp"
    )
  endif()

  # Add resource file if specified. rc.exe does not define _WIN64/_M_ARM64 itself, and the
  # version resource (shared/versinfo.rc2 via spl_vers.h) names the platform from them.
  if(PLUGIN_RC)
    list(APPEND ALL_SOURCES "${PLUGIN_RC}")
    sal_set_rc_platform_defines("${PLUGIN_RC}")
  endif()

  # Create the plugin as a MODULE (shared library loaded at runtime)
  add_library(${TARGET_NAME} MODULE ${ALL_SOURCES})

  # Set output name and extension
  set_target_properties(${TARGET_NAME} PROPERTIES
    OUTPUT_NAME "${PLUGIN_NAME}"
    SUFFIX ".dll"
    PREFIX ""
    # Output to plugins/<name>/ subdirectory
    RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/${PLUGIN_NAME}"
    LIBRARY_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/${PLUGIN_NAME}"
  )

  # Include directories - plugin dir FIRST so its precomp.h is found by shared sources
  target_include_directories(${TARGET_NAME} PRIVATE
    "${PLUGIN_DIR}"
    ${PLUGIN_INCLUDES}
    ${SAL_COMMON_INCLUDES}
  )

  # Preprocessor definitions
  target_compile_definitions(${TARGET_NAME} PRIVATE
    ${SAL_COMMON_DEFINES}
    _USRDLL
    $<$<CONFIG:Debug>:${SAL_DEBUG_DEFINES}>
    $<${SAL_IS_RELEASE}:${SAL_RELEASE_DEFINES}>
    ${PLUGIN_DEFINES}
  )

  # Link libraries
  target_link_libraries(${TARGET_NAME} PRIVATE
    ${SAL_COMMON_LIBS}
    ${PLUGIN_LIBS}
  )

  # Library directories for prebuilt libs
  target_link_directories(${TARGET_NAME} PRIVATE
    "${SAL_SHARED}/libs/${SAL_PLATFORM}"
  )

  # MSVC-specific settings
  if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /MP /W3 /J)

    # Build linker flags: DEF file + no manifest + suppress import library
    set(PLUGIN_LINK_FLAGS "/MANIFEST:NO /NOIMPLIB")
    if(PLUGIN_DEF)
      set(PLUGIN_LINK_FLAGS "${PLUGIN_LINK_FLAGS} /DEF:\"${PLUGIN_DEF}\"")
    endif()

    set_target_properties(${TARGET_NAME} PROPERTIES
      LINK_FLAGS "${PLUGIN_LINK_FLAGS}"
    )
  endif()

  # Precompiled headers (CMake 3.16+)
  if(NOT PLUGIN_NO_PCH AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    # Find the precomp.h in the plugin directory
    set(PCH_HEADER "${PLUGIN_DIR}/${PLUGIN_PCH}")
    if(EXISTS "${PCH_HEADER}")
      # Use REUSE_FROM if we had a shared PCH, but each plugin has its own
      target_precompile_headers(${TARGET_NAME} PRIVATE "${PCH_HEADER}")
      message(STATUS "  PCH: ${PLUGIN_PCH}")
    else()
      message(STATUS "  PCH: not found (${PLUGIN_PCH})")
    endif()
  endif()

  # Store original plugin name for install paths (directory name stays the same even if OUTPUT_NAME differs)
  set_target_properties(${TARGET_NAME} PROPERTIES SAL_PLUGIN_NAME "${PLUGIN_NAME}")

  # Add to global list of plugins
  set_property(GLOBAL APPEND PROPERTY SAL_PLUGINS_LIST ${TARGET_NAME})

  # Built-in languages (unless NO_LANG is specified)
  if(NOT PLUGIN_NO_LANG)
    sal_add_plugin_languages(TARGET ${TARGET_NAME} LANG_DIR "${PLUGIN_DIR}/lang")
  endif()

  message(STATUS "Added plugin: ${PLUGIN_NAME}")
endfunction()

# Convenience function to get all plugin targets
function(sal_get_all_plugins OUT_VAR)
  get_property(_plugins GLOBAL PROPERTY SAL_PLUGINS_LIST)
  set(${OUT_VAR} ${_plugins} PARENT_SCOPE)
endfunction()
