add_executable(cmd
    cmd.cpp
    tokencredentials.cpp
)
set_target_properties(cmd PROPERTIES OUTPUT_NAME "opencloudcmd")
ecm_mark_nongui_executable(cmd)

target_link_libraries(cmd libsync Qt::Core Qt::Network ZLIB::ZLIB)
apply_common_target_settings(cmd)


if(APPLE)
    set_target_properties(cmd PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:opencloud>")
else()
    install(TARGETS cmd ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
endif()

if(UNIX AND NOT APPLE)
    install(FILES ${CMAKE_SOURCE_DIR}/opencloudcmd.desktop DESTINATION ${KDE_INSTALL_DATADIR}/applications)
endif()
