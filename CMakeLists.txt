# Copyright 2018-2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

cmake_minimum_required(VERSION 3.10.0)
project(XRT VERSION 0.1.0)

# CMake 3.11 introduced CMP0072 - Prefer GLVND
if(POLICY CMP0072)
	cmake_policy(SET CMP0072 NEW)
endif()

option(OPENXR_USE_LOADER "Application uses loader" ON)
option(OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH "Use the absolute path to the runtime in the installed manifest, rather than a bare filename." ON)
option(VULKAN_ENABLE_VALIDATION "Enable Vulkan validation for Compositor" ON)
option(INSTALL_ACTIVE_RUNTIME "Make Monado the default OpenXR runtime on install" ON)

###
# Dependencies
###
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CMakeDependentOption)
include(SPIR-V)

find_package(Eigen3 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(EGL)
find_package(HIDAPI)
find_package(OpenHMD)
find_package(OpenCV COMPONENTS core calib3d highgui imgproc imgcodecs features2d video)
find_package(Libusb1)
find_package(JPEG)
find_package(SDL2)
find_package(Threads)
find_package(ZLIB)

# @TODO Turn into a find_package LIBUVC file.
pkg_check_modules(LIBUVC libuvc)

# @TODO Turn into a find_package FFMPEG file.
pkg_check_modules(FFMPEG libavcodec)


if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# Compositor backend
	find_package(X11)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(XCB xcb xcb-randr)

	find_package(udev REQUIRED)
	set(BUILD_WITH_LIBUDEV TRUE)
	set(BUILD_DRIVER_V4L2 TRUE)

	pkg_search_module(WAYLAND wayland-client)
	pkg_search_module(WAYLAND_SCANNER wayland-scanner)
	pkg_search_module(WAYLAND_PROTOCOLS wayland-protocols)

	find_package(OpenGL COMPONENTS GLX)
else()
	find_package(OpenGL)
endif()

cmake_dependent_option(BUILD_WITH_WAYLAND "Enable Wayland support" ON "WAYLAND_FOUND AND WAYLAND_SCANNER_FOUND AND WAYLAND_PROTOCOLS_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_XLIB "Enable xlib support" ON "X11_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_XCB "Enable xcb support" ON "XCB_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_OPENGL "Enable OpenGL Graphics API support" ON "OPENGL_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_EGL "Enable OpenGL on EGL Graphics API support" ON "BUILD_WITH_OPENGL AND EGL_FOUND" OFF)

set(BUILD_WITH_LIBUSB TRUE)
cmake_dependent_option(BUILD_WITH_JPEG "Enable jpeg code (used for some video drivers)" ON "JPEG_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_OPENCV "Enable OpenCV backend" ON "OpenCV_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_LIBUVC "Enable libuvc video driver" ON "LIBUVC_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_FFMPEG "Enable ffmpeg testing video driver" ON "FFMPEG_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_PSVR "Enable PSVR HMD driver" ON "HIDAPI_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_VIVE "Enable Vive driver" ON "ZLIB_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_OPENHMD "Enable OpenHMD driver" ON "OPENHMD_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_SDL2 "Enable SDL2 based test application" ON "SDL2_FOUND" OFF)

# These all use the Monado internal hid wrapper which is assumed to be available.
option(BUILD_WITH_HDK "Enable HDK driver" ON)
option(BUILD_WITH_PSMV "Enable Playstation Move driver" ON)
option(BUILD_WITH_HYDRA "Enable Hydra driver" ON)

###
# Flags
###

if(BUILD_WITH_LIBUDEV)
	add_definitions(-DXRT_HAVE_LIBUDEV)
endif()

if(BUILD_DRIVER_V4L2)
	add_definitions(-DXRT_HAVE_V4L2)
endif()

if(BUILD_WITH_LIBUSB)
	add_definitions(-DXRT_HAVE_LIBUSB)
endif()

if(BUILD_WITH_OPENCV)
	add_definitions(-DXRT_HAVE_OPENCV)

	# Tracking requires OpenCV
	set(BUILD_TRACKING TRUE)
endif()

if(BUILD_WITH_JPEG)
	add_definitions(-DXRT_HAVE_JPEG)
endif()

if(BUILD_WITH_LIBUVC)
	add_definitions(-DXRT_HAVE_LIBUVC)
endif()

if(BUILD_WITH_FFMPEG)
	add_definitions(-DXRT_HAVE_FFMPEG)
endif()

if(BUILD_WITH_SDL2)
	add_definitions(-DXRT_HAVE_SDL2)

	# Arch work around
	if(NOT DEFINED SDL2_LIBRARIES)
		set(SDL2_LIBRARIES SDL2::SDL2)
	endif()

	# SDL2 based gui
	set(BUILD_TARGET_GUI TRUE)
endif()

if(BUILD_WITH_OPENHMD)
	add_definitions(-DXRT_HAVE_OPENHMD)

	# Drivers enabled with OpenHMD.
	set(BUILD_DRIVER_OHMD TRUE)
endif()

if(BUILD_WITH_PSVR)
	if (NOT ${HIDAPI_FOUND})
		message(FATAL_ERROR "PSVR driver requires hidapi")
	endif()

	set(BUILD_DRIVER_PSVR TRUE)
endif()

if(BUILD_WITH_VIVE)
	set(BUILD_DRIVER_VIVE TRUE)
endif()

if(BUILD_WITH_HDK)
	set(BUILD_DRIVER_HDK TRUE)
endif()

if(BUILD_WITH_PSMV)
	set(BUILD_DRIVER_PSMV TRUE)
endif()

if(BUILD_WITH_HYDRA)
	set(BUILD_DRIVER_HYDRA TRUE)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")

if(NOT MSVC)
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--no-undefined")
endif()

# Default to PIC code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)


###
# Decend into madness.
###

add_subdirectory(src)
add_subdirectory(doc)
