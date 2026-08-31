set(NETLINK_BASE   "${CMAKE_CURRENT_SOURCE_DIR}/netlink-monitor")
set(OBS_BASE       "${CMAKE_CURRENT_SOURCE_DIR}/observation-model")
set(TRANSPORT_BASE "${CMAKE_CURRENT_SOURCE_DIR}/transports")

add_subdirectory(lldp-observer)
add_subdirectory(netlink-monitor)
add_subdirectory(observation-model)
add_subdirectory(transports)
add_subdirectory(apps)
