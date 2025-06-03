if (WIN32)
    add_vfs_plugin(NAME cfapi
        SRC
            cfapiwrapper.cpp
            hydrationjob.cpp
            vfs_cfapi.cpp
            nc_utility.cpp
        LIBS
            cldapi
    )

endif()
