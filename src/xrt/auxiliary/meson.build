# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

aux_include = include_directories('.')

subdir('bindings')

u_git_tag_c = vcs_tag(
	input: 'util/u_git_tag.c.in',
	output: 'u_git_tag.c',
	replace_string: '@GIT_DESC@',
)

lib_aux_util = static_library(
	'aux_util',
	files(
		'util/u_bitwise.c',
		'util/u_bitwise.h',
		'util/u_debug.c',
		'util/u_debug.h',
		'util/u_device.c',
		'util/u_device.h',
		'util/u_distortion.c',
		'util/u_distortion.h',
		'util/u_distortion_mesh.c',
		'util/u_distortion_mesh.h',
		'util/u_documentation.h',
		'util/u_file.c',
		'util/u_file.h',
		'util/u_format.c',
		'util/u_format.h',
		'util/u_frame.c',
		'util/u_frame.h',
		'util/u_git_tag.h',
		'util/u_handles.c',
		'util/u_handles.h',
		'util/u_hashmap.cpp',
		'util/u_hashmap.h',
		'util/u_hashset.cpp',
		'util/u_hashset.h',
		'util/u_json.c',
		'util/u_json.h',
		'util/u_logging.c',
		'util/u_logging.h',
		'util/u_misc.c',
		'util/u_misc.h',
		'util/u_render_timing.c',
		'util/u_render_timing.h',
		'util/u_sink.h',
		'util/u_sink_converter.c',
		'util/u_sink_deinterleaver.c',
		'util/u_sink_queue.c',
		'util/u_sink_quirk.c',
		'util/u_sink_split.c',
		'util/u_time.cpp',
		'util/u_time.h',
		'util/u_var.cpp',
		'util/u_var.h',
		'util/u_hand_tracking.c',
		'util/u_hand_tracking.h',
	) + [
		u_git_tag_c,
	],
	include_directories: [
		xrt_include,
		cjson_include,
	],
	dependencies: [
		xrt_config_have,
	]
)

aux_util = declare_dependency(
	include_directories: aux_include,
	link_with: lib_aux_util,
)

if build_opengl or build_opengles
	ogl_files = [
		'ogl/ogl_documentation.h',
		'ogl/ogl_api.h',
		'ogl/ogl_api.c',
		'ogl/ogl_helpers.c',
		'ogl/ogl_helpers.h',
	]

	if build_opengles or build_egl
		ogl_files += [
			'ogl/egl_api.h',
			'ogl/egl_api.c',
		]
	endif

	lib_aux_ogl = static_library(
		'aux_ogl',
		files(ogl_files),
		include_directories: [
			xrt_include,
			glad_include,
		],
	)

	aux_ogl = declare_dependency(
		include_directories: aux_include,
		link_with: lib_aux_ogl,
	)
endif

aux_os_files = files(
	'os/os_documentation.h',
	'os/os_hid.h',
	'os/os_hid_hidraw.c',
	'os/os_threading.h',
	'os/os_time.h',
	'os/os_ble.h',
)

aux_os_deps = []

if dbus.found() and not get_option('dbus').disabled()
	aux_os_files += files(
		'os/os_ble_dbus.c'
	)
	aux_os_deps += dbus
endif

lib_aux_os = static_library(
	'aux_os',
	aux_os_files,
	dependencies: aux_os_deps,
	include_directories: xrt_include,
)

aux_os = declare_dependency(
	include_directories: aux_include,
	link_with: lib_aux_os,
)

lib_aux_math = static_library(
	'aux_math',
	files(
		'math/m_api.h',
		'math/m_base.cpp',
		'math/m_eigen_interop.hpp',
		'math/m_filter_fifo.c',
		'math/m_filter_fifo.h',
		'math/m_hash.cpp',
		'math/m_imu_3dof.c',
		'math/m_imu_3dof.h',
		'math/m_imu_pre.c',
		'math/m_imu_pre.h',
		'math/m_optics.c',
		'math/m_permutation.c',
		'math/m_permutation.h',
		'math/m_predict.c',
		'math/m_predict.h',
		'math/m_quatexpmap.cpp',
		'math/m_space.cpp',
		'math/m_space.h',
		'math/m_vec2.h',
		'math/m_vec3.h',
	),
	include_directories: xrt_include,
	dependencies: [eigen3],
)

aux_math = declare_dependency(
	include_directories: aux_include,
	link_with: lib_aux_math,
)

tracking_srcs = [
	'tracking/t_data_utils.c',
	'tracking/t_imu.h',
	'tracking/t_imu_fusion.hpp',
	'tracking/t_imu.cpp',
	'tracking/t_lowpass.hpp',
	'tracking/t_lowpass_vector.hpp',
	'tracking/t_tracking.h',
]
tracking_deps = [eigen3]

if build_tracking
	tracking_srcs += [
		'tracking/t_calibration.cpp',
		'tracking/t_calibration_opencv.hpp',
		'tracking/t_convert.cpp',
		'tracking/t_debug_hsv_filter.cpp',
		'tracking/t_debug_hsv_picker.cpp',
		'tracking/t_debug_hsv_viewer.cpp',
		'tracking/t_file.cpp',
		'tracking/t_fusion.hpp',
		'tracking/t_helper_debug_sink.hpp',
		'tracking/t_hsv_filter.c',
		'tracking/t_kalman.cpp',
		'tracking/t_tracker_psmv.cpp',
		'tracking/t_tracker_psmv_fusion.hpp',
		'tracking/t_tracker_psvr.cpp',
		'tracking/t_tracker_hand.cpp',
	]
	tracking_deps += [opencv]
endif

lib_aux_tracking = static_library(
	'aux_tracking',
	files(tracking_srcs),
	include_directories: [
		xrt_include,
		flexkalman_include,
		hungarian_include,
	],
	dependencies: tracking_deps
)

aux_tracking = declare_dependency(
	include_directories: aux_include,
	link_with: lib_aux_tracking,
)

lib_aux_vk = static_library(
	'aux_vk',
	files(
		'vk/vk_documentation.h',
		'vk/vk_helpers.h',
		'vk/vk_helpers.c',
		'vk/vk_image_allocator.h',
		'vk/vk_image_allocator.c',
	),
	include_directories: [
		xrt_include,
	],
)

aux_vk = declare_dependency(
	include_directories: aux_include,
	link_with: lib_aux_vk,
)


all_aux = [aux_util, aux_os, aux_math, aux_tracking, aux_generated_bindings]

aux = declare_dependency(dependencies: all_aux)
