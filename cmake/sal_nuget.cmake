# NuGet package restoration for Sally CMake build
# Restores packages listed in packages.config and sets variables for consuming them.

include_guard(GLOBAL)

set(SAL_PACKAGES_DIR "${CMAKE_BINARY_DIR}/packages")

# Find nuget.exe on PATH or download it
find_program(NUGET_EXE nuget)
if(NOT NUGET_EXE)
  set(NUGET_EXE "${CMAKE_BINARY_DIR}/nuget.exe")
  if(NOT EXISTS "${NUGET_EXE}")
    message(STATUS "Downloading nuget.exe...")
    file(DOWNLOAD
      "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe"
      "${NUGET_EXE}"
      STATUS NUGET_DL_STATUS
    )
    list(GET NUGET_DL_STATUS 0 NUGET_DL_CODE)
    if(NOT NUGET_DL_CODE EQUAL 0)
      message(FATAL_ERROR "Failed to download nuget.exe: ${NUGET_DL_STATUS}")
    endif()
  endif()
endif()

# Restore packages
set(NUGET_CONFIG "${SAL_ROOT}/packages.config")
if(EXISTS "${NUGET_CONFIG}")
  message(STATUS "Restoring NuGet packages...")
  execute_process(
    COMMAND "${NUGET_EXE}" restore "${NUGET_CONFIG}"
      -PackagesDirectory "${SAL_PACKAGES_DIR}"
      -NonInteractive
    RESULT_VARIABLE NUGET_RESULT
    OUTPUT_VARIABLE NUGET_OUTPUT
    ERROR_VARIABLE NUGET_ERROR
  )
  if(NOT NUGET_RESULT EQUAL 0)
    message(WARNING "NuGet restore failed (${NUGET_RESULT}): ${NUGET_ERROR}")
  endif()
endif()

# Find WebView2 SDK package
file(GLOB WEBVIEW2_DIRS "${SAL_PACKAGES_DIR}/Microsoft.Web.WebView2.*")
if(WEBVIEW2_DIRS)
  list(GET WEBVIEW2_DIRS -1 WEBVIEW2_DIR)  # Use latest version if multiple
  set(WEBVIEW2_INCLUDE "${WEBVIEW2_DIR}/build/native/include")
  # Use static loader library (no WebView2Loader.dll to ship)
  set(WEBVIEW2_LIB_DIR "${WEBVIEW2_DIR}/build/native/${SAL_PLATFORM}")
  set(WEBVIEW2_LIB "${WEBVIEW2_LIB_DIR}/WebView2LoaderStatic.lib")
  if(EXISTS "${WEBVIEW2_INCLUDE}/WebView2.h" AND EXISTS "${WEBVIEW2_LIB}")
    message(STATUS "WebView2 SDK found: ${WEBVIEW2_DIR}")
    message(STATUS "  Include: ${WEBVIEW2_INCLUDE}")
    message(STATUS "  Lib: ${WEBVIEW2_LIB}")
  else()
    message(WARNING "WebView2 SDK package found but files missing")
    message(STATUS "  Expected include: ${WEBVIEW2_INCLUDE}/WebView2.h")
    message(STATUS "  Expected lib: ${WEBVIEW2_LIB}")
  endif()
else()
  message(WARNING "WebView2 SDK package not found in ${SAL_PACKAGES_DIR}")
endif()
