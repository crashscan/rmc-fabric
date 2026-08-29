# LLDP Observer module
cmake_minimum_required(VERSION 3.16)

# --- liblldp-observer.so — LLDP observation source ---
add_library(lldp-observer SHARED
    LldpObserver.cpp
    lldpd/LldpdSource.cpp
)

find_path(LLDP_CPP_INCLUDE_DIR
        NAMES lldpctl.h
        ONLY_CMAKE_FIND_ROOT_PATH
)

target_include_directories(lldp-observer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/public
    ${CMAKE_CURRENT_SOURCE_DIR}/lldpd
    ${OBS_BASE}/public
    ${LLDP_CPP_INCLUDE_DIR}
)

add_subdirectory(manual-tests)

# Find lldpctl via pkg-config
#find_package(PkgConfig REQUIRED)
#pkg_check_modules(LLDPCTL REQUIRED lldpctl)

#target_include_directories(lldp-observer PRIVATE
#    ${LLDPCTL_INCLUDE_DIRS}
#)

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
            ${OBS_BASE}/public
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
