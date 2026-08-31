cmake_minimum_required(VERSION 3.16)

add_library(lldp-observer SHARED
    LldpObserver.cpp
    lldpd/LldpdSource.cpp
)
target_compile_features(lldp-observer PUBLIC cxx_std_20)

find_path(LLDP_CPP_INCLUDE_DIR
        NAMES lldpctl.h
        ONLY_CMAKE_FIND_ROOT_PATH
)

target_include_directories(lldp-observer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/public
    ${CMAKE_CURRENT_SOURCE_DIR}/lldpd
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/public
    ${LLDP_CPP_INCLUDE_DIR}
)

if(BUILD_MANUAL_TESTS)
    add_subdirectory(manual-tests)
endif()

target_link_libraries(lldp-observer PRIVATE
        liblldpctl.so
)

if(BUILD_TESTING)
    find_package(GTest CONFIG)
    if(GTest_FOUND)
        add_executable(lldp_observer_tests
            tests/LldpUtilsTest.cpp
            tests/LldpdSourceTest.cpp
        )
        target_include_directories(lldp_observer_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/public
            ${CMAKE_CURRENT_SOURCE_DIR}/lldpd
            ${CMAKE_CURRENT_SOURCE_DIR}/../../core
            ${CMAKE_CURRENT_SOURCE_DIR}/../../core/public
            ${LLDP_CPP_INCLUDE_DIR}
        )
        target_link_libraries(lldp_observer_tests PRIVATE
            lldp-observer
            GTest::gtest
            GTest::gtest_main
        )
        add_test(NAME lldp_observer_tests COMMAND lldp_observer_tests)
    endif()
endif()
