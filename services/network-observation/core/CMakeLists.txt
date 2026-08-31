add_library(observation-model SHARED
    engine/ObservationModelEngine.cpp
    engine/HardFilter.cpp
    engine/LocalStateTracker.cpp
    classifier/RuleBasedClassifier.cpp
    classifier/ScoringClassifier.cpp
    classifier/ClassifierFactory.cpp
    policy/DefaultInterfacePolicy.cpp
)
target_compile_features(observation-model PUBLIC cxx_std_20)
target_include_directories(observation-model PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>
)
target_include_directories(observation-model PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/classifier>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/policy>
)
target_link_libraries(observation-model pthread glog rsc_util interop_contract)

install(
    TARGETS observation-model
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT rel
)

if(BUILD_TESTING)
    find_package(GTest CONFIG)
    if(GTest_FOUND)
        add_executable(observation_model_tests
            tests/EngineLifecycleTest.cpp
            tests/LldpIngestionTest.cpp
        )
        target_include_directories(observation_model_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
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
        set_tests_properties(observation_model_tests PROPERTIES LABELS "unit")
    endif()
endif()
