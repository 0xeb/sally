include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(INSTALL_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(gtest_prompter
    gtest_prompter.cpp
)

target_link_libraries(gtest_prompter
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME gtest_prompter COMMAND gtest_prompter)

set_target_properties(gtest_prompter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_filesystem
    gtest_filesystem.cpp
)

target_link_libraries(gtest_filesystem
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME gtest_filesystem COMMAND gtest_filesystem)

set_target_properties(gtest_filesystem PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_clipboard
    gtest_clipboard.cpp
)

target_link_libraries(gtest_clipboard
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME gtest_clipboard COMMAND gtest_clipboard)

set_target_properties(gtest_clipboard PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_registry
    gtest_registry.cpp
)

target_link_libraries(gtest_registry
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

add_test(NAME gtest_registry COMMAND gtest_registry)

set_target_properties(gtest_registry PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_fileenumerator
    gtest_fileenumerator.cpp
)

target_link_libraries(gtest_fileenumerator
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

add_test(NAME gtest_fileenumerator COMMAND gtest_fileenumerator)

set_target_properties(gtest_fileenumerator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_process
    gtest_process.cpp
)

target_link_libraries(gtest_process
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

add_test(NAME gtest_process COMMAND gtest_process)

set_target_properties(gtest_process PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_shell
    gtest_shell.cpp
)

target_link_libraries(gtest_shell
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

add_test(NAME gtest_shell COMMAND gtest_shell)

set_target_properties(gtest_shell PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_environment
    gtest_environment.cpp
)

target_link_libraries(gtest_environment
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

add_test(NAME gtest_environment COMMAND gtest_environment)

set_target_properties(gtest_environment PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_widepath
    gtest_widepath.cpp
    ../salpath_standalone.cpp
)

target_include_directories(gtest_widepath PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_compile_definitions(gtest_widepath PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(gtest_widepath
    GTest::gtest GTest::gtest_main kernel32
)

add_test(NAME gtest_widepath COMMAND gtest_widepath)

set_target_properties(gtest_widepath PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_fsutil_wide
    gtest_fsutil_wide.cpp
    ../fsutil_standalone.cpp
)

target_include_directories(gtest_fsutil_wide PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_compile_definitions(gtest_fsutil_wide PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(gtest_fsutil_wide
    GTest::gtest GTest::gtest_main kernel32
)

add_test(NAME gtest_fsutil_wide COMMAND gtest_fsutil_wide)

set_target_properties(gtest_fsutil_wide PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_salwidepath
    gtest_salwidepath.cpp
    ../widepath_standalone.cpp
)

target_include_directories(gtest_salwidepath PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_compile_definitions(gtest_salwidepath PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(gtest_salwidepath
    GTest::gtest GTest::gtest_main kernel32
)

add_test(NAME gtest_salwidepath COMMAND gtest_salwidepath)

set_target_properties(gtest_salwidepath PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_filesystem_ops
    gtest_filesystem_ops.cpp
    ../widepath_standalone.cpp
    ../fsutil_standalone.cpp
)

target_include_directories(gtest_filesystem_ops PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_compile_definitions(gtest_filesystem_ops PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(gtest_filesystem_ops
    GTest::gtest GTest::gtest_main kernel32
)

add_test(NAME gtest_filesystem_ops COMMAND gtest_filesystem_ops)

set_target_properties(gtest_filesystem_ops PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_pathutils
    gtest_pathutils.cpp
    ../pathutils_standalone.cpp
)

target_compile_definitions(gtest_pathutils PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(gtest_pathutils
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME gtest_pathutils COMMAND gtest_pathutils)

set_target_properties(gtest_pathutils PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

add_executable(gtest_cpathbuffer_winapi
    gtest_cpathbuffer_winapi.cpp
    ../widepath_standalone.cpp
)

target_include_directories(gtest_cpathbuffer_winapi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_compile_definitions(gtest_cpathbuffer_winapi PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(gtest_cpathbuffer_winapi
    GTest::gtest GTest::gtest_main kernel32 shlwapi
)

add_test(NAME gtest_cpathbuffer_winapi COMMAND gtest_cpathbuffer_winapi)

set_target_properties(gtest_cpathbuffer_winapi PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# SalGetFullNameW tests
add_executable(gtest_salgetfullname
    gtest_salgetfullname.cpp
    ../salgetfullname_standalone.cpp
)

target_compile_definitions(gtest_salgetfullname PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(gtest_salgetfullname
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME gtest_salgetfullname COMMAND gtest_salgetfullname)

set_target_properties(gtest_salgetfullname PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# BuildNameW tests
add_executable(gtest_buildname
    gtest_buildname.cpp
)

target_compile_definitions(gtest_buildname PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(gtest_buildname
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME gtest_buildname COMMAND gtest_buildname)

set_target_properties(gtest_buildname PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# SalSplitGeneralPathW + wide path buffer utility tests
add_executable(gtest_splitpath
    gtest_splitpath.cpp
)

target_compile_definitions(gtest_splitpath PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(gtest_splitpath
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME gtest_splitpath COMMAND gtest_splitpath)

set_target_properties(gtest_splitpath PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# IWorkerObserver / CTestWorkerObserver headless tests
add_executable(gtest_worker_observer
    gtest_worker_observer.cpp
)

target_include_directories(gtest_worker_observer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_compile_definitions(gtest_worker_observer PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(gtest_worker_observer
    GTest::gtest GTest::gtest_main
)

add_test(NAME gtest_worker_observer COMMAND gtest_worker_observer)

set_target_properties(gtest_worker_observer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# Headless integration tests — real file I/O through decoupled observer
add_executable(gtest_headless_worker
    gtest_headless_worker.cpp
)

target_include_directories(gtest_headless_worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_compile_definitions(gtest_headless_worker PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE
)

target_link_libraries(gtest_headless_worker
    GTest::gtest GTest::gtest_main
)

add_test(NAME gtest_headless_worker COMMAND gtest_headless_worker)

set_target_properties(gtest_headless_worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# Headless copy/move integration tests
add_executable(gtest_headless_copy
    gtest_headless_copy.cpp
)

target_include_directories(gtest_headless_copy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_compile_definitions(gtest_headless_copy PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE
)

target_link_libraries(gtest_headless_copy
    GTest::gtest GTest::gtest_main
)

add_test(NAME gtest_headless_copy COMMAND gtest_headless_copy)

set_target_properties(gtest_headless_copy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# CSelectionSnapshot tests
add_executable(gtest_snapshot
    gtest_snapshot.cpp
)

target_include_directories(gtest_snapshot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_compile_definitions(gtest_snapshot PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(gtest_snapshot
    GTest::gtest GTest::gtest_main
)

add_test(NAME gtest_snapshot COMMAND gtest_snapshot)

set_target_properties(gtest_snapshot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# End-to-end integration tests — full pipeline through headless worker
add_executable(gtest_e2e_worker
    gtest_e2e_worker.cpp
)

target_include_directories(gtest_e2e_worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_compile_definitions(gtest_e2e_worker PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE
)

target_link_libraries(gtest_e2e_worker
    GTest::gtest GTest::gtest_main
)

add_test(NAME gtest_e2e_worker COMMAND gtest_e2e_worker)

set_target_properties(gtest_e2e_worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# Wide worker operations integration tests (compression, encryption, dir info, long paths, Unicode)
add_executable(gtest_worker_wide
    gtest_worker_wide.cpp
)

target_compile_definitions(gtest_worker_wide PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE
)

target_link_libraries(gtest_worker_wide
    GTest::gtest GTest::gtest_main
)

add_test(NAME gtest_worker_wide COMMAND gtest_worker_wide)

set_target_properties(gtest_worker_wide PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# WebView2 integration tests (ieviewer plugin: path utilities, markdown, WebView2 runtime)
add_executable(gtest_webview2
    gtest_webview2.cpp
)

target_include_directories(gtest_webview2 PRIVATE
    "${WEBVIEW2_INCLUDE}"
)

target_compile_definitions(gtest_webview2 PRIVATE
    WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(gtest_webview2
    GTest::gtest GTest::gtest_main
    "${WEBVIEW2_LIB}"
    ole32 shell32
)

add_test(NAME gtest_webview2 COMMAND gtest_webview2)

set_target_properties(gtest_webview2 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)
