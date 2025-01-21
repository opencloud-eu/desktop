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

    ecm_add_test(${SRC_PATH}
        ${ARGN}
        TEST_NAME "${OC_TEST_CLASS}Test"
        LINK_LIBRARIES
        OpenCloudGui syncenginetestutils testutilsloader Qt::Test
    )
    apply_common_target_settings(${OC_TEST_CLASS}Test)
    target_compile_definitions(${OC_TEST_CLASS}Test PRIVATE SOURCEDIR="${PROJECT_SOURCE_DIR}" QT_FORCE_ASSERTS)

    target_include_directories(${OC_TEST_CLASS}Test PRIVATE "${CMAKE_SOURCE_DIR}/test/")
    if (UNIX AND NOT APPLE)
        set_property(TEST ${OC_TEST_CLASS}Test PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endfunction()
