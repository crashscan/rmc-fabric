add_executable(net-observe
    main.cpp
)

target_link_libraries(net-observe
    PRIVATE
        network_observation_client
        pthread
        glog
        gflags
        jsoncpp
)

install(
    TARGETS net-observe
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT rel
)
