find_package(LibreGraphAPI 1.0.4 REQUIRED)
set_package_properties(LibreGraphAPI PROPERTIES
        URL https://github.com/owncloud/libre-graph-api-cpp-qt-client.git
        DESCRIPTION "Libre Graph is a free API for cloud collaboration inspired by the MS Graph API."
        TYPE OPTIONAL
)

configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

set(libsync_SRCS
    account.cpp
    bandwidthmanager.cpp
    capabilities.cpp
    cookiejar.cpp
    discovery.cpp
    discoveryinfo.cpp
    discoveryphase.cpp
    discoveryremoteinfo.cpp
    filesystem.cpp
    httplogger.cpp
    jobqueue.cpp
    logger.cpp
    accessmanager.cpp
    configfile.cpp
    globalconfig.cpp
    abstractnetworkjob.cpp
    networkjobs.cpp
    owncloudpropagator.cpp
    opencloudtheme.cpp
    platform.cpp
    progressdispatcher.cpp
    propagatorjobs.cpp
    propagatedownload.cpp
    propagateupload.cpp
    propagateuploadv1.cpp
    propagateuploadtus.cpp
    propagateremotedelete.cpp
    propagateremotemove.cpp
    propagateremotemkdir.cpp
    syncengine.cpp
    syncfileitem.cpp
    syncfilestatustracker.cpp
    localdiscoverytracker.cpp
    syncresult.cpp
    syncoptions.cpp
    theme.cpp

    creds/credentialmanager.cpp
    creds/abstractcredentials.cpp
    creds/oauth.cpp
    creds/jwt.cpp
    creds/webfinger.cpp
    creds/idtoken.cpp

    networkjobs/getfilejob.cpp
    networkjobs/checkserverjobfactory.cpp
    networkjobs/fetchuserinfojobfactory.cpp
    networkjobs/jsonjob.cpp
    networkjobs/resources.cpp

    abstractcorejob.cpp

    appprovider.cpp
)

if(WIN32)
    list(APPEND libsync_SRCS platform_win.cpp)
elseif(UNIX)
    if (APPLE)
        list(APPEND libsync_SRCS platform_mac.mm)
    else()
        list(APPEND libsync_SRCS platform_unix.cpp)
    endif()
endif()

set(libsync_SRCS ${libsync_SRCS} creds/httpcredentials.cpp)

# These headers are installed for libopencloudsync to be used by 3rd party apps
INSTALL(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/opencloudsynclib.h
        logger.h
        accessmanager.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/${APPLICATION_SHORTNAME}/libsync
)

add_library(libsync SHARED ${libsync_SRCS})
set_target_properties(libsync PROPERTIES EXPORT_NAME SyncCore)

target_link_libraries(libsync
    PUBLIC
        OpenCloudResources
        Qt::Core
        Qt::Network
        Qt::Widgets
        Qt::QuickControls2

    PRIVATE
        Qt::Concurrent
        ZLIB::ZLIB
        Qt6Keychain::Qt6Keychain
        SQLite::SQLite3
)

apply_common_target_settings(libsync)
ecm_add_qml_module(libsync
        URI eu.OpenCloud.libsync
        VERSION 1.0
        NAMESPACE OCC
        GENERATE_PLUGIN_SOURCE
)

ecm_finalize_qml_module(libsync DESTINATION ${KDE_INSTALL_QMLDIR})

set_source_files_properties(configfile.cpp
    PROPERTIES
        COMPILE_DEFINITIONS EXCLUDE_FILE_NAME="${EXCLUDE_FILE_NAME}"
)

target_link_libraries(libsync PUBLIC $<BUILD_INTERFACE:OpenAPI::LibreGraphAPI>)
add_subdirectory(graphapi)
add_subdirectory(vfs)

if ( APPLE )
    target_link_libraries(libsync PUBLIC
         /System/Library/Frameworks/CoreServices.framework
         /System/Library/Frameworks/Foundation.framework
         /System/Library/Frameworks/AppKit.framework
         /System/Library/Frameworks/IOKit.framework
    )
endif()

if(Inotify_FOUND)
    target_include_directories(libsync PRIVATE ${Inotify_INCLUDE_DIRS})
    target_link_libraries(libsync PUBLIC ${Inotify_LIBRARIES})
endif()

if(NO_MSG_HANDLER)
    target_compile_definitions(libsync PRIVATE -DNO_MSG_HANDLER=1)
endif()

GENERATE_EXPORT_HEADER(libsync
        EXPORT_MACRO_NAME OPENCLOUD_SYNC_EXPORT
        EXPORT_FILE_NAME opencloudsynclib.h
        STATIC_DEFINE OPENCLOUD_BUILT_AS_STATIC
)

set_target_properties(libsync PROPERTIES
        OUTPUT_NAME "OpenCloudLibSync"
        VERSION ${MIRALL_VERSION}
        SOVERSION ${MIRALL_SOVERSION}
)

if(WITH_CRASHREPORTER)
    set_source_files_properties(platform.cpp PROPERTIES COMPILE_DEFINITIONS CRASHREPORTER_EXECUTABLE="${CRASHREPORTER_EXECUTABLE}")
    target_link_libraries(libsync PRIVATE CrashReporterQt::Handler)

    if(UNIX AND NOT MAC)
        find_package(Threads REQUIRED)
        target_link_libraries(libsync PUBLIC Threads::Threads)
    endif()
endif()

target_sources(libsync PRIVATE
    csync.cpp
    csync_exclude.cpp
)


try_compile(HAS_CLOCK_CAST
        SOURCE_FROM_CONTENT chrono_test.cpp "
#include <chrono>
int main() {
    std::chrono::clock_cast<std::chrono::system_clock>;
}"
)
message(STATUS "HAS_CLOCK_CAST: ${HAS_CLOCK_CAST}")

if(HAS_CLOCK_CAST)
    set_source_files_properties(filesystem.cpp PROPERTIES COMPILE_DEFINITIONS HAS_CLOCK_CAST)
endif()

add_subdirectory(common)

install(TARGETS libsync EXPORT ${APPLICATION_SHORTNAME}Config ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
