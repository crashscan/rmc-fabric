# inventory_io is kept as an alias so that existing link references continue
# to compile. The implementation has moved to lib/file_watcher.
add_library(inventory_io INTERFACE)
target_link_libraries(inventory_io INTERFACE file_watcher)
