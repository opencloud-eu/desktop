find_package(Qt6 COMPONENTS Linguist REQUIRED)

set(output_file ${CMAKE_CURRENT_BINARY_DIR}/translations.ts)

# Test that the call to lupdate returns no warnings or errors, meaning that all our
# translatable strings are parsed correctly
add_test(
    NAME testtranslations
    COMMAND ${CMAKE_COMMAND}
        -DLUPDATE=$<TARGET_FILE:Qt::lupdate>
        -DTS_FILE=${output_file}
        -DSRC_DIR=${CMAKE_SOURCE_DIR}
        -DWAE=TRUE
        -P ${CMAKE_SOURCE_DIR}/cmake/run_lupdate.cmake
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Clean up the generated file after the test for consistent retries
add_test(
    NAME translationscleanup
    COMMAND ${CMAKE_COMMAND} -E rm -- ${output_file}
)

set_tests_properties(translationscleanup PROPERTIES
    FIXTURES_CLEANUP testtranslations)
