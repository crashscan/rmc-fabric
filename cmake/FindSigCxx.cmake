include(FindPackageHandleStandardArgs)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_SIGCXX QUIET sigc++-3.0)
endif()

if(PC_SIGCXX_FOUND)
    set(SigCxx_FOUND TRUE)
    set(SigCxx_INCLUDE_DIRS ${PC_SIGCXX_INCLUDE_DIRS})
    set(SigCxx_LIBRARIES ${PC_SIGCXX_LINK_LIBRARIES})
else()
    find_path(SigCxx_INCLUDE_DIR
            NAMES sigc++/sigc++.h
            PATH_SUFFIXES
            sigc++-3.0
            sigc++-2.0
            include
    )

    find_library(SigCxx_LIBRARY
            NAMES sigc-3.0 sigc-2.0 sigc++-3.0 sigc++-2.0
    )

    if(SigCxx_INCLUDE_DIR AND SigCxx_LIBRARY)
        set(SigCxx_FOUND TRUE)
        set(SigCxx_INCLUDE_DIRS ${SigCxx_INCLUDE_DIR})
        set(SigCxx_LIBRARIES ${SigCxx_LIBRARY})
    else()
        set(SigCxx_FOUND FALSE)
    endif()
endif()

find_package_handle_standard_args(SigCxx
        REQUIRED_VARS SigCxx_INCLUDE_DIRS SigCxx_LIBRARIES
)

if(SigCxx_FOUND AND NOT TARGET SigCxx::SigCxx)
    if(PC_SIGCXX_FOUND)
        add_library(SigCxx::SigCxx INTERFACE IMPORTED GLOBAL)
        set_target_properties(SigCxx::SigCxx PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${SigCxx_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${SigCxx_LIBRARIES}"
        )
    else()
        add_library(SigCxx::SigCxx UNKNOWN IMPORTED GLOBAL)
        set_target_properties(SigCxx::SigCxx PROPERTIES
                IMPORTED_LOCATION "${SigCxx_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${SigCxx_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(
        SigCxx_INCLUDE_DIR
        SigCxx_LIBRARY
        SigCxx_INCLUDE_DIRS
        SigCxx_LIBRARIES
)
