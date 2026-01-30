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
