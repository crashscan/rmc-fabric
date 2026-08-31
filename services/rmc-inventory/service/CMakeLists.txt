add_subdirectory(ports)

add_library(inventory_service STATIC
    InventoryService.cpp
)

target_compile_features(inventory_service PUBLIC cxx_std_20)

target_include_directories(inventory_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/rmc-inventory/service>
)

target_link_libraries(inventory_service
    PUBLIC
        inventory_core
        inventory_service_ports
        linux_support
        service_framework
    PRIVATE
        file_watcher
        glog
)

if(BUILD_TESTING)
    add_executable(inventory_service_tests
        tests/InventoryServiceTests.cpp
    )

    target_compile_features(inventory_service_tests PRIVATE cxx_std_20)

    target_link_libraries(inventory_service_tests
        PRIVATE
            inventory_service
            inventory_core
            file_watcher
            pthread
    )

    add_test(NAME inventory_service_tests COMMAND inventory_service_tests)
    set_tests_properties(inventory_service_tests PROPERTIES TIMEOUT 15)
endif()
