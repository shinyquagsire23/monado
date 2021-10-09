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
#include "util/u_logging.h"

#include <stdio.h>
#include <assert.h>


/*
 *
 * Helpers
 *
 */

#define P(...)                                                                                                         \
	do {                                                                                                           \
		ssize_t _ret = 0;                                                                                      \
		if ((size_t)curr < sizeof(buf)) {                                                                      \
			_ret = snprintf(buf + curr, sizeof(buf) - (size_t)curr, __VA_ARGS__);                          \
		}                                                                                                      \
		if (_ret > 0) {                                                                                        \
			curr += _ret;                                                                                  \
		}                                                                                                      \
	} while (false)

static void
dump_mat(const char *var, double mat[3][3])
{
	char buf[1024];
	ssize_t curr = 0;

	P("%s = [\n", var);
	for (uint32_t row = 0; row < 3; row++) {
		P("\t");
		for (uint32_t col = 0; col < 3; col++) {
			P("%f", mat[row][col]);
			if (col < 2) {
				P(", ");
			}
		}
		P("\n");
	}
	P("\t]");

	U_LOG_RAW("%s", buf);
}

static void
dump_vector(const char *var, double vec[3])
{
	char buf[1024];
	ssize_t curr = 0;

	P("%s = [", var);
	for (uint32_t col = 0; col < 3; col++) {
		P("%f", vec[col]);
		if (col < 2) {
			P(", ");
		}
	}
	P("]");

	U_LOG_RAW("%s", buf);
}

static void
dump_size(const char *var, struct xrt_size size)
{
	U_LOG_RAW("%s = [%ux%u]", var, size.w, size.h);
}

static void
dump_distortion(const char *prefix, struct t_camera_calibration *view)
{
	char buf[1024];
	ssize_t curr = 0;

	U_LOG_RAW("%s.use_fisheye = %s", prefix, view->use_fisheye ? "true" : "false");

	if (view->use_fisheye) {
		P("%s.distortion_fisheye = [", prefix);
		for (uint32_t col = 0; col < 4; col++) {
			P("%f", view->distortion_fisheye[col]);
			if (col < 3) {
				P(", ");
			}
		}
		P("]");
	} else {
		P("%s.distortion = [", prefix);
		for (uint32_t col = 0; col < view->distortion_num; col++) {
			P("%f", view->distortion[col]);
			if (col < view->distortion_num - 1) {
				P(", ");
			}
		}
		P("]");
	}
	U_LOG_RAW("%s", buf);
}


/*
 *
 * 'Exported' functions.
 *
 */

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

void
t_stereo_camera_calibration_dump(struct t_stereo_camera_calibration *c)
{
	dump_size("view[0].image_size_pixels", c->view[0].image_size_pixels);
	dump_mat("view[0].intrinsic", c->view[0].intrinsics);
	dump_distortion("view[0]", &c->view[0]);
	dump_size("view[1].image_size_pixels", c->view[0].image_size_pixels);
	dump_mat("view[1].intrinsic", c->view[1].intrinsics);
	dump_distortion("view[1]", &c->view[1]);
	dump_vector("camera_translation", c->camera_translation);
	dump_mat("camera_rotation", c->camera_rotation);
}
