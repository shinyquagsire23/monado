# Copyright 2018-2023, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

cmake_minimum_required(VERSION 3.10.2)
project(
	XRT
	VERSION 21.0.0
	LANGUAGES C CXX
	)

# CMake 3.11 introduced CMP0072 - Prefer GLVND
if(POLICY CMP0072)
	cmake_policy(SET CMP0072 NEW)
endif()

set(_default_absolute ON)
if(WIN32)
	set(_default_absolute OFF)
endif()
option(
	XRT_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH
	"Use the absolute path to the runtime in the installed manifest, rather than a bare filename."
	OFF
	)
option(
	XRT_OPENXR_INSTALL_MANIFEST_RELATIVE_RUNTIME_PATH
	"If XRT_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH is off, use a relative path from the manifest to the runtime."
	ON
	)
if(NOT WIN32 AND NOT ANDROID)
	option(
		XRT_OPENXR_INSTALL_ACTIVE_RUNTIME
		"Make Monado the system-wide default OpenXR runtime on install (requires superuser permissions)"
		OFF
		)
endif()

# We use C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# We use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# So that clangd/Intellisense/Sourcetrail know how to parse our code.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

###
# Dependencies
###
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sanitizers")
include(OptionWithDeps)
include(SPIR-V)
include(GNUInstallDirs)
if(NOT GIT_DESC)
	include(GetGitRevisionDescription)
	git_describe(GIT_DESC "--always")
endif()
if(NOT ${CMAKE_VERSION} VERSION_LESS 3.9)
	include(CheckIPOSupported)
	check_ipo_supported(RESULT HAS_IPO)
endif()

# Android SDK doesn't look for 3.8 and 3.9, which is what new distros ship with.
set(Python_ADDITIONAL_VERSIONS 3.8 3.9)
if(NOT CMAKE_VERSION VERSION_LESS 3.12)
	find_package(Python3 REQUIRED Interpreter)
	set(PYTHON_EXECUTABLE Python3::Interpreter)
else()
	find_program(PYTHON_EXECUTABLE python3)
	if(PYTHON_EXECUTABLE MATCHES "WindowsApps")
		# If you receive this error, you will have to install Python 3 or try harder to tell CMake where it is.
		message(
			FATAL_ERROR
				"Found WindowsApps alias for Python. Make sure Python3 is installed, then choose 'Manage App Execution Aliases' in Start and disable the aliases for Python."
			)
	endif()
endif()

# Redundant mention of version is required because module defaults to looking for 2.91-compatible,
# which the config file for a 3.x says it's not compatible with.
find_package(Eigen3 3 REQUIRED)
find_package(Vulkan MODULE)
find_package(HIDAPI MODULE)
find_package(bluetooth MODULE)
find_package(OpenHMD MODULE)
find_package(
	OpenCV
	COMPONENTS
		core
		calib3d
		highgui
		imgproc
		imgcodecs
		features2d
		video
		CONFIG
	)
find_package(Libusb1 MODULE)
find_package(JPEG MODULE)
find_package(realsense2 CONFIG)
find_package(depthai CONFIG)
find_package(SDL2 CONFIG)
find_package(ZLIB MODULE)
find_package(cJSON MODULE)
find_package(LeapV2 MODULE)
find_package(ONNXRuntime MODULE)
if(NOT WIN32)
	find_package(EGL MODULE)
	find_package(Percetto MODULE)
	find_package(Systemd MODULE)
	find_package(OpenGLES MODULE COMPONENTS V3)
	find_library(RT_LIBRARY rt)
endif()
if(NOT ANDROID AND NOT WIN32)
	find_package(PkgConfig MODULE)
endif()
if(ANDROID)
	find_library(ANDROID_LIBRARY android)
	find_library(ANDROID_LOG_LIBRARY log)
endif()
if(WIN32)
	find_package(wil CONFIG)
	find_library(D3D11_LIBRARY d3d11)
	find_library(D3D12_LIBRARY d3d12)
	find_library(DXGI_LIBRARY dxgi)
	find_library(WINDOWSAPP_LIBRARY WindowsApp)
endif()

#https://github.com/arsenm/sanitizers-cmake
find_package(Sanitizers MODULE)

add_library(xrt-pthreads INTERFACE)
if(WIN32)
	find_package(pthreads_windows REQUIRED)
	target_link_libraries(xrt-pthreads INTERFACE PThreads4W::PThreads4W_CXXEXC)
else()
	set(CMAKE_THREAD_PREFER_PTHREAD ON)
	find_package(Threads)
	target_link_libraries(xrt-pthreads INTERFACE Threads::Threads)
	target_compile_definitions(xrt-pthreads INTERFACE _GNU_SOURCE)
endif()

if(PKGCONFIG_FOUND AND NOT ANDROID)
	# @TODO Turn into a find_package LIBUVC file.
	pkg_check_modules(LIBUVC libuvc)

	# @TODO Turn into a find_package FFMPEG file.
	pkg_check_modules(FFMPEG libavcodec)
endif()

find_package(OpenGL)
set(OPENGL_WITHOUT_GLX_FOUND ${OPENGL_FOUND})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(XRT_HAVE_LINUX YES)
	# Compositor backend
	find_package(X11)

	find_package(udev REQUIRED)
	set(XRT_HAVE_V4L2 TRUE)
	if(PKGCONFIG_FOUND)
		pkg_check_modules(XRANDR xrandr)

		pkg_check_modules(XCB xcb xcb-randr x11-xcb)

		pkg_search_module(WAYLAND wayland-client)
		pkg_search_module(WAYLAND_SCANNER wayland-scanner)
		pkg_search_module(WAYLAND_PROTOCOLS wayland-protocols)
		pkg_search_module(LIBDRM IMPORTED_TARGET libdrm)
	endif()
	find_package(OpenGL COMPONENTS GLX)
	pkg_search_module(DBUS dbus-1)
	pkg_search_module(LIBBSD libbsd)

	pkg_check_modules(GST gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)

	pkg_check_modules(SURVIVE IMPORTED_TARGET survive)
endif()

# Find a external SLAM implementation
set(EXTERNAL_SLAM_SYSTEMS kimera_vio basalt)
foreach(slam_system IN LISTS EXTERNAL_SLAM_SYSTEMS)
	if(PKGCONFIG_FOUND)
		pkg_check_modules(${slam_system} ${slam_system})
	endif()
	if(${slam_system}_FOUND)
		set(SLAM_NAME ${slam_system})
		set(SLAM_LDFLAGS ${${slam_system}_LDFLAGS})
		set(SLAM_LIBRARIES ${${slam_system}_LIBRARIES})
		set(SLAM_INCLUDE_DIRS ${${slam_system}_INCLUDE_DIRS})
		# Exit after the first we find.
		break()
	endif()
endforeach()

# Find openvr library
find_library(OPENVR_LIBRARIES NAMES openvr_api)
if(OPENVR_LIBRARIES)
	set(OPENVR_FOUND on)
	message(STATUS "Found OpenVR: " ${OPENVR_LIBRARIES})
else()
	set(OPENVR_FOUND off)
	message(STATUS "OpenVR not found")
endif()

# ILLIXR
set(ILLIXR_PATH
    ""
    CACHE PATH "Path to ILLIXR headers"
	)

# This one is named differently because that's what CTest uses
option(BUILD_TESTING "Enable building of the test suite?" ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(XRT_HAVE_INTERNAL_HID ON)
endif()

# cmake-format: off
option_with_deps(CMAKE_INTERPROCEDURAL_OPTIMIZATION "Enable inter-procedural (link-time) optimization" DEFAULT OFF DEPENDS HAS_IPO)
option(XRT_HAVE_TRACY "Enable Tracy support for tracing, make sure to set XRT_FEATURE_TRACING as well." OFF)

# Graphics deps to use (sorted in each group, groups thematic and ordered to handle internal deps: e.g. EGL and SDL2 need OpenGL/GLES)
option_with_deps(XRT_HAVE_OPENGL "Enable OpenGL Graphics API support" DEPENDS OPENGL_WITHOUT_GLX_FOUND)
option_with_deps(XRT_HAVE_OPENGL_GLX "Enable OpenGL Graphics API support on X11 (GLX)" DEPENDS XRT_HAVE_OPENGL OpenGL_GLX_FOUND)
option_with_deps(XRT_HAVE_OPENGLES "Enable OpenGL-ES Graphics API support" DEPENDS OpenGLES_FOUND)

option_with_deps(XRT_HAVE_EGL "Enable OpenGL(-ES) on EGL Graphics API support" DEPENDS EGL_FOUND "XRT_HAVE_OPENGL OR XRT_HAVE_OPENGLES")
option_with_deps(XRT_HAVE_SDL2 "Enable use of SDL2" DEPENDS SDL2_FOUND XRT_HAVE_OPENGL)

option_with_deps(XRT_HAVE_DXGI "Use DXGI APIs for some Windows-specific functionality" DEPENDS DXGI_LIBRARY)
option_with_deps(XRT_HAVE_WIL "Use WIL for Windows-specific functionality" DEPENDS wil_FOUND)
option_with_deps(XRT_HAVE_WINRT "Use WinRT APIs for some Windows-specific functionality" DEPENDS WINDOWSAPP_LIBRARY)

option_with_deps(XRT_HAVE_VULKAN "Enable Vulkan Graphics API support (also needed for compositor)" DEPENDS VULKAN_FOUND)
option_with_deps(XRT_HAVE_D3D11 "Enable Direct3D 11 Graphics API support" DEPENDS D3D11_LIBRARY XRT_HAVE_VULKAN XRT_HAVE_DXGI XRT_HAVE_WIL)
option_with_deps(XRT_HAVE_D3D12 "Enable Direct3D 12 Graphics API support" DEPENDS D3D12_LIBRARY XRT_HAVE_D3D11 XRT_HAVE_VULKAN XRT_HAVE_DXGI XRT_HAVE_WIL)
option_with_deps(XRT_HAVE_WAYLAND "Enable Wayland support" DEPENDS WAYLAND_FOUND WAYLAND_SCANNER_FOUND WAYLAND_PROTOCOLS_FOUND LIBDRM_FOUND)
option_with_deps(XRT_HAVE_WAYLAND_DIRECT "Enable Wayland direct support" DEPENDS XRT_HAVE_WAYLAND LIBDRM_FOUND "WAYLAND_PROTOCOLS_VERSION VERSION_GREATER_EQUAL 1.22")
option_with_deps(XRT_HAVE_XCB "Enable xcb support" DEPENDS XCB_FOUND)
option_with_deps(XRT_HAVE_XLIB "Enable xlib support" DEPENDS X11_FOUND)
option_with_deps(XRT_HAVE_XRANDR "Enable xlib-xrandr support" DEPENDS XRANDR_FOUND)

# System deps to use (sorted)
option_with_deps(XRT_HAVE_DBUS "Enable dbus support (for BLE support)" DEPENDS DBUS_FOUND)
option_with_deps(XRT_HAVE_LIBBSD "Enable libbsd support" DEPENDS LIBBSD_FOUND)
option_with_deps(XRT_HAVE_LIBUDEV "Enable libudev (used for device probing on Linux)" DEPENDS UDEV_FOUND)
option_with_deps(XRT_HAVE_PERCETTO "Enable percetto support" DEPENDS PERCETTO_FOUND)
option_with_deps(XRT_HAVE_SYSTEMD "Enable systemd support" DEPENDS Systemd_FOUND)

# Only use system cJSON if it includes https://github.com/DaveGamble/cJSON/pull/377
option_with_deps(XRT_HAVE_SYSTEM_CJSON "Enable cJSON from system, instead of bundled source" DEPENDS CJSON_FOUND "cJSON_VERSION VERSION_GREATER_EQUAL 1.7.13")

# SLAM backends and deps (sorted)
option_with_deps(XRT_HAVE_OPENCV "Enable OpenCV backend" DEPENDS OpenCV_FOUND)
option_with_deps(XRT_HAVE_BASALT "Enable Basalt support" DEPENDS basalt_FOUND)
option_with_deps(XRT_HAVE_KIMERA "Enable Kimera support" DEPENDS kimera_vio_FOUND)

# Hand tracking deps
option_with_deps(XRT_HAVE_ONNXRUNTIME "Enable ONNX runtime support" DEPENDS ONNXRUNTIME_FOUND)

option(XRT_MODULE_IPC "Enable the build of the IPC layer" ON)
option(XRT_MODULE_COMPOSITOR "Enable the compositor at all" ON)
option_with_deps(XRT_MODULE_COMPOSITOR_MAIN "Build main compositor host functionality" DEPENDS
	XRT_MODULE_COMPOSITOR
	XRT_HAVE_VULKAN
	"XRT_HAVE_WAYLAND OR XRT_HAVE_XCB OR ANDROID OR WIN32"
	)
option_with_deps(XRT_MODULE_COMPOSITOR_NULL "Build testing null compositor" DEPENDS XRT_MODULE_COMPOSITOR XRT_HAVE_VULKAN)
option_with_deps(XRT_MODULE_MERCURY_HANDTRACKING "Enable Mercury hand tracking" DEPENDS XRT_HAVE_OPENCV XRT_HAVE_ONNXRUNTIME)

option(XRT_MODULE_MONADO_CLI "Build monado-cli" ON)
option_with_deps(XRT_MODULE_MONADO_GUI "Build monado-gui" DEPENDS XRT_HAVE_SDL2)
option(XRT_MODULE_AUX_VIVE "Build aux_vive" ON)

# Feature configuration (sorted)
option(XRT_FEATURE_COLOR_LOG "Enable logging in color on supported platforms" ON)
option_with_deps(XRT_FEATURE_OPENXR "Build OpenXR runtime target" DEPENDS "XRT_MODULE_COMPOSITOR_MAIN OR XRT_MODULE_COMPOSITOR_NULL")
set(XRT_FEATURE_OPENXR_DEBUG_UTILS OFF) # Has never been enabled
option_with_deps(XRT_FEATURE_RENDERDOC "Enable RenderDoc API" DEPENDS "RT_LIBRARY OR WIN32")
option_with_deps(XRT_FEATURE_SERVICE "Enable separate service module for OpenXR runtime" DEPENDS XRT_MODULE_IPC XRT_FEATURE_OPENXR)
option_with_deps(XRT_FEATURE_SERVICE_SYSTEMD "Enable systemd socket activation of the service" DEPENDS XRT_HAVE_SYSTEMD XRT_FEATURE_SERVICE)
option_with_deps(XRT_FEATURE_SLAM "Enable SLAM tracking support" DEPENDS XRT_HAVE_OPENCV "XRT_HAVE_BASALT OR XRT_HAVE_KIMERA")
option(XRT_FEATURE_SSE2 "Build using SSE2 instructions, if building for 32-bit x86" ON)
option_with_deps(XRT_FEATURE_STEAMVR_PLUGIN "Build SteamVR plugin" DEPENDS "NOT ANDROID")
option_with_deps(XRT_FEATURE_TRACING "Enable debug tracing on supported platforms" DEFAULT OFF DEPENDS "XRT_HAVE_PERCETTO OR XRT_HAVE_TRACY")
option_with_deps(XRT_FEATURE_WINDOW_PEEK "Enable a window that displays the content of the HMD on screen" DEPENDS XRT_HAVE_SDL2)
option_with_deps(XRT_FEATURE_DEBUG_GUI "Enable debug window to be used" DEPENDS XRT_HAVE_SDL2)

if (XRT_FEATURE_SERVICE)
	# Disable the client debug gui by default for out-of-proc -
	# too many clients have problems with depending on SDL/GStreamer/etc and we rarely use it in this configuration
	option_with_deps(XRT_FEATURE_CLIENT_DEBUG_GUI "Allow clients to have their own instances of the debug gui" DEFAULT OFF DEPENDS XRT_FEATURE_DEBUG_GUI)
else()
	# Enable the client debug gui by default for in-proc -
	# In in-proc, the client debug gui is the same as the server debug gui, and we use it a lot in this configuration
	option_with_deps(XRT_FEATURE_CLIENT_DEBUG_GUI "Allow clients to have their own instances of the debug gui" DEFAULT ON DEPENDS XRT_FEATURE_DEBUG_GUI)
endif()

# systemd detailed config
option_with_deps(XRT_INSTALL_SYSTEMD_UNIT_FILES "Install user unit files for systemd socket activation on installation" DEPENDS XRT_HAVE_SYSTEMD)
option_with_deps(XRT_INSTALL_ABSOLUTE_SYSTEMD_UNIT_FILES "Use an absolute path to monado-system in installed user unit files for systemd socket activation" DEPENDS XRT_HAVE_SYSTEMD)

# Driver deps to use (sorted, though there are some internal dependencies)
option_with_deps(XRT_HAVE_BLUETOOTH "Enable Bluetooth (legacy, non-ble)" DEPENDS BLUETOOTH_FOUND)
option_with_deps(XRT_HAVE_FFMPEG "Enable ffmpeg testing video driver" DEPENDS FFMPEG_FOUND)
option_with_deps(XRT_HAVE_GST "Enable gstreamer" DEPENDS GST_FOUND)
option_with_deps(XRT_HAVE_HIDAPI "Enable libhidapi (used for PSVR)" DEPENDS HIDAPI_FOUND)
option_with_deps(XRT_HAVE_JPEG "Enable jpeg code (used for some video drivers)" DEPENDS JPEG_FOUND)
option_with_deps(XRT_HAVE_LIBUSB "Enable libusb (used for most drivers)" DEPENDS LIBUSB1_FOUND)
option_with_deps(XRT_HAVE_LIBUVC "Enable libuvc video driver" DEPENDS LIBUVC_FOUND XRT_HAVE_LIBUSB)
option_with_deps(XRT_HAVE_REALSENSE "Enable RealSense support" DEPENDS realsense2_FOUND)
option_with_deps(XRT_HAVE_OPENVR "Enable OpenVR support" DEPENDS OPENVR_FOUND)

# Drivers to build (sorted)
option_with_deps(XRT_BUILD_DRIVER_ANDROID "Enable Android sensors driver" DEPENDS ANDROID)
option_with_deps(XRT_BUILD_DRIVER_ARDUINO "Enable Arduino input device with BLE" DEPENDS XRT_HAVE_DBUS)
option_with_deps(XRT_BUILD_DRIVER_DAYDREAM "Enable the Google Daydream View controller driver (BLE)" DEPENDS XRT_HAVE_DBUS)
option_with_deps(XRT_BUILD_DRIVER_DEPTHAI "DepthAI" DEPENDS depthai_FOUND)
option_with_deps(XRT_BUILD_DRIVER_EUROC "Enable EuRoC dataset driver for SLAM evaluation" DEPENDS XRT_HAVE_OPENCV "NOT MSVC")
option_with_deps(XRT_BUILD_DRIVER_HANDTRACKING "Enable Camera Hand Tracking driver" DEPENDS XRT_MODULE_MERCURY_HANDTRACKING)
option_with_deps(XRT_BUILD_DRIVER_TWRAP "Enable Tracking Wrapper drivers" ON) # only depends on imu
option_with_deps(XRT_BUILD_DRIVER_HDK "Enable HDK driver" DEPENDS XRT_HAVE_INTERNAL_HID)
option_with_deps(XRT_BUILD_DRIVER_HYDRA "Enable Hydra driver" DEPENDS XRT_HAVE_INTERNAL_HID)
option_with_deps(XRT_BUILD_DRIVER_ILLIXR "Enable ILLIXR driver" DEPENDS ILLIXR_PATH)
option(XRT_BUILD_DRIVER_NS "Enable North Star driver" ON)
option_with_deps(XRT_BUILD_DRIVER_OHMD "Enable OpenHMD driver" DEPENDS OPENHMD_FOUND)
option_with_deps(XRT_BUILD_DRIVER_OPENGLOVES "Enable OpenGloves driver" DEPENDS XRT_HAVE_LIBUDEV XRT_HAVE_BLUETOOTH)
option_with_deps(XRT_BUILD_DRIVER_PSMV "Enable Playstation Move driver" DEPENDS XRT_HAVE_INTERNAL_HID "NOT MSVC")
option_with_deps(XRT_BUILD_DRIVER_PSSENSE "Enable PlayStation Sense driver" DEPENDS XRT_HAVE_INTERNAL_HID)
option_with_deps(XRT_BUILD_DRIVER_PSVR "Enable PSVR HMD driver" DEPENDS XRT_HAVE_HIDAPI)
option_with_deps(XRT_BUILD_DRIVER_QWERTY "Enable Qwerty driver" DEPENDS XRT_HAVE_SDL2)
option_with_deps(XRT_BUILD_DRIVER_REALSENSE "Enable RealSense device driver" DEPENDS XRT_HAVE_REALSENSE)
option_with_deps(XRT_BUILD_DRIVER_REMOTE "Enable remote debugging driver" DEPENDS "XRT_HAVE_LINUX OR ANDROID" XRT_MODULE_AUX_VIVE)
option_with_deps(XRT_BUILD_DRIVER_RIFT_S "Enable Oculus Rift S device driver" DEPENDS XRT_HAVE_HIDAPI XRT_HAVE_V4L2)
option_with_deps(XRT_BUILD_DRIVER_SURVIVE "Enable libsurvive driver" DEPENDS SURVIVE_FOUND XRT_MODULE_AUX_VIVE)
option_with_deps(XRT_BUILD_DRIVER_ULV2 "Enable Ultraleap v2 driver" DEPENDS LeapV2_FOUND)
option_with_deps(XRT_BUILD_DRIVER_VF "Build video frame driver (for video file support, uses gstreamer)" DEPENDS XRT_HAVE_GST)
option_with_deps(XRT_BUILD_DRIVER_VIVE "Enable driver for HTC Vive, Vive Pro, Valve Index, and their controllers" DEPENDS ZLIB_FOUND XRT_HAVE_LINUX XRT_MODULE_AUX_VIVE)
option_with_deps(XRT_BUILD_DRIVER_WMR "Enable Windows Mixed Reality driver" DEPENDS "NOT WIN32")
option_with_deps(XRT_BUILD_DRIVER_SIMULAVR "Enable simula driver" DEPENDS XRT_HAVE_REALSENSE)
option(XRT_BUILD_DRIVER_SIMULATED "Enable simulated driver" ON)

option(XRT_BUILD_SAMPLES "Enable compiling sample code implementations that will not be linked into any final targets" ON)
set(XRT_IPC_MSG_SOCK_FILENAME monado_comp_ipc CACHE STRING "Service socket filename")
set(XRT_IPC_SERVICE_PID_FILENAME monado.pid CACHE STRING "Service pidfile filename")
set(XRT_OXR_RUNTIME_SUFFIX monado CACHE STRING "OpenXR client library suffix")

# cmake-format: on

# Most users won't touch these.
mark_as_advanced(XRT_MODULE_COMPOSITOR_MAIN XRT_MODULE_COMPOSITOR_NULL XRT_FEATURE_OPENXR)

# Defaults for OpenXR layer support
if(NOT DEFINED XRT_FEATURE_OPENXR_LAYER_DEPTH)
	set(XRT_FEATURE_OPENXR_LAYER_DEPTH ON)
endif()
if(NOT DEFINED XRT_FEATURE_OPENXR_LAYER_CUBE)
	set(XRT_FEATURE_OPENXR_LAYER_CUBE ON)
endif()
if(NOT DEFINED XRT_FEATURE_OPENXR_LAYER_CYLINDER)
	set(XRT_FEATURE_OPENXR_LAYER_CYLINDER ON)
endif()
if(NOT DEFINED XRT_FEATURE_OPENXR_LAYER_EQUIRECT2)
	set(XRT_FEATURE_OPENXR_LAYER_EQUIRECT2 ON)
endif()
if(NOT DEFINED XRT_FEATURE_OPENXR_LAYER_EQUIRECT1)
	set(XRT_FEATURE_OPENXR_LAYER_EQUIRECT1 ON)
endif()

# You can set this from a superproject to add a driver
# All drivers must be listed in here to be included in the generated header!
list(
	APPEND
	AVAILABLE_DRIVERS
	"ANDROID"
	"ARDUINO"
	"DAYDREAM"
	"SIMULATED"
	"HANDTRACKING"
	"HDK"
	"HYDRA"
	"ILLIXR"
	"NS"
	"OHMD"
	"OPENGLOVES"
	"PSMV"
	"PSSENSE"
	"PSVR"
	"REALSENSE"
	"REMOTE"
	"RIFT_S"
	"SURVIVE"
	"V4L2"
	"ULV2"
	"VF"
	"DEPTHAI"
	"VIVE"
	"QWERTY"
	"WMR"
	"EUROC"
	"SIMULAVR"
	"TWRAP"
	)

# Package name needs to be known by the native code itself.
# Can be overridden from outside/command line
if(ANDROID AND NOT XRT_ANDROID_PACKAGE)
	if(XRT_FEATURE_SERVICE)
		set(XRT_ANDROID_PACKAGE "org.freedesktop.monado.openxr_runtime.out_of_process")
	else()
		set(XRT_ANDROID_PACKAGE "org.freedesktop.monado.openxr_runtime.in_process")
	endif()
endif()

###
# Flags
###

if(XRT_HAVE_TRACY AND XRT_HAVE_PERCETTO)
	message(FATAL_ERROR "Max one tracing backend, XRT_HAVE_TRACY and XRT_HAVE_PERCETTO enabled")
endif()

if(XRT_HAVE_XLIB AND NOT XRT_HAVE_XRANDR)
	message(WARNING "XRT_HAVE_XLIB requires XRT_HAVE_XRANDR but XRT_HAVE_XRANDR is disabled")
endif()

if(XRT_HAVE_OPENGLES AND NOT XRT_HAVE_EGL)
	message(FATAL_ERROR "XRT_HAVE_OPENGLES requires XRT_HAVE_EGL but XRT_HAVE_EGL is disabled")
endif()

if(XRT_HAVE_SDL2)
	if(NOT TARGET SDL2::SDL2 AND DEFINED SDL2_LIBRARIES)
		add_library(SDL2::SDL2 INTERFACE IMPORTED)
		target_include_directories(SDL2::SDL2 SYSTEM INTERFACE "${SDL2_INCLUDE_DIRS}")
		target_link_libraries(SDL2::SDL2 INTERFACE "${SDL2_LIBRARIES}")
	endif()
endif()

# Vulkan flags for the shared Vulkan code.
if(XRT_HAVE_XCB)
	set(VK_USE_PLATFORM_XCB_KHR TRUE)
endif()
if(XRT_HAVE_XCB
   AND XRT_HAVE_XLIB
   AND XRT_HAVE_XRANDR
	)
	set(VK_USE_PLATFORM_XLIB_XRANDR_EXT TRUE)
endif()
if(XRT_HAVE_WAYLAND)
	set(VK_USE_PLATFORM_WAYLAND_KHR TRUE)
endif()
if(ANDROID)
	set(VK_USE_PLATFORM_ANDROID_KHR TRUE)
endif()
if(WIN32)
	set(VK_USE_PLATFORM_WIN32_KHR TRUE)
endif()
if(XRT_HAVE_VULKAN AND NOT ANDROID)
	set(VK_USE_PLATFORM_DISPLAY_KHR TRUE)
endif()

include(CompilerFlags.cmake)

# Default to PIC code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Describe IPO setting
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
	message(STATUS "Inter-procedural optimization enabled")
endif()

# Make sure we have pretty colours
option(DISABLE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." OFF)

if(NOT DISABLE_COLORED_OUTPUT)
	if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		add_compile_options(-fdiagnostics-color=always)
	elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		add_compile_options(-fcolor-diagnostics)
	endif()
endif()

###
# Descend into the source
###

add_subdirectory(src)
add_subdirectory(doc)

if(BUILD_TESTING)
	include(CTest)
	add_subdirectory(tests)
endif()

###
# Keep these lists sorted
###
message(STATUS "#####----- Config -----#####")
message(STATUS "#    GIT_DESC:        ${GIT_DESC}")
message(STATUS "#")
message(STATUS "#    BASALT:          ${XRT_HAVE_BASALT}")
message(STATUS "#    BLUETOOTH:       ${XRT_HAVE_BLUETOOTH}")
message(STATUS "#    D3D11:           ${XRT_HAVE_D3D11}")
message(STATUS "#    D3D12:           ${XRT_HAVE_D3D12}")
message(STATUS "#    DBUS:            ${XRT_HAVE_DBUS}")
message(STATUS "#    EGL:             ${XRT_HAVE_EGL}")
message(STATUS "#    FFMPEG:          ${XRT_HAVE_FFMPEG}")
message(STATUS "#    GST (GStreamer): ${XRT_HAVE_GST}")
message(STATUS "#    HIDAPI:          ${XRT_HAVE_HIDAPI}")
message(STATUS "#    JPEG:            ${XRT_HAVE_JPEG}")
message(STATUS "#    KIMERA:          ${XRT_HAVE_KIMERA}")
message(STATUS "#    LIBBSD:          ${XRT_HAVE_LIBBSD}")
message(STATUS "#    LIBUSB:          ${XRT_HAVE_LIBUSB}")
message(STATUS "#    LIBUVC:          ${XRT_HAVE_LIBUVC}")
message(STATUS "#    ONNXRUNTIME:     ${XRT_HAVE_ONNXRUNTIME}")
message(STATUS "#    OPENCV:          ${XRT_HAVE_OPENCV}")
message(STATUS "#    OPENGL:          ${XRT_HAVE_OPENGL}")
message(STATUS "#    OPENGLES:        ${XRT_HAVE_OPENGLES}")
message(STATUS "#    OPENGL_GLX:      ${XRT_HAVE_OPENGL_GLX}")
message(STATUS "#    PERCETTO:        ${XRT_HAVE_PERCETTO}")
message(STATUS "#    REALSENSE:       ${XRT_HAVE_REALSENSE}")
message(STATUS "#    OPENVR:          ${XRT_HAVE_OPENVR}")
message(STATUS "#    SDL2:            ${XRT_HAVE_SDL2}")
message(STATUS "#    SYSTEM_CJSON:    ${XRT_HAVE_SYSTEM_CJSON}")
message(STATUS "#    SYSTEMD:         ${XRT_HAVE_SYSTEMD}")
message(STATUS "#    TRACY:           ${XRT_HAVE_TRACY}")
message(STATUS "#    VULKAN:          ${XRT_HAVE_VULKAN}")
message(STATUS "#    WAYLAND:         ${XRT_HAVE_WAYLAND}")
message(STATUS "#    WAYLAND_DIRECT:  ${XRT_HAVE_WAYLAND_DIRECT}")
message(STATUS "#    XCB:             ${XRT_HAVE_XCB}")
message(STATUS "#    XLIB:            ${XRT_HAVE_XLIB}")
message(STATUS "#    XRANDR:          ${XRT_HAVE_XRANDR}")
message(STATUS "#")
message(STATUS "#    MODULE_AUX_VIVE:             ${XRT_MODULE_AUX_VIVE}")
message(STATUS "#    MODULE_COMPOSITOR:           ${XRT_MODULE_COMPOSITOR}")
message(STATUS "#    MODULE_COMPOSITOR_MAIN:      ${XRT_MODULE_COMPOSITOR_MAIN}")
message(STATUS "#    MODULE_COMPOSITOR_NULL:      ${XRT_MODULE_COMPOSITOR_NULL}")
message(STATUS "#    MODULE_MERCURY_HANDTRACKING: ${XRT_MODULE_MERCURY_HANDTRACKING}")
message(STATUS "#    MODULE_IPC:                  ${XRT_MODULE_IPC}")
message(STATUS "#    MODULE_MONADO_GUI:           ${XRT_MODULE_MONADO_GUI}")
message(STATUS "#    MODULE_MONADO_CLI:           ${XRT_MODULE_MONADO_CLI}")
message(STATUS "#")
message(STATUS "#    FEATURE_CLIENT_DEBUG_GUI:             ${XRT_FEATURE_CLIENT_DEBUG_GUI}")
message(STATUS "#    FEATURE_COLOR_LOG:                    ${XRT_FEATURE_COLOR_LOG}")
message(STATUS "#    FEATURE_DEBUG_GUI:                    ${XRT_FEATURE_DEBUG_GUI}")
message(STATUS "#    FEATURE_OPENXR:                       ${XRT_FEATURE_OPENXR}")
message(STATUS "#    FEATURE_OPENXR_DEBUG_UTILS:           ${XRT_FEATURE_OPENXR_DEBUG_UTILS}")
message(STATUS "#    FEATURE_OPENXR_LAYER_CUBE:            ${XRT_FEATURE_OPENXR_LAYER_CUBE}")
message(STATUS "#    FEATURE_OPENXR_LAYER_CYLINDER:        ${XRT_FEATURE_OPENXR_LAYER_CYLINDER}")
message(STATUS "#    FEATURE_OPENXR_LAYER_DEPTH:           ${XRT_FEATURE_OPENXR_LAYER_DEPTH}")
message(STATUS "#    FEATURE_OPENXR_LAYER_EQUIRECT1:       ${XRT_FEATURE_OPENXR_LAYER_EQUIRECT1}")
message(STATUS "#    FEATURE_OPENXR_LAYER_EQUIRECT2:       ${XRT_FEATURE_OPENXR_LAYER_EQUIRECT2}")
message(STATUS "#    FEATURE_RENDERDOC:                    ${XRT_FEATURE_RENDERDOC}")
message(STATUS "#    FEATURE_SERVICE:                      ${XRT_FEATURE_SERVICE}")
message(STATUS "#    FEATURE_SERVICE_SYSTEMD:              ${XRT_FEATURE_SERVICE_SYSTEMD}")
message(STATUS "#    FEATURE_SLAM:                         ${XRT_FEATURE_SLAM}")
message(STATUS "#    FEATURE_SSE2:                         ${XRT_FEATURE_SSE2}")
message(STATUS "#    FEATURE_STEAMVR_PLUGIN:               ${XRT_FEATURE_STEAMVR_PLUGIN}")
message(STATUS "#    FEATURE_TRACING:                      ${XRT_FEATURE_TRACING}")
message(STATUS "#    FEATURE_WINDOW_PEEK:                  ${XRT_FEATURE_WINDOW_PEEK}")
message(STATUS "#")
message(STATUS "#    DRIVER_ANDROID:      ${XRT_BUILD_DRIVER_ANDROID}")
message(STATUS "#    DRIVER_ARDUINO:      ${XRT_BUILD_DRIVER_ARDUINO}")
message(STATUS "#    DRIVER_DAYDREAM:     ${XRT_BUILD_DRIVER_DAYDREAM}")
message(STATUS "#    DRIVER_DEPTHAI:      ${XRT_BUILD_DRIVER_DEPTHAI}")
message(STATUS "#    DRIVER_EUROC:        ${XRT_BUILD_DRIVER_EUROC}")
message(STATUS "#    DRIVER_HANDTRACKING: ${XRT_BUILD_DRIVER_HANDTRACKING}")
message(STATUS "#    DRIVER_HDK:          ${XRT_BUILD_DRIVER_HDK}")
message(STATUS "#    DRIVER_HYDRA:        ${XRT_BUILD_DRIVER_HYDRA}")
message(STATUS "#    DRIVER_ILLIXR:       ${XRT_BUILD_DRIVER_ILLIXR}")
message(STATUS "#    DRIVER_NS:           ${XRT_BUILD_DRIVER_NS}")
message(STATUS "#    DRIVER_OHMD:         ${XRT_BUILD_DRIVER_OHMD}")
message(STATUS "#    DRIVER_OPENGLOVES:   ${XRT_BUILD_DRIVER_OPENGLOVES}")
message(STATUS "#    DRIVER_PSMV:         ${XRT_BUILD_DRIVER_PSMV}")
message(STATUS "#    DRIVER_PSSENSE:      ${XRT_BUILD_DRIVER_PSSENSE}")
message(STATUS "#    DRIVER_PSVR:         ${XRT_BUILD_DRIVER_PSVR}")
message(STATUS "#    DRIVER_QWERTY:       ${XRT_BUILD_DRIVER_QWERTY}")
message(STATUS "#    DRIVER_REALSENSE:    ${XRT_BUILD_DRIVER_REALSENSE}")
message(STATUS "#    DRIVER_REMOTE:       ${XRT_BUILD_DRIVER_REMOTE}")
message(STATUS "#    DRIVER_RIFT_S:       ${XRT_BUILD_DRIVER_RIFT_S}")
message(STATUS "#    DRIVER_SIMULATED:    ${XRT_BUILD_DRIVER_SIMULATED}")
message(STATUS "#    DRIVER_SIMULAVR:     ${XRT_BUILD_DRIVER_SIMULAVR}")
message(STATUS "#    DRIVER_SURVIVE:      ${XRT_BUILD_DRIVER_SURVIVE}")
message(STATUS "#    DRIVER_TWRAP:        ${XRT_BUILD_DRIVER_TWRAP}")
message(STATUS "#    DRIVER_ULV2:         ${XRT_BUILD_DRIVER_ULV2}")
message(STATUS "#    DRIVER_VF:           ${XRT_BUILD_DRIVER_VF}")
message(STATUS "#    DRIVER_VIVE:         ${XRT_BUILD_DRIVER_VIVE}")
message(STATUS "#    DRIVER_WMR:          ${XRT_BUILD_DRIVER_WMR}")
message(STATUS "#####----- Config -----#####")

if(XRT_FEATURE_SERVICE AND NOT XRT_FEATURE_OPENXR)
	message(FATAL_ERROR "XRT_FEATURE_SERVICE requires XRT_FEATURE_OPENXR to be enabled")
endif()
if(XRT_FEATURE_SERVICE AND NOT XRT_MODULE_IPC)
	message(FATAL_ERROR "XRT_FEATURE_SERVICE requires XRT_MODULE_IPC to be enabled")
endif()
if(XRT_FEATURE_OPENXR_DEBUG_UTILS)
	message(FATAL_ERROR "XRT_FEATURE_OPENXR_DEBUG_UTILS is not fully implemented")
endif()
