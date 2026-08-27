find_package(SigCxx REQUIRED MODULE)
find_package(DBusCxx REQUIRED MODULE)

set(CONTRACTS_BASE   "${CMAKE_CURRENT_SOURCE_DIR}/fabric-contracts")
set(INCLUDE_BASE     "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(LIBS_BASE        "${CMAKE_CURRENT_SOURCE_DIR}/libs")
set(LIBS_CLIENT_BASE "${CMAKE_CURRENT_SOURCE_DIR}/libs/inventory-client")
set(PUBLISHERS_BASE  "${CMAKE_CURRENT_SOURCE_DIR}/publishers")
set(SRC_BASE         "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(INVENTORY_CORE_BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/inventory_core/include")

add_subdirectory(libs)
add_subdirectory(apps)
