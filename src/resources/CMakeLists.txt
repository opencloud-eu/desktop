add_library(OpenCloudResources SHARED
    client.qrc
    core_theme.qrc
    loadresources.cpp
    resources.cpp
    qmlresources.cpp
    themewatcher.cpp
    template.cpp
    fonticon.cpp
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

# Handle Translations, pick all desktop_*.ts files from translations directory.
file(GLOB desktop_translations ${CMAKE_SOURCE_DIR}/translations/desktop_*.ts)
qt_add_lrelease(OpenCloudResources TS_FILES ${desktop_translations} QM_FILES_OUTPUT_VARIABLE desktop_compiled_translations)
add_resources_to_target(
    TARGET OpenCloudResources
    PREFIX translations
    FILES "${desktop_compiled_translations}"
)

ecm_add_qml_module(OpenCloudResources
        URI eu.OpenCloud.resources
        VERSION 1.0
        NAMESPACE OCC
        GENERATE_PLUGIN_SOURCE
)

ecm_finalize_qml_module(OpenCloudResources DESTINATION ${KDE_INSTALL_QMLDIR})

install(TARGETS OpenCloudResources EXPORT ${APPLICATION_SHORTNAME}Config ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
