add_library(inventory-client STATIC
        InventoryClient.cpp
)

target_compile_features(inventory-client PUBLIC cxx_std_20)

target_include_directories(inventory-client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${RMC_INVENTORY_ROOT_DIR}/transports/dbus>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/rmc_fabric/inventory>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib/dbus_client_support>
)

target_link_libraries(inventory-client
    PUBLIC
        interop_contract
    PRIVATE
        inventory_dbus_codec
        DBusCxx::DBusCxx
        SigCxx::SigCxx
)

install(
    TARGETS inventory-client
    EXPORT  rmc_fabricTargets
    ARCHIVE COMPONENT dev
)

install(
    DIRECTORY   ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rmc_fabric/inventory
    COMPONENT   dev
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
