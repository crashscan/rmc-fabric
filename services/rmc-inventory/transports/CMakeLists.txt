set(TRANSPORT_SOURCES
    DbusInventoryTransport.cpp
    InventoryDbusAdapter.cpp
    TransportFactory.cpp
)

if(INVENTORY_ENABLE_STDOUT_TRANSPORT)
    list(APPEND TRANSPORT_SOURCES StdoutInventoryTransport.cpp)
    add_compile_definitions(INVENTORY_ENABLE_STDOUT_TRANSPORT)
endif()

add_library(inventory_transport STATIC ${TRANSPORT_SOURCES})
add_library(inventory_dbus_codec STATIC InventoryDbusCodec.cpp)

target_compile_features(inventory_transport PUBLIC cxx_std_20)
target_compile_features(inventory_dbus_codec PUBLIC cxx_std_20)

target_include_directories(inventory_transport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../service/ports>
        $<INSTALL_INTERFACE:include/rmc-inventory/transports>
)

target_include_directories(inventory_dbus_codec
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/rmc_fabric/inventory>
)

target_link_libraries(inventory_transport
    PUBLIC
        dbus_transport_base
        interop_contract
        inventory_service_ports
        service_framework
    PRIVATE
        inventory_dbus_codec
        DBusCxx::DBusCxx
        SigCxx::SigCxx
        glog
)

target_link_libraries(inventory_dbus_codec
    PUBLIC
        interop_contract
        DBusCxx::DBusCxx
)

if(BUILD_TESTING)
    add_executable(inventory_dbus_codec_characterization_tests
        tests/InventoryDbusCodecCharacterizationTests.cpp
    )

    target_compile_features(inventory_dbus_codec_characterization_tests PRIVATE cxx_std_20)

    target_link_libraries(inventory_dbus_codec_characterization_tests
        PRIVATE
            DBusCxx::DBusCxx
            interop_contract
            inventory_dbus_codec
    )

    add_test(
        NAME inventory_dbus_codec_characterization_tests
        COMMAND inventory_dbus_codec_characterization_tests
    )
    set_tests_properties(inventory_dbus_codec_characterization_tests PROPERTIES TIMEOUT 10 LABELS "unit")
endif()

install(
    TARGETS inventory_dbus_codec
    EXPORT  rmc_fabricTargets
    ARCHIVE COMPONENT dev
)
