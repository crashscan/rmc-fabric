add_library(inventory-client STATIC
        src/InventoryClient.cpp
)

target_include_directories(inventory-client PUBLIC
        ${DBusCxx_INCLUDE_DIRS}
        ${SigCxx_INCLUDE_DIRS}
        include
)

if(BUILD_TESTING)
    add_executable(inventory_dbus_codec_characterization_tests
        tests/InventoryDbusCodecCharacterizationTests.cpp
    )

    target_include_directories(inventory_dbus_codec_characterization_tests PRIVATE
        ${DBusCxx_INCLUDE_DIRS}
        ${SigCxx_INCLUDE_DIRS}
        include
    )

    target_link_libraries(inventory_dbus_codec_characterization_tests
        PRIVATE DBusCxx::DBusCxx
                interop_contract
                inventory_transport
    )
endif()

target_link_libraries(inventory-client
    PUBLIC interop_contract
           inventory_transport
)