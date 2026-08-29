add_library(dbus_client_support INTERFACE)

target_include_directories(dbus_client_support
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Consumers must link DBusCxx themselves — this library is header-only.
