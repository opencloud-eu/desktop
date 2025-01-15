target_sources(OpenCloudGui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/socketapi.cpp
    )
    
if( APPLE )
    target_sources(OpenCloudGui PRIVATE socketapisocket_mac.mm)
endif()
