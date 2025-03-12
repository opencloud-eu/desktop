set( APPLICATION_NAME       "OpenCloud Desktop" )
set( APPLICATION_SHORTNAME  "OpenCloud" )
set( APPLICATION_EXECUTABLE "opencloud" )
set( APPLICATION_DOMAIN     "opencloud.eu" )
set( APPLICATION_VENDOR     "OpenCloud" )
set( APPLICATION_ICON_NAME  "opencloud" )

# TODO: re enable once we got icons
#set( MAC_INSTALLER_BACKGROUND_FILE "${CMAKE_SOURCE_DIR}/admin/osx/installer-background.png")

set( THEME_CLASS            "OpenCloudTheme" )
set( APPLICATION_REV_DOMAIN "eu.opencloud.desktop" )

set( THEME_INCLUDE          "opencloudtheme.h" )

option( WITH_CRASHREPORTER "Build crashreporter" OFF )
