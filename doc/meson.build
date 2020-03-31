# Copyright 2018-2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

doxyconf = configuration_data()
doxyconf.set('SRCDIR', meson.source_root())
doxyconf.set('BUILDDIR', meson.build_root())
doxyconf.set('CURBUILDDIR', meson.current_build_dir())

if get_option('doxygen-warn-undocumented') and get_option('doxygen-extract-all')
	error('doxygen-warn-undocumented and doxygen-extract-all are mutually incompatible')
endif

if get_option('doxygen-warn-undocumented')
	doxyconf.set('DOXYGEN_WARN_UNDOCUMENTED', 'YES')
else
	doxyconf.set('DOXYGEN_WARN_UNDOCUMENTED', 'NO')
endif

if get_option('doxygen-extract-all')
	doxyconf.set('DOXYGEN_EXTRACT_ALL', 'YES')
else
	doxyconf.set('DOXYGEN_EXTRACT_ALL', 'NO')
endif

doxyfile = configure_file(input: 'Doxyfile.in',
	output: 'Doxyfile', configuration: doxyconf)

html = custom_target('docs',
	input: doxyfile, output: 'html', command: [doxygen, doxyfile],
	build_by_default: true)
