add_executable(inventory-agentd
        main.cpp
)

target_link_libraries(inventory-agentd
        PRIVATE DBusCxx::DBusCxx SigCxx::SigCxx
        pthread glog gflags jsoncpp
        inventory_service
        inventory_core
        inventory_file_inputs
        inventory_transport
        daemon_support
)

install(
    TARGETS inventory-agentd
    RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR}
    COMPONENT rel
)
