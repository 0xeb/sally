# SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
# SPDX-License-Identifier: GPL-2.0-or-later
include_guard(GLOBAL)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_supporter_root "${CMAKE_CURRENT_LIST_DIR}/..")
get_filename_component(_supporter_root "${_supporter_root}" ABSOLUTE)
set(_sodium "${_supporter_root}/src/common/dep/libsodium")
include("${CMAKE_CURRENT_LIST_DIR}/sal_sodium_sources.cmake")
function(sal_configure_sodium_header)
  set(VERSION "1.0.22")
  set(SODIUM_LIBRARY_VERSION_MAJOR 26)
  set(SODIUM_LIBRARY_VERSION_MINOR 4)
  set(SODIUM_LIBRARY_MINIMAL_DEF "")
  configure_file("${_sodium}/src/libsodium/include/sodium/version.h.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/sodium-include/sodium/version.h" @ONLY)
endfunction()
sal_configure_sodium_header()
set(SALLY_PUBLIC_KEY_MANIFEST "${_supporter_root}/src/common/supporter/public-keys.json" CACHE FILEPATH "Public verification key manifest")
set(_public_keys "${CMAKE_CURRENT_BINARY_DIR}/supporter-generated/PublicKeys.cpp")
add_custom_command(OUTPUT "${_public_keys}"
  COMMAND "${Python3_EXECUTABLE}" -B "${_supporter_root}/tools/generate-public-keys.py"
    --manifest "${SALLY_PUBLIC_KEY_MANIFEST}" --output "${_public_keys}"
  DEPENDS "${SALLY_PUBLIC_KEY_MANIFEST}" "${_supporter_root}/tools/generate-public-keys.py"
    VERBATIM)
set(SALLY_PUBLIC_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/supporter-generated/PublicResources.rc")
file(GENERATE OUTPUT "${SALLY_PUBLIC_RESOURCE}" CONTENT
  "#include <windows.h>\nLANGUAGE LANG_NEUTRAL, SUBLANG_NEUTRAL\n62099 RCDATA \"${SALLY_PUBLIC_KEY_MANIFEST}\"\n")
set_source_files_properties("${SALLY_PUBLIC_RESOURCE}" PROPERTIES OBJECT_DEPENDS "${SALLY_PUBLIC_KEY_MANIFEST}")

set(_cmark "${_supporter_root}/src/plugins/webviewer/cmark-gfm")
file(GLOB _cmark_sources CONFIGURE_DEPENDS "${_cmark}/src/*.c")
list(FILTER _cmark_sources EXCLUDE REGEX "/main\\.c$")

function(sal_add_supporter_variant suffix runtime)
  add_library(sally_sodium_${suffix} STATIC ${SALLY_SODIUM_FILES})
  target_include_directories(sally_sodium_${suffix} PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}/sodium-include" "${CMAKE_CURRENT_BINARY_DIR}/sodium-include/sodium" "${_sodium}/src/libsodium/include"
    "${_sodium}/src/libsodium/include/sodium")
  target_compile_definitions(sally_sodium_${suffix} PUBLIC SODIUM_STATIC)
  target_compile_definitions(sally_sodium_${suffix} PRIVATE inline=__inline NATIVE_LITTLE_ENDIAN _CRT_SECURE_NO_WARNINGS)
  if(MSVC)
    target_compile_options(sally_sodium_${suffix} PRIVATE /wd4146 /wd4244)
  endif()
  target_link_libraries(sally_sodium_${suffix} PUBLIC advapi32)
  set_target_properties(sally_sodium_${suffix} PROPERTIES MSVC_RUNTIME_LIBRARY "${runtime}" INTERPROCEDURAL_OPTIMIZATION FALSE)
  add_library(sally_cmark_${suffix} STATIC ${_cmark_sources})
  target_include_directories(sally_cmark_${suffix} PUBLIC "${_cmark}/src" "${_cmark}/build/src")
  target_compile_definitions(sally_cmark_${suffix} PUBLIC CMARK_GFM_STATIC_DEFINE)
  target_compile_definitions(sally_cmark_${suffix} PRIVATE _CRT_SECURE_NO_WARNINGS)
  set_target_properties(sally_cmark_${suffix} PROPERTIES MSVC_RUNTIME_LIBRARY "${runtime}")
  add_library(sally_supporter_${suffix} STATIC
    "${_supporter_root}/src/common/supporter/Supporter.cpp"
    "${_supporter_root}/src/common/Win32SupporterState.cpp"
    "${_supporter_root}/src/common/Win32ApplicationAppearance.cpp"
    "${_supporter_root}/src/common/Win32SupporterUi.cpp"
    "${_supporter_root}/src/common/update/DisplayContent.cpp" "${_public_keys}")
  target_include_directories(sally_supporter_${suffix} PUBLIC "${_supporter_root}/src/common" PRIVATE "${_supporter_root}/src" "${_supporter_root}/src/common/dep")
  target_link_libraries(sally_supporter_${suffix} PUBLIC sally_sodium_${suffix} sally_cmark_${suffix})
  target_link_libraries(sally_supporter_${suffix} PUBLIC shell32 ole32 uuid)
  set_target_properties(sally_supporter_${suffix} PROPERTIES MSVC_RUNTIME_LIBRARY "${runtime}")
  if(MSVC)
    target_compile_options(sally_supporter_${suffix} PRIVATE /utf-8)
  endif()
endfunction()
sal_add_supporter_variant(md "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
sal_add_supporter_variant(mt "MultiThreaded$<$<CONFIG:Debug>:Debug>")
