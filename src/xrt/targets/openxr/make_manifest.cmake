# Copyright 2019-2020, Collabora, Ltd.
# Copyright 2019, Benjamin Saunders <ben.e.saunders@gmail.com>
# SPDX-License-Identifier: BSL-1.0

# Get input from main CMake script
set(MANIFEST_INPUT @MANIFEST_INPUT@)
set(MANIFEST_RELATIVE_DIR @MANIFEST_RELATIVE_DIR@)
set(RUNTIME_RELATIVE_DIR @RUNTIME_RELATIVE_DIR@)
set(RUNTIME_FILENAME @RUNTIME_FILENAME@)
set(XRT_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH @XRT_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH@)

# Remove trailing slash
string(REGEX REPLACE "/$" "" MANIFEST_RELATIVE_DIR "${MANIFEST_RELATIVE_DIR}")

if(XRT_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH)
    # Absolute path to runtime
    set(RUNTIME_PATH ${RUNTIME_RELATIVE_DIR}/${RUNTIME_FILENAME})
    if(NOT IS_ABSOLUTE ${RUNTIME_RELATIVE_DIR})
      set(RUNTIME_PATH ${CMAKE_INSTALL_PREFIX}/${RUNTIME_PATH})
    endif()
else()
    # Relative path to runtime: requires it exist on the system shared library search path.
    set(RUNTIME_PATH ${RUNTIME_FILENAME})
endif()

set(runtime_path ${RUNTIME_PATH})

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)

    # Create manifest
    configure_file(${MANIFEST_INPUT} ${CMAKE_CURRENT_LIST_DIR}/@RUNTIME_TARGET@.json)

    # Install it
    file(INSTALL
        DESTINATION "${CMAKE_INSTALL_PREFIX}/${MANIFEST_RELATIVE_DIR}"
        TYPE FILE
        FILES "${CMAKE_CURRENT_LIST_DIR}/@RUNTIME_TARGET@.json")
endif()
