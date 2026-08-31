add_library(file_watcher STATIC
    InotifyFileWatcher.cpp
)

target_include_directories(file_watcher
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(file_watcher
    PRIVATE
        linux_support
        glog
)
