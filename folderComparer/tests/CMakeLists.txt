add_executable(fc_tests
    TestMain.cpp
    test_action_history.cpp
    test_file_manager.cpp
    test_folder_manager.cpp
    test_presentation.cpp
    test_session.cpp
)
target_link_libraries(fc_tests PRIVATE fc_core)
fc_configure_target(fc_tests)

add_test(NAME fc_unit_tests COMMAND fc_tests)
