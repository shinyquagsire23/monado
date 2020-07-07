// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: Apache-2.0
/*!
 * @file
 * @brief  Adapter to Libsurvive.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_survive
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string.h>

#include "math/m_api.h"
#include "xrt/xrt_device.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_device.h"

#include "../auxiliary/os/os_time.h"

#include "xrt/xrt_prober.h"
#include "survive_interface.h"

#include "survive_api.h"

#include "survive_wrap.h"

#include "util/u_json.h"

#define SURVIVE_SPEW(p, ...)                                                   \
	do {                                                                   \
		if (p->print_spew) {                                           \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define SURVIVE_DEBUG(p, ...)                                                  \
	do {                                                                   \
		if (p->print_debug) {                                          \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define SURVIVE_ERROR(p, ...)                                                  \
	do {                                                                   \
		fprintf(stderr, "%s - ", __func__);                            \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, "\n");                                         \
	} while (false)

struct survive_system;

enum input_index
{
	VIVE_CONTROLLER_INDEX_AIM_POSE = 0,
	VIVE_CONTROLLER_INDEX_GRIP_POSE,
	VIVE_CONTROLLER_INDEX_SYSTEM_CLICK,
	VIVE_CONTROLLER_INDEX_TRIGGER_CLICK,
	VIVE_CONTROLLER_INDEX_TRIGGER_VALUE,
	VIVE_CONTROLLER_INDEX_TRACKPAD,
	VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH,

	// Vive Wand specific inputs
	VIVE_CONTROLLER_INDEX_SQUEEZE_CLICK,
	VIVE_CONTROLLER_INDEX_MENU_CLICK,
	VIVE_CONTROLLER_INDEX_TRACKPAD_CLICK,

	// Valve Index specific inputs
	VIVE_CONTROLLER_INDEX_THUMBSTICK,
	VIVE_CONTROLLER_INDEX_A_CLICK,
	VIVE_CONTROLLER_INDEX_B_CLICK,
	VIVE_CONTROLLER_INDEX_THUMBSTICK_CLICK,

	// Valve Index Gen2, not yet supported
	VIVE_CONTROLLER_INDEX_THUMBSTICK_TOUCH,
	VIVE_CONTROLLER_INDEX_SYSTEM_TOUCH,
	VIVE_CONTROLLER_INDEX_A_TOUCH,
	VIVE_CONTROLLER_INDEX_B_TOUCH,
	VIVE_CONTROLLER_INDEX_SQUEEZE_VALUE,
	VIVE_CONTROLLER_INDEX_SQUEEZE_FORCE,
	VIVE_CONTROLLER_INDEX_TRIGGER_TOUCH,
	VIVE_CONTROLLER_INDEX_TRACKPAD_FORCE,

	VIVE_CONTROLLER_MAX_INDEX,
};

// also used as index in sys->controllers[] array
typedef enum
{
	SURVIVE_LEFT_CONTROLLER = 0,
	SURVIVE_RIGHT_CONTROLLER = 1,
	SURVIVE_HMD = 2,
} SurviveDeviceType;

static bool survive_already_initialized = false;

#define MAX_PENDING_EVENTS 30

/*!
 * @implements xrt_device
 */
struct survive_device
{
	struct xrt_device base;
	struct survive_system *sys;
	const SurviveSimpleObject *survive_obj;

	/* event needs to be processed if
	 * type != SurviveSimpleEventType_None */
	struct SurviveSimpleEvent pending_events[30];
	int num;

	struct xrt_quat rot[2];
};

enum VIVE_VARIANT
{
	VIVE_UNKNOWN = 0,
	VIVE_VARIANT_VIVE,
	VIVE_VARIANT_PRO,
	VIVE_VARIANT_INDEX
};

/*!
 * @extends xrt_tracking_origin
 */
struct survive_system
{
	struct xrt_tracking_origin base;
	SurviveSimpleContext *ctx;
	struct survive_device *hmd;
	struct survive_device *controllers[2];
	bool print_spew;
	bool print_debug;
	enum VIVE_VARIANT variant;
};

static void
survive_device_destroy(struct xrt_device *xdev)
{
	printf("destroying survive device\n");
	struct survive_device *survive = (struct survive_device *)xdev;

	if (survive == survive->sys->hmd)
		survive->sys->hmd = NULL;
	if (survive == survive->sys->controllers[SURVIVE_LEFT_CONTROLLER])
		survive->sys->controllers[SURVIVE_LEFT_CONTROLLER] = NULL;
	if (survive == survive->sys->controllers[SURVIVE_RIGHT_CONTROLLER])
		survive->sys->controllers[SURVIVE_RIGHT_CONTROLLER] = NULL;

	if (survive->sys->hmd == NULL &&
	    survive->sys->controllers[SURVIVE_LEFT_CONTROLLER] == NULL &&
	    survive->sys->controllers[SURVIVE_RIGHT_CONTROLLER] == NULL) {
		printf("Tearing down libsurvive context\n");
		survive_simple_close(survive->sys->ctx);

		free(survive->sys);
	}

	free(survive);
}

static void
_get_survive_pose(const SurviveSimpleObject *survive_object,
                  SurviveSimpleContext *ctx,
                  uint64_t *out_relation_timestamp_ns,
                  struct xrt_space_relation *out_relation)
{
	int64_t now = os_monotonic_get_ns();
	//! @todo adjust for latency here
	*out_relation_timestamp_ns = now;

	out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;

	if (survive_simple_object_get_type(survive_object) !=
	        SurviveSimpleObject_OBJECT &&
	    survive_simple_object_get_type(survive_object) !=
	        SurviveSimpleObject_HMD) {
		return;
	}

	SurvivePose pose;

	uint32_t timecode =
	    survive_simple_object_get_latest_pose(survive_object, &pose);

	(void)timecode;

	struct xrt_quat out_rot = {.x = pose.Rot[1],
	                           .y = pose.Rot[2],
	                           .z = pose.Rot[3],
	                           .w = pose.Rot[0]};
	/*
	printf("quat %f %f %f %f\n", out_rot.x, out_rot.y, out_rot.z,
	out_rot.w);
	*/

	/* libsurvive looks down when it should be looking forward, so
	 * rotate the quat.
	 * because the HMD quat is the opposite of the in world
	 * rotation, we rotate down. */

	struct xrt_quat down_rot;
	down_rot.x = sqrtf(2) / 2.;
	down_rot.y = 0;
	down_rot.z = 0;
	down_rot.w = -sqrtf(2) / 2.;

	math_quat_rotate(&down_rot, &out_rot, &out_rot);

	// just to be sure
	math_quat_normalize(&out_rot);


	SurviveVelocity vel;
	timecode =
	    survive_simple_object_get_latest_velocity(survive_object, &vel);

	out_relation->pose.orientation = out_rot;

	/* because the quat is rotated, y and z axes are switched. */
	out_relation->pose.position.x = pose.Pos[0];
	out_relation->pose.position.y = pose.Pos[2];
	out_relation->pose.position.z = -pose.Pos[1];

	struct xrt_vec3 linear_vel = {
	    .x = vel.Pos[0], .y = vel.Pos[2], .z = -vel.Pos[1]};

	struct xrt_vec3 angular_vel = {.x = vel.AxisAngleRot[0],
	                               .y = vel.AxisAngleRot[2],
	                               .z = -vel.AxisAngleRot[1]};

	if (math_quat_validate(&out_rot)) {
		out_relation->relation_flags |=
		    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
		    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;
	}

	if (math_vec3_validate(&out_relation->pose.position)) {
		out_relation->relation_flags |=
		    XRT_SPACE_RELATION_POSITION_VALID_BIT |
		    XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
	}

	out_relation->linear_velocity = linear_vel;
	if (math_vec3_validate(&out_relation->linear_velocity)) {
		out_relation->relation_flags |=
		    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT;
	}

	out_relation->angular_velocity = angular_vel;
	if (math_vec3_validate(&out_relation->angular_velocity)) {
		out_relation->relation_flags |=
		    XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT;
	}
}

static bool
_try_update_codenames(struct survive_system *sys)
{
	// TODO: better method

	if (sys->hmd->survive_obj && sys->controllers[0]->survive_obj &&
	    sys->controllers[1]->survive_obj)
		return true;

	SurviveSimpleContext *ctx = sys->ctx;

	for (const SurviveSimpleObject *it =
	         survive_simple_get_first_object(ctx);
	     it != 0; it = survive_simple_get_next_object(ctx, it)) {
		const char *codename = survive_simple_object_name(it);

		enum SurviveSimpleObject_type type =
		    survive_simple_object_get_type(it);
		if (type == SurviveSimpleObject_HMD &&
		    sys->hmd->survive_obj == NULL) {
			printf("Found HMD: %s\n", codename);
			sys->hmd->survive_obj = it;
		}
		if (type == SurviveSimpleObject_OBJECT) {
			for (int i = 0; i < 2 /* TODO */; i++) {
				if (sys->controllers[i]->survive_obj == it) {
					break;
				}

				if (sys->controllers[i]->survive_obj == NULL) {
					printf("Found Controller %d: %s\n", i,
					       codename);
					sys->controllers[i]->survive_obj = it;
					break;
				}
			}
		}
	}

	return true;
}

static void
survive_device_get_tracked_pose(struct xrt_device *xdev,
                                enum xrt_input_name name,
                                uint64_t at_timestamp_ns,
                                uint64_t *out_relation_timestamp_ns,
                                struct xrt_space_relation *out_relation)
{
	struct survive_device *survive = (struct survive_device *)xdev;
	if ((survive == survive->sys->hmd &&
	     name != XRT_INPUT_GENERIC_HEAD_POSE) ||
	    ((survive == survive->sys->controllers[0] ||
	      survive == survive->sys->controllers[1]) &&
	     (name != XRT_INPUT_INDEX_AIM_POSE &&
	      name != XRT_INPUT_INDEX_GRIP_POSE) &&
	     (name != XRT_INPUT_VIVE_AIM_POSE &&
	      name != XRT_INPUT_VIVE_GRIP_POSE))) {

		SURVIVE_ERROR(survive, "unknown input name");
		return;
	}

	_try_update_codenames(survive->sys);
	if (!survive->survive_obj) {
		// printf("Obj not set for %p\n", (void*)survive);
		return;
	}


	_get_survive_pose(survive->survive_obj, survive->sys->ctx,
	                  out_relation_timestamp_ns, out_relation);

	/*
	SURVIVE_SPEW(
	        survive,
	      "GET_POSITION (%f %f %f) GET_ORIENTATION (%f, %f, %f, %f)",
	             out_relation->pose.position.x,
	      out_relation->pose.position.y,
	      out_relation->pose.position.z, out_rot.x, out_rot.y,
	      out_rot.z, out_rot.w);
	      */
	// printf("Get pose %f %f %f\n", out_relation->pose.position.x,
	// out_relation->pose.position.y, out_relation->pose.position.z);
}

static void
survive_device_get_view_pose(struct xrt_device *xdev,
                             struct xrt_vec3 *eye_relation,
                             uint32_t view_index,
                             struct xrt_pose *out_pose)
{
	struct xrt_pose pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
	bool adjust = view_index == 0;

	struct survive_device *survive = (struct survive_device *)xdev;
	pose.orientation = survive->rot[view_index];
	pose.position.x = eye_relation->x / 2.0f;
	pose.position.y = eye_relation->y / 2.0f;
	pose.position.z = eye_relation->z / 2.0f;

	// Adjust for left/right while also making sure there aren't any -0.f.
	if (pose.position.x > 0.0f && adjust) {
		pose.position.x = -pose.position.x;
	}
	if (pose.position.y > 0.0f && adjust) {
		pose.position.y = -pose.position.y;
	}
	if (pose.position.z > 0.0f && adjust) {
		pose.position.z = -pose.position.z;
	}

	*out_pose = pose;
}

struct display_info
{
	float w_meters;
	float h_meters;
	int w_pixels;
	int h_pixels;
};

struct device_info
{
	struct display_info display;

	float lens_horizontal_separation;
	float lens_vertical_position;

	float pano_distortion_k[4];
	float pano_aberration_k[4];
	float pano_warp_scale;

	struct
	{
		float fov;

		struct display_info display;

		float lens_center_x_meters;
		float lens_center_y_meters;
	} views[2];
};

static void
survive_hmd_update_inputs(struct xrt_device *xdev)
{}

static void
_queue_event(struct survive_system *sys,
             SurviveSimpleObject *obj,
             SurviveSimpleEvent *event)
{
	struct survive_device *dev[] = {
	    sys->hmd,
	    sys->controllers[SURVIVE_LEFT_CONTROLLER],
	    sys->controllers[SURVIVE_RIGHT_CONTROLLER],
	};
	for (int i = 0; i < 3; i++) {
		if (dev[i] && dev[i]->survive_obj == obj) {
			int queue_index = 0;
			for (queue_index = 0; queue_index < MAX_PENDING_EVENTS;
			     queue_index++) {
				if (dev[i]
				        ->pending_events[queue_index]
				        .event_type ==
				    SurviveSimpleEventType_None) {
					break;
				}
			}
			if (queue_index == MAX_PENDING_EVENTS) {
				printf(
				    "Pending event queue full for device %d\n",
				    i);
				return;
			}
			// printf("Queue event for device %d at index %d\n", i,
			// queue_index);
			memcpy(&dev[i]->pending_events[queue_index], event,
			       sizeof(SurviveSimpleEvent));
		}
	}
}

static void
_process_event(struct survive_device *survive,
               struct SurviveSimpleEvent *event,
               const SurviveSimpleObject *current,
               int64_t now)
{
	// ??
	const int survive_btn_id_0 = 0;
	const int survive_trackpad_touch_btn_id_1 = 1;

	const int survive_squeeze_click_btn_id_2 = 2;

	const int survive_a_btn_id_4 = 4;
	const int survive_b_btn_id_5 = 5;

	const int survive_menu_btn_id_12 = 12;

	const int survive_trigger_click_btn_id_24 = 24;

	const int survive_trigger_axis_id = 1;

	/* xy either thumbstick or trackpad */
	const int survive_xy_axis_id_x = 2;
	const int survive_xy_axis_id_y = 3;

	switch (event->event_type) {
	case SurviveSimpleEventType_ButtonEvent: {
		const struct SurviveSimpleButtonEvent *e =
		    survive_simple_get_button_event(event);

		if (e->object != current) {
			if (e->event_type == SurviveSimpleEventType_None) {
				// no need to queue empty event
				return;
			}
			_queue_event(survive->sys, e->object, event);
			return;
		}

		/*
		printf("Btn id %d type %d, axes %d  ", e->button_id,
		e->event_type, e->axis_count); for (int i = 0; i <
		e->axis_count; i++) { printf("axis id: %d val %hu    ",
		e->axis_ids[i], e->axis_val[i]);
		}
		printf("\n");
		*/

		if (e->button_id == survive_btn_id_0) {
			for (int i = 0; i < e->axis_count; i++) {
				if (e->axis_ids[i] == survive_trigger_axis_id) {
					uint16_t raw = e->axis_val[0];
					float scaled = (float)raw / 32768.;

					survive->base
					    .inputs
					        [VIVE_CONTROLLER_INDEX_TRIGGER_VALUE]
					    .value.vec1.x = scaled;
					survive->base
					    .inputs
					        [VIVE_CONTROLLER_INDEX_TRIGGER_VALUE]
					    .timestamp = now;
					// printf("Trigger value %f %lu\n",
					// survive->base.inputs[TRIGGER_VALUE].value.vec1.x,
					// now);
				}
				if (e->axis_ids[i] == survive_xy_axis_id_x) {
					enum input_index input;
					if (survive->base
					        .inputs
					            [VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH]
					        .value.boolean) {
						input =
						    VIVE_CONTROLLER_INDEX_TRACKPAD;
					} else {
						input =
						    VIVE_CONTROLLER_INDEX_THUMBSTICK;
					}

					float x =
					    (float)((int16_t)e->axis_val[i]) /
					    32768.;
					survive->base.inputs[input]
					    .value.vec2.x = x;
					survive->base.inputs[input].timestamp =
					    now;
					// printf("x: %f\n", x);
				}
				if (e->axis_ids[i] == survive_xy_axis_id_y) {
					enum input_index input;
					if (survive->base
					        .inputs
					            [VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH]
					        .value.boolean) {
						input =
						    VIVE_CONTROLLER_INDEX_TRACKPAD;
					} else {
						input =
						    VIVE_CONTROLLER_INDEX_THUMBSTICK;
					}

					float y =
					    (float)((int16_t)e->axis_val[i]) /
					    32768.;
					survive->base.inputs[input]
					    .value.vec2.y = y;
					survive->base.inputs[input].timestamp =
					    now;
					// printf("y: %f\n", y);
				}
			}
		}

		if (e->button_id == survive_squeeze_click_btn_id_2) {
			survive->base
			    .inputs[VIVE_CONTROLLER_INDEX_SQUEEZE_CLICK]
			    .timestamp = now;
			survive->base
			    .inputs[VIVE_CONTROLLER_INDEX_SQUEEZE_CLICK]
			    .value.boolean = e->event_type == 1;
		}

		if (e->button_id == survive_trigger_click_btn_id_24) {
			// 1 = pressed, 2 = released
			// printf("trigger click %d\n", e->event_type);

			survive->base
			    .inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
			    .timestamp = now;
			survive->base
			    .inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
			    .value.boolean = e->event_type == 1;
			// printf("Trigger click %d\n",
			// survive->base.inputs[INDEX_TRIGGER_CLICK].value.boolean);
		}

		if (e->button_id == survive_a_btn_id_4) {
			survive->base.inputs[VIVE_CONTROLLER_INDEX_A_CLICK]
			    .timestamp = now;
			survive->base.inputs[VIVE_CONTROLLER_INDEX_A_CLICK]
			    .value.boolean = e->event_type == 1;
		}

		if (e->button_id == survive_b_btn_id_5) {
			survive->base.inputs[VIVE_CONTROLLER_INDEX_B_CLICK]
			    .timestamp = now;
			survive->base.inputs[VIVE_CONTROLLER_INDEX_B_CLICK]
			    .value.boolean = e->event_type == 1;
		}

		if (e->button_id == survive_menu_btn_id_12) {
			survive->base.inputs[VIVE_CONTROLLER_INDEX_MENU_CLICK]
			    .timestamp = now;
			survive->base.inputs[VIVE_CONTROLLER_INDEX_MENU_CLICK]
			    .value.boolean = e->event_type == 1;
		}

		if (e->button_id == survive_trackpad_touch_btn_id_1) {
			survive->base
			    .inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH]
			    .timestamp = now;
			survive->base
			    .inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH]
			    .value.boolean = e->event_type == 1;
		}

		break;
	}
	case SurviveSimpleEventType_None: break;
	}
}

static void
survive_device_update_inputs(struct xrt_device *xdev)
{
	struct survive_device *survive = (struct survive_device *)xdev;

	uint64_t now = os_monotonic_get_ns();

	const SurviveSimpleObject *current = survive->survive_obj;

	for (int i = 0; i < MAX_PENDING_EVENTS; i++) {
		struct SurviveSimpleEvent *event = &survive->pending_events[i];
		_process_event(survive, event, current, now);

		survive->pending_events[i].event_type =
		    SurviveSimpleEventType_None;
	}

	struct SurviveSimpleEvent event = {0};
	while (survive_simple_next_event(survive->sys->ctx, &event) !=
	       SurviveSimpleEventType_None) {
		_process_event(survive, &event, current, now);
	}
}

static bool
wait_for_hmd_config(SurviveSimpleContext *ctx)
{
	for (const SurviveSimpleObject *it =
	         survive_simple_get_first_object(ctx);
	     it != 0; it = survive_simple_get_next_object(ctx, it)) {

		enum SurviveSimpleObject_type type =
		    survive_simple_object_get_type(it);
		if (type == SurviveSimpleObject_HMD && survive_config_ready(it))
			return true;
	}
	return false;
}

void
print_vec3(const char *title, struct xrt_vec3 *vec)
{
	printf("%s = %f %f %f\n", title, (double)vec->x, (double)vec->y,
	       (double)vec->z);
}

static long long
_json_to_int(const cJSON *item)
{
	if (item != NULL) {
		return item->valueint;
	} else {
		return 0;
	}
}

static bool
_json_get_matrix_3x3(const cJSON *json,
                     const char *name,
                     struct xrt_matrix_3x3 *result)
{
	const cJSON *vec3_arr = cJSON_GetObjectItemCaseSensitive(json, name);

	// Some sanity checking.
	if (vec3_arr == NULL || cJSON_GetArraySize(vec3_arr) != 3) {
		return false;
	}

	size_t total = 0;
	const cJSON *vec = NULL;
	cJSON_ArrayForEach(vec, vec3_arr)
	{
		assert(cJSON_GetArraySize(vec) == 3);
		const cJSON *elem = NULL;
		cJSON_ArrayForEach(elem, vec)
		{
			// Just in case.
			if (total >= 9) {
				break;
			}

			assert(cJSON_IsNumber(elem));
			result->v[total++] = (float)elem->valuedouble;
		}
	}

	return true;
}

static float
_json_get_float(const cJSON *json, const char *name)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
	return (float)item->valuedouble;
}

static long long
_json_get_int(const cJSON *json, const char *name)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
	return _json_to_int(item);
}

static void
_get_color_coeffs(struct xrt_hmd_parts *hmd,
                  const cJSON *coeffs,
                  uint8_t eye,
                  uint8_t channel)
{
	// this is 4 on index, all values populated
	// assert(coeffs->length == 8);
	// only 3 coeffs contain values
	const cJSON *item = NULL;
	size_t i = 0;
	cJSON_ArrayForEach(item, coeffs)
	{
		hmd->distortion.vive.coefficients[eye][i][channel] =
		    (float)item->valuedouble;
		++i;
		if (i == 3) {
			break;
		}
	}
}

static void
_get_color_coeffs_lookup(struct xrt_hmd_parts *hmd,
                         const cJSON *eye_json,
                         const char *name,
                         uint8_t eye,
                         uint8_t channel)
{
	const cJSON *distortion =
	    cJSON_GetObjectItemCaseSensitive(eye_json, name);
	if (distortion == NULL) {
		return;
	}

	const cJSON *coeffs =
	    cJSON_GetObjectItemCaseSensitive(distortion, "coeffs");
	if (coeffs == NULL) {
		return;
	}

	_get_color_coeffs(hmd, coeffs, eye, channel);
}

static void
get_distortion_properties(struct survive_device *d,
                          const cJSON *eye_transform_json,
                          uint8_t eye)
{
	struct xrt_hmd_parts *hmd = d->base.hmd;

	const cJSON *eye_json = cJSON_GetArrayItem(eye_transform_json, eye);
	if (eye_json == NULL) {
		return;
	}

	struct xrt_matrix_3x3 rot = {0};
	if (_json_get_matrix_3x3(eye_json, "eye_to_head", &rot)) {
		math_quat_from_matrix_3x3(&rot, &d->rot[eye]);
	}

	// TODO: store grow_for_undistort per eye
	// clang-format off
	hmd->distortion.vive.grow_for_undistort = _json_get_float(eye_json, "grow_for_undistort");
	hmd->distortion.vive.undistort_r2_cutoff[eye] = _json_get_float(eye_json, "undistort_r2_cutoff");
	// clang-format on

	const cJSON *distortion =
	    cJSON_GetObjectItemCaseSensitive(eye_json, "distortion");
	if (distortion != NULL) {
		// TODO: store center per color
		// clang-format off
		hmd->distortion.vive.center[eye][0] = _json_get_float(distortion, "center_x");
		hmd->distortion.vive.center[eye][1] = _json_get_float(distortion, "center_y");
		// clang-format on

		// green
		const cJSON *coeffs =
		    cJSON_GetObjectItemCaseSensitive(distortion, "coeffs");
		if (coeffs != NULL) {
			_get_color_coeffs(hmd, coeffs, eye, 1);
		}
	}

	_get_color_coeffs_lookup(hmd, eye_json, "distortion_red", eye, 0);
	_get_color_coeffs_lookup(hmd, eye_json, "distortion_blue", eye, 2);
}
static bool
_create_hmd_device(struct survive_system *sys)
{
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)U_DEVICE_ALLOC_HMD;
	int inputs = 1;
	int outputs = 0;

	struct survive_device *survive =
	    U_DEVICE_ALLOCATE(struct survive_device, flags, inputs, outputs);
	sys->hmd = survive;
	survive->sys = sys;
	survive->survive_obj = NULL;

	survive->base.name = XRT_DEVICE_GENERIC_HMD;
	snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "Survive HMD");
	survive->base.destroy = survive_device_destroy;
	survive->base.update_inputs = survive_hmd_update_inputs;
	survive->base.get_tracked_pose = survive_device_get_tracked_pose;
	survive->base.get_view_pose = survive_device_get_view_pose;
	survive->base.tracking_origin = &sys->base;

	survive_simple_start_thread(sys->ctx);

	while (!wait_for_hmd_config(sys->ctx)) {
		printf("Waiting for survive HMD config parsing\n");
		os_nanosleep(1000 * 1000 * 100);
	}
	printf("survive got HMD config\n");

	while (!survive->survive_obj) {
		printf("Waiting for survive HMD to be found...\n");
		for (const SurviveSimpleObject *it =
		         survive_simple_get_first_object(sys->ctx);
		     it != 0;
		     it = survive_simple_get_next_object(sys->ctx, it)) {
			const char *codename = survive_simple_object_name(it);

			enum SurviveSimpleObject_type type =
			    survive_simple_object_get_type(it);
			if (type == SurviveSimpleObject_HMD &&
			    sys->hmd->survive_obj == NULL) {
				printf("Found HMD: %s\n", codename);
				survive->survive_obj = it;
			}
		}
	}
	printf("survive HMD present\n");

	survive->base.hmd->blend_mode = XRT_BLEND_MODE_OPAQUE;

	char *json_string = survive_get_json_config(survive->survive_obj);
	cJSON *json = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json)) {
		printf("Could not parse JSON data.");
		return false;
	}


	// TODO: Replace hard coded values from OpenHMD with config
	double w_meters = 0.122822 / 2.0;
	double h_meters = 0.068234;
	double lens_horizontal_separation = 0.057863;
	double eye_to_screen_distance = 0.023226876441867737;
	double fov = 2 * atan2(w_meters - lens_horizontal_separation / 2.0,
	                       eye_to_screen_distance);

	survive->base.hmd->distortion.vive.aspect_x_over_y =
	    0.89999997615814209f;
	survive->base.hmd->distortion.vive.grow_for_undistort = 0.5f;
	survive->base.hmd->distortion.vive.undistort_r2_cutoff[0] = 1.0f;
	survive->base.hmd->distortion.vive.undistort_r2_cutoff[1] = 1.0f;

	survive->rot[0].w = 1.0f;
	survive->rot[1].w = 1.0f;

	uint16_t w_pixels = 1080;
	uint16_t h_pixels = 1200;
	const cJSON *device_json =
	    cJSON_GetObjectItemCaseSensitive(json, "device");
	if (device_json) {
		if (sys->variant != VIVE_VARIANT_INDEX) {
			survive->base.hmd->distortion.vive.aspect_x_over_y =
			    _json_get_float(device_json,
			                    "physical_aspect_x_over_y");

			//! @todo: fov calculation needs to be fixed, only works
			//! with hardcoded value
			// lens_horizontal_separation = _json_get_double(json,
			// "lens_separation");
		}
		h_pixels = (uint16_t)_json_get_int(
		    device_json, "eye_target_height_in_pixels");
		w_pixels = (uint16_t)_json_get_int(
		    device_json, "eye_target_width_in_pixels");
	}

	const cJSON *eye_transform_json =
	    cJSON_GetObjectItemCaseSensitive(json, "tracking_to_eye_transform");
	if (eye_transform_json) {
		for (uint8_t eye = 0; eye < 2; eye++) {
			get_distortion_properties(survive, eye_transform_json,
			                          eye);
		}
	}

	printf("Survive eye resolution %dx%d\n", w_pixels, h_pixels);

	cJSON_Delete(json);

	// Main display.
	survive->base.hmd->screens[0].w_pixels = (int)w_pixels * 2;
	survive->base.hmd->screens[0].h_pixels = (int)h_pixels;

	if (sys->variant == VIVE_VARIANT_INDEX)
		survive->base.hmd->screens[0].nominal_frame_interval_ns =
		    (uint64_t)time_s_to_ns(1.0f / 144.0f);
	else
		survive->base.hmd->screens[0].nominal_frame_interval_ns =
		    (uint64_t)time_s_to_ns(1.0f / 90.0f);

	for (uint8_t eye = 0; eye < 2; eye++) {
		struct xrt_view *v = &survive->base.hmd->views[eye];
		v->display.w_meters = (float)w_meters;
		v->display.h_meters = (float)h_meters;
		v->display.w_pixels = w_pixels;
		v->display.h_pixels = h_pixels;
		v->viewport.w_pixels = w_pixels;
		v->viewport.h_pixels = h_pixels;
		v->viewport.y_pixels = 0;
		v->lens_center.y_meters = (float)h_meters / 2.0f;
		v->rot = u_device_rotation_ident;
	}

	// Left
	survive->base.hmd->views[0].lens_center.x_meters =
	    (float)(w_meters - lens_horizontal_separation / 2.0);
	survive->base.hmd->views[0].viewport.x_pixels = 0;

	// Right
	survive->base.hmd->views[1].lens_center.x_meters =
	    (float)lens_horizontal_separation / 2.0f;
	survive->base.hmd->views[1].viewport.x_pixels = w_pixels;

	for (uint8_t eye = 0; eye < 2; eye++) {
		if (!math_compute_fovs(w_meters,
		                       (double)survive->base.hmd->views[eye]
		                           .lens_center.x_meters,
		                       fov, h_meters,
		                       (double)survive->base.hmd->views[eye]
		                           .lens_center.y_meters,
		                       0, &survive->base.hmd->views[eye].fov)) {
			printf("Failed to compute the partial fields of view.");
			free(survive);
			return NULL;
		}
	}

	survive->base.hmd->distortion.models = XRT_DISTORTION_MODEL_VIVE;
	survive->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_VIVE;

	survive->base.orientation_tracking_supported = true;
	survive->base.position_tracking_supported = true;
	survive->base.device_type = XRT_DEVICE_TYPE_HMD;

	return true;
}

static bool
_create_controller_device(struct survive_system *sys, int controller_num)
{

	enum u_device_alloc_flags flags = 0;
	int inputs = VIVE_CONTROLLER_MAX_INDEX;
	int outputs = 0;
	struct survive_device *controller =
	    U_DEVICE_ALLOCATE(struct survive_device, flags, inputs, outputs);
	sys->controllers[controller_num] = controller;
	controller->sys = sys;
	controller->survive_obj = NULL;
	for (int i = 0; i < MAX_PENDING_EVENTS; i++)
		controller->pending_events[i].event_type =
		    SurviveSimpleEventType_None;

	controller->num = controller_num;
	controller->base.tracking_origin = &sys->base;

	controller->base.destroy = survive_device_destroy;
	controller->base.update_inputs = survive_device_update_inputs;
	controller->base.get_tracked_pose = survive_device_get_tracked_pose;

	//! @todo: May use Vive Wands + Index HMDs or Index Controllers + Vive
	//! HMD
	if (sys->variant == VIVE_VARIANT_INDEX) {
		controller->base.name = XRT_DEVICE_INDEX_CONTROLLER;
		snprintf(controller->base.str, XRT_DEVICE_NAME_LEN,
		         "Survive Valve Index Controller %d", controller_num);

		controller->base.inputs[VIVE_CONTROLLER_INDEX_AIM_POSE].name =
		    XRT_INPUT_INDEX_AIM_POSE;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_GRIP_POSE].name =
		    XRT_INPUT_INDEX_GRIP_POSE;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_VALUE]
		    .name = XRT_INPUT_INDEX_TRIGGER_VALUE;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_VALUE]
		    .value.vec1.x = 0;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
		    .name = XRT_INPUT_INDEX_TRIGGER_CLICK;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
		    .value.boolean = false;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_A_CLICK].name =
		    XRT_INPUT_INDEX_A_CLICK;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_A_CLICK]
		    .value.boolean = false;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_B_CLICK].name =
		    XRT_INPUT_INDEX_B_CLICK;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_B_CLICK]
		    .value.boolean = false;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
		    .name = XRT_INPUT_INDEX_TRIGGER_CLICK;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
		    .value.boolean = false;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
		    .name = XRT_INPUT_INDEX_TRIGGER_CLICK;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
		    .value.boolean = false;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_THUMBSTICK].name =
		    XRT_INPUT_INDEX_THUMBSTICK;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD].name =
		    XRT_INPUT_INDEX_TRACKPAD;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH]
		    .name = XRT_INPUT_INDEX_TRACKPAD_TOUCH;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH]
		    .value.boolean = false;

		//! @todo: find out left/right hand from survive
		controller->base.device_type =
		    XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER;


	} else {
		controller->base.name = XRT_DEVICE_VIVE_WAND;
		snprintf(controller->base.str, XRT_DEVICE_NAME_LEN,
		         "Survive Vive Wand Controller %d", controller_num);

		controller->base.inputs[VIVE_CONTROLLER_INDEX_AIM_POSE].name =
		    XRT_INPUT_VIVE_AIM_POSE;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_GRIP_POSE].name =
		    XRT_INPUT_VIVE_GRIP_POSE;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_VALUE]
		    .name = XRT_INPUT_VIVE_TRIGGER_VALUE;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_VALUE]
		    .value.vec1.x = 0;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
		    .name = XRT_INPUT_VIVE_TRIGGER_CLICK;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
		    .value.boolean = false;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_MENU_CLICK].name =
		    XRT_INPUT_VIVE_MENU_CLICK;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_MENU_CLICK]
		    .value.boolean = false;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
		    .name = XRT_INPUT_VIVE_TRIGGER_CLICK;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
		    .value.boolean = false;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
		    .name = XRT_INPUT_VIVE_TRIGGER_CLICK;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_CLICK]
		    .value.boolean = false;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD].name =
		    XRT_INPUT_VIVE_TRACKPAD;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH]
		    .name = XRT_INPUT_VIVE_TRACKPAD_TOUCH;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH]
		    .value.boolean = false;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_CLICK]
		    .name = XRT_INPUT_VIVE_TRACKPAD_CLICK;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_CLICK]
		    .value.boolean = false;

		controller->base.inputs[VIVE_CONTROLLER_INDEX_SQUEEZE_CLICK]
		    .name = XRT_INPUT_VIVE_SQUEEZE_CLICK;
		controller->base.inputs[VIVE_CONTROLLER_INDEX_SQUEEZE_CLICK]
		    .value.boolean = false;
	}

	controller->base.orientation_tracking_supported = true;
	controller->base.position_tracking_supported = true;

	return true;
}

DEBUG_GET_ONCE_BOOL_OPTION(survive_spew, "SURVIVE_PRINT_SPEW", false)
DEBUG_GET_ONCE_BOOL_OPTION(survive_debug, "SURVIVE_PRINT_DEBUG", false)

static enum VIVE_VARIANT
_product_to_variant(uint16_t product_id)
{
	switch (product_id) {
	case VIVE_PID: return VIVE_VARIANT_VIVE;
	case VIVE_PRO_MAINBOARD_PID: return VIVE_VARIANT_PRO;
	case VIVE_PRO_LHR_PID: return VIVE_VARIANT_INDEX;
	default:
		printf("No product ids matched %.4x\n", product_id);
		return VIVE_UNKNOWN;
	}
}

int
survive_found(struct xrt_prober *xp,
              struct xrt_prober_device **devices,
              size_t num_devices,
              size_t index,
              cJSON *attached_data,
              struct xrt_device **out_xdevs)
{
	if (survive_already_initialized) {
		printf(
		    "Skipping libsurvive initialization, already "
		    "initialized\n");
		return 0;
	}

	SurviveSimpleContext *actx = NULL;
#if 1
	char *survive_args[] = {
	    "Monado-libsurvive",
	    //"--time-window", "1500000"
	    //"--use-imu", "0",
	    //"--use-kalman", "0"
	};
	actx = survive_simple_init(
	    sizeof(survive_args) / sizeof(survive_args[0]), survive_args);
#else
	actx = survive_simple_init(0, 0);
#endif

	if (!actx) {
		SURVIVE_ERROR(survive, "failed to init survive");
		return false;
	}

	struct survive_system *ss = U_TYPED_CALLOC(struct survive_system);

	struct xrt_prober_device *dev = devices[index];
	ss->variant = _product_to_variant(dev->product_id);
	printf("survive: Assuming variant %d\n", ss->variant);

	ss->ctx = actx;
	snprintf(ss->base.name, XRT_TRACKING_NAME_LEN, "%s",
	         "Libsurvive Tracking");
	ss->base.offset.position.x = 0.0f;
	ss->base.offset.position.y = 0.0f;
	ss->base.offset.position.z = 0.0f;
	ss->base.offset.orientation.w = 1.0f;

	ss->print_spew = debug_get_bool_option_survive_spew();
	ss->print_debug = debug_get_bool_option_survive_debug();

	_create_hmd_device(ss);
	_create_controller_device(ss, 0);
	_create_controller_device(ss, 1);

	// printf("Survive HMD %p, controller %p %p\n", ss->hmd,
	// ss->controllers[0], ss->controllers[1]);

	if (ss->print_debug) {
		u_device_dump_config(&ss->hmd->base, __func__, "libsurvive");
	}

	out_xdevs[0] = &ss->hmd->base;
	out_xdevs[1] = &ss->controllers[0]->base;
	out_xdevs[2] = &ss->controllers[1]->base;

	survive_already_initialized = true;
	return 3;
}
