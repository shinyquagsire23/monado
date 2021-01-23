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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifndef XRT_OS_WINDOWS
#include <unistd.h> // for sleep()
#endif

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

// Define this if you have the appropriately hacked-up OpenHMD version.
#undef OHMD_HAVE_ANG_VEL

struct openhmd_values
{
	float hmd_warp_param[4];
	float aberr[3];
	struct xrt_vec2 lens_center;
	struct xrt_vec2 viewport_scale;
	float warp_scale;
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

	enum u_logging_level ll;
	bool enable_finite_difference;

	struct
	{
		struct u_vive_values vive[2];
		struct openhmd_values openhmd[2];
	} distortion;
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

	// Remove the variable tracking.
	u_var_remove_root(ohd);

	if (ohd->dev != NULL) {
		ohmd_close_device(ohd->dev);
		ohd->dev = NULL;
	}

	u_device_free(&ohd->base);
}

static void
oh_device_update_inputs(struct xrt_device *xdev)
{
	// Empty
}

static void
oh_device_get_tracked_pose(struct xrt_device *xdev,
                           enum xrt_input_name name,
                           uint64_t at_timestamp_ns,
                           struct xrt_space_relation *out_relation)
{
	struct oh_device *ohd = oh_device(xdev);
	struct xrt_quat quat = {0.f, 0.f, 0.f, 1.f};
	struct xrt_vec3 pos = {0.f, 0.f, 0.f};

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
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
		OHMD_TRACE(ohd, "GET_TRACKED_POSE - no new data");
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

		OHMD_TRACE(ohd, "GET_TRACKED_POSE (%f, %f, %f, %f) (%f, %f, %f)", quat.x, quat.y, quat.z, quat.w,
		           ang_vel.x, ang_vel.y, ang_vel.z);
	} else {
		OHMD_TRACE(ohd, "GET_TRACKED_POSE (%f, %f, %f, %f)", quat.x, quat.y, quat.z, quat.w);
	}

	// Update state within driver
	ohd->last_update = now;
	ohd->last_relation = *out_relation;
}

static void
oh_device_get_view_pose(struct xrt_device *xdev,
                        struct xrt_vec3 *eye_relation,
                        uint32_t view_index,
                        struct xrt_pose *out_pose)
{
	struct xrt_pose pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
	bool adjust = view_index == 0;

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
get_info(struct oh_device *ohd, const char *prod)
{
	struct device_info info = {0};

	// clang-format off
	ohmd_device_getf(ohd->dev, OHMD_SCREEN_HORIZONTAL_SIZE, &info.display.w_meters);
	ohmd_device_getf(ohd->dev, OHMD_SCREEN_VERTICAL_SIZE, &info.display.h_meters);
	ohmd_device_getf(ohd->dev, OHMD_LENS_HORIZONTAL_SEPARATION, &info.lens_horizontal_separation);
	ohmd_device_getf(ohd->dev, OHMD_LENS_VERTICAL_POSITION, &info.lens_vertical_position);
	ohmd_device_getf(ohd->dev, OHMD_LEFT_EYE_FOV, &info.views[0].fov);
	ohmd_device_getf(ohd->dev, OHMD_RIGHT_EYE_FOV, &info.views[1].fov);
	ohmd_device_geti(ohd->dev, OHMD_SCREEN_HORIZONTAL_RESOLUTION, &info.display.w_pixels);
	ohmd_device_geti(ohd->dev, OHMD_SCREEN_VERTICAL_RESOLUTION, &info.display.h_pixels);
	ohmd_device_getf(ohd->dev, OHMD_UNIVERSAL_DISTORTION_K, &info.pano_distortion_k[0]);
	ohmd_device_getf(ohd->dev, OHMD_UNIVERSAL_ABERRATION_K, &info.pano_aberration_k[0]);

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
		ohmd_device_getf(ohd->dev, OHMD_SCREEN_HORIZONTAL_SIZE, &info.display.h_meters);
		ohmd_device_getf(ohd->dev, OHMD_SCREEN_VERTICAL_SIZE, &info.display.w_meters);
		ohmd_device_geti(ohd->dev, OHMD_SCREEN_HORIZONTAL_RESOLUTION, &info.display.h_pixels);
		ohmd_device_geti(ohd->dev, OHMD_SCREEN_VERTICAL_RESOLUTION, &info.display.w_pixels);
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

struct xrt_device *
oh_device_create(ohmd_context *ctx, ohmd_device *dev, const char *prod)
{
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct oh_device *ohd = U_DEVICE_ALLOCATE(struct oh_device, flags, 1, 0);
	ohd->base.update_inputs = oh_device_update_inputs;
	ohd->base.get_tracked_pose = oh_device_get_tracked_pose;
	ohd->base.get_view_pose = oh_device_get_view_pose;
	ohd->base.destroy = oh_device_destroy;
	ohd->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;
	ohd->base.name = XRT_DEVICE_GENERIC_HMD;
	ohd->ctx = ctx;
	ohd->dev = dev;
	ohd->ll = debug_get_log_option_ohmd_log();
	ohd->enable_finite_difference = debug_get_bool_option_ohmd_finite_diff();

	snprintf(ohd->base.str, XRT_DEVICE_NAME_LEN, "%s (OpenHMD)", prod);

	const struct device_info info = get_info(ohd, prod);

	{
		/* right eye */
		if (!math_compute_fovs(info.views[1].display.w_meters, info.views[1].lens_center_x_meters,
		                       info.views[1].fov, info.views[1].display.h_meters,
		                       info.views[1].lens_center_y_meters, 0, &ohd->base.hmd->views[1].fov)) {
			OHMD_ERROR(ohd, "Failed to compute the partial fields of view.");
			free(ohd);
			return NULL;
		}
	}
	{
		/* left eye - just mirroring right eye now */
		ohd->base.hmd->views[0].fov.angle_up = ohd->base.hmd->views[1].fov.angle_up;
		ohd->base.hmd->views[0].fov.angle_down = ohd->base.hmd->views[1].fov.angle_down;

		ohd->base.hmd->views[0].fov.angle_left = -ohd->base.hmd->views[1].fov.angle_right;
		ohd->base.hmd->views[0].fov.angle_right = -ohd->base.hmd->views[1].fov.angle_left;
	}

	// clang-format off
	// Main display.
	ohd->base.hmd->screens[0].w_pixels = info.display.w_pixels;
	ohd->base.hmd->screens[0].h_pixels = info.display.h_pixels;
	ohd->base.hmd->screens[0].nominal_frame_interval_ns = info.display.nominal_frame_interval_ns;

	// Left
	ohd->base.hmd->views[0].display.w_meters = info.views[0].display.w_meters;
	ohd->base.hmd->views[0].display.h_meters = info.views[0].display.h_meters;
	ohd->base.hmd->views[0].display.w_pixels = info.views[0].display.w_pixels;
	ohd->base.hmd->views[0].display.h_pixels = info.views[0].display.h_pixels;
	ohd->base.hmd->views[0].viewport.x_pixels = 0;
	ohd->base.hmd->views[0].viewport.y_pixels = 0;
	ohd->base.hmd->views[0].viewport.w_pixels = info.views[0].display.w_pixels;
	ohd->base.hmd->views[0].viewport.h_pixels = info.views[0].display.h_pixels;
	ohd->base.hmd->views[0].rot = u_device_rotation_ident;

	// Right
	ohd->base.hmd->views[1].display.w_meters = info.views[1].display.w_meters;
	ohd->base.hmd->views[1].display.h_meters = info.views[1].display.h_meters;
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

		ohd->distortion.openhmd[view].viewport_scale.x = ohd->base.hmd->views[view].display.w_meters;
		ohd->distortion.openhmd[view].viewport_scale.y = ohd->base.hmd->views[view].display.h_meters;
	}
	// clang-format on

	ohd->base.hmd->distortion.models |= XRT_DISTORTION_MODEL_COMPUTE;
	ohd->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_COMPUTE;
	ohd->base.compute_distortion = compute_distortion_openhmd;

	// Which blend modes does the device support.
	ohd->base.hmd->blend_mode = XRT_BLEND_MODE_OPAQUE;
	if (info.quirks.video_see_through) {
		ohd->base.hmd->blend_mode =
		    (enum xrt_blend_mode)(ohd->base.hmd->blend_mode | XRT_BLEND_MODE_ALPHA_BLEND);
	}

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
		ohd->base.hmd->views[1].viewport.y_pixels = h0;
		ohd->base.hmd->views[1].viewport.w_pixels = w1;
		ohd->base.hmd->views[1].viewport.h_pixels = h1;
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
		unsigned int time_to_sleep = 1;
		do {
			//! @todo convert to os_nanosleep
			time_to_sleep = sleep(time_to_sleep);
		} while (time_to_sleep);
	}

	if (ohd->ll <= U_LOGGING_DEBUG) {
		u_device_dump_config(&ohd->base, __func__, prod);
	}

	u_var_add_root(ohd, "OpenHMD Wrapper", true);
	u_var_add_ro_text(ohd, ohd->base.str, "Card");

	return &ohd->base;
}
