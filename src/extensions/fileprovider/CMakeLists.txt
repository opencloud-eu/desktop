# CMake build configuration for the macOS File Provider App Extension.
# Builds an .appex bundle implementing NSFileProviderReplicatedExtension
# for Files On Demand support on macOS 12+.

if(APPLE)
    enable_language(OBJCXX)

    set(FILEPROVIDER_EXTENSION_SOURCES
        OpenCloudFileProviderExtension.mm
        FileProviderXPCService.mm
        FileProviderItem.mm
        FileProviderEnumerator.mm
        FileProviderThumbnails.mm
    )

    set(FILEPROVIDER_EXTENSION_HEADERS
        OpenCloudFileProviderExtension.h
        FileProviderXPCService.h
        FileProviderItem.h
        FileProviderEnumerator.h
        FileProviderThumbnails.h
    )

    # Must be add_executable (not add_library MODULE) so the Mach-O filetype
    # is MH_EXECUTE (2) rather than MH_BUNDLE (8). macOS requires app
    # extensions to be executables for sandbox entitlements to be embedded.
    add_executable(OpenCloudFileProviderExtension
        ${FILEPROVIDER_EXTENSION_SOURCES}
        ${FILEPROVIDER_EXTENSION_HEADERS}
    )

    # Mark as an App Extension bundle (.appex)
    set_target_properties(OpenCloudFileProviderExtension PROPERTIES
        BUNDLE_EXTENSION "appex"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "OpenCloudFileProvider"
        MACOSX_BUNDLE_BUNDLE_VERSION "${MIRALL_VERSION_FULL}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${MIRALL_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${APPLICATION_REV_DOMAIN}.fileprovider"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/OpenCloudFileProvider.entitlements"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "$(CODE_SIGN_IDENTITY)"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "P4D766R5ZA"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${APPLICATION_REV_DOMAIN}.fileprovider"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
    )

    # App Extension compile flag: restricts API surface to extension-safe APIs
    target_compile_options(OpenCloudFileProviderExtension PRIVATE
        -fapplication-extension
        -fobjc-arc
    )

    # Link against required Apple frameworks
    target_link_libraries(OpenCloudFileProviderExtension PRIVATE
        "-framework CoreGraphics"
        "-framework Foundation"
        "-framework FileProvider"
        "-framework UniformTypeIdentifiers"
    )

    # Linker flag for extension-safe linking
    target_link_options(OpenCloudFileProviderExtension PRIVATE
        -fapplication-extension
        -e _NSExtensionMain
    )

    # Set deployment target to macOS 12+ (minimum for NSFileProviderReplicatedExtension)
    set_target_properties(OpenCloudFileProviderExtension PROPERTIES
        XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "12.0"
    )

    # Embed the extension in the main application bundle under PlugIns/
    # XCODE_EMBED_APP_EXTENSIONS only works with Xcode generator.
    # For Ninja/Makefile generators, use a post-build copy step.
    if(TARGET ${APPLICATION_EXECUTABLE})
        if(CMAKE_GENERATOR STREQUAL "Xcode")
            set_target_properties(${APPLICATION_EXECUTABLE} PROPERTIES
                XCODE_EMBED_APP_EXTENSIONS OpenCloudFileProviderExtension
            )
        else()
            add_custom_command(TARGET OpenCloudFileProviderExtension POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "$<TARGET_BUNDLE_DIR:OpenCloudFileProviderExtension>"
                    "$<TARGET_FILE_DIR:${APPLICATION_EXECUTABLE}>/../PlugIns/OpenCloudFileProviderExtension.appex"
                COMMENT "Embedding FileProviderExtension.appex into app bundle"
            )
            # Sign the extension with entitlements (XCODE_ATTRIBUTE_* only works
            # with the Xcode generator, so Ninja/Make need an explicit codesign).
            if(DEFINED CODESIGN_IDENTITY AND NOT CODESIGN_IDENTITY STREQUAL "")
                add_custom_command(TARGET OpenCloudFileProviderExtension POST_BUILD
                    COMMAND codesign --force --sign "${CODESIGN_IDENTITY}"
                        --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/OpenCloudFileProvider.entitlements"
                        "$<TARGET_BUNDLE_DIR:OpenCloudFileProviderExtension>"
                    COMMAND codesign --force --sign "${CODESIGN_IDENTITY}"
                        --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/OpenCloudFileProvider.entitlements"
                        "$<TARGET_FILE_DIR:${APPLICATION_EXECUTABLE}>/../PlugIns/OpenCloudFileProviderExtension.appex"
                    COMMENT "Code-signing FileProviderExtension with sandbox entitlements"
                )
            endif()
        endif()
    endif()

    # Notarisation: After archiving, run `xcrun notarytool submit <archive> --apple-id ...`
    # to notarise the signed application bundle. This is handled by the release pipeline,
    # not as part of the CMake build. Hardened Runtime (set above) is required for
    # successful notarisation.

    # Install extension into the app bundle PlugIns directory
    install(TARGETS OpenCloudFileProviderExtension
        RUNTIME DESTINATION "${KDE_INSTALL_BUNDLEDIR}/${APPLICATION_NAME}.app/Contents/PlugIns"
        BUNDLE DESTINATION "${KDE_INSTALL_BUNDLEDIR}/${APPLICATION_NAME}.app/Contents/PlugIns"
    )
endif()
