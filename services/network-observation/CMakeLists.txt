add_subdirectory(inputs)
add_subdirectory(core)
add_subdirectory(service)
add_subdirectory(transports)
add_subdirectory(clients)
add_subdirectory(apps)

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/config/network-observationd.conf
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/config/network-observationd.conf.example
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/network-observation
    COMPONENT rel
)

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/monit/network-observationd.cfg
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/monit.d
    COMPONENT rel
)

install(
    PROGRAMS
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/scripts/start-network-observationd
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/scripts/stop-network-observationd
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/scripts/check-network-observationd
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/network-observation
    COMPONENT rel
)

install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/dbus/org.rsc.NetworkObservation.conf
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/dbus-1/system.d
    COMPONENT rel
)
