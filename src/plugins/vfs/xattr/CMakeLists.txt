if (NOT WIN32)
    add_vfs_plugin(NAME xattr
        SRC
            vfs_xattr.cpp
    )
endif()
