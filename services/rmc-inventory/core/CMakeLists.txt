add_library(inventory_core STATIC
    DefaultInventoryManager.cpp
    InventoryIssueUtil.cpp
    DeviceMetaFileSource.cpp
    FileBackedInventorySource.cpp
    InventorySourceUtil.cpp
)

target_include_directories(inventory_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(inventory_core
    PUBLIC
        interop_contract
        jsoncpp
)
