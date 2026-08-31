add_library(interop_contract INTERFACE)

target_include_directories(interop_contract
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Intentionally no transport-library linkage here.

install(
    TARGETS interop_contract
    EXPORT  rmc_fabricTargets
    COMPONENT dev
)

install(
    DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION
        ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT dev
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        PATTERN "tests" EXCLUDE
)

if(BUILD_TESTING)
    add_executable(interop_contract_tests
        tests/StatusTests.cpp
    )

    add_executable(interop_inventory_contract_tests
        tests/InventoryContractAllTests.cpp
    )

    target_link_libraries(interop_contract_tests
        PRIVATE
            interop_contract
    )

    target_link_libraries(interop_inventory_contract_tests
        PRIVATE
            interop_contract
    )

    add_test(
        NAME interop_contract_tests
        COMMAND interop_contract_tests
    )
    set_tests_properties(interop_contract_tests PROPERTIES LABELS "unit")

    add_test(
        NAME interop_inventory_contract_tests
        COMMAND interop_inventory_contract_tests
    )
    set_tests_properties(interop_inventory_contract_tests PROPERTIES LABELS "unit")
endif()