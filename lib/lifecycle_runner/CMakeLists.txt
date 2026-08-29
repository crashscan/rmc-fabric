add_library(lifecycle_runner STATIC
    LifecycleManager.cpp
    WorkerThread.cpp
)

target_include_directories(lifecycle_runner
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(lifecycle_runner
    PRIVATE
        glog
)
