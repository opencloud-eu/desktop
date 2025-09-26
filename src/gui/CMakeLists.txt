include(ECMAddAppIcon)

find_package(KDSingleApplication-qt6 1.0.0 REQUIRED)

if (WIN32)
    find_package(LibSnoreToast)
    set_package_properties(LibSnoreToast PROPERTIES
            URL https://invent.kde.org/libraries/snoretoast
            DESCRIPTION "Command-line application capable of creating Windows Toast notifications"
            TYPE OPTIONAL
    )
endif()


add_library(OpenCloudGui SHARED)

target_sources(OpenCloudGui PRIVATE
    aboutdialog.ui
    accountsettings.ui
    generalsettings.ui
    ignorelisteditor.ui
    networksettings.ui
    protocolwidget.ui
    issueswidget.ui
    settingsdialog.ui
    tlserrordialog.ui
    logbrowser.ui
)

target_sources(OpenCloudGui PRIVATE
    aboutdialog.cpp
    accountmanager.cpp
    accountsettings.cpp

    accountmodalwidget.cpp
    accountmodalwidget.ui

    application.cpp
    fetchserversettings.cpp
    clientproxy.cpp
    commonstrings.cpp
    connectionvalidator.cpp
    folder.cpp
    folderdefinition.cpp
    folderman.cpp
    folderstatusmodel.cpp
    folderwatcher.cpp
    generalsettings.cpp
    ignorelisteditor.cpp
    lockwatcher.cpp
    logbrowser.cpp
    messagebox.cpp
    networkinformation.cpp
    networksettings.cpp
    notifications.cpp
    openfilemanager.cpp
    protocolwidget.cpp
    protocolitem.cpp
    issueswidget.cpp
    activitywidget.cpp
    selectivesyncwidget.cpp
    settingsdialog.cpp
    tlserrordialog.cpp
    syncrunfilelog.cpp
    systray.cpp
    accountstate.cpp
    guiutility.cpp
    elidedlabel.cpp
    translations.cpp
    creds/httpcredentialsgui.cpp
    creds/qmlcredentials.cpp
    updateurldialog.cpp
    updatenotifier.cpp

    models/expandingheaderview.cpp
    models/models.cpp
    models/protocolitemmodel.cpp

    scheduling/syncscheduler.cpp
    scheduling/etagwatcher.cpp

    notifications/systemnotification.cpp
    notifications/systemnotificationmanager.cpp
    notifications/systemnotificationbackend.cpp

    qmlutils.cpp
)

# 3rd party code
target_sources(OpenCloudGui PRIVATE ../3rdparty/QProgressIndicator/QProgressIndicator.cpp)

add_subdirectory(newwizard)
add_subdirectory(folderwizard)

set_target_properties(OpenCloudGui PROPERTIES AUTOUIC ON AUTORCC ON)
# for the generated qml module
target_include_directories(OpenCloudGui PRIVATE models spaces creds)
target_link_libraries(OpenCloudGui
    PUBLIC
        Qt::Widgets Qt::Network Qt::Xml Qt::Quick Qt::QuickWidgets Qt::QuickControls2
        newwizard folderwizard
        libsync
        Qt6Keychain::Qt6Keychain
)

apply_common_target_settings(OpenCloudGui)
ecm_add_qml_module(OpenCloudGui
        URI eu.OpenCloud.gui
        VERSION 1.0
        NAMESPACE OCC
        QML_FILES
            qml/OpenCloud.js

            qml/AccountBar.qml
            qml/AccountButton.qml
            qml/FolderDelegate.qml
            qml/FolderError.qml

            qml/UpdateUrlDialog.qml

            qml/credentials/Credentials.qml
            qml/credentials/OAuthCredentials.qml

            spaces/qml/SpaceDelegate.qml
            spaces/qml/SpacesView.qml
)

generate_export_header(OpenCloudGui
        EXPORT_MACRO_NAME OPENCLOUD_GUI_EXPORT
        EXPORT_FILE_NAME opencloudguilib.h
        STATIC_DEFINE OPENCLOUD_BUILT_AS_STATIC
)
add_subdirectory(spaces)


add_subdirectory(socketapi)

target_include_directories(OpenCloudGui PUBLIC
    ${CMAKE_SOURCE_DIR}/src/3rdparty/QProgressIndicator
    ${CMAKE_CURRENT_BINARY_DIR}
)

IF( APPLE )
    target_sources(OpenCloudGui PRIVATE
            notifications/macnotifications.mm
            settingsdialog_mac.mm
            guiutility_mac.mm
            folderwatcher_mac.cpp)
    set_source_files_properties(guiutility_mac.mm PROPERTIES COMPILE_DEFINITIONS SOCKETAPI_TEAM_IDENTIFIER_PREFIX="${SOCKETAPI_TEAM_IDENTIFIER_PREFIX}")
elseif( WIN32 )
    target_sources(OpenCloudGui PRIVATE
            guiutility_win.cpp
            folderwatcher_win.cpp
            navigationpanehelper.cpp
    )
    if (TARGET SnoreToast::SnoreToastActions)
        target_sources(OpenCloudGui PRIVATE
                notifications/snoretoast.cpp)
        target_compile_definitions(OpenCloudGui PRIVATE WITH_SNORE_TOAST)
        target_link_libraries(OpenCloudGui PRIVATE SnoreToast::SnoreToastActions)
    endif()
elseif(UNIX AND NOT APPLE)
    ## handle DBUS for Fdo notifications
    target_link_libraries(OpenCloudGui PUBLIC Qt::DBus)
    target_sources(OpenCloudGui PRIVATE
            folderwatcher_linux.cpp
            guiutility_unix.cpp
            notifications/dbusnotifications.cpp
    )
    qt_add_dbus_interface(notifications_dbus_SRCS notifications/org.freedesktop.Notifications.xml dbusnotifications_interface)
    target_sources(OpenCloudGui PRIVATE ${notifications_dbus_SRCS})
endif()

if(WITH_AUTO_UPDATER)
    add_subdirectory(updater)
    target_compile_definitions(OpenCloudGui PUBLIC $<BUILD_INTERFACE:WITH_AUTO_UPDATER>)
endif()

add_executable(opencloud main.cpp)
set_target_properties(opencloud PROPERTIES
    OUTPUT_NAME "${APPLICATION_EXECUTABLE}"
    AUTOUIC ON
    AUTORCC ON
)
apply_common_target_settings(opencloud)
target_link_libraries(opencloud PUBLIC OpenCloudGui OpenCloudResources KDAB::kdsingleapplication )

MESSAGE(STATUS "OPENCLOUD_SIDEBAR_ICONS: ${APPLICATION_ICON_NAME}: ${OPENCLOUD_SIDEBAR_ICONS}")

ecm_add_app_icon(appIcons ICONS "${OPENCLOUD_ICONS}" SIDEBAR_ICONS "${OPENCLOUD_SIDEBAR_ICONS}" OUTFILE_BASENAME "${APPLICATION_ICON_NAME}")
target_sources(opencloud PRIVATE ${appIcons})

if(NOT APPLE)
    if(WIN32)
        target_sources(opencloud PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/opencloud.exe.manifest)
    else()
        file(GLOB _icons "${OEM_THEME_DIR}/theme/colored/*-${APPLICATION_ICON_NAME}-icon.png")
        foreach(_file ${_icons})
            string(REPLACE "${OEM_THEME_DIR}/theme/colored/" "" _res ${_file})
            string(REPLACE "-${APPLICATION_ICON_NAME}-icon.png" "" _res ${_res})
            install(FILES ${_file} RENAME ${APPLICATION_ICON_NAME}.png DESTINATION ${KDE_INSTALL_DATADIR}/icons/hicolor/${_res}x${_res}/apps)
        endforeach(_file)
    endif()

else()
    target_sources(opencloud PRIVATE ${OPENCLOUD_BUNDLED_RESOURCES})

    set_source_files_properties(
      ${OPENCLOUD_BUNDLED_RESOURCES}
      PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources
      )
  set_target_properties(opencloud PROPERTIES OUTPUT_NAME "${APPLICATION_SHORTNAME}" MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/MacOSXBundleInfo.plist)
endif()

install(TARGETS opencloud OpenCloudGui ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
ecm_finalize_qml_module(OpenCloudGui DESTINATION ${KDE_INSTALL_QMLDIR})

if(UNIX AND NOT APPLE)
    configure_file(${CMAKE_SOURCE_DIR}/opencloud.desktop.in
                   ${CMAKE_CURRENT_BINARY_DIR}/${APPLICATION_EXECUTABLE}.desktop)
    install(FILES  ${CMAKE_CURRENT_BINARY_DIR}/${APPLICATION_EXECUTABLE}.desktop DESTINATION ${KDE_INSTALL_DATADIR}/applications )
endif()
