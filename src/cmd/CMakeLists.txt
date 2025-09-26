add_executable(cmd
    cmd.cpp
    tokencredentials.cpp
)
set_target_properties(cmd PROPERTIES OUTPUT_NAME "${APPLICATION_EXECUTABLE}cmd")
ecm_mark_nongui_executable(cmd)

target_link_libraries(cmd libsync Qt::Core Qt::Network ZLIB::ZLIB)
apply_common_target_settings(cmd)

# FontIcon Collector Tool
add_executable(fonticon_collector
    fonticon_collector.cpp
)
set_target_properties(fonticon_collector PROPERTIES OUTPUT_NAME "fonticon_collector")
add_compile_definitions(SRC_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(fonticon_collector 
    OpenCloudResources 
    Qt::Core 
    Qt::Widgets 
    Qt::Gui
)
apply_common_target_settings(fonticon_collector)

if(APPLE)
    set_target_properties(cmd PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:opencloud>")
else()
    install(TARGETS cmd ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
endif()

if(UNIX AND NOT APPLE)
    configure_file(${CMAKE_SOURCE_DIR}/opencloud.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/${APPLICATION_EXECUTABLE}cmd.desktop)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${APPLICATION_EXECUTABLE}cmd.desktop DESTINATION ${KDE_INSTALL_DATADIR}/applications)
endif()
