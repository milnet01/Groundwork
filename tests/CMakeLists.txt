# One Qt Test executable per test file, each registered with CTest.
function(groundwork_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE groundwork_items groundwork_core Qt6::Core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

groundwork_test(tst_smoke)
groundwork_test(tst_filereader)
groundwork_test(tst_commandrunner)
groundwork_test(tst_checkrunner)
groundwork_test(tst_systemidentity)
groundwork_test(tst_selection)
groundwork_test(tst_flathubitem)
groundwork_test(tst_updateitem)
# The built program itself: --version answers and exits 0.
add_test(NAME cli_version COMMAND groundwork --version)
set_tests_properties(cli_version PROPERTIES
    PASS_REGULAR_EXPRESSION "^groundwork [0-9]+\\.[0-9]+\\.[0-9]+\n$")

# Check mode on fixture systems: GROUNDWORK_ROOT points at a fixture tree,
# and PATH holds only fake commands, so nothing real is read or run.
add_test(NAME cli_check_refuses_leap15 COMMAND groundwork --check)
set_tests_properties(cli_check_refuses_leap15 PROPERTIES
    ENVIRONMENT "GROUNDWORK_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/leap15"
    WILL_FAIL TRUE)
add_test(NAME cli_check_refuses_leap15_says_why COMMAND groundwork --check)
set_tests_properties(cli_check_refuses_leap15_says_why PROPERTIES
    ENVIRONMENT "GROUNDWORK_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/leap15"
    PASS_REGULAR_EXPRESSION "Leap 16 or later")
add_test(NAME cli_check_setup_machine_starts_nothing COMMAND groundwork --check)
set_tests_properties(cli_check_setup_machine_starts_nothing PROPERTIES
    ENVIRONMENT "GROUNDWORK_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tumbleweed;PATH=${CMAKE_CURRENT_SOURCE_DIR}/fakes/flathub-present"
    PASS_REGULAR_EXPRESSION "Flatpak and Flathub: already done\n.*Nothing would start switched on")
