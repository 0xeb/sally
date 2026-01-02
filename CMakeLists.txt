cmake_minimum_required(VERSION 3.21)

project(OpenSalamander LANGUAGES C CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "Open Salamander CMake build targets Windows only.")
endif()

# ==============================================================================
# Global Settings
# ==============================================================================

if(MSVC)
  cmake_policy(SET CMP0091 NEW)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  add_compile_options(/MP /W3 /J)
  # Generate PDB files for Release builds too (for debugging crash dumps)
  add_compile_options("$<$<CONFIG:Release>:/Zi>")
  add_link_options("$<$<CONFIG:Release>:/DEBUG>")
elseif(MINGW)
  # MinGW / MinGW-w64 (GCC or Clang, native or cross-compilation)
  add_compile_options(
    -Wall
    -funsigned-char
    -fno-strict-aliasing
    "$<$<CONFIG:Debug>:-g;-O0>"
    "$<$<CONFIG:Release>:-O2>"
  )

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC-specific: Allow MSVC-style template code (dependent name lookup)
    add_compile_options(-fpermissive)
    add_link_options(-static-libgcc -static-libstdc++)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang/LLVM-specific options
    add_compile_options(
      -Wno-microsoft-template  # Allow MSVC template quirks
      -Wno-ignored-pragma-intrinsic
    )
    # Static link C++ runtime for standalone executables
    add_link_options(-static)
  endif()

  add_link_options(-Wl,--no-insert-timestamp)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Load common settings
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sal_common.cmake)

# ==============================================================================
# Main Executable: salamander
# ==============================================================================

# Source list (extracted from vcxproj)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/targets/salamand_sources.cmake)

add_executable(salamander WIN32)

target_include_directories(salamander PRIVATE
  ${SAL_COMMON_INCLUDES}
)

target_compile_definitions(salamander PRIVATE
  SAFE_ALLOC
  INSIDE_SALAMANDER
  ${SAL_COMMON_DEFINES}
  $<$<CONFIG:Debug>:${SAL_DEBUG_DEFINES}>
  $<$<CONFIG:Release>:${SAL_RELEASE_DEFINES}>
)

target_link_libraries(salamander PRIVATE
  htmlhelp comctl32 mpr wsock32 netapi32 msimg32 shlwapi
  ${SAL_COMMON_LIBS}
)

target_link_directories(salamander PRIVATE
  "${SAL_SHARED}/libs/${SAL_PLATFORM}"
)

if(MSVC)
  target_link_options(salamander PRIVATE
    /SUBSYSTEM:WINDOWS
    /STACK:3145728
  )
elseif(MINGW)
  target_link_options(salamander PRIVATE
    -Wl,--subsystem,windows
    -Wl,--stack,3145728
  )
endif()

# Resource file settings
set_source_files_properties(${SALAMANDER_RC} PROPERTIES
  COMPILE_DEFINITIONS "WINVER=0x0601"
)

# bzip2 C sources need BZ_NO_STDIO
file(GLOB BZIP2_C_SOURCES "${SAL_SRC}/common/dep/bzip2/*.c")
set_source_files_properties(${BZIP2_C_SOURCES} PROPERTIES
  COMPILE_DEFINITIONS "BZ_NO_STDIO"
)

# Build reglib as a separate static library
add_library(reglib STATIC
  "${SAL_SRC}/reglib/src/regcopy.cpp"
  "${SAL_SRC}/reglib/src/reginmem.cpp"
  "${SAL_SRC}/reglib/src/registry.cpp"
  "${SAL_SRC}/reglib/src/regparse.cpp"
)
target_include_directories(reglib PRIVATE "${SAL_SRC}/reglib/src")
target_compile_definitions(reglib PRIVATE ${SAL_COMMON_DEFINES})

# Add sources excluding reglib (it's linked separately)
set(SAL_SOURCES_NO_REGLIB)
foreach(src IN LISTS SALAMANDER_SOURCES)
  if(NOT src MATCHES "/reglib/src/")
    list(APPEND SAL_SOURCES_NO_REGLIB ${src})
  endif()
endforeach()

target_sources(salamander PRIVATE ${SAL_SOURCES_NO_REGLIB} ${SALAMANDER_RC})
target_link_libraries(salamander PRIVATE reglib)

# Shell extension registration (C file with extern "C" linkage)
target_sources(salamander PRIVATE "${SAL_SRC}/shexreg.c")
set_source_files_properties("${SAL_SRC}/shexreg.c" PROPERTIES
  COMPILE_DEFINITIONS "INSIDE_SALAMANDER"
)

# Output directories
set_target_properties(salamander PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}"
  ARCHIVE_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/lib"
  LIBRARY_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/bin"
)

# ==============================================================================
# Language Resource: English .SLG
# ==============================================================================

# RC files need defines set via source properties, not target_compile_definitions
set_source_files_properties("${SAL_SRC}/lang/lang.rc" PROPERTIES
  COMPILE_DEFINITIONS "_LANG;_LANG_SALAMANDER;WINVER=0x0601"
)

add_library(sal_lang_english SHARED "${SAL_SRC}/lang/lang.rc")

target_include_directories(sal_lang_english PRIVATE
  "${SAL_SRC}"
  "${SAL_SHARED}"
)

if(MSVC)
  target_link_options(sal_lang_english PRIVATE /NOENTRY)
elseif(MINGW)
  # Resource-only DLL: use minimal entry stub
  target_sources(sal_lang_english PRIVATE "${SAL_SRC}/common/dllstub.c")
endif()

set_target_properties(sal_lang_english PROPERTIES
  OUTPUT_NAME english
  SUFFIX .slg
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/lang"
  LIBRARY_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/lang"
  ARCHIVE_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/lang"
)

# ==============================================================================
# Bug Reporter: salmon.exe
# ==============================================================================

add_executable(salmon WIN32
  "${SAL_SRC}/salmon/compress.cpp"
  "${SAL_SRC}/salmon/config.cpp"
  "${SAL_SRC}/salmon/dialogs.cpp"
  "${SAL_SRC}/salmon/minidump.cpp"
  "${SAL_SRC}/salmon/precomp.cpp"
  "${SAL_SRC}/salmon/salmon.cpp"
  "${SAL_SRC}/salmon/upload.cpp"
  "${SAL_SRC}/salmon/salmon.rc"
  # Common library sources
  "${SAL_SRC}/common/winlib.cpp"
  "${SAL_SRC}/common/lstrfix.cpp"
  "${SAL_SRC}/common/trace.cpp"
  "${SAL_SRC}/common/messages.cpp"
  "${SAL_SRC}/common/handles.cpp"
  "${SAL_SRC}/common/array.cpp"
)

target_include_directories(salmon PRIVATE
  "${SAL_SRC}/salmon"
  ${SAL_COMMON_INCLUDES}
)

# Salmon debug defines (matches salmon_debug.props - no __DEBUG_WINLIB, adds TRACE_IGNORE_AUTOCLEAR)
set(SALMON_DEBUG_DEFINES
  _DEBUG
  TRACE_ENABLE
  TRACE_IGNORE_AUTOCLEAR
  HANDLES_ENABLE
  MESSAGES_DEBUG
  MULTITHREADED_TRACE_ENABLE
  MULTITHREADED_MESSAGES_ENABLE
  MULTITHREADED_HANDLES_ENABLE
  _CRTDBG_MAP_ALLOC
  _ALLOW_RTCc_IN_STL
)

target_compile_definitions(salmon PRIVATE
  ${SAL_COMMON_DEFINES}
  $<$<CONFIG:Debug>:${SALMON_DEBUG_DEFINES}>
  $<$<CONFIG:Release>:${SAL_RELEASE_DEFINES}>
)

target_link_libraries(salmon PRIVATE
  comctl32 shlwapi wininet dbghelp wsock32
)

if(MSVC)
  target_link_options(salmon PRIVATE /SUBSYSTEM:WINDOWS)
elseif(MINGW)
  target_link_options(salmon PRIVATE -Wl,--subsystem,windows)
endif()

set_target_properties(salmon PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/utils"
)

# ==============================================================================
# Plugins
# ==============================================================================

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/targets/plugins.cmake)

# ==============================================================================
# Installation Rules
# ==============================================================================

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sal_install.cmake)
include(GNUInstallDirs)

# Main executable
sal_install_main(salamander)

# Bug reporter goes in utils/
install(TARGETS salmon RUNTIME DESTINATION utils)

# MSVC runtime
sal_install_runtime()

# Language files
install(TARGETS sal_lang_english
  RUNTIME DESTINATION lang
  LIBRARY DESTINATION lang
)

# Plugins
sal_install_plugins()

# Resource files
sal_install_resources()

# ==============================================================================
# Populate Target (install to build output)
# ==============================================================================

sal_create_populate_target()

# Ensure populate depends on main targets
add_dependencies(populate salamander salmon sal_lang_english)

# ==============================================================================
# Tests
# ==============================================================================

enable_testing()
add_subdirectory(src/tests)

message(STATUS "")
message(STATUS "=== Open Salamander CMake Build ===")
message(STATUS "Output: ${SAL_OUTPUT_BASE}/<Config>_${SAL_PLATFORM}")
message(STATUS "")
