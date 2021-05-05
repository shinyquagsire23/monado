# Copyright 2021 Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
# Original Author:
# 2021 Ryan Pavlik <ryan.pavlik@collabora.com>

#[[.rst:
FindPercetto
---------------

Find the Percetto C wrapper around the Perfetto tracing API.

Targets
^^^^^^^

If successful, the following imported targets are created.

* ``Percetto::percetto``

Cache variables
^^^^^^^^^^^^^^^

The following cache variable may also be set to assist/control the operation of this module:

``Percetto_ROOT_DIR``
 The root to search for Percetto.
#]]

set(Percetto_ROOT_DIR
    "${Percetto_ROOT_DIR}"
    CACHE PATH "Root to search for Percetto")

include(FeatureSummary)
set_package_properties(
    Percetto PROPERTIES
    URL "https://github.com/olvaffe/percetto/"
    DESCRIPTION "A C wrapper around the C++ Perfetto tracing SDK.")

# See if we can find something made by android prefab (gradle)
find_package(Percetto QUIET CONFIG NAMES percetto Percetto)
if(Percetto_FOUND)
    if(TARGET Percetto::percetto)
        # OK, good - unexpected, but good.
        get_target_property(Percetto_LIBRARY Percetto::percetto
                            IMPORTED_LOCATION)
        get_target_property(Percetto_INCLUDE_DIR Percetto::percetto
                            INTERFACE_INCLUDE_DIRECTORIES)
    elseif(TARGET percetto::percetto)
        # Let's make our own of the right name
        add_library(Percetto::percetto STATIC IMPORTED)
        get_target_property(Percetto_INCLUDE_DIR percetto::percetto
                            INTERFACE_INCLUDE_DIRECTORIES)
        get_target_property(Percetto_LIBRARY percetto::percetto
                            IMPORTED_LOCATION)
        set_target_properties(
            Percetto::percetto
            PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${Percetto_INCLUDE_DIR}"
                       IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                       IMPORTED_LOCATION ${Percetto_LIBRARY})
    else()
        message(FATAL_ERROR "assumptions failed")
    endif()
    find_package_handle_standard_args(
        Percetto REQUIRED_VARS Percetto_LIBRARY Percetto_INCLUDE_DIR)
    return()
endif()

if(NOT ANDROID)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        set(_old_prefix_path "${CMAKE_PREFIX_PATH}")
        # So pkg-config uses Percetto_ROOT_DIR too.
        if(Percetto_ROOT_DIR)
            list(APPEND CMAKE_PREFIX_PATH ${Percetto_ROOT_DIR})
        endif()
        pkg_check_modules(PC_percetto QUIET percetto)
        # Restore
        set(CMAKE_PREFIX_PATH "${_old_prefix_path}")
    endif()
endif()

find_path(
    Percetto_INCLUDE_DIR
    NAMES percetto.h
    PATHS ${Percetto_ROOT_DIR}
    HINTS ${PC_percetto_INCLUDE_DIRS}
    PATH_SUFFIXES include)

find_library(
    Percetto_LIBRARY
    NAMES percetto
    PATHS ${Percetto_ROOT_DIR}
    HINTS ${PC_percetto_LIBRARY_DIRS}
    PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Percetto REQUIRED_VARS Percetto_INCLUDE_DIR
                                                         Percetto_LIBRARY)
if(Percetto_FOUND)
    if(NOT TARGET Percetto::percetto)
        add_library(Percetto::percetto STATIC IMPORTED)

        set_target_properties(
            Percetto::percetto
            PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${Percetto_INCLUDE_DIR}"
                       IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                       IMPORTED_LOCATION ${Percetto_LIBRARY})
    endif()
    mark_as_advanced(Percetto_LIBRARY Percetto_INCLUDE_DIR)
endif()
mark_as_advanced(Percetto_ROOT_DIR)
