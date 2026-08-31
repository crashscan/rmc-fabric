add_executable(inventory-agentd
        main.cpp
)

target_link_libraries(inventory-agentd
        PRIVATE DBusCxx::DBusCxx SigCxx::SigCxx
        pthread glog gflags jsoncpp
        inventory_service
        inventory_core
        inventory_service_ports
        inventory_transport
        service_framework
        daemon_support
)