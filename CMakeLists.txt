cmake_minimum_required(VERSION 3.25.0 FATAL_ERROR)
project (rmc-fabric)

set (VERSION_MAJOR 0)
set (VERSION_MINOR 0)
set (VERSION_PATCH 1)
set (VERSION       ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})
message( "Building nmc-fabric version: " ${VERSION} )

set(RSC_CXX_STANDARD 20 CACHE STRING
        "C++ version used to build rmc-fabric (Currently: 20 and above are supported)")

set (CMAKE_CXX_STANDARD ${RSC_CXX_STANDARD})
set (CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS} -g -Wall -fPIC -pthread")
set (CMAKE_C_FLAGS   " ${CMAKE_C_FLAGS} -g -fPIC -pthread")

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

set(BUILD_TESTING ON)

add_subdirectory(services)
add_subdirectory(lib)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif(BUILD_TESTS)

if(BUILD_MANUAL_TESTS)
  add_subdirectory(tests_manual)
endif(BUILD_MANUAL_TESTS)

#
# finished with the build instructions.
# #####
# starting package build instructions.
#

set (CPACK_PACKAGE_NAME                rmc-fabric)
set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "RSC RMC fabric services, provides basic inventory and discovery services.")
set (CPACK_PACKAGE_CONTACT             "Vadim Vassiliev")
set (CPACK_PACKAGE_VENDOR              "RSC Group")
set (CPACK_PACKAGE_LICENSE             "GPL")
set (CPACK_PACKAGE_GROUP               "Management/Util")
set (CPACK_PACKAGE_RELEASE             1)
set (CPACK_PACKAGE_VERSION_MAJOR ${RMC_FABRIC_MAJOR_VERSION})
set (CPACK_PACKAGE_VERSION_MINOR ${RMC_FABRIC_MINOR_VERSION})
set (CPACK_PACKAGE_VERSION_PATCH ${RMC_FABRIC_PATCH_VERSION})
set (CPACK_PACKAGE_VERSION       ${RMC_FABRIC_VERSION})

set (CPACK_GENERATOR "DEB" "RPM" "TGZ")
set (CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
set (CPACK_OUTPUT_FILE_PREFIX packages)
set (CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set (CPACK_RPM_COMPONENT_INSTALL ON)
set (CPACK_DEB_COMPONENT_INSTALL ON)

set (CPACK_COMPONENT_LIBRARIES_DISPLAY_NAME "Libraries")
set (CPACK_COMPONENT_HEADERS_DISPLAY_NAME "C++ Headers")

set (CPACK_COMPONENTS_GROUPING ONE_PER_GROUP)
set (CPACK_COMPONENT_DEVLIBRARIES_GROUP "dev")
set (CPACK_COMPONENT_HEADERS_GROUP      "dev")
set (CPACK_COMPONENT_RELLIBRARIES_GROUP "rel")

include(CPack)


# End of CMakeLists.Txt file. Save it as a plain text file and the name should be exactly CMakeLists.Txt
