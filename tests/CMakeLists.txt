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
groundwork_test(tst_codecsitem)
groundwork_test(tst_serviceitems)
groundwork_test(tst_clockitem)
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

# The Worker end to end, driving the built program with fakes on PATH.
groundwork_test(tst_worker)
target_compile_definitions(tst_worker PRIVATE GROUNDWORK_BIN="$<TARGET_FILE:groundwork>")
add_dependencies(tst_worker groundwork)

# The password box, on Qt's offscreen display.
groundwork_test(tst_askpass)
target_link_libraries(tst_askpass PRIVATE groundwork_gui Qt6::Widgets)
set_tests_properties(tst_askpass PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Language choice, direction, and the pseudo-translation proof.
groundwork_test(tst_translations)
target_compile_definitions(tst_translations PRIVATE
    GROUNDWORK_BIN="$<TARGET_FILE:groundwork>"
    FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    FAKES="${CMAKE_CURRENT_SOURCE_DIR}/fakes")
add_dependencies(tst_translations groundwork)

# The Wizard, on Qt's offscreen display, with a fake Worker script.
groundwork_test(tst_wizard)
target_link_libraries(tst_wizard PRIVATE groundwork_gui Qt6::Widgets)
set_tests_properties(tst_wizard PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
