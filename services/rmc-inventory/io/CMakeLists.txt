add_library(inventory_io STATIC
    InotifyFileWatcher.cpp
)

target_include_directories(inventory_io
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(inventory_io
    PRIVATE
        glog
)
