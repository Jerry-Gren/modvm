# tests/CMakeLists.txt

# Automatically discover all test source files in the current directory
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "*.c")

# Iterate through each discovered test file
foreach(test_source IN LISTS TEST_SOURCES)
    # Extract the file name without extension (e.g., test_infra.c -> test_infra)
    get_filename_component(test_name ${test_source} NAME_WE)
    
    # Define an executable for each test case
    add_executable(${test_name} ${test_source})
    
    # Link against the modvm_core OBJECT library configured previously
    target_link_libraries(${test_name} PRIVATE modvm_core)
    
    # Register the executable with CTest (the test name will match the filename)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()