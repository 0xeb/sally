# Clang-CL Toolchain for Windows ARM64 cross-compilation (MSVC ABI)
#
# This targets the MSVC ABI, NOT MinGW. This means:
# - _MSC_VER is defined
# - __try/__except/__finally work natively
# - Links against MSVC runtime (vcruntime140.dll, etc.)
#
# Requires: xwin to download Windows SDK and CRT
#   xwin --accept-license --arch aarch64 splat --output /opt/xwin
#
# Or set XWIN_DIR environment variable to your xwin output location.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ARM64)

# Use clang-cl (MSVC-compatible driver) not plain clang
set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)

# Find xwin installation
set(XWIN_SEARCH_PATHS
    $ENV{XWIN_DIR}
    /opt/xwin
    /usr/local/xwin
    $ENV{HOME}/xwin
    $ENV{HOME}/.xwin
)

foreach(path ${XWIN_SEARCH_PATHS})
    if(path AND EXISTS "${path}/crt/include")
        set(XWIN_DIR "${path}")
        break()
    endif()
endforeach()

if(NOT XWIN_DIR)
    message(FATAL_ERROR
        "xwin installation not found!\n"
        "Install xwin and run:\n"
        "  xwin --accept-license --arch aarch64 splat --output /opt/xwin\n"
        "Or set XWIN_DIR environment variable.\n"
        "Get xwin from: https://github.com/Jake-Shadle/xwin"
    )
endif()

message(STATUS "Using xwin from: ${XWIN_DIR}")

# Target triple for ARM64 Windows
set(TARGET_TRIPLE "aarch64-pc-windows-msvc")

# Compiler flags
set(CMAKE_C_FLAGS_INIT "--target=${TARGET_TRIPLE}")
set(CMAKE_CXX_FLAGS_INIT "--target=${TARGET_TRIPLE}")

# Include paths for Windows SDK and CRT
set(CLANG_CL_INCLUDES
    "-imsvc \"${XWIN_DIR}/crt/include\""
    "-imsvc \"${XWIN_DIR}/sdk/include/ucrt\""
    "-imsvc \"${XWIN_DIR}/sdk/include/um\""
    "-imsvc \"${XWIN_DIR}/sdk/include/shared\""
)
string(JOIN " " CLANG_CL_INCLUDES_STR ${CLANG_CL_INCLUDES})

set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} ${CLANG_CL_INCLUDES_STR}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${CLANG_CL_INCLUDES_STR}")

# Library paths for linker - note: aarch64 directory name
set(LINK_DIRS
    "/libpath:\"${XWIN_DIR}/crt/lib/aarch64\""
    "/libpath:\"${XWIN_DIR}/sdk/lib/um/aarch64\""
    "/libpath:\"${XWIN_DIR}/sdk/lib/ucrt/aarch64\""
)
string(JOIN " " LINK_DIRS_STR ${LINK_DIRS})

set(CMAKE_EXE_LINKER_FLAGS_INIT "${LINK_DIRS_STR}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${LINK_DIRS_STR}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${LINK_DIRS_STR}")

# Release: optimize
set(CMAKE_C_FLAGS_RELEASE_INIT "/O2 /DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "/O2 /DNDEBUG")

# Debug: debug info, no optimization
set(CMAKE_C_FLAGS_DEBUG_INIT "/Od /Zi /D_DEBUG")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "/Od /Zi /D_DEBUG")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT "${LINK_DIRS_STR} /DEBUG")
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT "${LINK_DIRS_STR} /DEBUG")

# Resource compiler flags (llvm-rc)
set(CMAKE_RC_FLAGS_INIT "-I \"${XWIN_DIR}/sdk/include/um\"")

# Don't look for programs/libraries on the host
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

message(STATUS "Clang-CL ARM64 toolchain configured")
message(STATUS "  Target: ${TARGET_TRIPLE}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  Linker: ${CMAKE_LINKER}")
