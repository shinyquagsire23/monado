// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Loads and dumps a calibration file.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_config_have.h"

#include "cli_common.h"

#ifdef XRT_HAVE_OPENCV
#include "tracking/t_tracking.h"
#endif

#include <string.h>
#include <stdio.h>

#define P(...) fprintf(stderr, __VA_ARGS__)

int
cli_cmd_calibration_dump(int argc, const char **argv)
{
#ifdef XRT_HAVE_OPENCV
	if (argc < 3) {
		P("Must be given a file path\n");
		return 1;
	}

	const char *filename = argv[2];

	struct t_stereo_camera_calibration *data = NULL;
	if (!t_stereo_camera_calibration_load(filename, &data)) {
		P("Could not load '%s'!\n", filename);
		return 1;
	}

	t_stereo_camera_calibration_dump(data);
	t_stereo_camera_calibration_reference(&data, NULL);

	return 0;
#else
	P("Not compiled with XRT_HAVE_OPENCV, so can't load calibration files!\n");
	return 1;
#endif
}
