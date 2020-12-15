# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

#####
# Drivers config
#

drivers_conf = configuration_data()
foreach driver : drivers
	drivers_conf.set('XRT_BUILD_DRIVER_' + driver.to_upper(), true)
endforeach

xrt_config_drivers_h = configure_file(
	output: 'xrt_config_drivers.h',
	configuration: drivers_conf,
)

xrt_config_drivers = declare_dependency(
	sources: xrt_config_drivers_h,
	include_directories: xrt_include,
)


#####
# Have config
#

have_conf = configuration_data()

if build_egl
	have_conf.set('XRT_HAVE_EGL', true)
endif
if avcodec.found()
	have_conf.set('XRT_HAVE_FFMPEG', true)
endif
if libjpeg.found()
	have_conf.set('XRT_HAVE_JPEG', true)
endif
if udev.found()
	have_conf.set('XRT_HAVE_LIBUDEV', true)
endif

if libusb.found()
	have_conf.set('XRT_HAVE_LIBUSB', true)
endif

if libuvc.found()
	have_conf.set('XRT_HAVE_LIBUVC', true)
endif

if opencv.found()
	have_conf.set('XRT_HAVE_OPENCV', true)
endif

if build_opengl
	have_conf.set('XRT_HAVE_OPENGL', true)
endif

if build_opengles
	have_conf.set('XRT_HAVE_OPENGLES', true)
endif

if sdl2.found()
	have_conf.set('XRT_HAVE_SDL2', true)
endif

if has_v4l2_header and 'v4l2' in drivers
	have_conf.set('XRT_HAVE_V4L2', true)
endif

if 'vf' in drivers
	have_conf.set('XRT_HAVE_VF', true)
endif

if true
	have_conf.set('XRT_HAVE_VULKAN', true)	
endif

if dbus.found() and not get_option('dbus').disabled()
	have_conf.set('XRT_HAVE_DBUS', true)
endif

if get_option('layer_depth')
	have_conf.set('XRT_FEATURE_OPENXR_LAYER_DEPTH', true)
endif

if get_option('layer_cube')
	have_conf.set('XRT_FEATURE_OPENXR_LAYER_CUBE', true)
endif

if get_option('layer_cylinder')
	have_conf.set('XRT_FEATURE_OPENXR_LAYER_CYLINDER', true)
endif

if get_option('layer_equirect1')
	have_conf.set('XRT_FEATURE_OPENXR_LAYER_EQUIRECT1', true)
endif

if get_option('layer_equirect2')
	have_conf.set('XRT_FEATURE_OPENXR_LAYER_EQUIRECT2', true)
endif

xrt_config_have_h = configure_file(
	output: 'xrt_config_have.h',
	configuration: have_conf,
)

xrt_config_have = declare_dependency(
	sources: xrt_config_have_h,
	include_directories: xrt_include,
)


#####
# Build config
#

build_conf = configuration_data()
if get_option('service')
	build_conf.set('XRT_FEATURE_SERVICE', true)
endif

if get_option('color_log')
	build_conf.set('XRT_FEATURE_COLOR_LOG', true)
endif

xrt_config_build_h = configure_file(
	output: 'xrt_config_build.h',
	configuration: build_conf,
)

xrt_config_build = declare_dependency(
	sources: xrt_config_build_h,
	include_directories: xrt_include,
)


#####
# Vulkan config
#

vulkan_conf = configuration_data()
# Vulkan flags for the shared Vulkan code.
if build_wayland
	vulkan_conf.set('VK_USE_PLATFORM_WAYLAND_KHR', true)
endif
if build_xcb
	vulkan_conf.set('VK_USE_PLATFORM_XCB_KHR', true)
endif
if build_xcb_xrandr_direct
	vulkan_conf.set('VK_USE_PLATFORM_XLIB_XRANDR_EXT', true)
endif
if build_vk_khr_display
	vulkan_conf.set('VK_USE_PLATFORM_DISPLAY_KHR', true)
endif

xrt_config_vulkan_h = configure_file(
	output: 'xrt_config_vulkan.h',
	configuration: vulkan_conf,
)

xrt_config_vulkan = declare_dependency(
	sources: xrt_config_vulkan_h,
	include_directories: xrt_include,
)


#####
# Mock Android config
#

android_conf = configuration_data()

# No actual config data in here.

xrt_config_android_h = configure_file(
	output: 'xrt_config_android.h',
	configuration: android_conf,
)
