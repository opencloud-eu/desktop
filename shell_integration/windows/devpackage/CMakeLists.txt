# Development only: registers the build directory as an unpacked package, to test the Explorer integration of the msix package.
# Requires Developer Mode. Build register_dev_package, then start the client with launch_dev_package and attach the debugger.
# Packaged processes don't search PATH, register_dev_package links the bin directories of CMAKE_PREFIX_PATH into the package
# and the manifest adds them to the DLL search path, qt.conf points Qt to the prefix reported by qmake.
# unregister_dev_package removes the package again.

set(OC_DEV_PACKAGE_NAME "OpenCloudGmbH.OpenCloudDevelopment" CACHE STRING "Identity name of the development package")
set(OC_DEV_PACKAGE_PUBLISHER "CN=OpenCloud Development" CACHE STRING "Publisher of the development package")
set(OC_DEV_PACKAGE_APPLICATION_ID "OpenCloudDevelopment")
set(OC_DEV_PACKAGE_DISPLAY_NAME "OpenCloud Development")
set(OC_DEV_PACKAGE_VERSION "${PROJECT_VERSION}.0")

set(_loaderLinks "")
set(OC_DEV_PACKAGE_LOADER_SEARCH_PATHS "")
set(_index 0)
foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
    if(_index EQUAL 5)
        message(WARNING "The DLL search path of a package is limited to 5 folders, ignoring ${_prefix}")
        continue()
    endif()
    file(TO_NATIVE_PATH "${_prefix}/bin" _prefixBin)
    list(APPEND _loaderLinks "prefix-bin-${_index}=${_prefixBin}")
    string(APPEND OC_DEV_PACKAGE_LOADER_SEARCH_PATHS "        <uap6:LoaderSearchPathEntry FolderPath=\"prefix-bin-${_index}\" />\n")
    math(EXPR _index "${_index} + 1")
endforeach()
string(REPLACE ";" "$<SEMICOLON>" _loaderLinks "${_loaderLinks}")
execute_process(
    COMMAND "${QT_QMAKE_EXECUTABLE}" -query QT_INSTALL_PREFIX
    OUTPUT_VARIABLE _qtPrefix
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _qmakeResult
)
if(NOT _qmakeResult EQUAL 0 OR NOT _qtPrefix)
    message(FATAL_ERROR "Failed to query QT_INSTALL_PREFIX from ${QT_QMAKE_EXECUTABLE}")
endif()
file(TO_CMAKE_PATH "${_qtPrefix}" _qtPrefix)

set(_manifest "$<TARGET_FILE_DIR:opencloud>/AppxManifest.xml")
configure_file(AppxManifest.xml.in "${CMAKE_CURRENT_BINARY_DIR}/AppxManifest.xml.in" @ONLY)
file(GENERATE OUTPUT "${_manifest}" INPUT "${CMAKE_CURRENT_BINARY_DIR}/AppxManifest.xml.in")
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:opencloud>/qt.conf" CONTENT "[Paths]\nPrefix=${_qtPrefix}\n")

set(_iconDir "${PROJECT_SOURCE_DIR}/src/resources/theme/colored")
add_custom_target(dev_package_assets ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:opencloud>/Assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_iconDir}/44-opencloud-icon-ms.png" "${_iconDir}/150-opencloud-icon-ms.png" "$<TARGET_FILE_DIR:opencloud>/Assets/"
    VERBATIM
)

# Add-AppxPackage is not available in PowerShell 7 without the compatibility layer, use Windows PowerShell.
# Unset PSModulePath, when inherited from PowerShell 7 Windows PowerShell loads incompatible modules.
set(_powershell "${CMAKE_COMMAND}" -E env --unset=PSModulePath powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass)
add_custom_target(register_dev_package
    COMMAND ${_powershell} -File "${CMAKE_CURRENT_SOURCE_DIR}/register.ps1"
        -Manifest "$<SHELL_PATH:${_manifest}>"
        -PackageName "${OC_DEV_PACKAGE_NAME}"
        -LoaderLinks "${_loaderLinks}"
    DEPENDS dev_package_assets opencloud OpenCloudExplorerCommand
    COMMENT "Registering $<TARGET_FILE_DIR:opencloud> as development package"
    VERBATIM
)
add_custom_target(launch_dev_package
    COMMAND ${_powershell} -File "${CMAKE_CURRENT_SOURCE_DIR}/launch.ps1"
        -PackageName "${OC_DEV_PACKAGE_NAME}"
        -ApplicationId "${OC_DEV_PACKAGE_APPLICATION_ID}"
    COMMENT "Launching the client from the development package"
    VERBATIM
)
add_custom_target(unregister_dev_package
    COMMAND ${_powershell} -Command "Get-AppxPackage -Name '${OC_DEV_PACKAGE_NAME}' | Remove-AppxPackage"
    COMMENT "Unregistering the development package"
    VERBATIM
)
