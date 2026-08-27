# LLDP Observer module
cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
