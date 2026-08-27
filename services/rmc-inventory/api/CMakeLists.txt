add_library(inventory_service_api INTERFACE)

target_include_directories(inventory_service_api
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(inventory_service_api INTERFACE interop_contract inventory_core)
