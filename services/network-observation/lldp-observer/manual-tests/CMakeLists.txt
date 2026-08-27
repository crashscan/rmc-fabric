add_executable(lldp-dump lldp_dump.cpp)
target_link_libraries(lldp-dump lldp-observer
        pthread
        glog
        gflags
        jsoncpp
        liblldpctl.so
)

target_include_directories(lldp-dump PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../public
    ${CMAKE_CURRENT_SOURCE_DIR}/../lldpd
    ${OBS_BASE}/public
)