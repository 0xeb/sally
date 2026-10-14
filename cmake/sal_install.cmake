# Installation and deployment rules for Sally
# Handles copying runtime files, resources, and third-party dependencies

include_guard(GLOBAL)

if(NOT DEFINED SAL_ROOT)
  include("${CMAKE_CURRENT_LIST_DIR}/sal_common.cmake")
endif()

# Install the main executable
function(sal_install_main TARGET_NAME)
  install(TARGETS ${TARGET_NAME}
    RUNTIME DESTINATION .
  )
endfunction()

# Install MSVC runtime libraries
function(sal_install_runtime)
  # Skip for cross-compilation - runtime DLLs must be provided separately
  if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling: skipping MSVC runtime installation")
    return()
  endif()
  if(MSVC)
    # Prevent the module from creating its own install rules (which go to bin/)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    # Also find debug runtime libraries (adds to CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    set(CMAKE_INSTALL_DEBUG_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)

    # Separate debug and release libs into different lists
    # Debug libs are in paths containing "Debug_NonRedist" or "DebugCRT"
    set(RELEASE_RUNTIME_LIBS)
    set(DEBUG_RUNTIME_LIBS)
    foreach(LIB ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
      string(FIND "${LIB}" "Debug" IS_DEBUG_PATH)
      if(IS_DEBUG_PATH GREATER -1)
        list(APPEND DEBUG_RUNTIME_LIBS "${LIB}")
      else()
        list(APPEND RELEASE_RUNTIME_LIBS "${LIB}")
      endif()
    endforeach()

    # Install release libs for non-Debug configs
    if(RELEASE_RUNTIME_LIBS)
      install(PROGRAMS ${RELEASE_RUNTIME_LIBS}
        DESTINATION .
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel
      )
    endif()

    # Install debug libs for Debug config
    if(DEBUG_RUNTIME_LIBS)
      install(PROGRAMS ${DEBUG_RUNTIME_LIBS}
        DESTINATION .
        CONFIGURATIONS Debug
      )
    endif()
  endif()
endfunction()

# Install all registered plugins
function(sal_install_plugins)
  sal_get_all_plugins(PLUGINS)
  foreach(PLUGIN_TARGET ${PLUGINS})
    get_target_property(PLUGIN_DIR_NAME ${PLUGIN_TARGET} SAL_PLUGIN_NAME)
    install(TARGETS ${PLUGIN_TARGET}
      LIBRARY DESTINATION "plugins/${PLUGIN_DIR_NAME}"
      RUNTIME DESTINATION "plugins/${PLUGIN_DIR_NAME}"
    )
  endforeach()

endfunction()

# Generate plugins.ver file for auto-discovery of new plugins
# Format: first line is version, subsequent lines are "ver:path"
function(sal_generate_plugins_ver)
  sal_get_all_plugins(PLUGINS)

  # The number comes from the release (1.0.36 -> 10036), so every release offers its
  # plugins to existing profiles again; a profile takes only lines above the number it last
  # processed, and skips plugins already registered or removed by the user. It must match
  # sally::plugins_ver::NumberForVersion() in src/common/PluginsVerPolicy.h. (Version 1 was
  # generated into the install root with root-relative paths; 2 was fixed from 1.0.17 to
  # 1.0.36, so plugins added in between never reached existing profiles.)
  file(READ "${CMAKE_SOURCE_DIR}/src/plugins/shared/spl_vers.h" _plugins_ver_header)
  foreach(_part MAJOR MINORA MINORB)
    string(REGEX MATCH "#define[ \t]+VERSINFO_SALAMANDER_${_part}[ \t]+([0-9]+)" _plugins_ver_match "${_plugins_ver_header}")
    if(NOT _plugins_ver_match)
      message(FATAL_ERROR "plugins.ver: could not read VERSINFO_SALAMANDER_${_part} from spl_vers.h")
    endif()
    set(_plugins_ver_${_part} "${CMAKE_MATCH_1}")
  endforeach()
  math(EXPR PLUGINS_VER_NUMBER "${_plugins_ver_MAJOR} * 10000 + ${_plugins_ver_MINORA} * 100 + ${_plugins_ver_MINORB}")
  set(PLUGINS_VER_CONTENT "${PLUGINS_VER_NUMBER}\n")

  foreach(PLUGIN_TARGET ${PLUGINS})
    get_target_property(PLUGIN_DIR_NAME ${PLUGIN_TARGET} SAL_PLUGIN_NAME)
    get_target_property(PLUGIN_OUTPUT_NAME ${PLUGIN_TARGET} OUTPUT_NAME)
    # Use paths relative to the plugins directory, matching the DLL names saved
    # by the directory scanner so existing plugins are not installed twice.
    string(APPEND PLUGINS_VER_CONTENT "${PLUGINS_VER_NUMBER}:${PLUGIN_DIR_NAME}\\${PLUGIN_OUTPUT_NAME}.dll\n")
  endforeach()

  # Write to build directory
  file(WRITE "${CMAKE_BINARY_DIR}/plugins.ver" "${PLUGINS_VER_CONTENT}")

  # Install where the loader searches for it.
  install(FILES "${CMAKE_BINARY_DIR}/plugins.ver" DESTINATION plugins)

  # Version 1 was installed at the runtime root. Incremental populate must remove that obsolete
  # copy so a shipping tree has one authoritative manifest in the location the loader reads.
  install(CODE "file(REMOVE \"\${CMAKE_INSTALL_PREFIX}/plugins.ver\")")
endfunction()

# The files earlier releases installed and this one retired (src/salupdate/retired-files.txt),
# installed at the root as sally-update.obsolete for the updater and Sally to remove them from an
# installed Sally. An incremental populate removes them from the install tree too, so that a
# package never carries a retired file.
function(sal_install_obsolete_manifest)
  set(_manifest "${SAL_SRC}/salupdate/retired-files.txt")
  install(FILES "${_manifest}" DESTINATION . RENAME sally-update.obsolete)
  file(STRINGS "${_manifest}" _lines)
  foreach(_line IN LISTS _lines)
    if(_line STREQUAL "" OR _line MATCHES "^#" OR _line MATCHES "^SALLY-UPDATE-OBSOLETE")
      continue()
    endif()
    string(REPLACE "\\" "/" _path "${_line}")
    if(_path MATCHES "/$")
      string(REGEX REPLACE "/$" "" _path "${_path}")
      install(CODE "if(IS_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/${_path}\")
  file(GLOB _left \"\${CMAKE_INSTALL_PREFIX}/${_path}/*\")
  if(NOT _left)
    file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/${_path}\")
  endif()
endif()")
    else()
      install(CODE "file(REMOVE \"\${CMAKE_INSTALL_PREFIX}/${_path}\")")
    endif()
  endforeach()
endfunction()

# Install resource files (toolbars, convert tables, etc.)
function(sal_install_resources)
  # Toolbars
  if(EXISTS "${SAL_SRC}/res/toolbars")
    install(DIRECTORY "${SAL_SRC}/res/toolbars/" DESTINATION toolbars)
  endif()

  # Character conversion tables
  foreach(CONV_DIR centeuro cyrillic westeuro)
    if(EXISTS "${SAL_ROOT}/convert/${CONV_DIR}")
      install(DIRECTORY "${SAL_ROOT}/convert/${CONV_DIR}/"
        DESTINATION "convert/${CONV_DIR}"
        FILES_MATCHING PATTERN "*.*"
      )
    endif()
  endforeach()

  # Automation scripts
  if(EXISTS "${SAL_PLUGINS}/automation/sample-scripts")
    install(DIRECTORY "${SAL_PLUGINS}/automation/sample-scripts/"
      DESTINATION "plugins/automation/scripts"
      FILES_MATCHING PATTERN "*.*"
    )
  endif()

  # WebViewer CSS
  if(EXISTS "${SAL_PLUGINS}/webviewer/cmark-gfm/css")
    install(DIRECTORY "${SAL_PLUGINS}/webviewer/cmark-gfm/css/"
      DESTINATION "plugins/webviewer/css"
      FILES_MATCHING PATTERN "*.*"
    )
  endif()

  # ZIP plugin files
  if(EXISTS "${SAL_PLUGINS}/zip/zip2sfx")
    install(FILES
      "${SAL_PLUGINS}/zip/zip2sfx/readme.txt"
      "${SAL_PLUGINS}/zip/zip2sfx/sam_cz.set"
      "${SAL_PLUGINS}/zip/zip2sfx/sample.set"
      DESTINATION "plugins/zip/zip2sfx"
      OPTIONAL
    )
  endif()
endfunction()

# Create the 'populate' target that installs to the build output directory
function(sal_create_populate_target)
  add_custom_target(populate
    COMMAND ${CMAKE_COMMAND}
      -DCMAKE_INSTALL_CONFIG_NAME=$<CONFIG>
      -DCMAKE_INSTALL_PREFIX=${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}
      -P ${CMAKE_BINARY_DIR}/cmake_install.cmake
    COMMENT "Populating ${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}"
  )

  # Make populate depend on all plugin targets
  sal_get_all_plugins(PLUGINS)
  if(PLUGINS)
    add_dependencies(populate ${PLUGINS})
  endif()

  # Make populate depend on extra targets (like fcremote helper exe)
  get_property(EXTRA_DEPS GLOBAL PROPERTY SAL_EXTRA_POPULATE_DEPS)
  if(EXTRA_DEPS)
    add_dependencies(populate ${EXTRA_DEPS})
  endif()
endfunction()
