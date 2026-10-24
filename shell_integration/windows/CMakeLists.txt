# The extension is loaded into explorer.exe, use the static runtime to not depend on the runtime shipped with the client
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# keep the name in sync with OC_EXPLORER_COMMAND_DLL_NAME
add_library(OpenCloudExplorerCommand MODULE
    dllmain.cpp
    explorercommand.cpp
    socketclient.cpp
    OpenCloudExplorerCommand.def
)

set_target_properties(OpenCloudExplorerCommand PROPERTIES AUTOMOC OFF)
target_compile_definitions(OpenCloudExplorerCommand PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(OpenCloudExplorerCommand PRIVATE runtimeobject shlwapi)
add_windows_version_info(OpenCloudExplorerCommand)

install(TARGETS OpenCloudExplorerCommand
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)
