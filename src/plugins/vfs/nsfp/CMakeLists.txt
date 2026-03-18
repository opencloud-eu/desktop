# CMake build configuration for the macOS NSFileProvider VFS plugin (nsfp).
# Only compiled on Apple platforms (macOS 12+).

if(APPLE)
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

    # Enable ARC for all Objective-C++ sources in this plugin.
    target_compile_options(vfs_nsfp PRIVATE -fobjc-arc)

    # The XPC handler needs access to the shared XPC protocol header
    # defined in the File Provider extension sources.
    target_include_directories(vfs_nsfp PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../extensions/fileprovider"
    )
endif()
