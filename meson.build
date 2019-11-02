# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

project(
	'xrt',
	['c', 'cpp'],
	version: '0.1.0',
	license: 'BSL-1.0',
	meson_version: '>=0.49.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
	],
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

add_project_arguments(cc.get_supported_arguments([
	'-D_XOPEN_SOURCE=700',
	'-pedantic',
	'-Wall',
	'-Wextra',
	'-Wno-unused-parameter',
]), language: 'c')

add_project_arguments(cpp.get_supported_arguments([
	'-D_XOPEN_SOURCE=700',
	'-Wall',
	'-Wextra',
	'-Wno-unused-parameter',
	'-Wno-deprecated-copy', # Eigen
]), language: 'cpp')


#
# Pre-setting these variables
#

build_docs = false
build_tracking = false

v4l2_required = false
hidapi_required = false
openhmd_required = false


#
# Adding dependencies
#

doxygen = find_program('doxygen', required: get_option('docs'))
if get_option('docs').enabled() or get_option('docs').auto()
	build_docs = true
endif

glslangValidator = find_program('glslangValidator')

pthreads = cc.find_library('pthread', required: true)

avcodec = dependency('libavcodec', required: false)
eigen3  = dependency('eigen3')
libjpeg = dependency('libjpeg', required: false)
libusb  = dependency('libusb-1.0', required: false)
opengl  = dependency('gl')
sdl2    = dependency('sdl2', required: get_option('gui'))
udev    = dependency('libudev', required: false)
libuvc  = dependency('libuvc', required: false)
vulkan  = dependency('vulkan')
zlib    = dependency('zlib', required: false)

opencv = dependency('opencv4', required: false)
if not opencv.found()
	opencv = dependency('opencv', required: get_option('tracking'))
endif

if get_option('tracking').enabled() or get_option('tracking').auto()
	build_tracking = opencv.found()
endif

# TODO: make these behave well when not present
x11       = dependency('x11', required: false)
xcb       = dependency('xcb', required: false)
xcb_randr = dependency('xcb-randr', required: false)

wayland         = dependency('wayland-client', required: false)
wayland_protos  = dependency('wayland-protocols', required: false)
wayland_scanner = dependency('wayland-scanner', required: false)

if wayland_scanner.found()
	wayland_scanner = find_program(
		wayland_scanner.get_pkgconfig_variable('wayland_scanner'),
		native: true,
	)
endif

# For now required on Linux
if target_machine.system() == 'linux'
	v4l2_required = true
endif

drivers = get_option('drivers')
if 'ohmd' in drivers
	openhmd_required = true
endif
if 'psvr' in drivers
	hidapi_required = true
endif
if 'v4l2' in drivers
	v4l2_required = true
endif

if 'auto' in drivers
	drivers += ['psmv', 'hydra', 'hdk']
endif

openhmd = dependency('openhmd', required: openhmd_required)
hidapi  = dependency('hidapi-libusb', required: hidapi_required)
v4l2    = dependency('libv4l2', required: v4l2_required)

if openhmd.found() and ('auto' in drivers or 'ohmd' in drivers)
	if 'ohmd' not in drivers
		drivers += ['ohmd']
	endif
endif

if hidapi.found() and ('auto' in drivers or 'psvr' in drivers or 'hdk' in drivers)
	if 'psvr' not in drivers
		drivers += ['psvr']
	endif
endif

if zlib.found() and ('auto' in drivers or 'vive' in drivers)
	if 'vive' not in drivers
		drivers += ['vive']
	endif
endif

if v4l2.found() and ('auto' in drivers or 'v4l2' in drivers)
	if 'v4l2' not in drivers
		drivers += ['v4l2']
	endif
endif

if drivers.length() == 0 or drivers == ['auto']
	error('You must enable at least one driver.')
endif

if udev.found()
	add_project_arguments('-DXRT_HAVE_LIBUDEV', language: ['c', 'cpp'])
endif

if libusb.found()
	add_project_arguments('-DXRT_HAVE_LIBUSB', language: ['c', 'cpp'])
endif

if opencv.found()
	add_project_arguments('-DXRT_HAVE_OPENCV', language: ['c', 'cpp'])
endif

if libjpeg.found()
	add_project_arguments('-DXRT_HAVE_JPEG', language: ['c', 'cpp'])
endif

if libuvc.found()
	add_project_arguments('-DXRT_HAVE_LIBUVC', language: ['c', 'cpp'])
endif

if avcodec.found()
	add_project_arguments('-DXRT_HAVE_FFMPEG', language: ['c', 'cpp'])
endif

if sdl2.found()
	add_project_arguments('-DXRT_HAVE_SDL2', language: ['c', 'cpp'])
endif


#
# Go down sub directories
#

subdir('src')

if build_docs
	subdir('doc')
endif


#
# Final bits
#

# This is here so that it gets emitted in the top-level build directory
manifest_devconf = configuration_data()
# https://github.com/mesonbuild/meson/issues/5940
manifest_devconf.set('runtime_path', openxr.full_path())

manifest_dev_json = configure_file(
	input: manifest_in,
	output: 'openxr_monado-dev.json',
	configuration: manifest_devconf,
)

message('Configuration done!')
message('    drivers:  ' + ', '.join(drivers))

if build_docs
	message('    docs:     yes')
else
	message('    docs:     no')
endif

if build_tracking
	message('    tracking: yes')
else
	message('    tracking: no')
endif
