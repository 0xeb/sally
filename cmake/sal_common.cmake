# Common settings and utilities for Open Salamander CMake build
# This file is included by the parent project's CMakeLists.txt

# Compute paths relative to the salamander submodule root
get_filename_component(SAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SAL_SRC "${SAL_ROOT}/src")
set(SAL_PLUGINS "${SAL_SRC}/plugins")
set(SAL_SHARED "${SAL_PLUGINS}/shared")

# Detect cross-compilation
if(CMAKE_CROSSCOMPILING)
  message(STATUS "Cross-compiling for Windows with ${CMAKE_CXX_COMPILER_ID}")
endif()

# Platform short name (matches VS build: x86, x64, or ARM64)
# For cross-compilation, use CMAKE_SYSTEM_PROCESSOR since sizeof(void*) is host size
# For native VS builds, check CMAKE_GENERATOR_PLATFORM (set by -A flag or preset)
if(CMAKE_CROSSCOMPILING)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(SAL_PLATFORM "x64")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64|arm64")
    set(SAL_PLATFORM "ARM64")
  else()
    set(SAL_PLATFORM "x86")
  endif()
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
  # Native ARM64 build (Visual Studio with -A ARM64)
  set(SAL_PLATFORM "ARM64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(SAL_PLATFORM "x64")
else()
  set(SAL_PLATFORM "x86")
endif()

# Output directory base
if(NOT DEFINED SAL_OUTPUT_DIR)
  if(DEFINED ENV{OPENSAL_BUILD_DIR} AND NOT "$ENV{OPENSAL_BUILD_DIR}" STREQUAL "")
    set(SAL_OUTPUT_DIR "$ENV{OPENSAL_BUILD_DIR}")
  else()
    set(SAL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/out/")
  endif()
endif()

# Ensure trailing slash
string(REGEX REPLACE "([^/])$" "\\1/" SAL_OUTPUT_DIR "${SAL_OUTPUT_DIR}")

# Full output path with config and platform
set(SAL_OUTPUT_BASE "${SAL_OUTPUT_DIR}salamander")

# Common preprocessor definitions (from sal_base.props)
set(SAL_COMMON_DEFINES
  _MT
  WIN32
  _WINDOWS
  WINVER=0x0601
  _WIN32_WINNT=0x0601
  _WIN32_IE=0x0800
  _CRT_SECURE_NO_WARNINGS
  _SCL_SECURE_NO_WARNINGS
  _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES
  _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT
)

# Debug-specific defines
set(SAL_DEBUG_DEFINES
  _DEBUG
  __DEBUG_WINLIB
  TRACE_ENABLE
  HANDLES_ENABLE
  MESSAGES_DEBUG
  MULTITHREADED_TRACE_ENABLE
  MULTITHREADED_MESSAGES_ENABLE
  MULTITHREADED_HANDLES_ENABLE
  _CRTDBG_MAP_ALLOC
  _ALLOW_RTCc_IN_STL
)

# Release-specific defines
set(SAL_RELEASE_DEFINES
  NDEBUG
  MESSAGES_DISABLE
)

# Common include directories
set(SAL_COMMON_INCLUDES
  "${SAL_SRC}"
  "${SAL_SRC}/common"
  "${SAL_SRC}/common/dep"
  "${SAL_SHARED}"
)

# Common link libraries
set(SAL_COMMON_LIBS
  comctl32
)

# x86 compiler flags (match VS x86.props: EnableEnhancedInstructionSet=SSE2)
if(MSVC AND SAL_PLATFORM STREQUAL "x86")
  add_compile_options(/arch:SSE2)
endif()

# Release optimizations (match VS *_release.props)
if(MSVC)
  add_compile_options($<$<CONFIG:Release>:/GL>)  # Whole program optimization
  add_compile_options($<$<CONFIG:Release>:/Gy>)  # Function-level linking
  add_link_options($<$<CONFIG:Release>:/LTCG>)   # Link-time code generation
endif()

message(STATUS "Salamander root: ${SAL_ROOT}")
message(STATUS "Salamander output: ${SAL_OUTPUT_BASE}")
