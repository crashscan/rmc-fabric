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
)
