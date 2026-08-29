add_executable(inventory-agentd
        main.cpp
        ${SRC_BASE}/InventoryService.cpp
)

target_include_directories(inventory-agentd PRIVATE
        ${SRC_BASE}
        ${INVENTORY_CORE_BASE}
        ${LIBS_CLIENT_BASE}/include
)

target_link_libraries(inventory-agentd
        PRIVATE DBusCxx::DBusCxx SigCxx::SigCxx
        pthread glog gflags jsoncpp
        interop_contract
        inventory_core
        inventory_service_api
        inventory_io
        inventory_transport
        service_framework
        daemon_support
)