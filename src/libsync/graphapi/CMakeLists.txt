target_sources(libsync PRIVATE
        jobs/drives.cpp
        space.cpp
        spacesmanager.cpp)


add_library(libregraph SHARED)
apply_common_target_settings(libregraph)
qt_add_openapi_client(libregraph
        SPEC_FILE
        ${CMAKE_CURRENT_SOURCE_DIR}/libregraph.yaml
)
# add a helper that knows how to serialize QSet
target_precompile_headers(libregraph PRIVATE openapihelper.h)

set_target_properties(libregraph PROPERTIES
        OUTPUT_NAME "OpenCloudLibreGraph"
        AUTOUIC ON
        AUTORCC ON
        VERSION ${MIRALL_VERSION}
        SOVERSION ${MIRALL_SOVERSION}
)



# the generated code relies on implicit casts from ascii,
# KDECompilerSettings adds QT_NO_CAST_FROM_ASCII as a directory definition, so undefine it again
target_compile_options(libregraph PRIVATE -UQT_NO_CAST_FROM_ASCII -UQT_NO_KEYWORDS)

target_link_libraries(libsync PUBLIC libregraph)
install(TARGETS libregraph EXPORT ${APPLICATION_SHORTNAME}Config ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
