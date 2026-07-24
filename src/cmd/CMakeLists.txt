add_executable(cmd
    cmd.cpp
    tokencredentials.cpp
)
set_target_properties(cmd PROPERTIES OUTPUT_NAME "${APPLICATION_EXECUTABLE}cmd")
ecm_mark_nongui_executable(cmd)

target_link_libraries(cmd libsync Qt::Core Qt::Network ZLIB::ZLIB)
apply_common_target_settings(cmd)


if(APPLE)
    set_target_properties(cmd PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:opencloud>")
else()
    install(TARGETS cmd ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
endif()

if(UNIX AND NOT APPLE)
    set(_saved_icon_name "${APPLICATION_ICON_NAME}")
    set(APPLICATION_ICON_NAME "${APPLICATION_REV_DOMAIN}")
    configure_file(${CMAKE_SOURCE_DIR}/opencloud.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/${APPLICATION_REV_DOMAIN}-cmd.desktop)
    set(APPLICATION_ICON_NAME "${_saved_icon_name}")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${APPLICATION_REV_DOMAIN}-cmd.desktop DESTINATION ${KDE_INSTALL_DATADIR}/applications)
endif()
