find_package(SigCxx REQUIRED MODULE)
find_package(DBusCxx REQUIRED MODULE)

option(INVENTORY_ENABLE_STDOUT_TRANSPORT "Enable StdoutInventoryTransport for development/debugging" OFF)

add_subdirectory(core)
add_subdirectory(service)
add_subdirectory(inputs)
add_subdirectory(transports)
add_subdirectory(clients)
add_subdirectory(apps)

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/config/inventory-agentd.conf
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/inventory-agent
    COMPONENT rel
)

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/monit/inventory-agentd.cfg
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/monit.d
    COMPONENT rel
)

install(
    PROGRAMS
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/scripts/start-inventory-agentd
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/scripts/stop-inventory-agentd
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/scripts/check-inventory-agentd
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/inventory-agent
    COMPONENT rel
)

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/dbus/org.rsc.Inventory.conf
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/dbus-1/system.d
    COMPONENT rel
)
