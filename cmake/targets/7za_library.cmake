# SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
# SPDX-License-Identifier: GPL-2.0-or-later

# Build the shared 7-Zip codec library.
function(sal_add_sevenzip_codec target)
  include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/7za_sources.cmake")

  add_library(${target} SHARED ${7ZA_SOURCES})
  target_sources(${target} PRIVATE "${SEVENZIP_7ZA}/spl/splthread.c")

  target_include_directories(${target} PRIVATE
    "${SEVENZIP_7ZA}/cpp"
    "${SEVENZIP_7ZA}/c"
    "${SEVENZIP_7ZA}/spl"
  )

  target_compile_definitions(${target} PRIVATE
    WIN32
    _WINDOWS
    _USRDLL
    BZIP2_EXTRACT_ONLY
    Z7_LARGE_PAGES
    Z7_EXTERNAL_CODECS
    ${SAL_COMMON_DEFINES}
  )

  target_link_libraries(${target} PRIVATE
    mpr
    comctl32
    oleaut32
    ole32
    user32
  )

  if(MSVC)
    target_compile_options(${target} PRIVATE
      /MP
      /wd4005  # Macro redefinition
      /wd4456 /wd4457 /wd4458  # Shadowing warnings
      /wd4267  # Size conversion
      /wd5043  # Exception specification mismatch (third-party code)
    )
    target_link_options(${target} PRIVATE
      /DEF:"${SEVENZIP_7ZA}/cpp/7zip/Archive/Archive2.def"
      /MANIFEST:NO
    )
  endif()

  # C files don't use PCH - filter from 7ZA_SOURCES
  foreach(src ${7ZA_SOURCES})
    if(src MATCHES "\\.c$")
      set_source_files_properties("${src}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    endif()
  endforeach()


endfunction()
