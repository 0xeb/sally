# Plugin build definition for Sally
# Plugin: sftp
#
# libssh2, OpenSSL and zlib come from the plugin-local vcpkg.json and are linked statically into
# sftp.dll (triplet *-windows-static-md: static libraries, shared /MD C runtime like the rest of Sally).
# vcpkg is run from here at configure time; the rest of Sally's build does not use vcpkg.

# OFF by default: the first configure builds OpenSSL from source (10-20 minutes per platform). Turn it on with
# -DSAL_BUILD_SFTP=ON (also for release builds); then a missing vcpkg is a configure error, not a silent skip.
option(SAL_BUILD_SFTP "Build the SFTP plugin (needs vcpkg; libssh2, OpenSSL and zlib are built from source)" OFF)

if(NOT SAL_BUILD_SFTP)
  message(STATUS "Skipping plugin: sftp (SAL_BUILD_SFTP=OFF)")
  return()
endif()

# The libraries are built with the same MSVC toolset Sally links with, so only a native Windows build
# with the Visual Studio generator qualifies (not the clang-cl cross-compile presets).
if(NOT CMAKE_HOST_WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR NOT CMAKE_GENERATOR MATCHES "^Visual Studio")
  message(STATUS "Skipping plugin: sftp (needs a native Windows build with MSVC and a Visual Studio generator)")
  return()
endif()

# vcpkg executable: VCPKG_ROOT (CMake variable or environment), e.g. the copy bundled with Visual Studio
set(_SFTP_VCPKG_ROOT "")
if(DEFINED VCPKG_ROOT AND NOT "${VCPKG_ROOT}" STREQUAL "")
  set(_SFTP_VCPKG_ROOT "${VCPKG_ROOT}")
elseif(DEFINED ENV{VCPKG_ROOT})
  set(_SFTP_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
endif()
file(TO_CMAKE_PATH "${_SFTP_VCPKG_ROOT}" _SFTP_VCPKG_ROOT)

find_program(SAL_SFTP_VCPKG_EXE vcpkg PATHS "${_SFTP_VCPKG_ROOT}" NO_DEFAULT_PATH)
if(NOT SAL_SFTP_VCPKG_EXE)
  message(FATAL_ERROR "sftp: SAL_BUILD_SFTP=ON but vcpkg was not found. Set the VCPKG_ROOT environment variable "
    "(or the CMake variable) to a vcpkg folder, e.g. the one bundled with Visual Studio, or use -DSAL_BUILD_SFTP=OFF.")
endif()

if(SAL_PLATFORM STREQUAL "x64")
  set(_SFTP_TRIPLET "x64-windows-static-md")
elseif(SAL_PLATFORM STREQUAL "ARM64")
  set(_SFTP_TRIPLET "arm64-windows-static-md")
else()
  message(STATUS "Skipping plugin: sftp (unsupported platform ${SAL_PLATFORM})")
  return()
endif()

# editing vcpkg.json (versions, features) has to rerun this file, because that is where vcpkg install runs
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SAL_PLUGINS}/sftp/vcpkg.json")

# Overlay ports (OpenSSL 3.5 LTS, libssh2 with security patches): see vcpkg-ports/README.md. An overlay port
# replaces the registry port of the same name, so the "overrides" of those two ports are not in vcpkg.json.
set(_SFTP_OVERLAY_PORTS "${SAL_PLUGINS}/sftp/vcpkg-ports")
file(GLOB_RECURSE _SFTP_OVERLAY_FILES CONFIGURE_DEPENDS "${_SFTP_OVERLAY_PORTS}/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_SFTP_OVERLAY_FILES})

# Overlay triplet: same name and content as the stock triplet (so the install path stays the same), plus
# the MSVC toolset version of this build. Without it vcpkg picks the newest toolset on the machine, which can
# be newer than the one Sally links with (mixed C++ runtimes). VCPKG_VISUAL_STUDIO_PATH is not used: vcpkg
# rejects a forward-slash path and cannot write a backslash path, and the toolset version selects the
# Visual Studio installation that has it.
set(_SFTP_TRIPLET_DIR "${CMAKE_BINARY_DIR}/vcpkg_triplets")
set(_SFTP_STOCK_TRIPLET "")
foreach(_dir "${_SFTP_VCPKG_ROOT}/triplets" "${_SFTP_VCPKG_ROOT}/triplets/community")
  if(EXISTS "${_dir}/${_SFTP_TRIPLET}.cmake")
    set(_SFTP_STOCK_TRIPLET "${_dir}/${_SFTP_TRIPLET}.cmake")
    break()
  endif()
endforeach()
if(NOT _SFTP_STOCK_TRIPLET)
  message(FATAL_ERROR "sftp: the vcpkg triplet ${_SFTP_TRIPLET} was not found under ${_SFTP_VCPKG_ROOT}/triplets. "
    "Configure with -DSAL_BUILD_SFTP=OFF to skip the SFTP plugin.")
endif()
set(_SFTP_TRIPLET_TEXT "include(\"${_SFTP_STOCK_TRIPLET}\")\n")
# Toolset by version (MSVC 19.44 = toolset 14.44), not by name: vcpkg ties the name v143 to Visual Studio 2022
# installations, but Sally can use the v143 toolset installed side by side in a newer Visual Studio.
if(CMAKE_CXX_COMPILER_VERSION MATCHES "^19\\.([0-9]+)")
  string(APPEND _SFTP_TRIPLET_TEXT "set(VCPKG_PLATFORM_TOOLSET_VERSION \"14.${CMAKE_MATCH_1}\")\n")
endif()
file(WRITE "${_SFTP_TRIPLET_DIR}/${_SFTP_TRIPLET}.cmake" "${_SFTP_TRIPLET_TEXT}")

set(_SFTP_INSTALL_ROOT "${CMAKE_BINARY_DIR}/vcpkg_installed")
message(STATUS "sftp: vcpkg install (${_SFTP_TRIPLET}, MSVC ${CMAKE_CXX_COMPILER_VERSION}) - the first run builds OpenSSL and takes several minutes")
execute_process(
  COMMAND "${SAL_SFTP_VCPKG_EXE}" install
    "--x-manifest-root=${SAL_PLUGINS}/sftp"
    "--x-install-root=${_SFTP_INSTALL_ROOT}"
    "--triplet=${_SFTP_TRIPLET}"
    "--overlay-triplets=${_SFTP_TRIPLET_DIR}"
    "--overlay-ports=${_SFTP_OVERLAY_PORTS}"
    "--x-buildtrees-root=${CMAKE_BINARY_DIR}/vcpkg_work/buildtrees"
    "--x-packages-root=${CMAKE_BINARY_DIR}/vcpkg_work/packages"
    --disable-metrics
  RESULT_VARIABLE _SFTP_VCPKG_RESULT
)
if(NOT _SFTP_VCPKG_RESULT EQUAL 0)
  message(FATAL_ERROR "sftp: 'vcpkg install' failed (exit code ${_SFTP_VCPKG_RESULT}). "
    "See the output above and the logs in ${CMAKE_BINARY_DIR}/vcpkg_work/buildtrees, "
    "or configure with -DSAL_BUILD_SFTP=OFF to skip the SFTP plugin.")
endif()

# Pin OpenSSL and zlib to the vcpkg copies, per configuration (lib\ for Release, debug\lib\ for Debug), so
# that a Debug build links Debug libraries (matching the Debug C runtime) and no system copy can win.
# FORCE: a value cached by an earlier configure must not survive.
set(_SFTP_PREFIX "${_SFTP_INSTALL_ROOT}/${_SFTP_TRIPLET}")
set(OPENSSL_USE_STATIC_LIBS ON)
set(OPENSSL_INCLUDE_DIR "${_SFTP_PREFIX}/include" CACHE PATH "OpenSSL include directory (sftp plugin, vcpkg)" FORCE)
set(LIB_EAY_RELEASE "${_SFTP_PREFIX}/lib/libcrypto.lib" CACHE FILEPATH "libcrypto, Release (sftp plugin, vcpkg)" FORCE)
set(LIB_EAY_DEBUG "${_SFTP_PREFIX}/debug/lib/libcrypto.lib" CACHE FILEPATH "libcrypto, Debug (sftp plugin, vcpkg)" FORCE)
set(SSL_EAY_RELEASE "${_SFTP_PREFIX}/lib/libssl.lib" CACHE FILEPATH "libssl, Release (sftp plugin, vcpkg)" FORCE)
set(SSL_EAY_DEBUG "${_SFTP_PREFIX}/debug/lib/libssl.lib" CACHE FILEPATH "libssl, Debug (sftp plugin, vcpkg)" FORCE)
# vcpkg's static zlib is zs.lib / zsd.lib, names FindZLIB does not look for
set(ZLIB_INCLUDE_DIR "${_SFTP_PREFIX}/include" CACHE PATH "zlib include directory (sftp plugin, vcpkg)" FORCE)
set(ZLIB_LIBRARY_RELEASE "${_SFTP_PREFIX}/lib/zs.lib" CACHE FILEPATH "zlib, Release (sftp plugin, vcpkg)" FORCE)
set(ZLIB_LIBRARY_DEBUG "${_SFTP_PREFIX}/debug/lib/zsd.lib" CACHE FILEPATH "zlib, Debug (sftp plugin, vcpkg)" FORCE)

# vcpkg installs only Release and Debug; the other configurations link the Release libraries
# (they use the release C runtime, like Sally's own RelWithDebInfo and MinSizeRel)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)

list(PREPEND CMAKE_PREFIX_PATH "${_SFTP_PREFIX}")
find_package(libssh2 CONFIG REQUIRED)

# nothing from outside the vcpkg install may have been picked up (e.g. through OPENSSL_ROOT_DIR)
foreach(_target IN ITEMS libssh2::libssh2_static OpenSSL::Crypto OpenSSL::SSL ZLIB::ZLIB)
  foreach(_prop IN ITEMS IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_DEBUG INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(_value ${_target} ${_prop})
    foreach(_path IN LISTS _value)
      if(_path AND NOT _path MATCHES "-NOTFOUND$")
        cmake_path(IS_PREFIX _SFTP_INSTALL_ROOT "${_path}" NORMALIZE _SFTP_UNDER_ROOT)
        if(NOT _SFTP_UNDER_ROOT)
          message(FATAL_ERROR "sftp: ${_target} ${_prop} '${_path}' is outside the vcpkg install "
            "(${_SFTP_INSTALL_ROOT}); OpenSSL and zlib must come from there. Check OPENSSL_ROOT_DIR and ZLIB_ROOT.")
        endif()
      endif()
    endforeach()
  endforeach()
endforeach()

# libssh2.h makes LIBSSH2_API __declspec(dllimport) when _WINDLL is defined (MSBuild defines it for every
# DLL project); the library is linked statically, so its functions must not be imported
set_property(TARGET libssh2::libssh2_static APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS "LIBSSH2_API=")

sal_add_plugin(NAME sftp
  SOURCES
    "${SAL_PLUGINS}/sftp/precomp.cpp"
    "${SAL_PLUGINS}/sftp/fs/sftp.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_interface.cpp"
    "${SAL_PLUGINS}/sftp/fs/site_config.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_object.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_messages.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_data.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_transfer.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_namespace.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_delete.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_chmod.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_info.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_properties.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_view.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_commandline.cpp"
    "${SAL_PLUGINS}/sftp/fs/fs_menu.cpp"
    "${SAL_PLUGINS}/sftp/fs/operations_registry.cpp"
    "${SAL_PLUGINS}/sftp/fs/wait_for_request.cpp"
    "${SAL_PLUGINS}/sftp/ui/dark_controls.cpp"
    "${SAL_PLUGINS}/sftp/ui/host_key_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/prompt_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/interaction_ui.cpp"
    "${SAL_PLUGINS}/sftp/ui/error_text.cpp"
    "${SAL_PLUGINS}/sftp/ui/problem_text.cpp"
    "${SAL_PLUGINS}/sftp/ui/operation_window.cpp"
    "${SAL_PLUGINS}/sftp/ui/operations_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/solve_error_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/copy_move_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/chmod_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/info_list_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/info_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/exec_output_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/properties_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/site_edit_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/site_list_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/config_dialog.cpp"
    "${SAL_PLUGINS}/sftp/ui/session_log_window.cpp"
  RC "${SAL_PLUGINS}/sftp/sftp.rc"
  DEF "${SAL_PLUGINS}/sftp/sftp.def"
  LIBS sftp_ops sftp_core libssh2::libssh2_static ws2_32 crypt32 bcrypt user32 advapi32 comdlg32 uxtheme
)

# ---- Unit tests ----------------------------------------------------------------------------------------------
# sftp_core: the Sally-independent part of the plugin (libssh2/OpenSSL/zlib only). It gets no Sally include paths,
# so the layer rule (core/ includes nothing from Sally) is enforced by the build. Linked into plugin_sftp.
enable_testing()
include(GoogleTest)
find_package(GTest CONFIG REQUIRED)

file(GLOB _SFTP_CORE_SOURCES CONFIGURE_DEPENDS "${SAL_PLUGINS}/sftp/core/*.cpp")
add_library(sftp_core STATIC ${_SFTP_CORE_SOURCES})
target_include_directories(sftp_core PUBLIC "${SAL_PLUGINS}/sftp/core")
target_link_libraries(sftp_core PUBLIC libssh2::libssh2_static OpenSSL::Crypto ws2_32 crypt32 bcrypt)
# the same Windows target definitions as the plugin (WINVER/_WIN32_WINNT 0x0601, CRT warnings); not the Sally debug
# defines (TRACE_ENABLE, ...): they belong to the Sally headers, which core/ does not use
target_compile_definitions(sftp_core PRIVATE ${SAL_COMMON_DEFINES})
set_target_properties(sftp_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# sftp_ops: the Sally-independent part of the operations layer (ops/ may use core/, never Sally). Same rule: no Sally
# include paths. Linked into plugin_sftp.
file(GLOB _SFTP_OPS_SOURCES CONFIGURE_DEPENDS "${SAL_PLUGINS}/sftp/ops/*.cpp")
add_library(sftp_ops STATIC ${_SFTP_OPS_SOURCES})
target_include_directories(sftp_ops PUBLIC "${SAL_PLUGINS}/sftp/ops")
target_link_libraries(sftp_ops PUBLIC sftp_core)
target_compile_definitions(sftp_ops PRIVATE ${SAL_COMMON_DEFINES})
set_target_properties(sftp_ops PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

file(GLOB _SFTP_TEST_SOURCES CONFIGURE_DEPENDS "${SAL_PLUGINS}/sftp/tests/*.cpp")
add_executable(sftp_tests ${_SFTP_TEST_SOURCES})
target_link_libraries(sftp_tests PRIVATE sftp_ops sftp_core GTest::gtest GTest::gtest_main)
target_compile_definitions(sftp_tests PRIVATE SFTP_TEST_DIR_DEFAULT="${SAL_PLUGINS}/sftp/tests/docker/out"
                                                SFTP_SOURCE_DIR="${SAL_PLUGINS}/sftp")
set_target_properties(sftp_tests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
gtest_discover_tests(sftp_tests DISCOVERY_MODE PRE_TEST)
