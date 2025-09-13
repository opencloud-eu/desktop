if (WIN32)
    add_vfs_plugin(NAME cfapi
        SRC
            cfapiwrapper.cpp
            hydrationdevice.cpp
            vfs_cfapi.cpp
        LIBS
            cldapi
            WindowsApp
    )

endif()
