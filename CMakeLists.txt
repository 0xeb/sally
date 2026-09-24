cmake_minimum_required(VERSION 3.21)

project(Sally LANGUAGES C CXX RC)


# [endgame:P4.2] THE FLIP. Sally is a Unicode application: UNICODE/_UNICODE are
# defined unconditionally, so the TCHAR-generic Win32 macros resolve to their W forms.
#
# This was `SALLY_UNICODE_CANARY`, default OFF, and its entire job was to fail
# informatively - turning "what breaks if we define UNICODE?" into a categorisable
# error list instead of an estimate. That job is finished: the whole solution
# (sally.exe, 31 plugins, tools, salext) builds at 0 errors with the define, in
# both Debug and Release, and the private gate is 188/188.
#
# Unconditional, and there is no switch to turn it off. The narrow configuration
# is not a supported build of Sally any more, so offering an option to produce one
# would only create a way to ship or test something that is not the product.
#
# STILL TRUE, and the reason a green build was never the whole bar: a class/flag
# mismatch is a runtime TRACE_C that no compiler detects (see the CFilesBox
# UnicodeWnd bug, sally@ac00cbe9 - registered wide, flagged narrow, compiled
# perfectly, died on the first message). P4.3's GUI smoke is what covers that.
#
# tests/CMakeLists.txt sets the same two definitions and must stay in lockstep:
# that tree compiles these same production sources without including this file.
add_compile_definitions(UNICODE _UNICODE)

# Generate git version header
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/git_version.cmake)

# Detect CI by environment (can be overridden explicitly via -DSAL_UNDER_CI=...).
set(_SAL_UNDER_CI_DEFAULT OFF)
if(DEFINED ENV{CI} AND NOT "$ENV{CI}" STREQUAL "")
  set(_SAL_UNDER_CI_DEFAULT ON)
endif()
option(SAL_UNDER_CI "Enable CI-friendly defaults for reproducible automation builds" ${_SAL_UNDER_CI_DEFAULT})

# Demo/sample plugin toggle.
# Default is ON locally, OFF under CI unless explicitly overridden.
set(_SAL_BUILD_DEMOPLUG_DEFAULT ON)
if(SAL_UNDER_CI)
  set(_SAL_BUILD_DEMOPLUG_DEFAULT OFF)
endif()
option(SAL_BUILD_DEMOPLUG "Build DemoPlug sample plugin (includes DemoPlug FS)" ${_SAL_BUILD_DEMOPLUG_DEFAULT})
set(SALLY_PREVIEW_REGISTRY_VERSION "" CACHE STRING "Optional preview/beta registry version suffix, e.g. 1.0.21")

if(NOT WIN32)
  message(FATAL_ERROR "Sally CMake build targets Windows only.")
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

# yyjson 0.12.0 is shared by the core settings reader and CheckVer plugin.
add_library(yyjson STATIC "${SAL_SRC}/common/dep/yyjson/yyjson.c")
target_include_directories(yyjson PUBLIC "${SAL_SRC}/common/dep")

if(NOT SALLY_PREVIEW_REGISTRY_VERSION STREQUAL "")
  list(APPEND SAL_COMMON_DEFINES "SALLY_PREVIEW_REGISTRY_VERSION=\"${SALLY_PREVIEW_REGISTRY_VERSION}\"")
endif()

# Restore NuGet packages (WebView2 SDK, etc.)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sal_nuget.cmake)

# ==============================================================================
# Main Executable: sally
# ==============================================================================

# Source list (extracted from vcxproj)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/targets/sally_sources.cmake)

add_executable(sally WIN32)

target_include_directories(sally PRIVATE
  "${SALLY_GENERATED_INCLUDE_DIR}"
  ${SAL_COMMON_INCLUDES}
)

target_compile_definitions(sally PRIVATE
  SAFE_ALLOC
  INSIDE_SALAMANDER
  ${SAL_COMMON_DEFINES}
  $<$<CONFIG:Debug>:${SAL_DEBUG_DEFINES}>
  $<${SAL_IS_RELEASE}:${SAL_RELEASE_DEFINES}>
)

target_link_libraries(sally PRIVATE
  htmlhelp comctl32 mpr ws2_32 netapi32 msimg32 shlwapi
  yyjson
  ${SAL_COMMON_LIBS}
)

target_link_directories(sally PRIVATE
  "${SAL_SHARED}/libs/${SAL_PLATFORM}"
)

if(MSVC)
  target_link_options(sally PRIVATE
    /SUBSYSTEM:WINDOWS
    /STACK:3145728
    /MANIFEST:NO
  )
elseif(MINGW)
  target_link_options(sally PRIVATE
    -Wl,--subsystem,windows
    -Wl,--stack,3145728
  )
endif()

# Resource file settings
set_source_files_properties(${sally_RC} PROPERTIES
  COMPILE_DEFINITIONS "WINVER=0x0601;$<$<CONFIG:Debug>:_DEBUG>;$<${SAL_IS_RELEASE}:NDEBUG>;${SAL_RC_PLATFORM_DEFINES}"
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
foreach(src IN LISTS SALLY_SOURCES)
  if(NOT src MATCHES "/reglib/src/")
    list(APPEND SAL_SOURCES_NO_REGLIB ${src})
  endif()
endforeach()

target_sources(sally PRIVATE ${SAL_SOURCES_NO_REGLIB} ${sally_RC})
target_link_libraries(sally PRIVATE reglib)

# Shell extension registration (C file with extern "C" linkage)
target_sources(sally PRIVATE "${SAL_SRC}/shexreg.c")

# Output directories
set_target_properties(sally PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}"
  ARCHIVE_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/lib"
  LIBRARY_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/bin"
)

# ==============================================================================
# Language Resource: English .SLG
# ==============================================================================

# RC files need defines set via source properties, not target_compile_definitions
set_source_files_properties("${SAL_SRC}/lang/lang.rc" PROPERTIES
  COMPILE_DEFINITIONS "_LANG;_LANG_SALLY;WINVER=0x0601;$<$<CONFIG:Debug>:_DEBUG>;$<${SAL_IS_RELEASE}:NDEBUG>;${SAL_RC_PLATFORM_DEFINES}"
)

add_library(sal_lang_english SHARED "${SAL_SRC}/lang/lang.rc")

target_include_directories(sal_lang_english PRIVATE
  "${SAL_SRC}"
  "${SAL_SHARED}"
)

if(MSVC)
  target_link_options(sal_lang_english PRIVATE
    /NOENTRY /MANIFEST:NO /NOIMPLIB
    $<${SAL_IS_RELEASE}:/LTCG:OFF>  # No code to optimize in resource-only DLL
  )
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
  "${SAL_SRC}/salmon/salmon_unicode.cpp"
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
  $<${SAL_IS_RELEASE}:${SAL_RELEASE_DEFINES}>
)

target_link_libraries(salmon PRIVATE
  comctl32 shlwapi wininet ws2_32
)

if(MSVC)
  target_link_options(salmon PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO)
elseif(MINGW)
  target_link_options(salmon PRIVATE -Wl,--subsystem,windows)
endif()

set_target_properties(salmon PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/utils"
)

# ==============================================================================
# Helper Utilities: salspawn.exe (minimal, no CRT)
# ==============================================================================

# salspawn - Process spawning helper (console)
add_executable(salspawn
  "${SAL_SRC}/common/lstrfix.cpp"
  "${SAL_SRC}/common/rtc_stubs.cpp"
  "${SAL_SRC}/salspawn/salspawn.cpp"
  "${SAL_SRC}/salspawn/salspawn.rc"
)

target_include_directories(salspawn PRIVATE
  "${SAL_SRC}/salspawn"
  "${SAL_SRC}/common"
)

target_compile_definitions(salspawn PRIVATE
  WIN32 _CONSOLE
  WINVER=0x0601 _WIN32_WINNT=0x0601 _WIN32_IE=0x0800
  LSTRFIX_WITHOUT_RTL NDEBUG
)

if(MSVC)
  # Disable /GL (whole program optimization) - conflicts with no CRT
  target_compile_options(salspawn PRIVATE /MP /W3 /MT /GS- /GL-)
  target_link_options(salspawn PRIVATE
    /SUBSYSTEM:CONSOLE
    /MANIFEST:NO
    /NODEFAULTLIB
    /ENTRY:mainCRTStartup
  )
  target_link_libraries(salspawn PRIVATE kernel32 user32 shell32)
endif()

set_target_properties(salspawn PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/utils"
)

# salopen.exe was retired. It opened an association from a separate short-lived process,
# reached only through Configuration.UseSalOpen ("Open files through salopen.exe"), which
# defaulted to off. The Unicode work deleted SalOpenExecute() and every reader of that
# setting, leaving the executable shipped with no caller. The protection it was there for -
# never handing a shell extension a window Sally cares about - now lives in
# ExecuteAssociationW(), which invokes the default verb with a throwaway CShellExecuteWnd.

# ==============================================================================
# Trace Server: tserver.exe
# ==============================================================================

add_executable(tserver WIN32
  # tserver sources
  "${SAL_SRC}/tserver/config.cpp"
  "${SAL_SRC}/tserver/dialogs.cpp"
  "${SAL_SRC}/tserver/ms_init.cpp"
  "${SAL_SRC}/tserver/openedit.cpp"
  "${SAL_SRC}/tserver/precomp.cpp"
  "${SAL_SRC}/tserver/registry.cpp"
  "${SAL_SRC}/tserver/tablist.cpp"
  "${SAL_SRC}/tserver/trace_export.cpp"
  "${SAL_SRC}/tserver/tserver.cpp"
  "${SAL_SRC}/tserver/window.cpp"
  "${SAL_SRC}/tserver/tserver.rc"
  # Common library sources
  "${SAL_SRC}/common/allochan.cpp"
  "${SAL_SRC}/common/array.cpp"
  "${SAL_SRC}/common/dib.cpp"
  "${SAL_SRC}/common/handles.cpp"
  "${SAL_SRC}/common/heap.cpp"
  "${SAL_SRC}/common/lstrfix.cpp"
  "${SAL_SRC}/common/messages.cpp"
  "${SAL_SRC}/common/multimon.cpp"
  "${SAL_SRC}/common/sheets.cpp"
  "${SAL_SRC}/common/strutils.cpp"
  "${SAL_SRC}/common/trace.cpp"
  "${SAL_SRC}/common/winlib.cpp"
)

target_include_directories(tserver PRIVATE
  "${SAL_SRC}/tserver"
  "${SAL_SRC}/common"
  "${SAL_SRC}/common/dep"
  "${SAL_SRC}"
)

# tserver debug defines (matches tserver.vcxproj - includes TRACE_TO_FILE, no __DEBUG_WINLIB)
set(TSERVER_DEBUG_DEFINES
  _DEBUG
  SAFE_ALLOC
  __TRACESERVER
  TRACE_ENABLE
  TRACE_TO_FILE
  HANDLES_ENABLE
  MULTITHREADED_TRACE_ENABLE
  MULTITHREADED_MESSAGES_ENABLE
  MULTITHREADED_HANDLES_ENABLE
  MULTITHREADED_HEAP_ENABLE
  MESSAGES_DEBUG
  _CRTDBG_MAP_ALLOC
  _ALLOW_RTCc_IN_STL
)

target_compile_definitions(tserver PRIVATE
  UNICODE _UNICODE
  WIN32 _WINDOWS
  WINVER=0x0601 _WIN32_WINNT=0x0601 _WIN32_IE=0x0800
  __TRACESERVER  # Always needed (C__MessageType, IsErrorMsg, etc.)
  $<$<CONFIG:Debug>:${TSERVER_DEBUG_DEFINES}>
  $<${SAL_IS_RELEASE}:NDEBUG;SAFE_ALLOC;ARRAY_NODEBUG;MULTITHREADED_HEAP_ENABLE>
)

target_link_libraries(tserver PRIVATE comctl32)

if(MSVC)
  target_precompile_headers(tserver PRIVATE "${SAL_SRC}/tserver/precomp.h")
  target_link_options(tserver PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO)
  # openedit.cpp uses #import which is incompatible with /MP (multi-process compilation)
  set_source_files_properties("${SAL_SRC}/tserver/openedit.cpp" PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    COMPILE_FLAGS "/MP1"
  )
elseif(MINGW)
  target_link_options(tserver PRIVATE -Wl,--subsystem,windows)
endif()

set_source_files_properties("${SAL_SRC}/tserver/tserver.rc" PROPERTIES
  COMPILE_DEFINITIONS "WINVER=0x0601;$<$<CONFIG:Debug>:_DEBUG>;$<${SAL_IS_RELEASE}:NDEBUG>;$<$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>:_WIN64>"
)

set_target_properties(tserver PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/utils"
)

# ==============================================================================
# Shell Extension: salext.dll (COM shell extension for Explorer integration)
# ==============================================================================
# Builds for the current platform (x64 or x86). For full Explorer integration,
# both architectures are needed — build x86 variant via a separate CMake preset.

add_library(salext MODULE
  "${SAL_SRC}/common/lstrfix.c"
  "${SAL_SRC}/common/rtc_stubs.cpp"
  "${SAL_SRC}/shellext/copyhook.c"
  "${SAL_SRC}/shellext/ctxmenu.c"
  "${SAL_SRC}/shellext/shellext.c"
  "${SAL_SRC}/shellext/shexinit.c"
  "${SAL_SRC}/shexreg.c"
  "${SAL_SRC}/shexreg_ipc_names.c"
  "${SAL_SRC}/shexreg_ipc_protocol.c"
  "${SAL_SRC}/shellext/shellext.rc"
)

target_include_directories(salext PRIVATE
  "${SAL_SRC}/shellext"
  "${SAL_SRC}/common"
  "${SAL_SRC}"
)

target_compile_definitions(salext PRIVATE
  WIN32 _WINDOWS _USRDLL INC_OLE2
  WINVER=0x0601 _WIN32_WINNT=0x0601 _WIN32_IE=0x0800
  LSTRFIX_WITHOUT_RTL
  $<$<CONFIG:Debug>:_DEBUG>
  $<${SAL_IS_RELEASE}:NDEBUG>
)

set_source_files_properties("${SAL_SRC}/shellext/shellext.rc" PROPERTIES
  COMPILE_DEFINITIONS "WINVER=0x0601;$<$<CONFIG:Debug>:_DEBUG>;$<${SAL_IS_RELEASE}:NDEBUG>;${SAL_RC_PLATFORM_DEFINES}"
)

if(MSVC)
  # No CRT — standalone COM DLL (like salspawn)
  target_compile_options(salext PRIVATE /GS- /GL- /GR-)
  set_target_properties(salext PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
  target_link_options(salext PRIVATE
    /SUBSYSTEM:WINDOWS
    /MANIFEST:NO
    /NODEFAULTLIB
    /NOIMPLIB
    /DEF:"${SAL_SRC}/shellext/shellext.def"
    $<${SAL_IS_RELEASE}:/LTCG:OFF>
  )
  # x86 uses StdCall calling convention
  if(SAL_PLATFORM STREQUAL "x86")
    target_compile_options(salext PRIVATE /Gz)
  endif()
  target_link_libraries(salext PRIVATE kernel32 user32 advapi32 ole32 shell32 uuid)
endif()

# Output name matches VS convention: salextx64.dll or salextx86.dll
set_target_properties(salext PROPERTIES
  OUTPUT_NAME "salext${SAL_PLATFORM}"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/utils"
  LIBRARY_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/utils"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
)

# ==============================================================================
# Plugins
# ==============================================================================

add_subdirectory(src/plugins)

# ==============================================================================
# Optional Developer Tools
# ==============================================================================

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sal_dev_tools.cmake)

# ==============================================================================
# Installation Rules
# ==============================================================================

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sal_install.cmake)
include(GNUInstallDirs)

# Main executable
sal_install_main(sally)

# Utils go in utils/
install(TARGETS salmon salspawn tserver RUNTIME DESTINATION utils)
install(CODE [[
  file(REMOVE
    "${CMAKE_INSTALL_PREFIX}/utils/translator.exe"
    "${CMAKE_INSTALL_PREFIX}/utils/translator.pdb"
    "${CMAKE_INSTALL_PREFIX}/utils/translator.ilk"
  )
]])

# Shell extension goes in utils/ (Salamander looks for utils\salextx64.dll)
install(TARGETS salext
  LIBRARY DESTINATION utils
  RUNTIME DESTINATION utils
)

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

# Plugin version file for incremental plugin updates
sal_generate_plugins_ver()

# ==============================================================================
# Populate Target (install to build output)
# ==============================================================================

sal_create_populate_target()

# Ensure populate depends on main targets
add_dependencies(populate sally salmon salspawn tserver salext sal_lang_english)

message(STATUS "")
message(STATUS "=== Sally CMake Build ===")
message(STATUS "Output: ${SAL_OUTPUT_BASE}/<Config>_${SAL_PLATFORM}")
message(STATUS "SAL_UNDER_CI: ${SAL_UNDER_CI}")
message(STATUS "SAL_BUILD_DEMOPLUG: ${SAL_BUILD_DEMOPLUG}")
message(STATUS "")
