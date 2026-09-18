if(APPLE)
    add_vfs_plugin(NAME fileprovider
        SRC
            vfs_fileprovider.cpp
        LIBS
    )
endif()
