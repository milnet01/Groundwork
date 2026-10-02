# One Qt Test executable per test file, each registered with CTest.
function(groundwork_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE groundwork_core Qt6::Core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

groundwork_test(tst_smoke)
groundwork_test(tst_filereader)
groundwork_test(tst_commandrunner)
groundwork_test(tst_checkrunner)
groundwork_test(tst_systemidentity)
# The built program itself: --version answers and exits 0.
add_test(NAME cli_version COMMAND groundwork --version)
set_tests_properties(cli_version PROPERTIES
    PASS_REGULAR_EXPRESSION "^groundwork [0-9]+\\.[0-9]+\\.[0-9]+\n$")
