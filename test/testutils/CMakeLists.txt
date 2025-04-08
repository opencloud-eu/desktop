add_executable(test_helper test_helper.cpp)
target_link_libraries(test_helper PUBLIC Qt::Core libsync)

add_library(syncenginetestutils STATIC syncenginetestutils.cpp testutils.cpp)
target_link_libraries(syncenginetestutils PUBLIC OpenCloudGui Qt::Test)
target_compile_definitions(syncenginetestutils PRIVATE TEST_HELPER_EXE="$<TARGET_FILE:test_helper>")
set_source_files_properties(testutils.cpp PROPERTIES COMPILE_DEFINITIONS SOURCEDIR="${PROJECT_SOURCE_DIR}")

# testutilsloader.cpp uses Q_COREAPP_STARTUP_FUNCTION which can't used reliably in a static lib
# therefore we compile it in the tests
add_library(testutilsloader OBJECT testutilsloader.cpp)
target_link_libraries(testutilsloader PUBLIC OpenCloudGui OpenCloudResources)
