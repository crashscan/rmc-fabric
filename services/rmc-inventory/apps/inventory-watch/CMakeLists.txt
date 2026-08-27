add_executable(inventory-watch main.cpp)

target_include_directories(inventory-watch PRIVATE
        ${INVENTORY_CORE_BASE}
        ${LIBS_CLIENT_BASE}/include
        ${DBusCxx_INCLUDE_DIRS}
        ${SigCxx_INCLUDE_DIRS}
)

target_link_libraries(inventory-watch
        PRIVATE inventory-client
        DBusCxx::DBusCxx SigCxx::SigCxx
        glog
        pthread
)