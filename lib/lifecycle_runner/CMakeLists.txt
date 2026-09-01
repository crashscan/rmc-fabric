add_library(lifecycle_runner STATIC
    LifecycleCoordinator.cpp
    ManagedWorker.cpp
)

target_compile_features(lifecycle_runner PUBLIC cxx_std_20)

target_include_directories(lifecycle_runner
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(lifecycle_runner
    PRIVATE
        glog
)

if(BUILD_TESTING)
    add_executable(managed_worker_tests
        tests/ManagedWorkerTests.cpp
    )
    target_compile_features(managed_worker_tests PRIVATE cxx_std_20)
    target_link_libraries(managed_worker_tests
        PRIVATE
            lifecycle_runner
            glog
            pthread
    )
    add_test(NAME managed_worker_tests COMMAND managed_worker_tests)
    set_tests_properties(managed_worker_tests PROPERTIES TIMEOUT 30 LABELS "unit;resilience")

    add_executable(lifecycle_coordinator_tests
        tests/LifecycleCoordinatorTests.cpp
    )
    target_compile_features(lifecycle_coordinator_tests PRIVATE cxx_std_20)
    target_link_libraries(lifecycle_coordinator_tests
        PRIVATE
            lifecycle_runner
            glog
            pthread
    )
    add_test(NAME lifecycle_coordinator_tests COMMAND lifecycle_coordinator_tests)
    set_tests_properties(lifecycle_coordinator_tests PROPERTIES TIMEOUT 30 LABELS "unit;resilience")
endif()
