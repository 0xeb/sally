# Installation and deployment rules for Open Salamander
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
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
      install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION .)
    endif()
  endif()
endfunction()

# Install all registered plugins
function(sal_install_plugins)
  sal_get_all_plugins(PLUGINS)
  foreach(PLUGIN_TARGET ${PLUGINS})
    get_target_property(PLUGIN_OUTPUT_NAME ${PLUGIN_TARGET} OUTPUT_NAME)
    install(TARGETS ${PLUGIN_TARGET}
      LIBRARY DESTINATION "plugins/${PLUGIN_OUTPUT_NAME}"
      RUNTIME DESTINATION "plugins/${PLUGIN_OUTPUT_NAME}"
    )
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

  # IEViewer CSS
  if(EXISTS "${SAL_PLUGINS}/ieviewer/cmark-gfm/css")
    install(DIRECTORY "${SAL_PLUGINS}/ieviewer/cmark-gfm/css/"
      DESTINATION "plugins/ieviewer/css"
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
endfunction()
