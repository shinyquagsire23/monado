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

#define QWERTY_HMD_INITIAL_MOVEMENT_SPEED 0.002f // in meters per frame
#define QWERTY_HMD_INITIAL_LOOK_SPEED 0.02f      // in radians per frame
#define MOVEMENT_SPEED_STEP 1.25f // Multiplier for how fast will mov speed increase/decrease
#define SPRINT_STEPS 5            // Amount of MOVEMENT_SPEED_STEPs to increase when sprinting

// clang-format off
// Value copied from u_device_setup_tracking_origins.
#define QWERTY_HMD_INITIAL_POS (struct xrt_vec3){0, 1.6f, 0}
// clang-format on

// xrt_device functions

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
	struct qwerty_device *qd = qwerty_device(xd);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		printf("Unexpected input name = 0x%04X\n", name >> 8); // @todo: use u_logging.h
		return;
	}

	// Position

	float sprint_boost = qd->sprint_pressed ? powf(MOVEMENT_SPEED_STEP, SPRINT_STEPS) : 1;
	float mov_speed = qd->movement_speed * sprint_boost;
	struct xrt_vec3 pos_delta = {
	    mov_speed * (qd->right_pressed - qd->left_pressed),
	    0, // Up/down movement will be relative to base space
	    mov_speed * (qd->backward_pressed - qd->forward_pressed),
	};
	math_quat_rotate_vec3(&qd->pose.orientation, &pos_delta, &pos_delta);
	pos_delta.y += mov_speed * (qd->up_pressed - qd->down_pressed);
	math_vec3_accum(&pos_delta, &qd->pose.position);

	// Orientation

	// View rotation caused by keys
	float y_look_speed = qd->look_speed * (qd->look_left_pressed - qd->look_right_pressed);
	float x_look_speed = qd->look_speed * (qd->look_up_pressed - qd->look_down_pressed);

	// View rotation caused by mouse
	y_look_speed += qd->yaw_delta;
	x_look_speed += qd->pitch_delta;
	qd->yaw_delta = 0;
	qd->pitch_delta = 0;

	struct xrt_quat x_rotation, y_rotation;
	struct xrt_vec3 x_axis = {1, 0, 0}, y_axis = {0, 1, 0};
	math_quat_from_angle_vector(x_look_speed, &x_axis, &x_rotation);
	math_quat_from_angle_vector(y_look_speed, &y_axis, &y_rotation);
	math_quat_rotate(&qd->pose.orientation, &x_rotation, &qd->pose.orientation); // local-space pitch
	math_quat_rotate(&y_rotation, &qd->pose.orientation, &qd->pose.orientation); // base-space yaw
	math_quat_normalize(&qd->pose.orientation);

	out_relation->pose = qd->pose;
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

	qd->pose.orientation.w = 1.f;
	qd->pose.position = QWERTY_HMD_INITIAL_POS;
	qd->movement_speed = QWERTY_HMD_INITIAL_MOVEMENT_SPEED;
	qd->look_speed = QWERTY_HMD_INITIAL_LOOK_SPEED;

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

// Device methods

// clang-format off
void qwerty_press_left(struct qwerty_device *qd) { qd->left_pressed = true; }
void qwerty_release_left(struct qwerty_device *qd) { qd->left_pressed = false; }
void qwerty_press_right(struct qwerty_device *qd) { qd->right_pressed = true; }
void qwerty_release_right(struct qwerty_device *qd) { qd->right_pressed = false; }
void qwerty_press_forward(struct qwerty_device *qd) { qd->forward_pressed = true; }
void qwerty_release_forward(struct qwerty_device *qd) { qd->forward_pressed = false; }
void qwerty_press_backward(struct qwerty_device *qd) { qd->backward_pressed = true; }
void qwerty_release_backward(struct qwerty_device *qd) { qd->backward_pressed = false; }
void qwerty_press_up(struct qwerty_device *qd) { qd->up_pressed = true; }
void qwerty_release_up(struct qwerty_device *qd) { qd->up_pressed = false; }
void qwerty_press_down(struct qwerty_device *qd) { qd->down_pressed = true; }
void qwerty_release_down(struct qwerty_device *qd) { qd->down_pressed = false; }

void qwerty_press_look_left(struct qwerty_device *qd) { qd->look_left_pressed = true; }
void qwerty_release_look_left(struct qwerty_device *qd) { qd->look_left_pressed = false; }
void qwerty_press_look_right(struct qwerty_device *qd) { qd->look_right_pressed = true; }
void qwerty_release_look_right(struct qwerty_device *qd) { qd->look_right_pressed = false; }
void qwerty_press_look_up(struct qwerty_device *qd) { qd->look_up_pressed = true; }
void qwerty_release_look_up(struct qwerty_device *qd) { qd->look_up_pressed = false; }
void qwerty_press_look_down(struct qwerty_device *qd) { qd->look_down_pressed = true; }
void qwerty_release_look_down(struct qwerty_device *qd) { qd->look_down_pressed = false; }
// clang-format on

void
qwerty_press_sprint(struct qwerty_device *qd)
{
	qd->sprint_pressed = true;
}
void
qwerty_release_sprint(struct qwerty_device *qd)
{
	qd->sprint_pressed = false;
}

void
qwerty_add_look_delta(struct qwerty_device *qd, float yaw, float pitch)
{
	qd->yaw_delta += yaw * qd->look_speed;
	qd->pitch_delta += pitch * qd->look_speed;
}

void
qwerty_change_movement_speed(struct qwerty_device *qd, float steps)
{
	qd->movement_speed *= powf(MOVEMENT_SPEED_STEP, steps);
}
