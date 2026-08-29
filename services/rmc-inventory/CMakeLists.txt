find_package(SigCxx REQUIRED MODULE)
find_package(DBusCxx REQUIRED MODULE)

option(INVENTORY_ENABLE_STDOUT_TRANSPORT "Enable StdoutInventoryTransport for development/debugging" OFF)

set(CONTRACTS_BASE   "${CMAKE_CURRENT_SOURCE_DIR}/fabric-contracts")
set(INVENTORY_API_BASE "${CMAKE_CURRENT_SOURCE_DIR}/api")
set(INVENTORY_CORE_BASE "${CMAKE_CURRENT_SOURCE_DIR}/core")
set(LIBS_BASE        "${CMAKE_CURRENT_SOURCE_DIR}/libs")
set(LIBS_CLIENT_BASE "${CMAKE_CURRENT_SOURCE_DIR}/libs/inventory-client")
set(SRC_BASE         "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_subdirectory(core)
add_subdirectory(api)
add_subdirectory(transports)
add_subdirectory(libs)
add_subdirectory(apps)
