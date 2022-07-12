// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Adaptor to a OpenHMD device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ohmd
 */


#include "math/m_mathinclude.h"
#include "xrt/xrt_config_os.h"
#include "xrt/xrt_prober.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "os/os_time.h"

#include "openhmd.h"

#include "math/m_api.h"
#include "math/m_vec2.h"
#include "xrt/xrt_device.h"
#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_time.h"
#include "util/u_distortion_mesh.h"
#include "util/u_logging.h"

#include "oh_device.h"

// Should we permit finite differencing to compute angular velocities when not
// directly retrieved?
DEBUG_GET_ONCE_BOOL_OPTION(ohmd_finite_diff, "OHMD_ALLOW_FINITE_DIFF", true)
DEBUG_GET_ONCE_LOG_OPTION(ohmd_log, "OHMD_LOG", U_LOGGING_WARN)
DEBUG_GET_ONCE_BOOL_OPTION(ohmd_external, "OHMD_EXTERNAL_DRIVER", false)

// Define this if you have the appropriately hacked-up OpenHMD version.
#undef OHMD_HAVE_ANG_VEL

enum input_indices
{
	// khronos simple inputs for generic controllers
	SIMPLE_SELECT_CLICK = 0,
	SIMPLE_MENU_CLICK,
	SIMPLE_GRIP_POSE,
	SIMPLE_AIM_POSE,

	// longest list of aliased enums has to start last for INPUT_INDICES_LAST to get the biggest value
	OCULUS_TOUCH_X_CLICK = 0,
	OCULUS_TOUCH_X_TOUCH,
	OCULUS_TOUCH_Y_CLICK,
	OCULUS_TOUCH_Y_TOUCH,
	OCULUS_TOUCH_MENU_CLICK,
	OCULUS_TOUCH_A_CLICK,
	OCULUS_TOUCH_A_TOUCH,
	OCULUS_TOUCH_B_CLICK,
	OCULUS_TOUCH_B_TOUCH,
	OCULUS_TOUCH_SYSTEM_CLICK,
	OCULUS_TOUCH_SQUEEZE_VALUE,
	OCULUS_TOUCH_TRIGGER_TOUCH,
	OCULUS_TOUCH_TRIGGER_VALUE,
	OCULUS_TOUCH_THUMBSTICK_CLICK,
	OCULUS_TOUCH_THUMBSTICK_TOUCH,
	OCULUS_TOUCH_THUMBSTICK,
	OCULUS_TOUCH_THUMBREST_TOUCH,
	OCULUS_TOUCH_GRIP_POSE,
	OCULUS_TOUCH_AIM_POSE,

	INPUT_INDICES_LAST
};
#define SET_TOUCH_INPUT(NAME) (ohd->base.inputs[OCULUS_TOUCH_##NAME].name = XRT_INPUT_TOUCH_##NAME)

// all OpenHMD controls are floats, even "digital" controls.
// this means a trigger can be fed by a discrete button or by a pullable [0,1] analog trigger.
// in case of not so good calibrated triggers we may not reach 1.0
#define PRESS_FLOAT_THRESHOLD 0.95
#define FLOAT_TO_DIGITAL_THRESHOLD 0.5

// one mapping for each of enum ohmd_control_hint
#define CONTROL_MAPPING_SIZE 16

// generic controllers are mapped to the khronos simple profile
// touch controllers input mappings are special cased
enum openhmd_device_type
{
	OPENHMD_GENERIC_HMD,
	OPENHMD_GENERIC_CONTROLLER,
	OPENHMD_OCULUS_RIFT_HMD,
	OPENHMD_OCULUS_RIFT_CONTROLLER,
};

struct openhmd_values
{
	float hmd_warp_param[4];
	float aberr[3];
	struct xrt_vec2 lens_center;
	struct xrt_vec2 viewport_scale;
	float warp_scale;
};

struct oh_device;
struct oh_system
{
	struct xrt_tracking_origin base;
	struct oh_device *devices[XRT_MAX_DEVICES_PER_PROBE];

	//! index into oh_system::devices
	int hmd_idx;
	int left_idx;
	int right_idx;
};

/*!
 * @implements xrt_device
 */
struct oh_device
{
	struct xrt_device base;
	ohmd_context *ctx;
	ohmd_device *dev;

	bool skip_ang_vel;

	int64_t last_update;
	struct xrt_space_relation last_relation;

	enum u_logging_level log_level;
	bool enable_finite_difference;

	struct
	{
		struct u_vive_values vive[2];
		struct openhmd_values openhmd[2];
	} distortion;

	struct oh_system *sys;


	enum openhmd_device_type ohmd_device_type;

	// what function controls serve.
	int controls_fn[64];

	// whether controls are digital or analog.
	// unused because the OpenXR interaction profile forces the type.
	int controls_types[64];

	//! maps OpenHMD control hint enum to the corresponding index into base.inputs
	enum input_indices controls_mapping[CONTROL_MAPPING_SIZE];

	// For touch controller we map an analog trigger to a float interaction profile input.
	// For simple controller we map a potentially analog trigger to a bool input.
	bool make_trigger_digital;

	float last_control_state[256];
};

static inline struct oh_device *
oh_device(struct xrt_device *xdev)
{
	return (struct oh_device *)xdev;
}

static void
oh_device_destroy(struct xrt_device *xdev)
{
	struct oh_device *ohd = oh_device(xdev);

	if (ohd->dev != NULL) {
		ohmd_close_device(ohd->dev);
		ohd->dev = NULL;
	}

	bool all_null = true;
	for (int i = 0; i < XRT_MAX_DEVICES_PER_PROBE; i++) {
		if (ohd->sys->devices[i] == ohd) {
			ohd->sys->devices[i] = NULL;
		}

		if (ohd->sys->devices[i] != NULL) {
			all_null = false;
		}
	}

	if (all_null) {
		// Remove the variable tracking.
		u_var_remove_root(ohd->sys);

		free(ohd->sys);
	}

	u_device_free(&ohd->base);
}

#define CASE_VEC1(OHMD_CONTROL)                                                                                        \
	case OHMD_CONTROL:                                                                                             \
		if (ohd->controls_mapping[OHMD_CONTROL] == 0) {                                                        \
			break;                                                                                         \
		}                                                                                                      \
		if (control_state[i] != ohd->last_control_state[i]) {                                                  \
			ohd->base.inputs[ohd->controls_mapping[OHMD_CONTROL]].value.vec1.x = control_state[i];         \
			ohd->base.inputs[ohd->controls_mapping[OHMD_CONTROL]].timestamp = ts;                          \
		}                                                                                                      \
		break;

#define CASE_VEC1_OR_DIGITAL(OHMD_CONTROL, MAKE_DIGITAL)                                                               \
	case OHMD_CONTROL:                                                                                             \
		if (ohd->controls_mapping[OHMD_CONTROL] == 0) {                                                        \
			break;                                                                                         \
		}                                                                                                      \
		if (MAKE_DIGITAL) {                                                                                    \
			if ((control_state[i] > FLOAT_TO_DIGITAL_THRESHOLD) !=                                         \
			    (ohd->last_control_state[i] > FLOAT_TO_DIGITAL_THRESHOLD)) {                               \
				ohd->base.inputs[ohd->controls_mapping[OHMD_CONTROL]].value.vec1.x =                   \
				    control_state[i] > FLOAT_TO_DIGITAL_THRESHOLD;                                     \
				ohd->base.inputs[ohd->controls_mapping[OHMD_CONTROL]].timestamp = ts;                  \
			}                                                                                              \
		} else {                                                                                               \
			if (control_state[i] != ohd->last_control_state[i]) {                                          \
				ohd->base.inputs[ohd->controls_mapping[OHMD_CONTROL]].value.vec1.x = control_state[i]; \
				ohd->base.inputs[ohd->controls_mapping[OHMD_CONTROL]].timestamp = ts;                  \
			}                                                                                              \
		}                                                                                                      \
		break;

#define CASE_DIGITAL(OHMD_CONTROL, THRESHOLD)                                                                          \
	case OHMD_CONTROL:                                                                                             \
		if (ohd->controls_mapping[OHMD_CONTROL] == 0) {                                                        \
			break;                                                                                         \
		}                                                                                                      \
		if (control_state[i] != ohd->last_control_state[i]) {                                                  \
			ohd->base.inputs[ohd->controls_mapping[OHMD_CONTROL]].value.boolean =                          \
			    control_state[i] > THRESHOLD;                                                              \
			ohd->base.inputs[ohd->controls_mapping[OHMD_CONTROL]].timestamp = ts;                          \
		}                                                                                                      \
		break;

#define CASE_VEC2_X(OHMD_CONTROL)                                                                                      \
	case OHMD_CONTROL:                                                                                             \
		if (ohd->controls_mapping[OHMD_CONTROL] == 0) {                                                        \
			break;                                                                                         \
		}                                                                                                      \
		if (control_state[i] != ohd->last_control_state[i]) {                                                  \
			ohd->base.inputs[ohd->controls_mapping[OHMD_CONTROL]].value.vec2.x = control_state[i];         \
			ohd->base.inputs[ohd->controls_mapping[OHMD_CONTROL]].timestamp = ts;                          \
		}                                                                                                      \
		break;

#define CASE_VEC2_Y(OHMD_CONTROL)                                                                                      \
	case OHMD_CONTROL:                                                                                             \
		if (ohd->controls_mapping[OHMD_CONTROL] == 0) {                                                        \
			break;                                                                                         \
		}                                                                                                      \
		if (control_state[i] != ohd->last_control_state[i]) {                                                  \
			ohd->base.inputs[ohd->controls_mapping[OHMD_CONTROL]].value.vec2.y = control_state[i];         \
			ohd->base.inputs[ohd->controls_mapping[OHMD_CONTROL]].timestamp = ts;                          \
		}                                                                                                      \
		break;

static void
update_ohmd_controller(struct oh_device *ohd, int control_count, float *control_state)
{
	timepoint_ns ts = os_monotonic_get_ns();
	for (int i = 0; i < control_count; i++) {
		switch (ohd->controls_fn[i]) {
			// CASE macro does nothing, if ohd->controls_mapping[OHMD_CONTROL] has not been assigned
			CASE_VEC1_OR_DIGITAL(OHMD_TRIGGER, ohd->make_trigger_digital);
			CASE_DIGITAL(OHMD_TRIGGER_CLICK, PRESS_FLOAT_THRESHOLD);
			CASE_VEC1(OHMD_SQUEEZE);
			CASE_DIGITAL(OHMD_MENU, PRESS_FLOAT_THRESHOLD);
			CASE_DIGITAL(OHMD_HOME, PRESS_FLOAT_THRESHOLD);
			CASE_VEC2_X(OHMD_ANALOG_X);
			CASE_VEC2_Y(OHMD_ANALOG_Y);
			CASE_DIGITAL(OHMD_ANALOG_PRESS, PRESS_FLOAT_THRESHOLD);
			CASE_DIGITAL(OHMD_BUTTON_A, PRESS_FLOAT_THRESHOLD);
			CASE_DIGITAL(OHMD_BUTTON_B, PRESS_FLOAT_THRESHOLD);
			CASE_DIGITAL(OHMD_BUTTON_X, PRESS_FLOAT_THRESHOLD);
			CASE_DIGITAL(OHMD_BUTTON_Y, PRESS_FLOAT_THRESHOLD);
			CASE_DIGITAL(OHMD_VOLUME_PLUS, PRESS_FLOAT_THRESHOLD);
			CASE_DIGITAL(OHMD_VOLUME_MINUS, PRESS_FLOAT_THRESHOLD);
			CASE_DIGITAL(OHMD_MIC_MUTE, PRESS_FLOAT_THRESHOLD);
		}
	}
}

static void
oh_device_update_inputs(struct xrt_device *xdev)
{
	struct oh_device *ohd = oh_device(xdev);

	int control_count;
	float control_state[256];

	ohmd_device_geti(ohd->dev, OHMD_CONTROL_COUNT, &control_count);
	if (control_count > 64)
		control_count = 64;

	ohmd_device_getf(ohd->dev, OHMD_CONTROLS_STATE, control_state);

	if (ohd->ohmd_device_type == OPENHMD_OCULUS_RIFT_CONTROLLER ||
	    ohd->ohmd_device_type == OPENHMD_GENERIC_CONTROLLER) {
		update_ohmd_controller(ohd, control_count, control_state);
	}

	for (int i = 0; i < 256; i++) {
		ohd->last_control_state[i] = control_state[i];
	}
}

static void
oh_device_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
	struct oh_device *ohd = oh_device(xdev);
	(void)ohd;

	//! @todo OpenHMD haptic API not finished
}

static void
oh_device_get_tracked_pose(struct xrt_device *xdev,
                           enum xrt_input_name name,
                           uint64_t at_timestamp_ns,
                           struct xrt_space_relation *out_relation)
{
	struct oh_device *ohd = oh_device(xdev);
	struct xrt_quat quat = XRT_QUAT_IDENTITY;
	struct xrt_vec3 pos = XRT_VEC3_ZERO;

	// support generic head pose for all hmds,
	// support rift poses for rift controllers, and simple poses for generic controller
	if (name != XRT_INPUT_GENERIC_HEAD_POSE &&
	    (ohd->ohmd_device_type == OPENHMD_OCULUS_RIFT_CONTROLLER &&
	     (name != XRT_INPUT_TOUCH_AIM_POSE && name != XRT_INPUT_TOUCH_GRIP_POSE)) &&
	    ohd->ohmd_device_type == OPENHMD_GENERIC_CONTROLLER &&
	    (name != XRT_INPUT_SIMPLE_AIM_POSE && name != XRT_INPUT_SIMPLE_GRIP_POSE)) {
		OHMD_ERROR(ohd, "unknown input name");
		return;
	}

	ohmd_ctx_update(ohd->ctx);
	uint64_t now = os_monotonic_get_ns();

	//! @todo adjust for latency here
	ohmd_device_getf(ohd->dev, OHMD_ROTATION_QUAT, &quat.x);
	ohmd_device_getf(ohd->dev, OHMD_POSITION_VECTOR, &pos.x);
	out_relation->pose.orientation = quat;
	out_relation->pose.position = pos;
	//! @todo assuming that orientation is actually currently tracked
	out_relation->relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	                                                               XRT_SPACE_RELATION_POSITION_VALID_BIT);

	// we assume the position is tracked if and only if it is not zero
	if (pos.x != 0.0 || pos.y != 0.0 || pos.z != 0.0) {
		out_relation->relation_flags = (enum xrt_space_relation_flags)(out_relation->relation_flags |
		                                                               XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
	}

	bool have_ang_vel = false;
	struct xrt_vec3 ang_vel;
#ifdef OHMD_HAVE_ANG_VEL
	if (!ohd->skip_ang_vel) {
		if (0 == ohmd_device_getf(ohd->dev, OHMD_ANGULAR_VELOCITY, &ang_vel.x)) {
			have_ang_vel = true;
		} else {
			// we now know this device doesn't return angular
			// velocity.
			ohd->skip_ang_vel = true;
		}
	}
#endif
	struct xrt_quat old_quat = ohd->last_relation.pose.orientation;
	if (0 == memcmp(&quat, &old_quat, sizeof(quat))) {
		// Looks like the exact same as last time, let's pretend we got
		// no new report.
		/*! @todo this is a hack - should really get a timestamp on the
		 * USB data and use that instead.
		 */
		*out_relation = ohd->last_relation;
		OHMD_TRACE(ohd, "GET_TRACKED_POSE (%s) - no new data", ohd->base.str);
		return;
	}

	/*!
	 * @todo possibly hoist this out of the driver level, to provide as a
	 * common service?
	 */
	if (ohd->enable_finite_difference && !have_ang_vel) {
		// No angular velocity
		float dt = time_ns_to_s(now - ohd->last_update);
		if (ohd->last_update == 0) {
			// This is the first report, so just print a warning
			// instead of estimating ang vel.
			OHMD_DEBUG(ohd,
			           "Will use finite differencing to estimate "
			           "angular velocity.");
		} else if (dt < 1.0f && dt > 0.0005) {
			// but we can compute it:
			// last report was not long ago but not
			// instantaneously (at least half a millisecond),
			// so approximately safe to do this.
			math_quat_finite_difference(&old_quat, &quat, dt, &ang_vel);
			have_ang_vel = true;
		}
	}

	if (have_ang_vel) {
		out_relation->angular_velocity = ang_vel;
		out_relation->relation_flags = (enum xrt_space_relation_flags)(
		    out_relation->relation_flags | XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT);

		OHMD_TRACE(ohd, "GET_TRACKED_POSE (%s) (%f, %f, %f, %f) (%f, %f, %f)", ohd->base.str, quat.x, quat.y,
		           quat.z, quat.w, ang_vel.x, ang_vel.y, ang_vel.z);
	} else {
		OHMD_TRACE(ohd, "GET_TRACKED_POSE (%s) (%f, %f, %f, %f)", ohd->base.str, quat.x, quat.y, quat.z,
		           quat.w);
	}

	// Update state within driver
	ohd->last_update = now;
	ohd->last_relation = *out_relation;
}

static void
oh_device_get_view_poses(struct xrt_device *xdev,
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

struct display_info
{
	float w_meters;
	float h_meters;
	int w_pixels;
	int h_pixels;
	float nominal_frame_interval_ns;
};

struct device_info
{
	/* the display (or virtual display consisting of multiple physical
	 * displays) in its "physical" configuration as the user looks at it.
	 * e.g. a 1440x2560 portrait display that is rotated and built
	 * into a HMD in landscape mode, will be treated as 2560x1440. */
	struct display_info display;

	float lens_horizontal_separation;
	float lens_vertical_position;

	float pano_distortion_k[4];
	float pano_aberration_k[3];
	float pano_warp_scale;

	struct
	{
		float fov;

		/* the display or part of the display covering this view in its
		 * "physical" configuration as the user looks at it.
		 * e.g. a 1440x2560 portrait display that is rotated and built
		 * into a HMD in landscape mode, will be treated as 1280x1440
		 * per view */
		struct display_info display;

		float lens_center_x_meters;
		float lens_center_y_meters;
	} views[2];

	struct
	{
		bool rotate_lenses_right;
		bool rotate_lenses_left;
		bool rotate_lenses_inwards;
		bool video_see_through;
		bool video_distortion_none;
		bool video_distortion_vive;
		bool left_center_pano_scale;
		bool rotate_screen_right_after;
		bool delay_after_initialization;
	} quirks;
};

static struct device_info
get_info(ohmd_device *dev, const char *prod)
{
	struct device_info info = {0};

	// clang-format off
	ohmd_device_getf(dev, OHMD_SCREEN_HORIZONTAL_SIZE, &info.display.w_meters);
	ohmd_device_getf(dev, OHMD_SCREEN_VERTICAL_SIZE, &info.display.h_meters);
	ohmd_device_getf(dev, OHMD_LENS_HORIZONTAL_SEPARATION, &info.lens_horizontal_separation);
	ohmd_device_getf(dev, OHMD_LENS_VERTICAL_POSITION, &info.lens_vertical_position);
	ohmd_device_getf(dev, OHMD_LEFT_EYE_FOV, &info.views[0].fov);
	ohmd_device_getf(dev, OHMD_RIGHT_EYE_FOV, &info.views[1].fov);
	ohmd_device_geti(dev, OHMD_SCREEN_HORIZONTAL_RESOLUTION, &info.display.w_pixels);
	ohmd_device_geti(dev, OHMD_SCREEN_VERTICAL_RESOLUTION, &info.display.h_pixels);
	ohmd_device_getf(dev, OHMD_UNIVERSAL_DISTORTION_K, &info.pano_distortion_k[0]);
	ohmd_device_getf(dev, OHMD_UNIVERSAL_ABERRATION_K, &info.pano_aberration_k[0]);

	// Default to 90FPS
	info.display.nominal_frame_interval_ns =
	    time_s_to_ns(1.0f / 90.0f);

	// Find any needed quirks.
	if (strcmp(prod, "3Glasses-D3V2") == 0) {
		info.quirks.rotate_lenses_right = true;
		info.quirks.rotate_screen_right_after = true;
		info.quirks.left_center_pano_scale = true;

		// 70.43 FPS
		info.display.nominal_frame_interval_ns =
		    time_s_to_ns(1.0f / 70.43f);
	}

	if (strcmp(prod, "HTC Vive") == 0) {
		info.quirks.video_distortion_vive = true;
		info.quirks.video_see_through = true;
	}

	if (strcmp(prod, "LGR100") == 0) {
		info.quirks.rotate_lenses_inwards = true;
	}

	if (strcmp(prod, "External Device") == 0) {
		info.quirks.video_distortion_none = true;
		info.display.w_pixels = 1920;
		info.display.h_pixels = 1080;
		info.lens_horizontal_separation = 0.0630999878f;
		info.lens_vertical_position = 0.0394899882f;
		info.views[0].fov = 103.57f * M_PI / 180.0f;
		info.views[1].fov = 103.57f * M_PI / 180.0f;
	}

	if (strcmp(prod, "PSVR") == 0) {
		info.quirks.video_distortion_none = true;
	}

       if (strcmp(prod, "Rift (DK2)") == 0) {
               info.quirks.rotate_lenses_left = true;
       }

	if (strcmp(prod, "Rift (CV1)") == 0) {
		info.quirks.delay_after_initialization = true;
	}

	if (strcmp(prod, "Rift S") == 0) {
		info.quirks.delay_after_initialization = true;
		info.quirks.rotate_lenses_right = true;
	}

	/* Only the WVR2 display is rotated. OpenHMD can't easily tell us
	 * the WVR SKU, so just recognize it by resolution */
	if (strcmp(prod, "VR-Tek WVR") == 0 &&
	    info.display.w_pixels == 2560 &&
	    info.display.h_pixels == 1440) {
		info.quirks.rotate_lenses_left = true;
	}


	/*
	 * Assumptions made here:
	 *
	 * - There is a single, continuous, flat display serving both eyes, with
	 *   no dead space/gap between eyes.
	 * - This single panel is (effectively) perpendicular to the forward
	 *   (-Z) direction, with edges aligned with the X and Y axes.
	 * - Lens position is symmetrical about the center ("bridge of  nose").
	 * - Pixels are square and uniform across the entirety of the panel.
	 *
	 * If any of these are not true, then either the rendering will
	 * be inaccurate, or the properties will have to be "fudged" to
	 * make the math work.
	 */

	info.views[0].display.w_meters = info.display.w_meters / 2.0;
	info.views[0].display.h_meters = info.display.h_meters;
	info.views[1].display.w_meters = info.display.w_meters / 2.0;
	info.views[1].display.h_meters = info.display.h_meters;

	info.views[0].display.w_pixels = info.display.w_pixels / 2;
	info.views[0].display.h_pixels = info.display.h_pixels;
	info.views[1].display.w_pixels = info.display.w_pixels / 2;
	info.views[1].display.h_pixels = info.display.h_pixels;

	/*
	 * Assuming the lenses are centered vertically on the
	 * display. It's not universal, but 0.5 COP on Y is more
	 * common than on X, and it looked like many of the
	 * driver lens_vpos values were copy/pasted or marked
	 * with FIXME. Safer to fix it to 0.5 than risk an
	 * extreme geometry mismatch.
	 */

	const double cop_y = 0.5;
	const double h_1 = cop_y * info.display.h_meters;

	//! @todo This are probably all wrong!
	info.views[0].lens_center_x_meters = info.views[0].display.w_meters - info.lens_horizontal_separation / 2.0;
	info.views[0].lens_center_y_meters = h_1;

	info.views[1].lens_center_x_meters = info.lens_horizontal_separation / 2.0;
	info.views[1].lens_center_y_meters = h_1;

	// From OpenHMD: Assume calibration was for lens view to which ever edge
	//               of screen is further away from lens center.
	info.pano_warp_scale =
		(info.views[0].lens_center_x_meters > info.views[1].lens_center_x_meters) ?
			info.views[0].lens_center_x_meters :
			info.views[1].lens_center_x_meters;
	// clang-format on

	if (info.quirks.rotate_screen_right_after) {
		// OpenHMD describes the logical orintation not the physical.
		// clang-format off
		ohmd_device_getf(dev, OHMD_SCREEN_HORIZONTAL_SIZE, &info.display.h_meters);
		ohmd_device_getf(dev, OHMD_SCREEN_VERTICAL_SIZE, &info.display.w_meters);
		ohmd_device_geti(dev, OHMD_SCREEN_HORIZONTAL_RESOLUTION, &info.display.h_pixels);
		ohmd_device_geti(dev, OHMD_SCREEN_VERTICAL_RESOLUTION, &info.display.w_pixels);
		// clang-format on
	}

	return info;
}

#define mul m_vec2_mul
#define mul_scalar m_vec2_mul_scalar
#define add m_vec2_add
#define sub m_vec2_sub
#define div m_vec2_div
#define div_scalar m_vec2_div_scalar
#define len m_vec2_len

// slightly different to u_compute_distortion_panotools in u_distortion_mesh
static bool
u_compute_distortion_openhmd(struct openhmd_values *values, float u, float v, struct xrt_uv_triplet *result)
{
	struct openhmd_values val = *values;

	struct xrt_vec2 r = {u, v};
	r = mul(r, val.viewport_scale);
	r = sub(r, val.lens_center);
	r = div_scalar(r, val.warp_scale);

	float r_mag = len(r);
	r_mag = val.hmd_warp_param[3] +                        // r^1
	        val.hmd_warp_param[2] * r_mag +                // r^2
	        val.hmd_warp_param[1] * r_mag * r_mag +        // r^3
	        val.hmd_warp_param[0] * r_mag * r_mag * r_mag; // r^4

	struct xrt_vec2 r_dist = mul_scalar(r, r_mag);
	r_dist = mul_scalar(r_dist, val.warp_scale);

	struct xrt_vec2 r_uv = mul_scalar(r_dist, val.aberr[0]);
	r_uv = add(r_uv, val.lens_center);
	r_uv = div(r_uv, val.viewport_scale);

	struct xrt_vec2 g_uv = mul_scalar(r_dist, val.aberr[1]);
	g_uv = add(g_uv, val.lens_center);
	g_uv = div(g_uv, val.viewport_scale);

	struct xrt_vec2 b_uv = mul_scalar(r_dist, val.aberr[2]);
	b_uv = add(b_uv, val.lens_center);
	b_uv = div(b_uv, val.viewport_scale);

	result->r = r_uv;
	result->g = g_uv;
	result->b = b_uv;
	return true;
}

static bool
compute_distortion_openhmd(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	struct oh_device *ohd = oh_device(xdev);
	return u_compute_distortion_openhmd(&ohd->distortion.openhmd[view], u, v, result);
}

static bool
compute_distortion_vive(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	struct oh_device *ohd = oh_device(xdev);
	return u_compute_distortion_vive(&ohd->distortion.vive[view], u, v, result);
}

static inline void
swap(int *a, int *b)
{
	int temp = *a;
	*a = *b;
	*b = temp;
}

static struct oh_device *
create_hmd(ohmd_context *ctx, int device_idx, int device_flags)
{
	const char *prod = ohmd_list_gets(ctx, device_idx, OHMD_PRODUCT);
	ohmd_device *dev = ohmd_list_open_device(ctx, device_idx);
	if (dev == NULL) {
		return NULL;
	}


	const struct device_info info = get_info(dev, prod);

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_HMD;
	struct oh_device *ohd = U_DEVICE_ALLOCATE(struct oh_device, flags, 1, 0);
	ohd->base.update_inputs = oh_device_update_inputs;
	ohd->base.get_tracked_pose = oh_device_get_tracked_pose;
	ohd->base.get_view_poses = oh_device_get_view_poses;
	ohd->base.destroy = oh_device_destroy;
	ohd->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;
	ohd->base.name = XRT_DEVICE_GENERIC_HMD;
	ohd->ctx = ctx;
	ohd->dev = dev;
	ohd->log_level = debug_get_log_option_ohmd_log();
	ohd->enable_finite_difference = debug_get_bool_option_ohmd_finite_diff();
	if (strcmp(prod, "Rift (CV1)") == 0 || strcmp(prod, "Rift S") == 0) {
		ohd->ohmd_device_type = OPENHMD_OCULUS_RIFT_HMD;
	} else {
		ohd->ohmd_device_type = OPENHMD_GENERIC_HMD;
	}

	snprintf(ohd->base.str, XRT_DEVICE_NAME_LEN, "%s (OpenHMD)", prod);
	snprintf(ohd->base.serial, XRT_DEVICE_NAME_LEN, "%s (OpenHMD)", prod);

	{
		/* right eye */
		if (!math_compute_fovs(info.views[1].display.w_meters, info.views[1].lens_center_x_meters,
		                       info.views[1].fov, info.views[1].display.h_meters,
		                       info.views[1].lens_center_y_meters, 0, &ohd->base.hmd->distortion.fov[1])) {
			OHMD_ERROR(ohd, "Failed to compute the partial fields of view.");
			free(ohd);
			return NULL;
		}
	}
	{
		/* left eye - just mirroring right eye now */
		ohd->base.hmd->distortion.fov[0].angle_up = ohd->base.hmd->distortion.fov[1].angle_up;
		ohd->base.hmd->distortion.fov[0].angle_down = ohd->base.hmd->distortion.fov[1].angle_down;

		ohd->base.hmd->distortion.fov[0].angle_left = -ohd->base.hmd->distortion.fov[1].angle_right;
		ohd->base.hmd->distortion.fov[0].angle_right = -ohd->base.hmd->distortion.fov[1].angle_left;
	}

	// clang-format off
	// Main display.
	ohd->base.hmd->screens[0].w_pixels = info.display.w_pixels;
	ohd->base.hmd->screens[0].h_pixels = info.display.h_pixels;
	ohd->base.hmd->screens[0].nominal_frame_interval_ns = info.display.nominal_frame_interval_ns;

	// Left
	ohd->base.hmd->views[0].display.w_pixels = info.views[0].display.w_pixels;
	ohd->base.hmd->views[0].display.h_pixels = info.views[0].display.h_pixels;
	ohd->base.hmd->views[0].viewport.x_pixels = 0;
	ohd->base.hmd->views[0].viewport.y_pixels = 0;
	ohd->base.hmd->views[0].viewport.w_pixels = info.views[0].display.w_pixels;
	ohd->base.hmd->views[0].viewport.h_pixels = info.views[0].display.h_pixels;
	ohd->base.hmd->views[0].rot = u_device_rotation_ident;

	// Right
	ohd->base.hmd->views[1].display.w_pixels = info.views[1].display.w_pixels;
	ohd->base.hmd->views[1].display.h_pixels = info.views[1].display.h_pixels;
	ohd->base.hmd->views[1].viewport.x_pixels = info.views[0].display.w_pixels;
	ohd->base.hmd->views[1].viewport.y_pixels = 0;
	ohd->base.hmd->views[1].viewport.w_pixels = info.views[1].display.w_pixels;
	ohd->base.hmd->views[1].viewport.h_pixels = info.views[1].display.h_pixels;
	ohd->base.hmd->views[1].rot = u_device_rotation_ident;

	OHMD_DEBUG(ohd,
			   "Display/viewport/offset before rotation %dx%d/%dx%d/%dx%d, "
			   "%dx%d/%dx%d/%dx%d",
			ohd->base.hmd->views[0].display.w_pixels,
			ohd->base.hmd->views[0].display.h_pixels,
			ohd->base.hmd->views[0].viewport.w_pixels,
			ohd->base.hmd->views[0].viewport.h_pixels,
			ohd->base.hmd->views[0].viewport.x_pixels,
			ohd->base.hmd->views[0].viewport.y_pixels,
			ohd->base.hmd->views[1].display.w_pixels,
			ohd->base.hmd->views[1].display.h_pixels,
			ohd->base.hmd->views[1].viewport.w_pixels,
			ohd->base.hmd->views[1].viewport.h_pixels,
			ohd->base.hmd->views[0].viewport.x_pixels,
			ohd->base.hmd->views[0].viewport.y_pixels);

	for (int view = 0; view < 2; view++) {
		ohd->distortion.openhmd[view].hmd_warp_param[0] = info.pano_distortion_k[0];
		ohd->distortion.openhmd[view].hmd_warp_param[1] = info.pano_distortion_k[1];
		ohd->distortion.openhmd[view].hmd_warp_param[2] = info.pano_distortion_k[2];
		ohd->distortion.openhmd[view].hmd_warp_param[3] = info.pano_distortion_k[3];
		ohd->distortion.openhmd[view].aberr[0] = info.pano_aberration_k[0];
		ohd->distortion.openhmd[view].aberr[1] = info.pano_aberration_k[1];
		ohd->distortion.openhmd[view].aberr[2] = info.pano_aberration_k[2];
		ohd->distortion.openhmd[view].warp_scale = info.pano_warp_scale;

		ohd->distortion.openhmd[view].lens_center.x = info.views[view].lens_center_x_meters;
		ohd->distortion.openhmd[view].lens_center.y = info.views[view].lens_center_y_meters;

		ohd->distortion.openhmd[view].viewport_scale.x = info.views[view].display.w_meters;
		ohd->distortion.openhmd[view].viewport_scale.y = info.views[view].display.h_meters;
	}
	// clang-format on

	ohd->base.hmd->distortion.models |= XRT_DISTORTION_MODEL_COMPUTE;
	ohd->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_COMPUTE;
	ohd->base.compute_distortion = compute_distortion_openhmd;

	// Which blend modes does the device support.

	size_t bm_idx = 0;
	if (info.quirks.video_see_through) {
		ohd->base.hmd->blend_modes[bm_idx++] = XRT_BLEND_MODE_ALPHA_BLEND;
	}
	ohd->base.hmd->blend_modes[bm_idx++] = XRT_BLEND_MODE_OPAQUE;
	ohd->base.hmd->blend_mode_count = bm_idx;

	if (info.quirks.video_distortion_vive) {
		// clang-format off
		// These need to be acquired from the vive config
		for (int view = 0; view < 2; view++) {
			ohd->distortion.vive[view].aspect_x_over_y = 0.8999999761581421f;
			ohd->distortion.vive[view].grow_for_undistort = 0.6000000238418579f;
		}
		ohd->distortion.vive[0].undistort_r2_cutoff = 1.11622154712677f;
		ohd->distortion.vive[1].undistort_r2_cutoff = 1.101870775222778f;
		ohd->distortion.vive[0].center[0].x = 0.08946027017045266f;
		ohd->distortion.vive[0].center[0].y = -0.009002181016260827f;
		ohd->distortion.vive[0].center[1].x = 0.08946027017045266f;
		ohd->distortion.vive[0].center[1].y = -0.009002181016260827f;
		ohd->distortion.vive[0].center[2].x = 0.08946027017045266f;
		ohd->distortion.vive[0].center[2].y = -0.009002181016260827f;
		ohd->distortion.vive[1].center[0].x = -0.08933516629552526f;
		ohd->distortion.vive[1].center[0].y = -0.006014565287238661f;
		ohd->distortion.vive[1].center[1].x = -0.08933516629552526f;
		ohd->distortion.vive[1].center[1].y = -0.006014565287238661f;
		ohd->distortion.vive[1].center[2].x = -0.08933516629552526f;
		ohd->distortion.vive[1].center[2].y = -0.006014565287238661f;

		//! @todo These values are most likely wrong, needs to be transposed and correct channel.
		// left
		// green
		ohd->distortion.vive[0].coefficients[0][0] = -0.188236068524731f;
		ohd->distortion.vive[0].coefficients[0][1] = -0.221086205321053f;
		ohd->distortion.vive[0].coefficients[0][2] = -0.2537849057915209f;
		ohd->distortion.vive[0].coefficients[0][3] = 0.0f;

		// blue
		ohd->distortion.vive[0].coefficients[1][0] = -0.07316590815739493f;
		ohd->distortion.vive[0].coefficients[1][1] = -0.02332400789561968f;
		ohd->distortion.vive[0].coefficients[1][2] = 0.02469959434698275f;
		ohd->distortion.vive[0].coefficients[1][3] = 0.0f;

		// red
		ohd->distortion.vive[0].coefficients[2][0] = -0.02223805567703767f;
		ohd->distortion.vive[0].coefficients[2][1] = -0.04931309279533211f;
		ohd->distortion.vive[0].coefficients[2][2] = -0.07862881939243466f;
		ohd->distortion.vive[0].coefficients[2][3] = 0.0f;

		// right
		// green
		ohd->distortion.vive[1].coefficients[0][0] = -0.1906209981894497f;
		ohd->distortion.vive[1].coefficients[0][1] = -0.2248896677207884f;
		ohd->distortion.vive[1].coefficients[0][2] = -0.2721364516782803f;
		ohd->distortion.vive[1].coefficients[0][3] = 0.0f;

		// blue
		ohd->distortion.vive[1].coefficients[1][0] = -0.07346071902951497f;
		ohd->distortion.vive[1].coefficients[1][1] = -0.02189527566250131f;
		ohd->distortion.vive[1].coefficients[1][2] = 0.0581378652359256f;
		ohd->distortion.vive[1].coefficients[1][3] = 0.0f;

		// red
		ohd->distortion.vive[1].coefficients[2][0] = -0.01755850332081247f;
		ohd->distortion.vive[1].coefficients[2][1] = -0.04517245633373419f;
		ohd->distortion.vive[1].coefficients[2][2] = -0.0928909347763f;
		ohd->distortion.vive[1].coefficients[2][3] = 0.0f;
		// clang-format on

		ohd->base.compute_distortion = compute_distortion_vive;
	}

	if (info.quirks.video_distortion_none) {
		u_distortion_mesh_set_none(&ohd->base);
	}

	if (info.quirks.left_center_pano_scale) {
		for (int view = 0; view < 2; view++) {
			ohd->distortion.openhmd[view].warp_scale = info.views[0].lens_center_x_meters;
		}
	}

	if (info.quirks.rotate_lenses_right) {
		OHMD_DEBUG(ohd, "Displays rotated right");

		// openhmd display dimensions are *after* all rotations
		swap(&ohd->base.hmd->screens->w_pixels, &ohd->base.hmd->screens->h_pixels);

		// display dimensions are *after* all rotations
		int w0 = info.views[0].display.w_pixels;
		int w1 = info.views[1].display.w_pixels;
		int h0 = info.views[0].display.h_pixels;
		int h1 = info.views[1].display.h_pixels;

		// viewports is *before* rotations, as the OS sees the display
		ohd->base.hmd->views[0].viewport.x_pixels = 0;
		ohd->base.hmd->views[0].viewport.y_pixels = 0;
		ohd->base.hmd->views[0].viewport.w_pixels = h0;
		ohd->base.hmd->views[0].viewport.h_pixels = w0;
		ohd->base.hmd->views[0].rot = u_device_rotation_right;

		ohd->base.hmd->views[1].viewport.x_pixels = 0;
		ohd->base.hmd->views[1].viewport.y_pixels = w0;
		ohd->base.hmd->views[1].viewport.w_pixels = h1;
		ohd->base.hmd->views[1].viewport.h_pixels = w1;
		ohd->base.hmd->views[1].rot = u_device_rotation_right;
	}

	if (info.quirks.rotate_lenses_left) {
		OHMD_DEBUG(ohd, "Displays rotated left");

		// openhmd display dimensions are *after* all rotations
		swap(&ohd->base.hmd->screens->w_pixels, &ohd->base.hmd->screens->h_pixels);

		// display dimensions are *after* all rotations
		int w0 = info.views[0].display.w_pixels;
		int w1 = info.views[1].display.w_pixels;
		int h0 = info.views[0].display.h_pixels;
		int h1 = info.views[1].display.h_pixels;

		// viewports is *before* rotations, as the OS sees the display
		ohd->base.hmd->views[0].viewport.x_pixels = 0;
		ohd->base.hmd->views[0].viewport.y_pixels = w0;
		ohd->base.hmd->views[0].viewport.w_pixels = h1;
		ohd->base.hmd->views[0].viewport.h_pixels = w1;
		ohd->base.hmd->views[0].rot = u_device_rotation_left;

		ohd->base.hmd->views[1].viewport.x_pixels = 0;
		ohd->base.hmd->views[1].viewport.y_pixels = 0;
		ohd->base.hmd->views[1].viewport.w_pixels = h0;
		ohd->base.hmd->views[1].viewport.h_pixels = w0;
		ohd->base.hmd->views[1].rot = u_device_rotation_left;
	}

	if (info.quirks.rotate_lenses_inwards) {
		OHMD_DEBUG(ohd, "Displays rotated inwards");

		int w2 = info.display.w_pixels / 2;
		int h = info.display.h_pixels;

		ohd->base.hmd->views[0].display.w_pixels = h;
		ohd->base.hmd->views[0].display.h_pixels = w2;
		ohd->base.hmd->views[0].viewport.x_pixels = 0;
		ohd->base.hmd->views[0].viewport.y_pixels = 0;
		ohd->base.hmd->views[0].viewport.w_pixels = w2;
		ohd->base.hmd->views[0].viewport.h_pixels = h;
		ohd->base.hmd->views[0].rot = u_device_rotation_right;

		ohd->base.hmd->views[1].display.w_pixels = h;
		ohd->base.hmd->views[1].display.h_pixels = w2;
		ohd->base.hmd->views[1].viewport.x_pixels = w2;
		ohd->base.hmd->views[1].viewport.y_pixels = 0;
		ohd->base.hmd->views[1].viewport.w_pixels = w2;
		ohd->base.hmd->views[1].viewport.h_pixels = h;
		ohd->base.hmd->views[1].rot = u_device_rotation_left;
	}

	OHMD_DEBUG(ohd,
	           "Display/viewport/offset after rotation %dx%d/%dx%d/%dx%d, "
	           "%dx%d/%dx%d/%dx%d",
	           ohd->base.hmd->views[0].display.w_pixels, ohd->base.hmd->views[0].display.h_pixels,
	           ohd->base.hmd->views[0].viewport.w_pixels, ohd->base.hmd->views[0].viewport.h_pixels,
	           ohd->base.hmd->views[0].viewport.x_pixels, ohd->base.hmd->views[0].viewport.y_pixels,
	           ohd->base.hmd->views[1].display.w_pixels, ohd->base.hmd->views[1].display.h_pixels,
	           ohd->base.hmd->views[1].viewport.w_pixels, ohd->base.hmd->views[1].viewport.h_pixels,
	           ohd->base.hmd->views[0].viewport.x_pixels, ohd->base.hmd->views[0].viewport.y_pixels);


	if (info.quirks.delay_after_initialization) {
		os_nanosleep(time_s_to_ns(1.0));
	}

	if (ohd->log_level <= U_LOGGING_DEBUG) {
		u_device_dump_config(&ohd->base, __func__, prod);
	}

	ohd->base.orientation_tracking_supported = (device_flags & OHMD_DEVICE_FLAGS_ROTATIONAL_TRACKING) != 0;
	ohd->base.position_tracking_supported = (device_flags & OHMD_DEVICE_FLAGS_POSITIONAL_TRACKING) != 0;
	ohd->base.device_type = XRT_DEVICE_TYPE_HMD;


	if (ohd->log_level <= U_LOGGING_DEBUG) {
		u_device_dump_config(&ohd->base, __func__, prod);
	}

	return ohd;
}

static struct oh_device *
create_controller(ohmd_context *ctx, int device_idx, int device_flags, enum xrt_device_type device_type)
{
	const char *prod = ohmd_list_gets(ctx, device_idx, OHMD_PRODUCT);
	ohmd_device *dev = ohmd_list_open_device(ctx, device_idx);
	if (dev == NULL) {
		return 0;
	}

	bool oculus_touch = false;

	// khronos simple controller has 4 inputs
	int input_count = 4;
	int output_count = 0;

	if (strcmp(prod, "Rift (CV1): Right Controller") == 0 || strcmp(prod, "Rift (CV1): Left Controller") == 0 ||
	    strcmp(prod, "Rift S: Right Controller") == 0 || strcmp(prod, "Rift S: Left Controller") == 0) {
		oculus_touch = true;

		input_count = INPUT_INDICES_LAST;
		output_count = 1;
	}

	enum u_device_alloc_flags flags = 0;
	struct oh_device *ohd = U_DEVICE_ALLOCATE(struct oh_device, flags, input_count, output_count);
	ohd->base.update_inputs = oh_device_update_inputs;
	ohd->base.set_output = oh_device_set_output;
	ohd->base.get_tracked_pose = oh_device_get_tracked_pose;
	ohd->base.get_view_poses = oh_device_get_view_poses;
	ohd->base.destroy = oh_device_destroy;
	if (oculus_touch) {
		ohd->ohmd_device_type = OPENHMD_OCULUS_RIFT_CONTROLLER;
		ohd->base.name = XRT_DEVICE_TOUCH_CONTROLLER;
	} else {
		ohd->ohmd_device_type = OPENHMD_GENERIC_CONTROLLER;
		ohd->base.name = XRT_DEVICE_GENERIC_HMD; //! @todo generic tracker
	}
	ohd->ctx = ctx;
	ohd->dev = dev;
	ohd->log_level = debug_get_log_option_ohmd_log();
	ohd->enable_finite_difference = debug_get_bool_option_ohmd_finite_diff();

	for (int i = 0; i < CONTROL_MAPPING_SIZE; i++) {
		ohd->controls_mapping[i] = 0;
	}

	if (oculus_touch) {
		SET_TOUCH_INPUT(X_CLICK);
		SET_TOUCH_INPUT(X_TOUCH);
		SET_TOUCH_INPUT(Y_CLICK);
		SET_TOUCH_INPUT(Y_TOUCH);
		SET_TOUCH_INPUT(MENU_CLICK);
		SET_TOUCH_INPUT(A_CLICK);
		SET_TOUCH_INPUT(A_TOUCH);
		SET_TOUCH_INPUT(B_CLICK);
		SET_TOUCH_INPUT(B_TOUCH);
		SET_TOUCH_INPUT(SYSTEM_CLICK);
		SET_TOUCH_INPUT(SQUEEZE_VALUE);
		SET_TOUCH_INPUT(TRIGGER_TOUCH);
		SET_TOUCH_INPUT(TRIGGER_VALUE);
		SET_TOUCH_INPUT(THUMBSTICK_CLICK);
		SET_TOUCH_INPUT(THUMBSTICK_TOUCH);
		SET_TOUCH_INPUT(THUMBSTICK);
		SET_TOUCH_INPUT(THUMBREST_TOUCH);
		SET_TOUCH_INPUT(GRIP_POSE);
		SET_TOUCH_INPUT(AIM_POSE);

		ohd->make_trigger_digital = false;

		ohd->base.outputs[0].name = XRT_OUTPUT_NAME_TOUCH_HAPTIC;

		ohd->controls_mapping[OHMD_TRIGGER] = OCULUS_TOUCH_TRIGGER_VALUE;
		ohd->controls_mapping[OHMD_SQUEEZE] = OCULUS_TOUCH_SQUEEZE_VALUE;
		ohd->controls_mapping[OHMD_MENU] = OCULUS_TOUCH_MENU_CLICK;
		ohd->controls_mapping[OHMD_HOME] = OCULUS_TOUCH_SYSTEM_CLICK;
		ohd->controls_mapping[OHMD_ANALOG_X] = OCULUS_TOUCH_THUMBSTICK;
		ohd->controls_mapping[OHMD_ANALOG_Y] = OCULUS_TOUCH_THUMBSTICK;
		ohd->controls_mapping[OHMD_ANALOG_PRESS] = OCULUS_TOUCH_THUMBSTICK_CLICK;
		ohd->controls_mapping[OHMD_BUTTON_A] = OCULUS_TOUCH_A_CLICK;
		ohd->controls_mapping[OHMD_BUTTON_B] = OCULUS_TOUCH_B_CLICK;
		ohd->controls_mapping[OHMD_BUTTON_X] = OCULUS_TOUCH_X_CLICK;
		ohd->controls_mapping[OHMD_BUTTON_Y] = OCULUS_TOUCH_Y_CLICK;
	} else {
		ohd->base.inputs[SIMPLE_SELECT_CLICK].name = XRT_INPUT_SIMPLE_SELECT_CLICK;
		ohd->base.inputs[SIMPLE_MENU_CLICK].name = XRT_INPUT_SIMPLE_MENU_CLICK;
		ohd->base.inputs[SIMPLE_GRIP_POSE].name = XRT_INPUT_SIMPLE_GRIP_POSE;
		ohd->base.inputs[SIMPLE_AIM_POSE].name = XRT_INPUT_SIMPLE_AIM_POSE;

		// XRT_INPUT_SIMPLE_SELECT_CLICK is digital input.
		// in case the hardware is an analog trigger, change the input after a half pulled trigger.
		ohd->make_trigger_digital = true;

		if (output_count > 0) {
			ohd->base.outputs[0].name = XRT_OUTPUT_NAME_SIMPLE_VIBRATION;
		}

		ohd->controls_mapping[OHMD_TRIGGER] = SIMPLE_SELECT_CLICK;
		ohd->controls_mapping[OHMD_MENU] = SIMPLE_MENU_CLICK;
	}

	snprintf(ohd->base.str, XRT_DEVICE_NAME_LEN, "%s (OpenHMD)", prod);
	snprintf(ohd->base.serial, XRT_DEVICE_NAME_LEN, "%s (OpenHMD)", prod);

	ohd->base.orientation_tracking_supported = (device_flags & OHMD_DEVICE_FLAGS_ROTATIONAL_TRACKING) != 0;
	ohd->base.position_tracking_supported = (device_flags & OHMD_DEVICE_FLAGS_POSITIONAL_TRACKING) != 0;
	ohd->base.device_type = device_type;

	ohmd_device_geti(ohd->dev, OHMD_CONTROLS_HINTS, ohd->controls_fn);
	ohmd_device_geti(ohd->dev, OHMD_CONTROLS_TYPES, ohd->controls_types);

	OHMD_DEBUG(ohd, "Created %s controller",
	           device_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER ? "left" : "right");

	return ohd;
}

int
oh_device_create(ohmd_context *ctx, bool no_hmds, struct xrt_device **out_xdevs)
{
	int hmd_idx = -1;
	int hmd_flags = 0;
	int left_idx = -1;
	int left_flags = 0;
	int right_idx = -1;
	int right_flags = 0;

	if (no_hmds) {
		return 0;
	}

	/* Probe for devices */
	int device_count = ohmd_ctx_probe(ctx);

	/* Then loop */
	for (int i = 0; i < device_count; i++) {
		int device_class = 0, device_flags = 0;
		const char *prod = NULL;

		ohmd_list_geti(ctx, i, OHMD_DEVICE_CLASS, &device_class);
		ohmd_list_geti(ctx, i, OHMD_DEVICE_FLAGS, &device_flags);

		if (device_class == OHMD_DEVICE_CLASS_CONTROLLER) {
			if ((device_flags & OHMD_DEVICE_FLAGS_LEFT_CONTROLLER) != 0) {
				if (left_idx != -1) {
					continue;
				}
				U_LOG_D("Selecting left controller idx %i", i);
				left_idx = i;
				left_flags = device_flags;
			}
			if ((device_flags & OHMD_DEVICE_FLAGS_RIGHT_CONTROLLER) != 0) {
				if (right_idx != -1) {
					continue;
				}
				U_LOG_D("Selecting right controller idx %i", i);
				right_idx = i;
				right_flags = device_flags;
			}

			continue;
		}

		if (device_class == OHMD_DEVICE_CLASS_HMD) {
			if (hmd_idx != -1) {
				continue;
			}
			U_LOG_D("Selecting hmd idx %i", i);
			hmd_idx = i;
			hmd_flags = device_flags;
		}

		if (device_flags & OHMD_DEVICE_FLAGS_NULL_DEVICE) {
			U_LOG_D("Rejecting device idx %i, is a NULL device.", i);
			continue;
		}

		prod = ohmd_list_gets(ctx, i, OHMD_PRODUCT);
		if (strcmp(prod, "External Device") == 0 && !debug_get_bool_option_ohmd_external()) {
			U_LOG_D("Rejecting device idx %i, is a External device.", i);
			continue;
		}

		if (hmd_idx != -1 && left_idx != -1 && right_idx != -1) {
			break;
		}
	}


	// All OpenHMD devices share the same tracking origin.
	// Default everything to 3dof (NONE), but 6dof when the HMD supports position tracking.
	//! @todo: support mix of 3dof and 6dof OpenHMD devices
	struct oh_system *sys = U_TYPED_CALLOC(struct oh_system);
	sys->base.type = XRT_TRACKING_TYPE_NONE;
	sys->base.offset.orientation.w = 1.0f;
	sys->hmd_idx = -1;
	sys->left_idx = -1;
	sys->right_idx = -1;

	u_var_add_root(sys, "OpenHMD Wrapper", false);

	int created = 0;
	if (!no_hmds && hmd_idx != -1) {
		struct oh_device *hmd = create_hmd(ctx, hmd_idx, hmd_flags);
		if (hmd) {
			hmd->sys = sys;
			hmd->base.tracking_origin = &sys->base;

			sys->hmd_idx = created;
			sys->devices[sys->hmd_idx] = hmd;

			if (hmd->base.position_tracking_supported) {
				sys->base.type = XRT_TRACKING_TYPE_OTHER;
			}

			out_xdevs[created++] = &hmd->base;
		}
	}

	if (left_idx != -1) {
		struct oh_device *left =
		    create_controller(ctx, left_idx, left_flags, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER);
		if (left) {
			left->sys = sys;
			left->base.tracking_origin = &sys->base;

			sys->left_idx = created;
			sys->devices[sys->left_idx] = left;

			out_xdevs[created++] = &left->base;
		}
	}

	if (right_idx != -1) {
		struct oh_device *right =
		    create_controller(ctx, right_idx, right_flags, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER);
		if (right) {
			right->sys = sys;
			right->base.tracking_origin = &sys->base;

			sys->right_idx = created;
			sys->devices[sys->right_idx] = right;

			out_xdevs[created++] = &right->base;
		}
	}

	for (int i = 0; i < XRT_MAX_DEVICES_PER_PROBE; i++) {
		if (sys->devices[i] != NULL) {
			u_var_add_ro_text(sys, sys->devices[i]->base.str, "OpenHMD Device");
		}
	}

	//! @todo initialize more devices like generic trackers (nolo)

	return created;
}
