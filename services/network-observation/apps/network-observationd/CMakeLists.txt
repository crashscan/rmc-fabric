add_executable(network-observationd
    main.cpp
)

target_include_directories(network-observationd
        PRIVATE
        #$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../core/engine>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../core/classifier>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../core/policy>
        #$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../core/public>
        #$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../inputs/netlink/types>

)

target_link_libraries(network-observationd
    PRIVATE
        network_observation_service
        network_observation_transport_factory
        daemon_support
        pthread
        glog
        gflags
        jsoncpp
        liblldpctl.so
)

install(
    TARGETS network-observationd
    RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR}
    COMPONENT rel
)
