# FontIcon Collector Tool
add_executable(fonticon_collector
        fonticon_collector.cpp
)
add_compile_definitions(SRC_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(fonticon_collector
        OpenCloudResources
        Qt::Core
        Qt::Widgets
        Qt::Gui
)
apply_common_target_settings(fonticon_collector)
