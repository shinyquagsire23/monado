# Copyright 2018-2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

cmake_minimum_required(VERSION 3.10.0)
project(XRT VERSION 0.1.0)

# CMake 3.11 introduced CMP0072 - Prefer GLVND
if(POLICY CMP0072)
	cmake_policy(SET CMP0072 NEW)
endif()

option(OPENXR_USE_LOADER "Application uses loader" ON)
option(VULKAN_ENABLE_VALIDATION "Enable Vulkan validation for Compositor" ON)

###
# Dependencies
###
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CMakeDependentOption)
include(SPIR-V)

find_package(Eigen3 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS GLX)
find_package(HIDAPI)
find_package(OpenHMD)
find_package(OpenCV)

# @TODO Turn into a find_package LIBUSB-1.0 file.
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# @TODO Turn into a find_package LIBUVC file.
pkg_check_modules(LIBUVC libuvc)

# @TODO Turn into a find_package FFMPEG file.
pkg_check_modules(FFMPEG libavcodec)


if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# Compositor backend
	find_package(X11)
	find_package(Wayland)
	pkg_check_modules(XCB xcb xcb-randr)

	find_package(udev REQUIRED)
	set(BUILD_WITH_LIBUDEV TRUE)
endif()

cmake_dependent_option(BUILD_WITH_WAYLAND "Enable Wayland support" ON "WAYLAND_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_XLIB "Enable xlib support" ON "X11_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_OPENGL "Enable OpenGL Graphics API support" ON "OPENGL_FOUND" OFF)

set(BUILD_WITH_LIBUSB TRUE)
cmake_dependent_option(BUILD_WITH_OPENCV "Enable OpenCV backend" ON "OpenCV_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_LIBUVC "Enable libuvc video driver" ON "LIBUVC_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_FFMPEG "Enable ffmpeg testing video driver" ON "FFMPEG_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_HIDAPI "Enable HIDAPI-based driver(s) (OSVR HDK)" ON "HIDAPI_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_OPENHMD "Enable OpenHMD driver" ON "OPENHMD_FOUND" OFF)


###
# Error checking
###

if(NOT BUILD_WITH_OPENHMD AND NOT BUILD_WITH_HIDAPI)
	message(FATAL_ERROR "You must enable at least one driver: either provide OpenHMD and enable BUILD_WITH_OPENHMD (for a variety of devices via OpenHMD), or provide HIDAPI and enable BUILD_WITH_HIDAPI (for the HDK driver)")
endif()


###
# Flags
###

if(BUILD_WITH_LIBUDEV)
	add_definitions(-DXRT_HAVE_LIBUDEV)
endif()

if(BUILD_WITH_LIBUSB)
	add_definitions(-DXRT_HAVE_LIBUSB)
endif()

if(BUILD_WITH_OPENCV)
	add_definitions(-DXRT_HAVE_OPENCV)
endif()

if(BUILD_WITH_LIBUVC)
	add_definitions(-DXRT_HAVE_LIBUVC)
endif()

if(BUILD_WITH_FFMPEG)
	add_definitions(-DXRT_HAVE_FFMPEG)
endif()

if(BUILD_WITH_OPENHMD)
	add_definitions(-DXRT_HAVE_OPENHMD)

	# Drivers enabled with OpenHMD.
	add_definitions(-DXRT_BUILD_OHMD)
	set(BUILD_DRIVER_OHMD TRUE)
endif()

if(BUILD_WITH_HIDAPI)
	add_definitions(-DXRT_HAVE_HIDAPI)

	# Drivers enabled with hidapi.
	add_definitions(-DXRT_BUILD_HDK)
	set(BUILD_DRIVER_HDK TRUE)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")


###
# Decend into madness.
###

add_subdirectory(src)
add_subdirectory(doc)
