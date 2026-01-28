include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
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
