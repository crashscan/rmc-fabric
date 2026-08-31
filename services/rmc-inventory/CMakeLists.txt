find_package(SigCxx REQUIRED MODULE)
find_package(DBusCxx REQUIRED MODULE)

option(INVENTORY_ENABLE_STDOUT_TRANSPORT "Enable StdoutInventoryTransport for development/debugging" OFF)

add_subdirectory(core)
add_subdirectory(service)
add_subdirectory(inputs)
add_subdirectory(transports)
add_subdirectory(clients)
add_subdirectory(apps)
