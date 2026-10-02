if(APPLE)
# Standalone Xcode builds also need a nonempty version for extension discovery.
# Keep their defaults in step with VERSION.cmake; CMake builds override both
# values below to include the same optional build suffix as the containing app.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/OpenCloudFinderExtension/OpenCloudFinderExtension.xcodeproj/project.pbxproj"
    OC_XCODE_VERSION_SETTINGS REGEX "MARKETING_VERSION = [0-9.]+;")
foreach(OC_XCODE_VERSION_SETTING IN LISTS OC_XCODE_VERSION_SETTINGS)
    string(REGEX REPLACE ".*MARKETING_VERSION = ([0-9.]+);.*" "\\1" OC_XCODE_VERSION "${OC_XCODE_VERSION_SETTING}")
    if(NOT OC_XCODE_VERSION STREQUAL MIRALL_VERSION)
        message(FATAL_ERROR "Update the extension project's MARKETING_VERSION to ${MIRALL_VERSION} from VERSION.cmake")
    endif()
endforeach()
if(NOT OC_XCODE_VERSION_SETTINGS)
    message(FATAL_ERROR "The extension project must define a standalone MARKETING_VERSION")
endif()

set(OC_OEM_SHARE_ICNS "${CMAKE_BINARY_DIR}/src/gui/${APPLICATION_ICON_NAME}.icns")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(XCODE_CONFIG "Debug")
else()
    set(XCODE_CONFIG "Release")
endif()

# The bundle identifier and application group need to have compatible values with the client
# to be able to open a Mach port across the extension's sandbox boundary.
# Pass the info through the xcodebuild command line and make sure that the project uses
# those user-defined settings to build the plist.

# Build FinderSyncExt (Finder overlay icons and context menus)
add_custom_target( mac_overlayplugin ALL
    xcodebuild -project ${CMAKE_SOURCE_DIR}/shell_integration/MacOSX/OpenCloudFinderExtension/OpenCloudFinderExtension.xcodeproj
        -target FinderSyncExt -configuration "${XCODE_CONFIG}" "SYMROOT=${CMAKE_CURRENT_BINARY_DIR}"
        "OC_OEM_SHARE_ICNS=${OC_OEM_SHARE_ICNS}"
        "OC_APPLICATION_NAME=${APPLICATION_NAME}"
        "OC_APPLICATION_REV_DOMAIN=${APPLICATION_REV_DOMAIN}"
        "OC_SOCKETAPI_TEAM_IDENTIFIER_PREFIX=${SOCKETAPI_TEAM_IDENTIFIER_PREFIX}"
        "OC_APPLICATION_VERSION=${MIRALL_VERSION_STRING}"
        "OC_APPLICATION_BUILD_VERSION=${MIRALL_VERSION_FULL}"
        "DEVELOPMENT_TEAM=$ENV{APPLE_DEVELOPMENT_TEAM_ID}"
    COMMENT "Building FinderSyncExt (Finder overlay icons)"
    VERBATIM)
add_dependencies(mac_overlayplugin opencloud) # for the ${APPLICATION_ICON_NAME}.icns to be generated
add_custom_command(TARGET mac_overlayplugin POST_BUILD COMMAND ${CMAKE_COMMAND}
    ARGS -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/${XCODE_CONFIG}/FinderSyncExt.appex" "$<TARGET_FILE_DIR:opencloud>/../PlugIns/FinderSyncExt.appex")

# Build FileProviderExt (Virtual file system in Finder sidebar)
add_custom_target( mac_fileprovider ALL
    xcodebuild -project ${CMAKE_SOURCE_DIR}/shell_integration/MacOSX/OpenCloudFinderExtension/OpenCloudFinderExtension.xcodeproj
        -target FileProviderExt -configuration "${XCODE_CONFIG}" "SYMROOT=${CMAKE_CURRENT_BINARY_DIR}"
        "OC_APPLICATION_NAME=${APPLICATION_NAME}"
        "OC_APPLICATION_REV_DOMAIN=${APPLICATION_REV_DOMAIN}"
        "OC_SOCKETAPI_TEAM_IDENTIFIER_PREFIX=${SOCKETAPI_TEAM_IDENTIFIER_PREFIX}"
        "OC_APPLICATION_VERSION=${MIRALL_VERSION_STRING}"
        "OC_APPLICATION_BUILD_VERSION=${MIRALL_VERSION_FULL}"
        "DEVELOPMENT_TEAM=$ENV{APPLE_DEVELOPMENT_TEAM_ID}"
    COMMENT "Building FileProviderExt (Virtual file system)"
    VERBATIM)
add_dependencies(mac_fileprovider opencloud)
add_custom_command(TARGET mac_fileprovider POST_BUILD COMMAND ${CMAKE_COMMAND}
    ARGS -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/${XCODE_CONFIG}/FileProviderExt.appex" "$<TARGET_FILE_DIR:opencloud>/../PlugIns/FileProviderExt.appex")

# Install extensions into the installed app bundle
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${XCODE_CONFIG}/FinderSyncExt.appex"
    DESTINATION "${KDE_INSTALL_BUNDLEDIR}/${APPLICATION_SHORTNAME}.app/Contents/PlugIns"
    USE_SOURCE_PERMISSIONS)
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${XCODE_CONFIG}/FileProviderExt.appex"
    DESTINATION "${KDE_INSTALL_BUNDLEDIR}/${APPLICATION_SHORTNAME}.app/Contents/PlugIns"
    USE_SOURCE_PERMISSIONS)

endif()
