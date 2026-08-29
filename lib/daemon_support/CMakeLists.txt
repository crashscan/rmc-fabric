add_library(daemon_support STATIC
    DaemonSignals.cpp
)

target_include_directories(daemon_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
