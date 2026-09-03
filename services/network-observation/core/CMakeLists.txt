add_library(observation-model SHARED
        engine/ObservationModelEngine.cpp
        engine/NetworkObservationModelFactory.cpp
        engine/HardFilter.cpp
        engine/LocalStateTracker.cpp
        classifier/RuleBasedClassifier.cpp
        classifier/ScoringClassifier.cpp
        classifier/ClassifierFactory.cpp
        policy/DefaultInterfacePolicy.cpp
)

target_compile_features(observation-model
        PUBLIC cxx_std_20
)

target_include_directories(observation-model
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/classifier
        ${CMAKE_CURRENT_SOURCE_DIR}/policy
)

target_link_libraries(observation-model
        PUBLIC
        interop_contract
        PRIVATE
        pthread
        glog
        rsc_util
)

if(BUILD_TESTING)
    add_executable(observation_model_public_headers_test
            tests/PublicHeadersCompileTest.cpp
    )

    target_link_libraries(observation_model_public_headers_test
            PRIVATE
            observation-model
    )

    add_test(
            NAME observation_model_public_headers_test
            COMMAND observation_model_public_headers_test
    )
endif()

install(
        TARGETS observation-model
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT rel
)

