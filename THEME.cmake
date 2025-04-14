if(WITH_EXTERNAL_BRANDING)
    include(FetchContent)

    FetchContent_Declare(branding
            GIT_REPOSITORY ${WITH_EXTERNAL_BRANDING}
            GIT_TAG main
    )
    FetchContent_MakeAvailable(branding)

    set(OEM_THEME_DIR ${branding_SOURCE_DIR} CACHE STRING "The directory containing a custom theme")
else()
    if (EXISTS "${PROJECT_SOURCE_DIR}/branding")
        set(OEM_THEME_DIR "${PROJECT_SOURCE_DIR}/branding" CACHE STRING "The directory containing a custom theme")
    else()
        set(OEM_THEME_DIR "${PROJECT_SOURCE_DIR}/src/resources/" CACHE STRING "Define directory containing a custom theme")
    endif()
endif()

if (EXISTS "${OEM_THEME_DIR}/OEM.cmake")
    include("${OEM_THEME_DIR}/OEM.cmake")
else()
    include ("${CMAKE_CURRENT_LIST_DIR}/OPENCLOUD.cmake")
endif()

message(STATUS "Branding: ${APPLICATION_NAME}")


if(NOT CRASHREPORTER_EXECUTABLE)
    set(CRASHREPORTER_EXECUTABLE "${APPLICATION_EXECUTABLE}_crash_reporter")
endif()


include("${CMAKE_CURRENT_LIST_DIR}/VERSION.cmake")
