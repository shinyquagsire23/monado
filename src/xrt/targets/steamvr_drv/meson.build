# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

driver_monado = shared_library(
	'driver_monado',
	files(
		'main.c'
	),
	link_whole: [
		lib_target_instance_no_comp,
		lib_st_ovrd,
	],
	include_directories: [
	openvr_include,
	st_ovrd_include,
	openvr_include,
		aux_include,
		xrt_include,
	],
	link_with : driver_libs,
	dependencies : [pthreads, libjpeg],
	# build 'driver_monado.so' instead of 'libdriver_monado.so'
	name_prefix: '',
)

if meson.version().version_compare('<0.56')
	build_root = meson.build_root()
else
	build_root = meson.project_build_root()
endif

plugin_dir = join_paths(build_root, 'steamvr-monado')

bindings_json_path = join_paths(aux_bindings_dir, 'bindings.json')
resources_dir = join_paths(plugin_dir, 'resources')
input_profile_dir = join_paths(resources_dir, 'input')
driver_manifest_path = join_paths(plugin_dir, 'driver.vrdrivermanifest')

copy_asset = find_program('copy_assets.py')
copy_plugin = find_program('copy_plugin.py')
generate_profiles = find_program(join_paths(aux_bindings_dir, 'steamvr_profiles.py'))

fake_driver_manifest = custom_target(
	'driver_manifest_copy',
	depends : driver_monado,
	input : 'driver.vrdrivermanifest',
	output : 'fake_driver_manifest',
	command : [copy_asset, 'FILE', '@INPUT@', driver_manifest_path],
	build_by_default : true
)

fake_driver_resources = custom_target(
	'driver_resources_copy',
	depends : driver_monado,
	input : 'resources',  # https://github.com/mesonbuild/meson/issues/4717
	output : 'fake_driver_resources',
	command : [copy_asset, 'DIRECTORY', '@INPUT@', resources_dir],
	build_by_default : true
)

custom_target(
	'driver_input_profiles_generate',
	depends : driver_monado,
	input : bindings_json_path,
	output : 'fake_input_profiles_generate',
	command : [generate_profiles, '@INPUT@', input_profile_dir],
	build_by_default : true
)

plugin_archdir = ''

if host_machine.system() == 'windows'
	if host_machine.cpu_family() == 'x86'
		plugin_archdir = 'win32'
	elif host_machine.cpu_family() == 'x86_64'
		plugin_archdir = 'win64'
	endif
elif host_machine.system() == 'linux'
	if host_machine.cpu_family() == 'x86'
		plugin_archdir = 'linux32'
	elif host_machine.cpu_family() == 'x86_64'
		plugin_archdir = 'linux64'
	endif
endif

custom_target(
	'plugin_copy',
	depends : [driver_monado, fake_driver_manifest, fake_driver_resources],
	input : driver_monado,
	output : 'fake_plugin',
	command : [copy_plugin, '@INPUT@', join_paths(plugin_dir, 'bin', plugin_archdir, '@PLAINNAME@')],
	build_by_default : true
)


install_subdir(join_paths(plugin_dir), install_dir: join_paths(get_option('prefix'), 'share'))
