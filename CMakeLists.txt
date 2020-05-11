# Copyright 2018-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

cmake_minimum_required(VERSION 3.13.0)
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
include(GNUInstallDirs)

find_package(Eigen3 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(EGL)
find_package(HIDAPI)
find_package(OpenHMD)
find_package(OpenCV COMPONENTS core calib3d highgui imgproc imgcodecs features2d video)
find_package(Libusb1)
find_package(JPEG)
find_package(realsense2)
find_package(SDL2)
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
	set(BUILD_DRIVER_V4L2 TRUE)
	if(PKGCONFIG_FOUND)
		pkg_check_modules(XCB xcb xcb-randr x11-xcb)

		pkg_search_module(WAYLAND wayland-client)
		pkg_search_module(WAYLAND_SCANNER wayland-scanner)
		pkg_search_module(WAYLAND_PROTOCOLS wayland-protocols)
	endif()
	find_package(OpenGL COMPONENTS GLX)
	pkg_search_module(DBUS dbus-1)

else()
	find_package(OpenGL)
endif()

cmake_dependent_option(BUILD_WITH_WAYLAND "Enable Wayland support" ON "WAYLAND_FOUND AND WAYLAND_SCANNER_FOUND AND WAYLAND_PROTOCOLS_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_XLIB "Enable xlib support" ON "X11_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_XCB "Enable xcb support" ON "XCB_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_OPENGL "Enable OpenGL Graphics API support" ON "OPENGL_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_EGL "Enable OpenGL on EGL Graphics API support" ON "BUILD_WITH_OPENGL AND EGL_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_DBUS "Enable dbus support (for BLE support)" ON "DBUS_FOUND" OFF)
cmake_dependent_option(BUILD_COMPOSITOR_MAIN "Build main compositor host" ON "BUILD_WITH_WAYLAND OR BUILD_WITH_XCB" OFF)
cmake_dependent_option(XRT_BUILD_IPC "Build OpenXR runtime target" ON "BUILD_COMPOSITOR_MAIN" OFF)
cmake_dependent_option(XRT_HAVE_SYSTEMD "Enable systemd support (for socket activation of service)" ON "Systemd_FOUND AND XRT_BUILD_IPC" OFF)
cmake_dependent_option(XRT_INSTALL_SYSTEMD_UNIT_FILES "Install user unit files for systemd socket activation on installation" ON "XRT_HAVE_SYSTEMD" OFF)
cmake_dependent_option(XRT_INSTALL_ABSOLUTE_SYSTEMD_UNIT_FILES "Use an absolute path to monado-system in installed user unit files for systemd socket activation" ON "XRT_INSTALL_SYSTEMD_UNIT_FILES" OFF)
cmake_dependent_option(BUILD_TARGET_OPENXR "Build OpenXR runtime target" ON "BUILD_COMPOSITOR_MAIN OR XRT_BUILD_IPC" OFF)

# Most users won't touch these.
mark_as_advanced(BUILD_COMPOSITOR_MAIN BUILD_TARGET_OPENXR)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(BUILD_WITH_LIBUDEV ON)
else()
	cmake_dependent_option(BUILD_WITH_LIBUDEV "Enable libudev (used for device probing on Linux)" ON "UDEV_FOUND" OFF)
endif()
cmake_dependent_option(BUILD_WITH_LIBUSB "Enable libusb (used for most drivers)" ON "LIBUSB1_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_JPEG "Enable jpeg code (used for some video drivers)" ON "JPEG_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_OPENCV "Enable OpenCV backend" ON "OpenCV_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_LIBUVC "Enable libuvc video driver" ON "LIBUVC_FOUND AND BUILD_WITH_LIBUSB" OFF)
cmake_dependent_option(BUILD_WITH_FFMPEG "Enable ffmpeg testing video driver" ON "FFMPEG_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_PSVR "Enable PSVR HMD driver" ON "HIDAPI_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_RS "Enable RealSense device driver" ON "realsense2_FOUND" OFF)
option(BUILD_WITH_DUMMY "Enable dummy driver" ON)
cmake_dependent_option(BUILD_WITH_VIVE "Enable Vive driver" ON "ZLIB_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_OPENHMD "Enable OpenHMD driver" ON "OPENHMD_FOUND" OFF)
cmake_dependent_option(BUILD_WITH_SDL2 "Enable SDL2 based test application" ON "SDL2_FOUND AND BUILD_WITH_OPENGL" OFF)
cmake_dependent_option(BUILD_WITH_DAYDREAM "Enable Bluetooth LE via DBUS" ON "BUILD_WITH_DBUS" OFF)
cmake_dependent_option(BUILD_WITH_ARDUINO "Enable Arduino input device with BLE via DBUS" ON "BUILD_WITH_DBUS" OFF)

cmake_dependent_option(BUILD_WITH_SYSTEM_CJSON "Enable cJSON from system, instead of bundled source" ON "CJSON_FOUND" OFF)

# These all use the Monado internal hid wrapper which is assumed to be available.
option(BUILD_WITH_HDK "Enable HDK driver" ON)
option(BUILD_WITH_PSMV "Enable Playstation Move driver" ON)
option(BUILD_WITH_HYDRA "Enable Hydra driver" ON)
option(BUILD_WITH_NS "Enable North Star driver" ON)
option(BUILD_WITH_LIBSURVIVE "Enable libsurvive driver" OFF)

# You can set this from a superproject to add a driver
list(APPEND AVAILABLE_DRIVERS ARDUINO DUMMY HDK HYDRA NS OHMD PSMV PSVR RS V4L2 VIVE DAYDREAM)

###
# Flags
###

if(BUILD_WITH_LIBUDEV)
	set(XRT_HAVE_LIBUDEV TRUE)
endif()

if(BUILD_WITH_DBUS)
	set(XRT_HAVE_DBUS TRUE)
endif()

if(BUILD_DRIVER_V4L2)
	set(XRT_HAVE_V4L2 TRUE)
endif()

if(BUILD_WITH_LIBUSB)
	set(XRT_HAVE_LIBUSB TRUE)
endif()

if(BUILD_WITH_OPENCV)
	set(XRT_HAVE_OPENCV TRUE)

	# Tracking requires OpenCV
	set(BUILD_TRACKING TRUE)
endif()

if(BUILD_WITH_JPEG)
	set(XRT_HAVE_JPEG TRUE)
endif()

if(BUILD_WITH_LIBUVC)
	set(XRT_HAVE_LIBUVC TRUE)
endif()

if(BUILD_WITH_FFMPEG)
	set(XRT_HAVE_FFMPEG TRUE)
endif()

if(BUILD_WITH_SDL2)
	set(XRT_HAVE_SDL2 TRUE)

	# Arch work around
	if(NOT DEFINED SDL2_LIBRARIES)
		set(SDL2_LIBRARIES SDL2::SDL2)
	endif()

	# SDL2 based gui
	set(BUILD_TARGET_GUI TRUE)
endif()


if(BUILD_WITH_ARDUINO)
	set(BUILD_DRIVER_ARDUINO TRUE)
endif()

if(BUILD_WITH_DUMMY)
	set(BUILD_DRIVER_DUMMY TRUE)
endif()

if(BUILD_WITH_OPENHMD)
	set(XRT_HAVE_OPENHMD TRUE)

	# Drivers enabled with OpenHMD.
	set(BUILD_DRIVER_OHMD TRUE)
endif()

if(BUILD_WITH_NS)
	set(BUILD_DRIVER_NS TRUE)
endif()

if (BUILD_WITH_LIBSURVIVE)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(SURVIVE REQUIRED IMPORTED_TARGET survive)
	add_definitions(-DXRT_BUILD_DRIVER_SURVIVE)
	set(BUILD_DRIVER_SURVIVE TRUE)
endif()

if(BUILD_WITH_PSVR)
	if (NOT ${HIDAPI_FOUND})
		message(FATAL_ERROR "PSVR driver requires hidapi")
	endif()

	set(BUILD_DRIVER_PSVR TRUE)
endif()

if(BUILD_WITH_RS)
	if (NOT ${realsense2_FOUND})
		message(FATAL_ERROR "RealSense driver requires librealsense2")
	endif()

	set(BUILD_DRIVER_RS TRUE)
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

if(BUILD_WITH_DAYDREAM)
	set(BUILD_DRIVER_DAYDREAM TRUE)
endif()


# Vulkan flags for the shared Vulkan code.
if(BUILD_WITH_XCB)
	set(VK_USE_PLATFORM_XCB_KHR TRUE)
endif()
if(BUILD_WITH_XCB AND BUILD_WITH_XLIB)
	set(VK_USE_PLATFORM_XLIB_XRANDR_EXT TRUE)
endif()
if(BUILD_WITH_WAYLAND)
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


###
# Decend into madness.
###

add_subdirectory(src)
add_subdirectory(doc)
