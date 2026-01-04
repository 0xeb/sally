# Plugin building utilities for Open Salamander
# Provides sal_add_plugin() function to create plugin targets

include_guard(GLOBAL)

# Ensure common settings are loaded
if(NOT DEFINED SAL_ROOT)
  include("${CMAKE_CURRENT_LIST_DIR}/sal_common.cmake")
endif()

# sal_add_plugin_lang(NAME <plugin_name>)
#   Adds the English language file (.slg) for a plugin
#   Looks for lang/lang.rc in the plugin directory
function(sal_add_plugin_lang)
  cmake_parse_arguments(PARSE_ARGV 0 LANG "" "NAME" "")

  if(NOT LANG_NAME)
    message(FATAL_ERROR "sal_add_plugin_lang: NAME is required")
  endif()

  set(PLUGIN_DIR "${SAL_PLUGINS}/${LANG_NAME}")
  set(LANG_RC "${PLUGIN_DIR}/lang/lang.rc")

  if(NOT EXISTS "${LANG_RC}")
    return()  # No language file for this plugin
  endif()

  set(TARGET_NAME "plugin_${LANG_NAME}_lang")

  # RC files need defines set via source properties
  set_source_files_properties("${LANG_RC}" PROPERTIES
    COMPILE_DEFINITIONS "_LANG;WINVER=0x0601;$<$<CONFIG:Debug>:_DEBUG>;$<$<CONFIG:Release>:NDEBUG>;$<$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>:_WIN64>"
  )

  add_library(${TARGET_NAME} SHARED "${LANG_RC}")

  target_include_directories(${TARGET_NAME} PRIVATE
    "${PLUGIN_DIR}"
    "${PLUGIN_DIR}/lang"
    "${SAL_SRC}"
    "${SAL_SHARED}"
  )

  if(MSVC)
    target_link_options(${TARGET_NAME} PRIVATE /NOENTRY)
  endif()

  set_target_properties(${TARGET_NAME} PROPERTIES
    OUTPUT_NAME english
    SUFFIX .slg
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/${LANG_NAME}/lang"
    LIBRARY_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/${LANG_NAME}/lang"
    ARCHIVE_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/${LANG_NAME}/lang"
  )

  # Add to global list of plugin language targets
  set_property(GLOBAL APPEND PROPERTY SAL_PLUGIN_LANGS_LIST ${TARGET_NAME})
endfunction()

# sal_add_plugin(NAME <plugin_name>
#   SOURCES <source_files>...
#   [INCLUDES <include_dirs>...]
#   [DEFINES <preprocessor_defines>...]
#   [LIBS <libraries>...]
#   [RC <resource_file>]
#   [DEF <module_definition_file>]
#   [PCH <precomp_header>]  # Precompiled header (default: precomp.h)
#   [NO_SHARED]  # Don't include shared plugin sources
#   [NO_PCH]     # Disable precompiled headers
# )
function(sal_add_plugin)
  cmake_parse_arguments(PARSE_ARGV 0 PLUGIN
    "NO_SHARED;NO_PCH"
    "NAME;RC;DEF;PCH"
    "SOURCES;INCLUDES;DEFINES;LIBS"
  )

  if(NOT PLUGIN_NAME)
    message(FATAL_ERROR "sal_add_plugin: NAME is required")
  endif()

  set(TARGET_NAME "plugin_${PLUGIN_NAME}")
  set(PLUGIN_DIR "${SAL_PLUGINS}/${PLUGIN_NAME}")

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
      "${SAL_SHARED}/mhandles.cpp"
      "${SAL_SHARED}/winliblt.cpp"
    )
  endif()

  # Add resource file if specified
  if(PLUGIN_RC)
    list(APPEND ALL_SOURCES "${PLUGIN_RC}")
  endif()

  # Create the plugin as a MODULE (shared library loaded at runtime)
  add_library(${TARGET_NAME} MODULE ${ALL_SOURCES})

  # Set output name and extension
  set_target_properties(${TARGET_NAME} PROPERTIES
    OUTPUT_NAME "${PLUGIN_NAME}"
    SUFFIX ".spl"
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
    $<$<CONFIG:Release>:${SAL_RELEASE_DEFINES}>
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

    # Build linker flags: DEF file + no manifest
    set(PLUGIN_LINK_FLAGS "/MANIFEST:NO")
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

  # Add to global list of plugins
  set_property(GLOBAL APPEND PROPERTY SAL_PLUGINS_LIST ${TARGET_NAME})

  # Build language file if it exists
  sal_add_plugin_lang(NAME ${PLUGIN_NAME})

  message(STATUS "Added plugin: ${PLUGIN_NAME}")
endfunction()

# Convenience function to get all plugin targets
function(sal_get_all_plugins OUT_VAR)
  get_property(_plugins GLOBAL PROPERTY SAL_PLUGINS_LIST)
  set(${OUT_VAR} ${_plugins} PARENT_SCOPE)
endfunction()
