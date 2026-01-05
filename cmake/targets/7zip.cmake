# 7-Zip plugin and dependencies for Open Salamander
# Builds: 7za.dll (library), 7zwrapper.dll (wrapper), 7zip.spl (plugin)

include_guard(GLOBAL)

set(SEVENZIP_DIR "${SAL_PLUGINS}/7zip")
set(SEVENZIP_7ZA "${SEVENZIP_DIR}/7za")

# =============================================================================
# 7za.dll - 7-Zip compression library
# =============================================================================

# Load exact source list extracted from vcxproj
include("${CMAKE_CURRENT_LIST_DIR}/7za_sources.cmake")

add_library(7za SHARED ${7ZA_SOURCES})

target_include_directories(7za PRIVATE
  "${SEVENZIP_7ZA}/cpp"
  "${SEVENZIP_7ZA}/c"
)

target_compile_definitions(7za PRIVATE
  WIN32
  _WINDOWS
  _USRDLL
  BZIP2_EXTRACT_ONLY
  _7ZIP_LARGE_PAGES
  ${SAL_COMMON_DEFINES}
)

target_link_libraries(7za PRIVATE
  mpr
  comctl32
  oleaut32
  ole32
  user32
)

if(MSVC)
  target_compile_options(7za PRIVATE
    /MP
    /wd4456 /wd4457 /wd4458  # Shadowing warnings
    /wd4267  # Size conversion
    /Gz  # __stdcall
  )
  target_link_options(7za PRIVATE
    /DEF:"${SEVENZIP_7ZA}/spl/7za.def"
    /MANIFEST:NO
  )
endif()

# C files don't use PCH - filter from 7ZA_SOURCES
foreach(src ${7ZA_SOURCES})
  if(src MATCHES "\\.c$")
    set_source_files_properties("${src}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
  endif()
endforeach()

set_target_properties(7za PROPERTIES
  OUTPUT_NAME "7za"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/7zip"
  LIBRARY_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/7zip"
)

# =============================================================================
# 7zwrapper.dll - Wrapper for simplified 7za.dll access
# =============================================================================

add_library(7zwrapper SHARED
  "${SEVENZIP_7ZA}/cpp/7zip/Common/FileStreams.cpp"
  "${SEVENZIP_7ZA}/cpp/7zip/UI/7zwrapper/7zwrapper.cpp"
  "${SEVENZIP_7ZA}/cpp/7zip/UI/7zwrapper/StdAfx.cpp"
  "${SEVENZIP_7ZA}/cpp/Common/IntToString.cpp"
  "${SEVENZIP_7ZA}/cpp/Common/MyString.cpp"
  "${SEVENZIP_7ZA}/cpp/Common/MyVector.cpp"
  "${SEVENZIP_7ZA}/cpp/Common/NewHandler.cpp"
  "${SEVENZIP_7ZA}/cpp/Common/StringConvert.cpp"
  "${SEVENZIP_7ZA}/cpp/Common/Wildcard.cpp"
  "${SEVENZIP_7ZA}/cpp/Windows/DLL.cpp"
  "${SEVENZIP_7ZA}/cpp/Windows/FileDir.cpp"
  "${SEVENZIP_7ZA}/cpp/Windows/FileFind.cpp"
  "${SEVENZIP_7ZA}/cpp/Windows/FileIO.cpp"
  "${SEVENZIP_7ZA}/cpp/Windows/FileName.cpp"
  "${SEVENZIP_7ZA}/cpp/Windows/PropVariant.cpp"
  "${SEVENZIP_7ZA}/cpp/Windows/PropVariantConv.cpp"
)

target_include_directories(7zwrapper PRIVATE
  "${SEVENZIP_7ZA}/cpp"
)

target_compile_definitions(7zwrapper PRIVATE
  WIN32
  _WINDOWS
  _USRDLL
  ${SAL_COMMON_DEFINES}
)

target_link_libraries(7zwrapper PRIVATE
  oleaut32
  ole32
  user32
)

if(MSVC)
  target_compile_options(7zwrapper PRIVATE /MP /wd4267)
  target_link_options(7zwrapper PRIVATE /MANIFEST:NO)
endif()

set_target_properties(7zwrapper PROPERTIES
  OUTPUT_NAME "7zwrapper"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/7zip"
  LIBRARY_OUTPUT_DIRECTORY "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/plugins/7zip"
)

# =============================================================================
# 7zip.spl - Salamander plugin
# =============================================================================

sal_add_plugin(NAME 7zip
  SOURCES
    # 7za helper sources
    "${SEVENZIP_7ZA}/cpp/7zip/Common/FileStreams.cpp"
    "${SEVENZIP_7ZA}/cpp/Common/IntToString.cpp"
    "${SEVENZIP_7ZA}/cpp/Common/MyString.cpp"
    "${SEVENZIP_7ZA}/cpp/Common/MyVector.cpp"
    "${SEVENZIP_7ZA}/cpp/Common/StringConvert.cpp"
    "${SEVENZIP_7ZA}/cpp/Windows/DLL.cpp"
    "${SEVENZIP_7ZA}/cpp/Windows/FileIO.cpp"
    "${SEVENZIP_7ZA}/cpp/Windows/PropVariant.cpp"
    "${SEVENZIP_7ZA}/cpp/Windows/PropVariantConv.cpp"
    # Plugin sources
    "${SEVENZIP_DIR}/7zclient.cpp"
    "${SEVENZIP_DIR}/7zip.cpp"
    "${SEVENZIP_DIR}/7zthreads.cpp"
    "${SEVENZIP_DIR}/dialogs.cpp"
    "${SEVENZIP_DIR}/extract.cpp"
    "${SEVENZIP_DIR}/FStreams.cpp"
    "${SEVENZIP_DIR}/open.cpp"
    "${SEVENZIP_DIR}/precomp.cpp"
    "${SEVENZIP_DIR}/update.cpp"
  RC "${SEVENZIP_DIR}/7zip.rc"
  DEF "${SEVENZIP_DIR}/7zip.def"
  INCLUDES
    "${SEVENZIP_7ZA}/cpp"
)

# Plugin depends on wrapper and library being built
add_dependencies(plugin_7zip 7za 7zwrapper)

# Install 7za.dll and 7zwrapper.dll with plugin
install(TARGETS 7za 7zwrapper
  RUNTIME DESTINATION "plugins/7zip"
  LIBRARY DESTINATION "plugins/7zip"
)

message(STATUS "Configured 7-Zip plugin with dependencies")
