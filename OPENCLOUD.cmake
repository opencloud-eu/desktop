option(DEV_BUILD "Use a standalone profile for developement" OFF)

if (DEV_BUILD)
    set(THEME_SUFFIX " Dev")
else()
    set(THEME_SUFFIX "")
endif()

set( APPLICATION_NAME       "OpenCloud Desktop${THEME_SUFFIX}")
set( APPLICATION_SHORTNAME  "OpenCloud${THEME_SUFFIX}" )
set( APPLICATION_EXECUTABLE "opencloud${THEME_SUFFIX}" )
set( APPLICATION_VENDOR     "OpenCloud${THEME_SUFFIX}" )
set( APPLICATION_ICON_NAME  "opencloud" )

# TODO: re enable once we got icons
#set( MAC_INSTALLER_BACKGROUND_FILE "${CMAKE_SOURCE_DIR}/admin/osx/installer-background.png")

set( THEME_CLASS            "OpenCloudTheme" )
set( APPLICATION_REV_DOMAIN "eu.opencloud.desktop" )

set( THEME_INCLUDE          "opencloudtheme.h" )

option( WITH_CRASHREPORTER "Build crashreporter" OFF )
