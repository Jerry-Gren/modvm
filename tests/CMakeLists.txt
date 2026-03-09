# tests/CMakeLists.txt

add_executable(test_infra test_infra.c)
add_executable(test_machine test_machine.c)

target_link_libraries(test_infra PRIVATE 
    -Wl,--whole-archive modvm_core -Wl,--no-whole-archive
)
target_link_libraries(test_machine PRIVATE 
    -Wl,--whole-archive modvm_core -Wl,--no-whole-archive
)

add_test(NAME InfraSanityCheck COMMAND test_infra)
add_test(NAME MachineCheck COMMAND test_machine)