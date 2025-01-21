add_library(OpenCloudResources SHARED
    client.qrc
    core_theme.qrc
    loadresources.cpp
    resources.cpp
    qmlresources.cpp
    themewatcher.cpp
    template.cpp
)

generate_theme(OpenCloudResources OPENCLOUD_SIDEBAR_ICONS)

# make them available to the whole project
set(OPENCLOUD_SIDEBAR_ICONS ${OPENCLOUD_SIDEBAR_ICONS} CACHE INTERNAL "Sidebar icons" FORCE)

target_link_libraries(OpenCloudResources PUBLIC Qt::Core Qt::Gui Qt::Quick)
apply_common_target_settings(OpenCloudResources)
target_include_directories(OpenCloudResources PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>)
target_compile_definitions(OpenCloudResources PRIVATE APPLICATION_SHORTNAME="${APPLICATION_SHORTNAME}")

set_target_properties(OpenCloudResources PROPERTIES
    OUTPUT_NAME "OpenCloudResources"
    AUTOUIC ON
    AUTORCC ON
    VERSION ${MIRALL_VERSION}
    SOVERSION ${MIRALL_SOVERSION}
)

generate_export_header(OpenCloudResources
        EXPORT_MACRO_NAME OPENCLOUD_RESOURCES_EXPORT
        EXPORT_FILE_NAME opencloudresourceslib.h
        STATIC_DEFINE OPENCLOUD_BUILT_AS_STATIC
)

# Handle Translations, pick all client_* files from trans directory.
file(GLOB client_translations ${CMAKE_SOURCE_DIR}/translations/client_*.ts)
qt_add_lrelease(OpenCloudResources TS_FILES ${client_translations} QM_FILES_OUTPUT_VARIABLE client_compiled_translations)
add_resources_to_target(
    TARGET OpenCloudResources
    PREFIX translations
    FILES "${client_compiled_translations}"
)

ecm_add_qml_module(OpenCloudResources
        URI eu.OpenCloud.resources
        VERSION 1.0
        NAMESPACE OCC
)

ecm_finalize_qml_module(OpenCloudResources DESTINATION ${KDE_INSTALL_QMLDIR})

install(TARGETS OpenCloudResources EXPORT ${APPLICATION_SHORTNAME}Config ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
