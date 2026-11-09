# The extension is loaded into explorer.exe, use the static runtime to not depend on the runtime shipped with the client
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(OC_EXPLORER_COMMAND_CLSID_STRING "850FD5BE-2D56-4653-9DD7-C6F5DF8143B3")

add_library(OpenCloudExplorerCommand MODULE
    dllmain.cpp
    explorercommand.cpp
    socketclient.cpp
    OpenCloudExplorerCommand.def
)
set_target_properties(OpenCloudExplorerCommand PROPERTIES AUTOMOC OFF)
target_compile_definitions(OpenCloudExplorerCommand PRIVATE UNICODE _UNICODE NOMINMAX OC_EXPLORER_COMMAND_CLSID_STRING="${OC_EXPLORER_COMMAND_CLSID_STRING}")
target_link_libraries(OpenCloudExplorerCommand PRIVATE runtimeobject shlwapi)
add_windows_version_info(OpenCloudExplorerCommand)

install(TARGETS OpenCloudExplorerCommand
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WITH_DEV_PACKAGE)
    add_subdirectory(devpackage)
endif()
