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
#include "util/u_var.h"
#include "util/u_logging.h"
#include "util/u_time.h"

#include "math/m_api.h"
#include "math/m_space.h"
#include "math/m_mathinclude.h"

#include "xrt/xrt_device.h"

#include <stdio.h>
#include <assert.h>

#define QWERTY_HMD_INITIAL_MOVEMENT_SPEED 0.002f // in meters per frame
#define QWERTY_HMD_INITIAL_LOOK_SPEED 0.02f      // in radians per frame
#define QWERTY_CONTROLLER_INITIAL_MOVEMENT_SPEED 0.005f
#define QWERTY_CONTROLLER_INITIAL_LOOK_SPEED 0.05f
#define MOVEMENT_SPEED_STEP 1.25f // Multiplier for how fast will mov speed increase/decrease
#define SPRINT_STEPS 5            // Amount of MOVEMENT_SPEED_STEPs to increase when sprinting

// clang-format off
// Values copied from u_device_setup_tracking_origins. CONTROLLER relative to HMD.
#define QWERTY_HMD_INITIAL_POS (struct xrt_vec3){0, 1.6f, 0}
#define QWERTY_CONTROLLER_INITIAL_POS(is_left) (struct xrt_vec3){(is_left) ? -0.2f : 0.2f, -0.3f, -0.5f}
// clang-format on

// Indices for fake controller input components
#define QWERTY_SELECT 0
#define QWERTY_MENU 1
#define QWERTY_GRIP 2
#define QWERTY_AIM 3
#define QWERTY_VIBRATION 0

#define QWERTY_TRACE(qd, ...) U_LOG_XDEV_IFL_T(&qd->base, qd->sys->log_level, __VA_ARGS__)
#define QWERTY_DEBUG(qd, ...) U_LOG_XDEV_IFL_D(&qd->base, qd->sys->log_level, __VA_ARGS__)
#define QWERTY_INFO(qd, ...) U_LOG_XDEV_IFL_I(&qd->base, qd->sys->log_level, __VA_ARGS__)
#define QWERTY_WARN(qd, ...) U_LOG_XDEV_IFL_W(&qd->base, qd->sys->log_level, __VA_ARGS__)
#define QWERTY_ERROR(qd, ...) U_LOG_XDEV_IFL_E(&qd->base, qd->sys->log_level, __VA_ARGS__)

static void
qwerty_system_remove(struct qwerty_system *qs, struct qwerty_device *qd);

static void
qwerty_system_destroy(struct qwerty_system *qs);

// Compare any two pointers without verbose casts
static inline bool
eq(void *a, void *b)
{
	return a == b;
}

// xrt_device functions

struct qwerty_device *
qwerty_device(struct xrt_device *xd)
{
	struct qwerty_device *qd = (struct qwerty_device *)xd;
	bool is_qwerty_device = eq(qd, qd->sys->hmd) || eq(qd, qd->sys->lctrl) || eq(qd, qd->sys->rctrl);
	assert(is_qwerty_device);
	if (!is_qwerty_device) {
		return NULL;
	}
	return qd;
}

struct qwerty_hmd *
qwerty_hmd(struct xrt_device *xd)
{
	struct qwerty_hmd *qh = (struct qwerty_hmd *)xd;
	bool is_qwerty_hmd = eq(qh, qh->base.sys->hmd);
	assert(is_qwerty_hmd);
	if (!is_qwerty_hmd) {
		return NULL;
	}
	return qh;
}

struct qwerty_controller *
qwerty_controller(struct xrt_device *xd)
{
	struct qwerty_controller *qc = (struct qwerty_controller *)xd;
	bool is_qwerty_controller = eq(qc, qc->base.sys->lctrl) || eq(qc, qc->base.sys->rctrl);
	assert(is_qwerty_controller);
	if (!is_qwerty_controller) {
		return NULL;
	}
	return qc;
}

static void
qwerty_update_inputs(struct xrt_device *xd)
{
	if (xd->name != XRT_DEVICE_SIMPLE_CONTROLLER) {
		return;
	}

	struct qwerty_controller *qc = qwerty_controller(xd);
	struct qwerty_device *qd = &qc->base;

	xd->inputs[QWERTY_SELECT].value.boolean = qc->select_clicked;
	if (qc->select_clicked) {
		QWERTY_INFO(qd, "[%s] Select click", xd->str);
		qc->select_clicked = false;
	}

	xd->inputs[QWERTY_MENU].value.boolean = qc->menu_clicked;
	if (qc->menu_clicked) {
		QWERTY_INFO(qd, "[%s] Menu click", xd->str);
		qc->menu_clicked = false;
	}
}

static void
qwerty_set_output(struct xrt_device *xd, enum xrt_output_name name, const union xrt_output_value *value)
{
	struct qwerty_device *qd = qwerty_device(xd);
	float frequency = value->vibration.frequency;
	float amplitude = value->vibration.amplitude;
	time_duration_ns duration = value->vibration.duration_ns;
	if (amplitude || duration || frequency) {
		QWERTY_INFO(qd,
		            "[%s] Haptic output: \n"
		            "\tfrequency=%.2f amplitude=%.2f duration=%ld",
		            xd->str, frequency, amplitude, duration);
	}
}

static void
qwerty_get_tracked_pose(struct xrt_device *xd,
                        enum xrt_input_name name,
                        uint64_t at_timestamp_ns,
                        struct xrt_space_relation *out_relation)
{
	struct qwerty_device *qd = qwerty_device(xd);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE && name != XRT_INPUT_SIMPLE_GRIP_POSE &&
	    name != XRT_INPUT_SIMPLE_AIM_POSE) {
		QWERTY_ERROR(qd, "Unexpected input name = 0x%04X", name >> 8);
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

	struct xrt_quat x_rotation;
	struct xrt_quat y_rotation;
	const struct xrt_vec3 x_axis = XRT_VEC3_UNIT_X;
	const struct xrt_vec3 y_axis = XRT_VEC3_UNIT_Y;
	math_quat_from_angle_vector(x_look_speed, &x_axis, &x_rotation);
	math_quat_from_angle_vector(y_look_speed, &y_axis, &y_rotation);
	math_quat_rotate(&qd->pose.orientation, &x_rotation, &qd->pose.orientation); // local-space pitch
	math_quat_rotate(&y_rotation, &qd->pose.orientation, &qd->pose.orientation); // base-space yaw
	math_quat_normalize(&qd->pose.orientation);

	// HMD Parenting

	bool qd_is_ctrl = name == XRT_INPUT_SIMPLE_GRIP_POSE || name == XRT_INPUT_SIMPLE_AIM_POSE;
	struct qwerty_controller *qc = qd_is_ctrl ? qwerty_controller(&qd->base) : NULL;
	if (qd_is_ctrl && qc->follow_hmd) {
		struct xrt_relation_chain relation_chain = {0};
		struct qwerty_device *qd_hmd = &qd->sys->hmd->base;
		m_relation_chain_push_pose(&relation_chain, &qd->pose);     // controller pose
		m_relation_chain_push_pose(&relation_chain, &qd_hmd->pose); // base space is hmd space
		m_relation_chain_resolve(&relation_chain, out_relation);
	} else {
		out_relation->pose = qd->pose;
	}
	out_relation->relation_flags =
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
}

static void
qwerty_get_view_poses(struct xrt_device *xdev,
                      const struct xrt_vec3 *default_eye_relation,
                      uint64_t at_timestamp_ns,
                      uint32_t view_count,
                      struct xrt_space_relation *out_head_relation,
                      struct xrt_fov *out_fovs,
                      struct xrt_pose *out_poses)
{
	u_device_get_view_poses(xdev, default_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs,
	                        out_poses);
}

static void
qwerty_destroy(struct xrt_device *xd)
{
	// Note: do not destroy a single device of a qwerty system or its var tracking
	// ui will make a null reference
	struct qwerty_device *qd = qwerty_device(xd);
	qwerty_system_remove(qd->sys, qd);
	u_device_free(xd);
}

struct qwerty_hmd *
qwerty_hmd_create(void)
{
	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE;
	size_t input_count = 1;
	size_t output_count = 0;
	struct qwerty_hmd *qh = U_DEVICE_ALLOCATE(struct qwerty_hmd, flags, input_count, output_count);
	assert(qh);

	struct qwerty_device *qd = &qh->base;
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
	info.fov[0] = 85.0f * (M_PI / 180.0f);
	info.fov[1] = 85.0f * (M_PI / 180.0f);

	if (!u_device_setup_split_side_by_side(xd, &info)) {
		QWERTY_ERROR(qd, "Failed to setup HMD properties");
		qwerty_destroy(xd);
		assert(false);
		return NULL;
	}

	xd->tracking_origin->type = XRT_TRACKING_TYPE_OTHER;
	snprintf(xd->tracking_origin->name, XRT_TRACKING_NAME_LEN, QWERTY_HMD_TRACKER_STR);

	xd->inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	xd->update_inputs = qwerty_update_inputs;
	xd->get_tracked_pose = qwerty_get_tracked_pose;
	xd->get_view_poses = qwerty_get_view_poses;
	xd->destroy = qwerty_destroy;
	u_distortion_mesh_set_none(xd); // Fill in xd->compute_distortion()

	return qh;
}

struct qwerty_controller *
qwerty_controller_create(bool is_left, struct qwerty_hmd *qhmd)
{
	struct qwerty_controller *qc = U_DEVICE_ALLOCATE(struct qwerty_controller, U_DEVICE_ALLOC_TRACKING_NONE, 4, 1);
	assert(qc);
	qc->select_clicked = false;
	qc->menu_clicked = false;
	qc->follow_hmd = qhmd != NULL;

	struct qwerty_device *qd = &qc->base;
	qd->pose.orientation.w = 1.f;
	qd->pose.position = QWERTY_CONTROLLER_INITIAL_POS(is_left);
	qd->movement_speed = QWERTY_CONTROLLER_INITIAL_MOVEMENT_SPEED;
	qd->look_speed = QWERTY_CONTROLLER_INITIAL_LOOK_SPEED;

	struct xrt_device *xd = &qd->base;

	xd->name = XRT_DEVICE_SIMPLE_CONTROLLER;
	xd->device_type = is_left ? XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER : XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER;

	char *controller_name = is_left ? QWERTY_LEFT_STR : QWERTY_RIGHT_STR;
	snprintf(xd->str, XRT_DEVICE_NAME_LEN, "%s", controller_name);
	snprintf(xd->serial, XRT_DEVICE_NAME_LEN, "%s", controller_name);

	xd->tracking_origin->type = XRT_TRACKING_TYPE_OTHER;
	char *tracker_name = is_left ? QWERTY_LEFT_TRACKER_STR : QWERTY_RIGHT_TRACKER_STR;
	snprintf(xd->tracking_origin->name, XRT_TRACKING_NAME_LEN, "%s", tracker_name);

	xd->inputs[QWERTY_SELECT].name = XRT_INPUT_SIMPLE_SELECT_CLICK;
	xd->inputs[QWERTY_MENU].name = XRT_INPUT_SIMPLE_MENU_CLICK;
	xd->inputs[QWERTY_GRIP].name = XRT_INPUT_SIMPLE_GRIP_POSE;
	xd->inputs[QWERTY_AIM].name = XRT_INPUT_SIMPLE_AIM_POSE; //!< @todo: aim input not implemented
	xd->outputs[QWERTY_VIBRATION].name = XRT_OUTPUT_NAME_SIMPLE_VIBRATION;

	xd->update_inputs = qwerty_update_inputs;
	xd->get_tracked_pose = qwerty_get_tracked_pose;
	xd->set_output = qwerty_set_output;
	xd->destroy = qwerty_destroy;

	return qc;
}

// System methods

static void
qwerty_setup_var_tracking(struct qwerty_system *qs)
{
	struct qwerty_device *qd_hmd = qs->hmd ? &qs->hmd->base : NULL;
	struct qwerty_device *qd_left = &qs->lctrl->base;
	struct qwerty_device *qd_right = &qs->rctrl->base;

	u_var_add_root(qs, "Qwerty System", true);
	u_var_add_log_level(qs, &qs->log_level, "Log level");
	u_var_add_bool(qs, &qs->process_keys, "process_keys");

	u_var_add_ro_text(qs, "", "Focused Device");
	if (qd_hmd) {
		u_var_add_bool(qs, &qs->hmd_focused, "HMD Focused");
	}
	u_var_add_bool(qs, &qs->lctrl_focused, "Left Controller Focused");
	u_var_add_bool(qs, &qs->rctrl_focused, "Right Controller Focused");

	if (qd_hmd) {
		u_var_add_gui_header(qs, NULL, qd_hmd->base.str);
		u_var_add_pose(qs, &qd_hmd->pose, "hmd.pose");
		u_var_add_f32(qs, &qd_hmd->movement_speed, "hmd.movement_speed");
		u_var_add_f32(qs, &qd_hmd->look_speed, "hmd.look_speed");
	}

	u_var_add_gui_header(qs, NULL, qd_left->base.str);
	u_var_add_pose(qs, &qd_left->pose, "left.pose");
	u_var_add_f32(qs, &qd_left->movement_speed, "left.movement_speed");
	u_var_add_f32(qs, &qd_left->look_speed, "left.look_speed");

	u_var_add_gui_header(qs, NULL, qd_right->base.str);
	u_var_add_pose(qs, &qd_right->pose, "right.pose");
	u_var_add_f32(qs, &qd_right->movement_speed, "right.movement_speed");
	u_var_add_f32(qs, &qd_right->look_speed, "right.look_speed");

	u_var_add_gui_header(qs, NULL, "Help");
	u_var_add_ro_text(qs, "FD: focused device. FC: focused controller.", "Notation");
	u_var_add_ro_text(qs, "HMD is FD by default. Right is FC by default", "Defaults");
	u_var_add_ro_text(qs, "Hold left/right FD", "LCTRL/LALT");
	u_var_add_ro_text(qs, "Move FD", "WASDQE");
	u_var_add_ro_text(qs, "Rotate FD", "Arrow keys");
	u_var_add_ro_text(qs, "Rotate FD", "Hold right click");
	u_var_add_ro_text(qs, "Hold for movement speed", "LSHIFT");
	u_var_add_ro_text(qs, "Modify FD movement speed", "Mouse wheel");
	u_var_add_ro_text(qs, "Modify FD movement speed", "Numpad +/-");
	u_var_add_ro_text(qs, "Reset both or FC pose", "R");
	u_var_add_ro_text(qs, "Toggle both or FC parenting to HMD", "F");
	u_var_add_ro_text(qs, "FC Select click", "Left Click");
	u_var_add_ro_text(qs, "FC Menu click", "Middle Click");
}

struct qwerty_system *
qwerty_system_create(struct qwerty_hmd *qhmd,
                     struct qwerty_controller *qleft,
                     struct qwerty_controller *qright,
                     enum u_logging_level log_level)
{
	assert(qleft && "Cannot create a qwerty system when Left controller is NULL");
	assert(qright && "Cannot create a qwerty system when Right controller is NULL");

	struct qwerty_system *qs = U_TYPED_CALLOC(struct qwerty_system);
	qs->hmd = qhmd;
	qs->lctrl = qleft;
	qs->rctrl = qright;
	qs->log_level = log_level;
	qs->process_keys = true;

	if (qhmd) {
		qhmd->base.sys = qs;
	}
	qleft->base.sys = qs;
	qright->base.sys = qs;

	qwerty_setup_var_tracking(qs);

	return qs;
}

static void
qwerty_system_remove(struct qwerty_system *qs, struct qwerty_device *qd)
{
	if (eq(qd, qs->hmd)) {
		qs->hmd = NULL;
	} else if (eq(qd, qs->lctrl)) {
		qs->lctrl = NULL;
	} else if (eq(qd, qs->rctrl)) {
		qs->rctrl = NULL;
	} else {
		assert(false && "Trying to remove a device that is not in the qwerty system");
	}

	bool all_devices_clean = !qs->hmd && !qs->lctrl && !qs->rctrl;
	if (all_devices_clean) {
		qwerty_system_destroy(qs);
	}
}

static void
qwerty_system_destroy(struct qwerty_system *qs)
{
	bool all_devices_clean = !qs->hmd && !qs->lctrl && !qs->rctrl;
	assert(all_devices_clean && "Tried to destroy a qwerty_system without destroying its devices before.");
	if (!all_devices_clean) {
		return;
	}
	u_var_remove_root(qs);
	free(qs);
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

void
qwerty_release_all(struct qwerty_device *qd)
{
	qd->left_pressed = false;
	qd->right_pressed = false;
	qd->forward_pressed = false;
	qd->backward_pressed = false;
	qd->up_pressed = false;
	qd->down_pressed = false;
	qd->look_left_pressed = false;
	qd->look_right_pressed = false;
	qd->look_up_pressed = false;
	qd->look_down_pressed = false;
	qd->sprint_pressed = false;
	qd->yaw_delta = 0;
	qd->pitch_delta = 0;
}

// Controller methods

// clang-format off
void qwerty_select_click(struct qwerty_controller *qc) { qc->select_clicked = true; }
void qwerty_menu_click(struct qwerty_controller *qc) { qc->menu_clicked = true; }
// clang-format on

void
qwerty_follow_hmd(struct qwerty_controller *qc, bool follow)
{
	struct qwerty_device *qd = &qc->base;
	bool no_qhmd = !qd->sys->hmd;
	bool unchanged = qc->follow_hmd == follow;
	if (no_qhmd || unchanged) {
		return;
	}

	struct qwerty_device *qd_hmd = &qd->sys->hmd->base;
	struct xrt_relation_chain chain = {0};
	struct xrt_space_relation rel = XRT_SPACE_RELATION_ZERO;

	m_relation_chain_push_pose(&chain, &qd->pose);
	if (follow) { // From global to hmd
		m_relation_chain_push_inverted_pose_if_not_identity(&chain, &qd_hmd->pose);
	} else { // From hmd to global
		m_relation_chain_push_pose(&chain, &qd_hmd->pose);
	}
	m_relation_chain_resolve(&chain, &rel);

	qd->pose = rel.pose;
	qc->follow_hmd = follow;
}

void
qwerty_reset_controller_pose(struct qwerty_controller *qc)
{
	struct qwerty_device *qd = &qc->base;

	bool no_qhmd = !qd->sys->hmd;
	if (no_qhmd) {
		return;
	}

	bool is_left = qc == qd->sys->lctrl;

	qwerty_follow_hmd(qc, true);
	struct xrt_pose pose = {XRT_QUAT_IDENTITY, QWERTY_CONTROLLER_INITIAL_POS(is_left)};
	qd->pose = pose;
}
