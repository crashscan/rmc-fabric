find_package(DBusCxx REQUIRED MODULE)

add_library(dbus_transport_base STATIC
    DbusTransportBase.cpp
    DbusServiceAdapter.cpp
)

target_include_directories(dbus_transport_base
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(dbus_transport_base
    PRIVATE
        DBusCxx::DBusCxx
        glog
)

if(BUILD_TESTING)
    add_executable(service_binding_tests
        tests/ServiceBindingTest.cpp
    )
    target_include_directories(service_binding_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(service_binding_tests PRIVATE pthread)
    add_test(NAME service_binding_tests COMMAND service_binding_tests)
    set_tests_properties(service_binding_tests PROPERTIES TIMEOUT 10)
endif()
