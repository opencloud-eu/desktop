# CMake build configuration for the macOS NSFileProvider VFS plugin (nsfp).
# Only compiled on Apple platforms (macOS 12+).

if(APPLE)
    set(APP_GROUP_IDENTIFIER "${APPLE_DEVELOPMENT_TEAM}.${APPLICATION_REV_DOMAIN}")

    add_vfs_plugin(NAME nsfp
        SRC
            nsfpdomainmanager.mm
            nsfpxpchandler.mm
            vfs_nsfp.mm
        LIBS
            "-framework Foundation"
            "-framework FileProvider"
            "-lsqlite3"
    )

    target_compile_definitions(vfs_nsfp PRIVATE
        APP_GROUP_IDENTIFIER="${APP_GROUP_IDENTIFIER}"
    )

    # Enable ARC for all Objective-C++ sources in this plugin.
    target_compile_options(vfs_nsfp PRIVATE -fobjc-arc)

    # The XPC handler needs access to the shared XPC protocol header
    # defined in the File Provider extension sources.
    target_include_directories(vfs_nsfp PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../extensions/fileprovider"
    )
endif()
