add_executable(fc_tests
    TestMain.cpp
    test_file_manager.cpp
    test_folder_manager.cpp
    test_presentation.cpp
    test_session.cpp
)
target_link_libraries(fc_tests PRIVATE fc_core)
fc_configure_target(fc_tests)

add_test(NAME fc_unit_tests COMMAND fc_tests)

# Parity with the original Python implementation (optional: needs Python 3 and
# the Python sources one folder above this project).
find_package(Python3 COMPONENTS Interpreter QUIET)
set(FC_PYTHON_SOURCES "${PROJECT_SOURCE_DIR}/.." CACHE PATH "Folder holding the Python Managers/ package")
if(FC_BUILD_CLI AND Python3_Interpreter_FOUND AND EXISTS "${FC_PYTHON_SOURCES}/Managers/FolderManager.py")
    add_test(NAME fc_python_parity
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/parity/parity_check.py"
                --cli "$<TARGET_FILE:folder_comparer_cli>"
                --python-sources "${FC_PYTHON_SOURCES}")
endif()
