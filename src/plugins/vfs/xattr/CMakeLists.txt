if (NOT WIN32)
    find_package(OpenVFS QUIET  )
    set_package_properties(OpenVFS PROPERTIES TYPE OPTIONAL
            DESCRIPTION "A Virtual Filesystem Layer for cloud storages for the free desktop"
            URL "https://github.com/dragotin/openvfs")

    if(TARGET OpenVFS::OpenVFS)
        get_target_property (OPEN_VFS_CONFIG OpenVFS::OpenVFS IMPORTED_CONFIGURATIONS)
        get_target_property (OPEN_VFS_PATH OpenVFS::OpenVFS IMPORTED_LOCATION_${OPEN_VFS_CONFIG})
        message(STATUS "Using OpenVFS ${OPEN_VFS_CONFIG} (${OPEN_VFS_PATH})")
        add_vfs_plugin(NAME xattr
                SRC
                vfs_xattr.cpp
        )
        target_compile_definitions(vfs_xattr PRIVATE OPENVFS_EXE=\"${OPEN_VFS_PATH}\")
    endif()
endif()
