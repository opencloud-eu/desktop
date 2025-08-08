include(OCApplyCommonSettings)
find_package(Qt6 COMPONENTS Test REQUIRED)

include(ECMAddTests)

function(opencloud_add_test test_class)
    set(OC_TEST_CLASS ${test_class})
    string(TOLOWER "${OC_TEST_CLASS}" OC_TEST_CLASS_LOWERCASE)
    set(SRC_PATH test${OC_TEST_CLASS_LOWERCASE}.cpp)
    if (IS_DIRECTORY  ${CMAKE_CURRENT_SOURCE_DIR}/test${OC_TEST_CLASS_LOWERCASE}/)
        set(SRC_PATH test${OC_TEST_CLASS_LOWERCASE}/${SRC_PATH})
    endif()
    ecm_add_tests(${SRC_PATH}
        LINK_LIBRARIES
        OpenCloudGui syncenginetestutils testutilsloader Qt::Test
        TARGET_NAMES_VAR _test_target_name
    )
    apply_common_target_settings(${_test_target_name})
    target_compile_definitions(${_test_target_name} PRIVATE SOURCEDIR="${PROJECT_SOURCE_DIR}" QT_FORCE_ASSERTS)

    # depend on opencloud to ensure the plugins are up to date
    add_dependencies(${_test_target_name} opencloud)
    add_dependencies(${_test_target_name} test_helper)

    target_include_directories(${_test_target_name} PRIVATE "${CMAKE_SOURCE_DIR}/test/")
    if ((UNIX AND NOT APPLE) OR WIN32)
        set_property(TEST ${_test_target_name} PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=*=true")
    endif()
endfunction()
