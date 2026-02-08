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
