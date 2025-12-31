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
