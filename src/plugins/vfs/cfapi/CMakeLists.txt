if (WIN32)
    add_vfs_plugin(NAME cfapi
        SRC
            cfapiwrapper.cpp
            vfs_cfapi.cpp
        LIBS
            cldapi
            WindowsApp
    )

endif()
