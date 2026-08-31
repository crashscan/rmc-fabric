add_library(linux_support INTERFACE)

target_include_directories(linux_support
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(BUILD_TESTING)
    add_executable(linux_support_tests
        tests/ScopedFdTests.cpp
    )
    target_compile_features(linux_support_tests PRIVATE cxx_std_20)
    target_link_libraries(linux_support_tests PRIVATE linux_support)
    add_test(NAME linux_support_tests COMMAND linux_support_tests)
    set_tests_properties(linux_support_tests PROPERTIES TIMEOUT 10)
endif()
