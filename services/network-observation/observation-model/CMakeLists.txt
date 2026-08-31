# ---------------------------------------------------------------------------
# libobservation-model.so — observation model / classifier / hard filter
# ---------------------------------------------------------------------------
add_library(observation-model SHARED
    engine/ObservationModelEngine.cpp
    engine/HardFilter.cpp
    engine/LocalStateTracker.cpp
    classifier/RuleBasedClassifier.cpp
    classifier/ScoringClassifier.cpp
    classifier/ClassifierFactory.cpp
    policy/DefaultInterfacePolicy.cpp
    adapter/NetworkObservationAdapter.cpp
)
target_include_directories(observation-model PUBLIC
    public
    adapter
    classifier
    engine
    policy
    ${NETLINK_BASE}
    ${NETLINK_BASE}/types
    ${NETLINK_BASE}/utils
    ${NETLINK_BASE}/parser
    ${NETLINK_BASE}/state
)
target_include_directories(observation-model PRIVATE
    engine
    classifier
    policy
)
target_link_libraries(observation-model pthread glog rsc_util lldp-observer netlink-monitor liblldpctl.so interop_contract)

if(BUILD_TESTING)
    find_package(GTest CONFIG)
    if(GTest_FOUND)
        add_executable(observation_model_tests
            tests/EngineLifecycleTest.cpp
            tests/LldpIngestionTest.cpp
        )
        target_include_directories(observation_model_tests PRIVATE
            public
            engine
            classifier
        )
        target_link_libraries(observation_model_tests PRIVATE
            observation-model
            GTest::gtest
            GTest::gtest_main
        )
        add_test(NAME observation_model_tests COMMAND observation_model_tests)
    endif()
endif()
