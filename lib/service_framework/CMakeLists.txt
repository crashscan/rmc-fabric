add_library(service_framework STATIC
    ServiceBase.cpp
)

target_include_directories(service_framework
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_framework
    PUBLIC
        lifecycle_runner
    PRIVATE
        glog
)
