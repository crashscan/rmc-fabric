add_library(inventory_file_inputs STATIC
    FileBackedInventorySource.cpp
    DeviceMetaFileSource.cpp
)

target_compile_features(inventory_file_inputs PUBLIC cxx_std_20)

target_include_directories(inventory_file_inputs
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(inventory_file_inputs
    PUBLIC
        inventory_core
    PRIVATE
        jsoncpp
)

if(BUILD_TESTING)
    add_executable(inventory_file_input_tests
        tests/FileBackedInventorySourceTests.cpp
    )
    target_compile_features(inventory_file_input_tests PRIVATE cxx_std_20)
    target_link_libraries(inventory_file_input_tests
        PRIVATE
            inventory_file_inputs
    )
    add_test(NAME inventory_file_input_tests COMMAND inventory_file_input_tests)
    set_tests_properties(inventory_file_input_tests PROPERTIES
        TIMEOUT 15
        LABELS "unit"
    )
endif()
