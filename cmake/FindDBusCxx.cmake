find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_DBUSCXX QUIET dbus-cxx)
endif()

if(PC_DBUSCXX_FOUND)
    set(DBusCxx_FOUND TRUE)
    set(DBusCxx_INCLUDE_DIRS ${PC_DBUSCXX_INCLUDE_DIRS})
    set(DBusCxx_LIBRARIES ${PC_DBUSCXX_LINK_LIBRARIES})
else()
    find_path(DBusCxx_INCLUDE_DIR
            NAMES dbus-cxx.h
            PATH_SUFFIXES
            dbus-cxx-2.0
            dbus-cxx-1.0
            dbus-cxx
    )

    find_library(DBusCxx_LIBRARY
            NAMES dbus-cxx dbus-cxx-2.0 dbus-cxx-2 dbuscxx
    )

    if(DBusCxx_INCLUDE_DIR AND DBusCxx_LIBRARY)
        set(DBusCxx_FOUND TRUE)
        set(DBusCxx_INCLUDE_DIRS ${DBusCxx_INCLUDE_DIR})
        set(DBusCxx_LIBRARIES ${DBusCxx_LIBRARY})
    else()
        set(DBusCxx_FOUND FALSE)
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DBusCxx
        REQUIRED_VARS DBusCxx_INCLUDE_DIRS DBusCxx_LIBRARIES
)

if(DBusCxx_FOUND AND NOT TARGET DBusCxx::DBusCxx)
    if(PC_DBUSCXX_FOUND)
        add_library(DBusCxx::DBusCxx INTERFACE IMPORTED)
        set_target_properties(DBusCxx::DBusCxx PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${DBusCxx_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${DBusCxx_LIBRARIES}"
        )
    else()
        add_library(DBusCxx::DBusCxx UNKNOWN IMPORTED)
        set_target_properties(DBusCxx::DBusCxx PROPERTIES
                IMPORTED_LOCATION "${DBusCxx_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${DBusCxx_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(
        DBusCxx_INCLUDE_DIR
        DBusCxx_LIBRARY
        DBusCxx_INCLUDE_DIRS
        DBusCxx_LIBRARIES
)