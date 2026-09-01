 add_library(dbus_client_support INTERFACE)

 target_include_directories(dbus_client_support
         INTERFACE
         ${CMAKE_CURRENT_SOURCE_DIR}
 )

 # Internal client-boundary helpers. This target is used only while building
 # repository clients and is not part of the installed package surface.
 target_link_libraries(dbus_client_support
     INTERFACE
         interop_contract
         DBusCxx::DBusCxx
         glog
 )

 # This target is intentionally not installed or exported as public package API.