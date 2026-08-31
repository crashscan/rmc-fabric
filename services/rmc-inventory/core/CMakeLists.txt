add_library(inventory_core STATIC
    DefaultInventoryManager.cpp
    InventoryIssueUtil.cpp
    InventorySourceUtil.cpp
)

target_compile_features(inventory_core PUBLIC cxx_std_20)

target_include_directories(inventory_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(inventory_core
    PUBLIC
        interop_contract
        jsoncpp
)
