add_library(dbus_client_support INTERFACE)

target_include_directories(dbus_client_support
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Internal low-level helper only; consumers must link DBusCxx themselves.
# This target is intentionally not installed or exported as public package API.
