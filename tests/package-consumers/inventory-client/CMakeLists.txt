cmake_minimum_required(VERSION 3.25)
project(rmc_fabric_inventory_client_consumer LANGUAGES CXX)

find_package(rmc_fabric CONFIG REQUIRED)

add_executable(inventory_client_consumer main.cpp)
target_compile_features(inventory_client_consumer PRIVATE cxx_std_20)
target_link_libraries(inventory_client_consumer PRIVATE rmc_fabric::inventory-client)
