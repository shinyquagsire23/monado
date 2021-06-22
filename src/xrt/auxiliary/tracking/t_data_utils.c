// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Small data helpers for calibration.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#include "tracking/t_tracking.h"
#include "util/u_misc.h"

#include <stdio.h>
#include <assert.h>


void
t_stereo_camera_calibration_alloc(struct t_stereo_camera_calibration **out_c, uint32_t distortion_num)
{
	assert(distortion_num == 5 || distortion_num == 14);

	struct t_stereo_camera_calibration *c = U_TYPED_CALLOC(struct t_stereo_camera_calibration);
	c->view[0].distortion_num = distortion_num;
	c->view[1].distortion_num = distortion_num;
	t_stereo_camera_calibration_reference(out_c, c);
}

void
t_stereo_camera_calibration_destroy(struct t_stereo_camera_calibration *c)
{
	free(c);
}
