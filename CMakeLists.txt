cmake_minimum_required(VERSION 3.18)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(VERSION.cmake)
project(OpenCloudDesktop LANGUAGES CXX C VERSION ${MIRALL_VERSION_MAJOR}.${MIRALL_VERSION_MINOR}.${MIRALL_VERSION_PATCH})
include(FeatureSummary)

find_package(ECM 6.0.0 REQUIRED NO_MODULE)

set_package_properties(ECM PROPERTIES TYPE REQUIRED DESCRIPTION "Extra CMake Modules." URL "https://projects.kde.org/projects/kdesupport/extra-cmake-modules")
feature_summary(WHAT REQUIRED_PACKAGES_NOT_FOUND FATAL_ON_MISSING_REQUIRED_PACKAGES)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

# disable pointless warning in KDECMakeSettings
set(APPLE_SUPPRESS_X11_WARNING ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(QT 6.8 NAMES Qt6 COMPONENTS Core REQUIRED)

find_package(Qt6 COMPONENTS Core Concurrent Network Widgets Xml Quick QuickWidgets QuickControls2 REQUIRED)
find_package(Qt6LinguistTools REQUIRED)
get_target_property (QT_QMAKE_EXECUTABLE Qt::qmake IMPORTED_LOCATION)
message(STATUS "Using Qt ${QT_VERSION} (${QT_QMAKE_EXECUTABLE})")

if (UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()

include(KDEInstallDirs)
include(KDECMakeSettings)
include(ECMMarkNonGuiExecutable)
include(ECMSetupVersion)

include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMEnableSanitizers)

# while we use qt_add_qml_module instead of ecm_add_qml_module
# include ECMQmlModule to set up the policies
include(ECMQmlModule)

if(UNIT_TESTING)
    message(DEPRECATION "Setting UNIT_TESTING is deprecated please use BUILD_TESTING")
    set(BUILD_TESTING TRUE)
endif()
include(CTest)

include(OCBundleResources)
include(OCApplyCommonSettings)

include("${CMAKE_CURRENT_LIST_DIR}/THEME.cmake")

if(NOT WITH_CRASHREPORTER)
    message(STATUS "Build of crashreporter disabled.")
else()
    # obviously, a crash reported without the URL set is pretty pointless and won't work
    if(NOT DEFINED CRASHREPORTER_SUBMIT_URL)
        message(FATAL_ERROR "No crash reporter submit URL provided")
    endif()
endif()

include(GenerateExportHeader)

include(GetGitRevisionDescription)

# used in src/csync/CMakeLists.txt
get_git_head_revision(GIT_REFSPEC GIT_SHA1)

# if we cannot get it from git, directly try .tag (packages)
# this will work if the tar balls have been properly created
# via git-archive.
if ("${GIT_SHA1}" STREQUAL "GITDIR-NOTFOUND")
    file(READ ${CMAKE_SOURCE_DIR}/.tag sha1_candidate)
    string(REPLACE "\n" "" sha1_candidate ${sha1_candidate})
    if (NOT ${sha1_candidate} STREQUAL "$Format:%H$")
        message("${sha1_candidate}")
        set (GIT_SHA1 "${sha1_candidate}")
    endif()
endif()
message(STATUS "GIT_SHA1 ${GIT_SHA1}")

option(NO_MSG_HANDLER "Don't redirect QDebug outputs to the log window/file" OFF)

# this option builds the shell integration
option(BUILD_SHELL_INTEGRATION "BUILD_SHELL_INTEGRATION" ON)

# build the auto updater component
option(WITH_AUTO_UPDATER "WITH_AUTO_UPDATER" OFF)

# build with -DQT_FORCE_ASSERTS
option(FORCE_ASSERTS "FORCE_ASSERTS" OFF)

option(WITH_APPIMAGEUPDATER OFF "Enable built-in libappimageupdate based updater on Linux")
add_feature_info(AppImageUpdate WITH_APPIMAGEUPDATER "Built-in libappimageupdate based updater")

option(WITH_EXTERNAL_BRANDING "A URL to an external branding repo" "")

# specify additional vfs plugins
set(VIRTUAL_FILE_SYSTEM_PLUGINS off cfapi CACHE STRING "Name of internal plugin in src/libsync/vfs or the locations of virtual file plugins")

if(APPLE)
  set( SOCKETAPI_TEAM_IDENTIFIER_PREFIX "" CACHE STRING "SocketApi prefix (including a following dot) that must match the codesign key's TeamIdentifier/Organizational Unit" )
endif()


if (WITH_AUTO_UPDATER)
    if(APPLE)
        find_package(Sparkle REQUIRED)
    endif(APPLE)

    if(WITH_APPIMAGEUPDATER)
        find_package(AppImageUpdate REQUIRED)
    endif()
endif()

if(UNIX AND NOT APPLE)
    find_package(Inotify REQUIRED)
endif()

find_package(ZLIB REQUIRED)
find_package(SQLite3 3.9.0 REQUIRED)

# in the ownBrander themes, the icon files are named after the shortname
# the theme included in this repository defines a custom icon name, therefore we set the shortname as a fallback if the
# theme does not define the variable
if (NOT DEFINED APPLICATION_ICON_NAME)
    set(APPLICATION_ICON_NAME "${APPLICATION_SHORTNAME}")
endif()

file(GLOB_RECURSE OPENCLOUD_ICONS "${OEM_THEME_DIR}/theme/colored/*-${APPLICATION_ICON_NAME}-icon.png")
MESSAGE(STATUS "OPENCLOUD_ICONS: ${APPLICATION_ICON_NAME}: ${OPENCLOUD_ICONS}")

add_subdirectory(src)
if(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/admin)
    add_subdirectory(admin)
endif(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/admin)

if(BUILD_SHELL_INTEGRATION)
    add_subdirectory(shell_integration)
endif()

if(BUILD_TESTING)
    add_subdirectory(test)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

if(ECM_VERSION VERSION_GREATER_EQUAL 5.79)
    message(STATUS "Suitable ECM ${ECM_VERSION} found, installing clang-format git hook")
    include(KDEGitCommitHooks)
    kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)
else()
    message(WARNING "ECM ${ECM_VERSION} too old, cannot install clang-format git hook")
endif()
