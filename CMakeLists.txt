cmake_minimum_required(VERSION 3.25.0 FATAL_ERROR)
project(rmc-fabric VERSION 0.0.1 LANGUAGES C CXX)

message(STATUS "Building rmc-fabric version: ${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# C++ standard — strict, no compiler extensions
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Developer/CI options (not exported to downstream consumers)
# ---------------------------------------------------------------------------
option(RMC_FABRIC_WERROR   "Treat warnings as errors (CI/developer builds)"   OFF)
option(RMC_FABRIC_COVERAGE "Enable gcov/lcov coverage instrumentation"        OFF)
set(RMC_FABRIC_SANITIZE "" CACHE STRING
    "Comma-separated sanitizer list, e.g. address,undefined (passed to -fsanitize=)")

# ---------------------------------------------------------------------------
# rmc_fabric_warnings — INTERFACE target consumed only by in-tree targets.
# Downstream consumers never see these flags.
# ---------------------------------------------------------------------------
add_library(rmc_fabric_warnings INTERFACE)

target_compile_options(rmc_fabric_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall
        -Wextra
        -Wshadow
        -Wno-unused-parameter
        $<$<BOOL:${RMC_FABRIC_WERROR}>:-Werror>
    >
)

if(RMC_FABRIC_SANITIZE)
    target_compile_options(rmc_fabric_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
            -fsanitize=${RMC_FABRIC_SANITIZE}
            -fno-omit-frame-pointer
        >
    )
    target_link_options(rmc_fabric_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
            -fsanitize=${RMC_FABRIC_SANITIZE}
        >
    )
endif()

if(RMC_FABRIC_COVERAGE)
    target_compile_options(rmc_fabric_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU>:--coverage>
    )
    target_link_options(rmc_fabric_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU>:--coverage>
    )
endif()

# Global compile flags: -g is intentionally omitted here; debug symbols are
# controlled by CMAKE_BUILD_TYPE / CMAKE_C(XX)_FLAGS_DEBUG.
# Position-independent code and thread support are set repository-wide because
# all targets are either shared libs or executables that link shared libs.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -pthread")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -fPIC -pthread")

set(BASEPATH "${CMAKE_SOURCE_DIR}")
add_definitions(-DGLOG_USE_GLOG_EXPORT)

find_path(RSC_UTIL_INCLUDE_DIR
        NAMES rsc_util
        ONLY_CMAKE_FIND_ROOT_PATH
)
include_directories(
        ${PROJECT_BINARY_DIR}
        ${RSC_UTIL_INCLUDE_DIR}/
        ${RSC_UTIL_INCLUDE_DIR}/rsc_util
        ${GLOG_INCLUDE_DIRS}
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Testing — BUILD_TESTING is the CMake standard option; include(CTest) honours it.
# ---------------------------------------------------------------------------
include(CTest)

if(BUILD_TESTING)
  add_test(
    NAME architecture_no_transport_in_interop_contract
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/check_no_transport_in_interop_contract.sh
  )
  set_tests_properties(architecture_no_transport_in_interop_contract PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  add_test(
    NAME architecture_component_boundaries
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/check_component_boundaries.sh ${CMAKE_SOURCE_DIR}
  )
  set_tests_properties(architecture_component_boundaries PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
endif()

add_subdirectory(lib)
add_subdirectory(services)

if(BUILD_MANUAL_TESTS)
  add_subdirectory(tests_manual)
endif()

# ---------------------------------------------------------------------------
# Install and CMake package export
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Install the rmc_fabric_warnings helper (in-tree only — do NOT export to consumers)

set(RMC_FABRIC_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/rmc_fabric")

install(
    EXPORT      rmc_fabricTargets
    NAMESPACE   rmc_fabric::
    DESTINATION "${RMC_FABRIC_INSTALL_CMAKEDIR}"
    COMPONENT   dev
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rmc_fabricConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/rmc_fabricConfig.cmake"
    INSTALL_DESTINATION "${RMC_FABRIC_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/rmc_fabricConfigVersion.cmake"
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/rmc_fabricConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/rmc_fabricConfigVersion.cmake"
    DESTINATION "${RMC_FABRIC_INSTALL_CMAKEDIR}"
    COMPONENT   dev
)

# ---------------------------------------------------------------------------
# Packaging — derive all version variables from the single project() declaration.
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME                rmc-fabric)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "RSC RMC fabric services, provides basic inventory and discovery services.")
set(CPACK_PACKAGE_CONTACT             "Vadim Vassiliev")
set(CPACK_PACKAGE_VENDOR              "RSC Group")
set(CPACK_PACKAGE_LICENSE             "GPL")
set(CPACK_PACKAGE_GROUP               "Management/Util")
set(CPACK_PACKAGE_RELEASE             1)
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION       ${PROJECT_VERSION})

set(CPACK_GENERATOR "DEB" "RPM" "TGZ")
set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
set(CPACK_OUTPUT_FILE_PREFIX packages)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_DEB_COMPONENT_INSTALL ON)

set(CPACK_COMPONENT_LIBRARIES_DISPLAY_NAME "Libraries")
set(CPACK_COMPONENT_HEADERS_DISPLAY_NAME "C++ Headers")

set(CPACK_COMPONENTS_GROUPING ONE_PER_GROUP)
set(CPACK_COMPONENT_DEVLIBRARIES_GROUP "dev")
set(CPACK_COMPONENT_HEADERS_GROUP      "dev")
set(CPACK_COMPONENT_RELLIBRARIES_GROUP "rel")

include(CPack)
