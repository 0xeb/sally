# Test executables for Salamander

# Long path test - uses standalone widepath implementation (no precomp.h)
add_executable(test_longpath
    test_longpath.cpp
    widepath_standalone.cpp
)

target_include_directories(test_longpath PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_compile_definitions(test_longpath PRIVATE
    WIN32
    _WINDOWS
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(test_longpath PRIVATE
    kernel32
)

# Register with CTest
add_test(NAME longpath_test COMMAND test_longpath)

# Output to tests directory
set_target_properties(test_longpath PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# Operations test - tests COperation container behavior with std::vector
add_executable(test_operations
    test_operations.cpp
)

target_compile_definitions(test_operations PRIVATE
    WIN32
    _WINDOWS
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(test_operations PRIVATE
    kernel32
)

# Register with CTest
add_test(NAME operations_test COMMAND test_operations)

# Output to tests directory
set_target_properties(test_operations PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)
