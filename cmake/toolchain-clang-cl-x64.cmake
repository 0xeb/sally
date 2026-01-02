# Clang-CL Cross-Compilation Toolchain for Windows x64
#
# Uses Clang in MSVC-compatibility mode (clang-cl) to cross-compile
# from Linux/macOS to Windows, targeting the MSVC ABI.
#
# Benefits over MinGW:
# - Full SEH support (__try/__except/__finally)
# - Same ABI as MSVC
# - Links against MSVC runtime (vcruntime140.dll)
#
# Requirements:
# 1. LLVM/Clang + LLD installed:
#    apt install clang-18 lld-18 llvm-18
# 2. Windows SDK via xwin:
#    cargo install xwin
#    xwin --accept-license splat --output ~/xwin

cmake_minimum_required(VERSION 3.21)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Find xwin installation
set(XWIN_SEARCH_PATHS
    $ENV{XWIN_DIR}
    $ENV{HOME}/xwin
    /opt/xwin
    ${CMAKE_CURRENT_LIST_DIR}/../xwin
)

foreach(path ${XWIN_SEARCH_PATHS})
    if(path AND EXISTS "${path}/crt/include")
        set(XWIN_DIR "${path}")
        break()
    endif()
endforeach()

if(NOT XWIN_DIR)
    message(FATAL_ERROR
        "Windows SDK not found. Install xwin:\n"
        "  cargo install xwin\n"
        "  xwin --accept-license splat --output ~/xwin\n"
        "Or set XWIN_DIR environment variable."
    )
endif()

message(STATUS "Using Windows SDK from: ${XWIN_DIR}")

# Find clang-cl (or clang with driver-mode wrapper)
find_program(CLANG_CL_COMPILER
    NAMES clang-cl clang-cl-18 clang-cl-17 clang-cl-16
    PATHS /usr/bin /usr/local/bin $ENV{HOME}/.local/bin
)
if(NOT CLANG_CL_COMPILER)
    # Check for wrapper in ~/.local/bin
    if(EXISTS "$ENV{HOME}/.local/bin/clang-cl")
        set(CLANG_CL_COMPILER "$ENV{HOME}/.local/bin/clang-cl")
    else()
        message(FATAL_ERROR
            "clang-cl not found. Create a wrapper script:\n"
            "  mkdir -p ~/.local/bin\n"
            "  echo '#!/bin/sh' > ~/.local/bin/clang-cl\n"
            "  echo 'exec /usr/bin/clang-18 --driver-mode=cl \"$@\"' >> ~/.local/bin/clang-cl\n"
            "  chmod +x ~/.local/bin/clang-cl"
        )
    endif()
endif()

# Find LLD linker (lld-link or lld)
find_program(LLD_LINK_LINKER
    NAMES lld-link lld-link-18 lld-link-17
    PATHS /usr/bin /usr/local/bin $ENV{HOME}/.local/bin /usr/lib/llvm-18/bin
)
if(NOT LLD_LINK_LINKER)
    # Try ld.lld
    find_program(LLD_LINKER
        NAMES ld.lld ld.lld-18 lld lld-18
        PATHS /usr/bin /usr/local/bin /usr/lib/llvm-18/bin
    )
    if(LLD_LINKER)
        set(LLD_LINK_LINKER "${LLD_LINKER}")
    else()
        message(FATAL_ERROR
            "LLD linker not found. Install lld:\n"
            "  sudo apt install lld-18\n"
            "Then create lld-link wrapper:\n"
            "  echo '#!/bin/sh' > ~/.local/bin/lld-link\n"
            "  echo 'exec /usr/lib/llvm-18/bin/lld -flavor link \"$@\"' >> ~/.local/bin/lld-link\n"
            "  chmod +x ~/.local/bin/lld-link"
        )
    endif()
endif()

# Find other LLVM tools
# Use wrapper script for llvm-rc that handles relative paths correctly
find_program(LLVM_RC_COMPILER NAMES llvm-rc-wrapper
    PATHS $ENV{HOME}/.local/bin
    NO_DEFAULT_PATH)
if(NOT LLVM_RC_COMPILER)
    find_program(LLVM_RC_COMPILER NAMES llvm-rc llvm-rc-19 llvm-rc-18
        PATHS /usr/bin /usr/local/bin $ENV{HOME}/.local/bin /usr/lib/llvm-19/bin /usr/lib/llvm-18/bin)
endif()
find_program(LLVM_LIB_TOOL NAMES llvm-lib llvm-lib-19 llvm-lib-18 llvm-ar
    PATHS /usr/bin /usr/local/bin $ENV{HOME}/.local/bin /usr/lib/llvm-19/bin /usr/lib/llvm-18/bin)
find_program(LLVM_MT_TOOL NAMES llvm-mt llvm-mt-19 llvm-mt-18
    PATHS /usr/bin /usr/local/bin $ENV{HOME}/.local/bin /usr/lib/llvm-19/bin /usr/lib/llvm-18/bin)

set(CMAKE_C_COMPILER "${CLANG_CL_COMPILER}")
set(CMAKE_CXX_COMPILER "${CLANG_CL_COMPILER}")
set(CMAKE_LINKER "${LLD_LINK_LINKER}")
set(CMAKE_RC_COMPILER "${LLVM_RC_COMPILER}")
set(CMAKE_AR "${LLVM_LIB_TOOL}")
set(CMAKE_MT "${LLVM_MT_TOOL}")

# Target triple
set(TARGET_TRIPLE "x86_64-pc-windows-msvc")

# Force release runtime (xwin doesn't include debug runtimes by default)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "" FORCE)

# Tell CMake this is an MSVC-like environment
set(CMAKE_C_COMPILER_ID Clang)
set(CMAKE_CXX_COMPILER_ID Clang)
set(CMAKE_C_COMPILER_FRONTEND_VARIANT MSVC)
set(CMAKE_CXX_COMPILER_FRONTEND_VARIANT MSVC)

# Compiler flags - build up include paths
set(CLANG_CL_FLAGS "--target=${TARGET_TRIPLE}")
# Fix for UCRT inline functions with Clang - disable inline and link library instead
set(CLANG_CL_FLAGS "${CLANG_CL_FLAGS} /D_NO_CRT_STDIO_INLINE")
set(CLANG_CL_FLAGS "${CLANG_CL_FLAGS} /imsvc \"${XWIN_DIR}/crt/include\"")
set(CLANG_CL_FLAGS "${CLANG_CL_FLAGS} /imsvc \"${XWIN_DIR}/sdk/include/ucrt\"")
set(CLANG_CL_FLAGS "${CLANG_CL_FLAGS} /imsvc \"${XWIN_DIR}/sdk/include/um\"")
set(CLANG_CL_FLAGS "${CLANG_CL_FLAGS} /imsvc \"${XWIN_DIR}/sdk/include/shared\"")
# Use MSVC-style exceptions
set(CLANG_CL_FLAGS "${CLANG_CL_FLAGS} /EHsc")
# Allow building with Clang 18 against newer MSVC STL (which expects Clang 19+)
set(CLANG_CL_FLAGS "${CLANG_CL_FLAGS} /D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH")

set(CMAKE_C_FLAGS_INIT "${CLANG_CL_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CLANG_CL_FLAGS}")

# Linker flags - library paths (use forward slashes, they work on Windows too)
set(LINK_FLAGS "/libpath:\"${XWIN_DIR}/crt/lib/x86_64\"")
set(LINK_FLAGS "${LINK_FLAGS} /libpath:\"${XWIN_DIR}/sdk/lib/um/x86_64\"")
set(LINK_FLAGS "${LINK_FLAGS} /libpath:\"${XWIN_DIR}/sdk/lib/ucrt/x86_64\"")
# Link legacy stdio for _NO_CRT_STDIO_INLINE compatibility
set(LINK_FLAGS "${LINK_FLAGS} legacy_stdio_definitions.lib")

set(CMAKE_EXE_LINKER_FLAGS_INIT "${LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${LINK_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${LINK_FLAGS}")

# Release: strip debug
set(CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "/OPT:REF /OPT:ICF")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE_INIT "/OPT:REF /OPT:ICF")

# Debug: include debug info
set(CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT "/DEBUG")
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT "/DEBUG")

# Resource compiler configuration
set(CMAKE_RC_COMPILER "${LLVM_RC_COMPILER}")
# RC compiler needs all SDK include paths and UTF-8 codepage
set(CMAKE_RC_FLAGS "/C 65001 /I \"${XWIN_DIR}/sdk/include/um\" /I \"${XWIN_DIR}/sdk/include/shared\" /I \"${XWIN_DIR}/sdk/include/ucrt\"")
# Tell CMake how to compile RC files
set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> <DEFINES> <INCLUDES> <FLAGS> /fo <OBJECT> <SOURCE>")
# RC doesn't create libraries directly - this is just for CMake internals
set(CMAKE_RC_CREATE_SHARED_LIBRARY "")

# Don't search host paths for libraries
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

message(STATUS "Clang-CL x64 cross-compilation configured")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  Linker: ${CMAKE_LINKER}")
message(STATUS "  Target: ${TARGET_TRIPLE}")
