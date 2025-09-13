configure_file(vfspluginmetadata.json.in ${CMAKE_CURRENT_BINARY_DIR}/vfspluginmetadata.json)

target_sources(libsync PRIVATE vfs.cpp hydrationjob.cpp)
