// Copyright 2021, Mateo de Mayo.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Implementation of qwerty_device related methods.
 * @author Mateo de Mayo <mateodemayo@gmail.com>
 * @ingroup drv_qwerty
 */

#include "qwerty_device.h"

#include "util/u_device.h"

#include "util/u_distortion_mesh.h"

#include "math/m_api.h"
#include "math/m_mathinclude.h"

#include "xrt/xrt_device.h"

#include <stdio.h>
#include <assert.h>

struct qwerty_device *
qwerty_device(struct xrt_device *xd)
{
	struct qwerty_device *qd = (struct qwerty_device *)xd;
	assert(qd);
	return qd;
}

static void
qwerty_update_inputs(struct xrt_device *xd)
{
	return;
}

static void
qwerty_get_tracked_pose(struct xrt_device *xd,
                        enum xrt_input_name name,
                        uint64_t at_timestamp_ns,
                        struct xrt_space_relation *out_relation)
{
	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		printf("Unexpected input name = 0x%04X\n", name >> 8); // @todo: use u_logging.h
		return;
	}

	struct xrt_pose identity = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
	out_relation->pose = identity;
	out_relation->relation_flags =
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
}

static void
qwerty_get_view_pose(struct xrt_device *xd,
                     struct xrt_vec3 *eye_relation,
                     uint32_t view_index,
                     struct xrt_pose *out_pose)
{
	// Adapted from dummy_hmd_get_view_pose()
	struct xrt_pose pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
	bool is_left = view_index == 0;
	float adjust = is_left ? -0.5f : 0.5f;
	struct xrt_vec3 eye_offset = *eye_relation;
	math_vec3_scalar_mul(adjust, &eye_offset);
	math_vec3_accum(&eye_offset, &pose.position);
	*out_pose = pose;
}

static void
qwerty_destroy(struct xrt_device *xd)
{
	u_device_free(xd);
}

struct qwerty_device *
qwerty_hmd_create(void)
{
	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE;
	size_t num_inputs = 1, num_outputs = 0;
	struct qwerty_device *qd = U_DEVICE_ALLOCATE(struct qwerty_device, flags, num_inputs, num_outputs);
	assert(qd);

	struct xrt_device *xd = &qd->base;
	xd->name = XRT_DEVICE_GENERIC_HMD;
	xd->device_type = XRT_DEVICE_TYPE_HMD;

	snprintf(xd->str, XRT_DEVICE_NAME_LEN, QWERTY_HMD_STR);
	snprintf(xd->serial, XRT_DEVICE_NAME_LEN, QWERTY_HMD_STR);

	// Fill in xd->hmd
	struct u_device_simple_info info;
	info.display.w_pixels = 1280;
	info.display.h_pixels = 720;
	info.display.w_meters = 0.13f;
	info.display.h_meters = 0.07f;
	info.lens_horizontal_separation_meters = 0.13f / 2.0f;
	info.lens_vertical_position_meters = 0.07f / 2.0f;
	info.views[0].fov = 85.0f * (M_PI / 180.0f);
	info.views[1].fov = 85.0f * (M_PI / 180.0f);

	if (!u_device_setup_split_side_by_side(xd, &info)) {
		printf("Failed to setup HMD properties\n"); // @todo: Use u_logging.h
		qwerty_destroy(xd);
		assert(false);
		return NULL;
	}

	xd->tracking_origin->type = XRT_TRACKING_TYPE_OTHER;
	snprintf(xd->tracking_origin->name, XRT_TRACKING_NAME_LEN, QWERTY_HMD_TRACKER_STR);

	xd->inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	xd->update_inputs = qwerty_update_inputs;
	xd->get_tracked_pose = qwerty_get_tracked_pose;
	xd->get_view_pose = qwerty_get_view_pose;
	xd->destroy = qwerty_destroy;
	u_distortion_mesh_set_none(xd); // Fill in xd->compute_distortion()

	return qd;
}
