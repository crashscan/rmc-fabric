add_library(inventory_core STATIC
    src/DefaultInventoryManager.cpp
    src/InventoryFieldUtil.cpp
    src/InventoryIssueUtil.cpp
    src/DeviceMetaFileSource.cpp
    src/FileBackedInventorySource.cpp
    src/InventorySourceUtil.cpp
)

target_include_directories(inventory_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(inventory_core
    PUBLIC
        interop_contract
        jsoncpp
)
