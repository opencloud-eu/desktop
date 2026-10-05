target_sources(OpenCloudGui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/socketapi.cpp
    )
    
if( APPLE )
    target_sources(OpenCloudGui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/socketapisocket_mac.mm)
    # Enable ARC for this file as it uses __weak references in XPC handlers
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/socketapisocket_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()
