include(CheckFunctionExists)

add_library(csync SHARED
        csync.cpp
        csync_exclude.cpp

        std/c_time.cpp
)
set_target_properties(csync PROPERTIES EXPORT_NAME CSync)

check_function_exists(utimes HAVE_UTIMES)

if(HAVE_UTIMES)
    set_source_files_properties(std/c_time.cpp PROPERTIES COMPILE_DEFINITIONS HAVE_UTIMES=1)
endif()

target_include_directories(csync
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/std
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
        $<INSTALL_INTERFACE:include>
)

apply_common_target_settings(csync)

if (WIN32)
    target_sources(csync PRIVATE
            vio/csync_vio_local_win.cpp
    )
else()
    target_sources(csync PRIVATE
            vio/csync_vio_local_unix.cpp
    )
endif()

if(NO_MSG_HANDLER)
    target_compile_definitions(csync PUBLIC -DNO_MSG_HANDLER=1)
endif()

if(MINGWS)
    target_compile_definitions(csync PUBLIC __USE_MINGW_ANSI_STDIO=1)
endif()

generate_export_header(csync
        EXPORT_MACRO_NAME OCSYNC_EXPORT
        EXPORT_FILE_NAME ocsynclib.h
)

target_link_libraries(csync
        PUBLIC
        Qt::Core
        PRIVATE
        Qt::Concurrent
        SQLite::SQLite3
        ZLIB::ZLIB
)

# For src/common/utility_mac.cpp
if (APPLE)
    find_library(FOUNDATION_LIBRARY NAMES Foundation)
    find_library(CORESERVICES_LIBRARY NAMES CoreServices)
    target_link_libraries(csync
        PRIVATE
            ${FOUNDATION_LIBRARY} ${CORESERVICES_LIBRARY})
endif()

set_target_properties(
  csync
    PROPERTIES
      OUTPUT_NAME
        "OpenCloudCsync"
      VERSION
        ${MIRALL_VERSION}
      SOVERSION
        "0"
)

add_subdirectory(common)

INSTALL(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/ocsynclib.h
        common/filesystembase.h
        common/utility.h
    DESTINATION
        ${KDE_INSTALL_INCLUDEDIR}/${APPLICATION_SHORTNAME}/common
)

INSTALL(TARGETS csync EXPORT ${APPLICATION_SHORTNAME}Config ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
