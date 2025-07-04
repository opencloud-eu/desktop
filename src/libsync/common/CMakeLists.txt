configure_file(version.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/version.cpp @ONLY)

target_sources(libsync PRIVATE
        checksums.cpp
        checksumalgorithms.cpp
        chronoelapsedtimer.cpp
        filesystembase.cpp
        ownsql.cpp
        preparedsqlquerymanager.cpp
        syncjournaldb.cpp
        syncjournalfilerecord.cpp
        utility.cpp
        remotepermissions.cpp
        vfs.cpp
        pinstate.cpp
        plugin.cpp
        restartmanager.cpp
        syncfilestatus.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
)

if(WIN32)
    target_sources(libsync PRIVATE
            utility_win.cpp
    )
elseif(APPLE)
    target_sources(libsync PRIVATE
            utility_mac.mm
    )
elseif(UNIX AND NOT APPLE)
    target_sources(libsync PRIVATE
            utility_unix.cpp
    )
endif()

configure_file(vfspluginmetadata.json.in ${CMAKE_CURRENT_BINARY_DIR}/vfspluginmetadata.json)
