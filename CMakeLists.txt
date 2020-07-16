# Copyright 2018-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

cmake_minimum_required(VERSION 3.13.0)
project(XRT VERSION 0.3.0)

# CMake 3.11 introduced CMP0072 - Prefer GLVND
if(POLICY CMP0072)
	cmake_policy(SET CMP0072 NEW)
endif()

option(XRT_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH "Use the absolute path to the runtime in the installed manifest, rather than a bare filename." ON)
option(XRT_OPENXR_INSTALL_ACTIVE_RUNTIME "Make Monado the default OpenXR runtime on install" ON)

###
# Dependencies
###
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CMakeDependentOption)
include(SPIR-V)
include(GNUInstallDirs)
if(NOT ${CMAKE_VERSION} VERSION_LESS 3.9)
	include(CheckIPOSupported)
	check_ipo_supported(RESULT HAS_IPO)
endif()

find_package(Eigen3 REQUIRED)
find_package(Vulkan)
find_package(EGL)
find_package(HIDAPI)
find_package(OpenHMD)
find_package(OpenCV COMPONENTS core calib3d highgui imgproc imgcodecs features2d video CONFIG)
find_package(Libusb1)
find_package(JPEG)
find_package(realsense2 CONFIG)
find_package(SDL2 CONFIG)
find_package(Threads)
find_package(ZLIB)
find_package(cJSON)
find_package(Systemd)

# @TODO Turn into a find_package LIBUVC file.
pkg_check_modules(LIBUVC libuvc)

# @TODO Turn into a find_package FFMPEG file.
pkg_check_modules(FFMPEG libavcodec)


if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# Compositor backend
	find_package(X11)
	find_package(PkgConfig)

	find_package(udev REQUIRED)
	set(XRT_HAVE_V4L2 TRUE)
	if(PKGCONFIG_FOUND)
		pkg_check_modules(XCB xcb xcb-randr x11-xcb)

		pkg_search_module(WAYLAND wayland-client)
		pkg_search_module(WAYLAND_SCANNER wayland-scanner)
		pkg_search_module(WAYLAND_PROTOCOLS wayland-protocols)
	endif()
	find_package(OpenGL COMPONENTS GLX)
	pkg_search_module(DBUS dbus-1)

	pkg_check_modules(SURVIVE IMPORTED_TARGET survive)
else()
	find_package(OpenGL)
endif()

find_library(RT_LIBRARY rt)
if(ANDROID)
	find_library(ANDROID_LIBRARY android)
endif()

# This one is named differently because that's what CTest uses
option(BUILD_TESTING "Enable building of the test suite?" ON)

cmake_dependent_option(CMAKE_INTERPROCEDURAL_OPTIMIZATION "Enable inter-procedural (link-time) optimization" OFF "HAS_IPO" OFF)
cmake_dependent_option(XRT_HAVE_WAYLAND "Enable Wayland support" ON "WAYLAND_FOUND AND WAYLAND_SCANNER_FOUND AND WAYLAND_PROTOCOLS_FOUND" OFF)
cmake_dependent_option(XRT_HAVE_XLIB "Enable xlib support" ON "X11_FOUND" OFF)
cmake_dependent_option(XRT_HAVE_XCB "Enable xcb support" ON "XCB_FOUND" OFF)
cmake_dependent_option(XRT_HAVE_OPENGL "Enable OpenGL Graphics API support" ON "OPENGL_FOUND" OFF)
cmake_dependent_option(XRT_HAVE_VULKAN "Enable Vulkan Graphics API support (also needed for compositor)" ON "VULKAN_FOUND" OFF)
cmake_dependent_option(XRT_HAVE_EGL "Enable OpenGL on EGL Graphics API support" ON "XRT_HAVE_OPENGL AND EGL_FOUND" OFF)
cmake_dependent_option(XRT_HAVE_DBUS "Enable dbus support (for BLE support)" ON "DBUS_FOUND" OFF)
cmake_dependent_option(XRT_FEATURE_COMPOSITOR_MAIN "Build main compositor host functionality" ON "XRT_HAVE_VULKAN; XRT_HAVE_WAYLAND OR XRT_HAVE_XCB" OFF)
cmake_dependent_option(XRT_FEATURE_OPENXR "Build OpenXR runtime target" ON "XRT_FEATURE_COMPOSITOR_MAIN" OFF)
cmake_dependent_option(XRT_FEATURE_SERVICE "Enable separate service module for OpenXR runtime" ON "XRT_FEATURE_COMPOSITOR_MAIN AND XRT_FEATURE_OPENXR" OFF)
cmake_dependent_option(XRT_HAVE_SYSTEMD "Enable systemd support (for socket activation of service)" ON "Systemd_FOUND AND XRT_FEATURE_SERVICE" OFF)
cmake_dependent_option(XRT_INSTALL_SYSTEMD_UNIT_FILES "Install user unit files for systemd socket activation on installation" ON "XRT_HAVE_SYSTEMD" OFF)
cmake_dependent_option(XRT_INSTALL_ABSOLUTE_SYSTEMD_UNIT_FILES "Use an absolute path to monado-system in installed user unit files for systemd socket activation" ON "XRT_INSTALL_SYSTEMD_UNIT_FILES" OFF)

# Most users won't touch these.
mark_as_advanced(XRT_FEATURE_COMPOSITOR_MAIN XRT_FEATURE_OPENXR)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(XRT_HAVE_LIBUDEV ON)
	set(XRT_HAVE_INTERNAL_HID ON)
else()
	cmake_dependent_option(XRT_HAVE_LIBUDEV "Enable libudev (used for device probing on Linux)" ON "UDEV_FOUND" OFF)
endif()
cmake_dependent_option(XRT_HAVE_LIBUSB "Enable libusb (used for most drivers)" ON "LIBUSB1_FOUND" OFF)
cmake_dependent_option(XRT_HAVE_JPEG "Enable jpeg code (used for some video drivers)" ON "JPEG_FOUND" OFF)
cmake_dependent_option(XRT_HAVE_OPENCV "Enable OpenCV backend" ON "OpenCV_FOUND" OFF)
cmake_dependent_option(XRT_HAVE_LIBUVC "Enable libuvc video driver" ON "LIBUVC_FOUND AND XRT_HAVE_LIBUSB" OFF)
cmake_dependent_option(XRT_HAVE_FFMPEG "Enable ffmpeg testing video driver" ON "FFMPEG_FOUND" OFF)
cmake_dependent_option(XRT_HAVE_SDL2 "Enable use of SDL2" ON "SDL2_FOUND AND XRT_HAVE_OPENGL" OFF)
cmake_dependent_option(XRT_HAVE_SYSTEM_CJSON "Enable cJSON from system, instead of bundled source" ON "CJSON_FOUND" OFF)


cmake_dependent_option(XRT_BUILD_DRIVER_PSVR "Enable PSVR HMD driver" ON "HIDAPI_FOUND" OFF)
cmake_dependent_option(XRT_BUILD_DRIVER_RS "Enable RealSense device driver" ON "realsense2_FOUND" OFF)
cmake_dependent_option(XRT_BUILD_DRIVER_VIVE "Enable driver for HTC Vive, Vive Pro, Valve Index, and their controllers" ON "ZLIB_FOUND" OFF)
cmake_dependent_option(XRT_BUILD_DRIVER_OHMD "Enable OpenHMD driver" ON "OPENHMD_FOUND" OFF)
cmake_dependent_option(XRT_BUILD_DRIVER_DAYDREAM "Enable the Google Daydream View controller driver (BLE, via D-Bus)" ON "XRT_HAVE_DBUS" OFF)
cmake_dependent_option(XRT_BUILD_DRIVER_ARDUINO "Enable Arduino input device with BLE via via D-Bus" ON "XRT_HAVE_DBUS" OFF)


option(XRT_BUILD_DRIVER_DUMMY "Enable dummy driver" ON)

# These all use the Monado internal hid wrapper.
cmake_dependent_option(XRT_BUILD_DRIVER_HDK "Enable HDK driver" ON "XRT_HAVE_INTERNAL_HID" OFF)
cmake_dependent_option(XRT_BUILD_DRIVER_PSMV "Enable Playstation Move driver" ON "XRT_HAVE_INTERNAL_HID" OFF)
cmake_dependent_option(XRT_BUILD_DRIVER_HYDRA "Enable Hydra driver" ON "XRT_HAVE_INTERNAL_HID" OFF)
cmake_dependent_option(XRT_BUILD_DRIVER_NS "Enable North Star driver" ON "XRT_HAVE_INTERNAL_HID" OFF)

# This one defaults to off, even if we find the deps.
cmake_dependent_option(XRT_BUILD_DRIVER_SURVIVE "Enable libsurvive driver" OFF "SURVIVE_FOUND" OFF)

# You can set this from a superproject to add a driver
# All drivers must be listed in here to be included in the generated header!
list(APPEND AVAILABLE_DRIVERS ARDUINO DUMMY HDK HYDRA NS OHMD PSMV PSVR RS V4L2 VIVE DAYDREAM SURVIVE)

###
# Flags
###

if(XRT_HAVE_SDL2)
	# Arch work around
	if(NOT DEFINED SDL2_LIBRARIES)
		set(SDL2_LIBRARIES SDL2::SDL2)
	endif()
endif()

# Vulkan flags for the shared Vulkan code.
if(XRT_HAVE_XCB)
	set(VK_USE_PLATFORM_XCB_KHR TRUE)
endif()
if(XRT_HAVE_XCB AND XRT_HAVE_XLIB)
	set(VK_USE_PLATFORM_XLIB_XRANDR_EXT TRUE)
endif()
if(XRT_HAVE_WAYLAND)
	set(VK_USE_PLATFORM_WAYLAND_KHR TRUE)
endif()


if(NOT MSVC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Wno-unused-parameter")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -Wextra -Wno-unused-parameter")

	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
	set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--no-undefined")
endif()

# Default to PIC code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Describe IPO setting
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
	message(STATUS "Inter-procedural optimization enabled")
endif()

###
# Decend into madness.
###

add_subdirectory(src)
add_subdirectory(doc)

if(BUILD_TESTING)
    include(CTest)
    add_subdirectory(tests)
endif()
