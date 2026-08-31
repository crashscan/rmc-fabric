add_library(service_framework STATIC
    ServiceBase.cpp
    GflagsConfig.cpp
    LoggingInitializer.cpp
    DaemonRunner.cpp
)

target_include_directories(service_framework
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_framework
    PUBLIC
        lifecycle_runner
        daemon_support
    PRIVATE
        rmc_fabric_warnings
        glog
        gflags
)

if(BUILD_TESTING)
    add_executable(service_framework_tests
        tests/ServiceBaseTests.cpp
    )
    target_compile_features(service_framework_tests PRIVATE cxx_std_20)
    target_link_libraries(service_framework_tests
        PRIVATE
            service_framework
            pthread
    )
    add_test(NAME service_framework_tests COMMAND service_framework_tests)
    set_tests_properties(service_framework_tests PROPERTIES TIMEOUT 10 LABELS "unit")
endif()
