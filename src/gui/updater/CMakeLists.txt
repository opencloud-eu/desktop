if(APPLE)
    if (SPARKLE_FOUND)
        list(APPEND OPENCLOUD_BUNDLED_RESOURCES
            "${PROJECT_SOURCE_DIR}/admin/osx/deny_autoupdate_eu.opencloud.desktop.plist"
            "${PROJECT_SOURCE_DIR}/admin/osx/sparkle/dsa_pub.pem")
        set(OPENCLOUD_BUNDLED_RESOURCES ${OPENCLOUD_BUNDLED_RESOURCES} PARENT_SCOPE)
    endif()
endif()

target_sources(OpenCloudGui PRIVATE
    ocupdater.cpp
    ocupdater.h
    newversionavailablewidget.cpp
    newversionavailablewidget.h
    newversionavailablewidget.ui
    updateinfo.cpp
    updateinfo.h
    updater.cpp
    updater.h
    updatedownloadedwidget.cpp
    updatedownloadedwidget.h
    updatedownloadedwidget.ui
)

if(SPARKLE_FOUND)
    # Define this, we need to check in updater.cpp
    target_compile_definitions(OpenCloudGui PUBLIC HAVE_SPARKLE)
    target_sources(OpenCloudGui PRIVATE
        sparkleupdater_mac.mm
        sparkleupdater.h
    )
    target_link_libraries(OpenCloudGui PRIVATE ${SPARKLE_LIBRARY})
endif()

if(WITH_APPIMAGEUPDATER)
    message(STATUS "Including built-in libappimageupdate based updater")

    set(appimageupdater_sources
        ${CMAKE_CURRENT_SOURCE_DIR}/appimageupdateavailablewidget.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/appimageupdateavailablewidget.h
        ${CMAKE_CURRENT_SOURCE_DIR}/appimageupdateavailablewidget.ui
        ${CMAKE_CURRENT_SOURCE_DIR}/appimageupdater.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/appimageupdater.h
    )

    target_sources(OpenCloudGui PRIVATE ${appimageupdater_sources})

    # as libappimageupdate uses exceptions, we define a shim that catches all incoming exceptions
    # and handles them in a suitable way
    # we have to enable exceptions for this one compilation unit
    set_source_files_properties(${appimageupdater_sources}
        TARGET_DIRECTORY OpenCloudGui
        PROPERTIES COMPILE_OPTIONS "-fexceptions"
    )

    target_compile_definitions(OpenCloudGui PRIVATE WITH_APPIMAGEUPDATER)
    target_link_libraries(OpenCloudGui PRIVATE libappimageupdate)

    find_package(Threads REQUIRED)
    target_link_libraries(OpenCloudGui PUBLIC ${CMAKE_THREAD_LIBS_INIT})
endif()
