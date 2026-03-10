if (NOT WIN32)
    find_package(OpenVFS 0.1.0 QUIET)
    set_package_properties(OpenVFS PROPERTIES TYPE OPTIONAL
            DESCRIPTION "A Virtual Filesystem Layer for cloud storages for the free desktop"
            URL "https://github.com/opencloud-eu/openvfs/")

    if(TARGET OpenVFS::OpenVFS)
        get_target_property (OPEN_VFS_CONFIG OpenVFS::OpenVFS IMPORTED_CONFIGURATIONS)
        get_target_property (OPEN_VFS_PATH OpenVFS::OpenVFS IMPORTED_LOCATION_${OPEN_VFS_CONFIG})
        message(STATUS "Using OpenVFS ${OPEN_VFS_CONFIG} (${OPEN_VFS_PATH})")
        add_vfs_plugin(NAME openvfs
                SRC
                vfs_openvfs.cpp
                LIBS
                OpenVFS::LibOpenVFS
        )
        target_compile_definitions(vfs_openvfs PRIVATE OPENVFS_EXE=\"${OPEN_VFS_PATH}\")
    endif()
endif()
