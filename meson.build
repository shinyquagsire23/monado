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

glslangValidator = find_program('glslangValidator')

avcodec = dependency('libavcodec', required: false)
eigen3  = dependency('eigen3')
libjpeg = dependency('libjpeg', required: false)
libusb  = dependency('libusb-1.0', required: false)
opencv  = dependency('opencv4', required: get_option('tracking'))
opengl  = dependency('gl')
sdl2    = dependency('sdl2', required: get_option('gui'))
udev    = dependency('libudev', required: false)
libuvc  = dependency('libuvc', required: false)
vulkan  = dependency('vulkan')

doxygen = find_program('doxygen', required: get_option('docs'))

pthreads = cc.find_library('pthread', required: true)

if get_option('tracking').enabled() or get_option('tracking').auto()
	build_tracking = opencv.found()
else
	build_tracking = false
endif

# TODO: make these behave well when not present
x11       = dependency('x11', required: false)
xcb       = dependency('xcb', required: false)
xcb_randr = dependency('xcb-randr', required: false)
wayland   = dependency('wayland-client', required: false)

hidapi_required = false
openhmd_required = false
v4l2_required = false

# For now required on Linux
if target_machine.system() == 'linux'
	v4l2_required = true
endif

drivers = get_option('drivers')
if 'ohmd' in drivers
	openhmd_required = true
endif
if 'hdk' in drivers or 'psvr' in drivers
	hidapi_required = true
endif
if 'v4l2' in drivers
	v4l2_required = true
endif

if 'auto' in drivers
	drivers += ['psmv', 'hydra']
endif

openhmd = dependency('openhmd', required: openhmd_required)
hidapi  = dependency('hidapi', required: hidapi_required)
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
	if 'hdk' not in drivers
		drivers += ['hdk']
	endif
endif

if v4l2.found() and ('auto' in drivers or 'v4l2' in drivers)
	if 'v4l2' not in drivers
		drivers += ['v4l2']
	endif
endif

if drivers.length() == 0 or drivers == ['auto']
	error('You must enable at least one driver.')
else
	message('Building with drivers ' + ', '.join(drivers))
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

subdir('src')

if doxygen.found()
	subdir('doc')
endif

# This is here so that it gets emitted in the top-level build directory
manifest_devconf = configuration_data()
# https://github.com/mesonbuild/meson/issues/5940
manifest_devconf.set('runtime_path', openxr.full_path())

manifest_dev_json = configure_file(
	input: manifest_in,
	output: 'openxr_monado-dev.json',
	configuration: manifest_devconf,
)
