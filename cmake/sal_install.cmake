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

  # Install plugin language files
  get_property(PLUGIN_LANGS GLOBAL PROPERTY SAL_PLUGIN_LANGS_LIST)
  foreach(LANG_TARGET ${PLUGIN_LANGS})
    # Extract plugin name from target name (plugin_<name>_lang -> <name>)
    string(REGEX REPLACE "^plugin_(.+)_lang$" "\\1" PLUGIN_NAME ${LANG_TARGET})
    install(TARGETS ${LANG_TARGET}
      LIBRARY DESTINATION "plugins/${PLUGIN_NAME}/lang"
      RUNTIME DESTINATION "plugins/${PLUGIN_NAME}/lang"
    )
  endforeach()
endfunction()

# Generate plugins.ver file for auto-discovery of new plugins
# Format: first line is version, subsequent lines are "ver:path"
function(sal_generate_plugins_ver)
  sal_get_all_plugins(PLUGINS)

  # Start with version 1
  set(PLUGINS_VER_CONTENT "1\n")

  foreach(PLUGIN_TARGET ${PLUGINS})
    get_target_property(PLUGIN_DIR_NAME ${PLUGIN_TARGET} SAL_PLUGIN_NAME)
    get_target_property(PLUGIN_OUTPUT_NAME ${PLUGIN_TARGET} OUTPUT_NAME)
    # Use forward slashes in the file, Salamander handles both
    string(APPEND PLUGINS_VER_CONTENT "1:plugins/${PLUGIN_DIR_NAME}/${PLUGIN_OUTPUT_NAME}.dll\n")
  endforeach()

  # Write to build directory
  file(WRITE "${CMAKE_BINARY_DIR}/plugins.ver" "${PLUGINS_VER_CONTENT}")

  # Install to root
  install(FILES "${CMAKE_BINARY_DIR}/plugins.ver" DESTINATION .)
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

  # Make populate depend on all plugin language targets
  get_property(PLUGIN_LANGS GLOBAL PROPERTY SAL_PLUGIN_LANGS_LIST)
  if(PLUGIN_LANGS)
    add_dependencies(populate ${PLUGIN_LANGS})
  endif()

  # Make populate depend on extra targets (like fcremote helper exe)
  get_property(EXTRA_DEPS GLOBAL PROPERTY SAL_EXTRA_POPULATE_DEPS)
  if(EXTRA_DEPS)
    add_dependencies(populate ${EXTRA_DEPS})
  endif()
endfunction()
