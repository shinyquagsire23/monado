# Copyright 2018-2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

cmake_minimum_required(VERSION 3.10.0)
project(XRT VERSION 0.1.0)

# CMake 3.11 introduced CMP0072 - Prefer GLVND
if(POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()

option(BUILD_TESTS "Build compile and runtime tests" ON)
option(OPENXR_USE_LOADER "Application uses loader" ON)


###
# Dependencies
###
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CMakeDependentOption)
include(SPIR-V)

find_package(Eigen3 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(OpenGL)
find_package(HIDAPI)

# Push into a FindOpenHMD.cmake file.
find_package(PkgConfig)
pkg_check_modules(OPENHMD openhmd)


if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11)
    find_package(Wayland)
    pkg_check_modules(XCB xcb xcb-randr)
endif()

cmake_dependent_option(BUILD_WITH_OPENHMD "Enable OpenHMD driver" ON "OPENHMD_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_WAYLAND "Enable Wayland support" ON "WAYLAND_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_XLIB "Enable xlib support" ON "X11_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_OPENGL "Enable OpenGL Graphics API support?" ON "OPENGL_FOUND" OFF)


###
# Flags
###

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")


###
# Decend into madness.
###

add_subdirectory(src)
add_subdirectory(doc)
