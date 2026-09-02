add_executable(inventory-agentd
        main.cpp
)

target_include_directories(inventory-agentd
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${LIBPATH}/file_watcher>
        $<INSTALL_INTERFACE:include/rmc-inventory/service>
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
