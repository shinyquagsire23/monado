// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Adaptor to a OpenHMD device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ohmd
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "openhmd.h"

#include "math/m_api.h"
#include "xrt/xrt_device.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_time.h"

#include "oh_device.h"

// Should we permit finite differencing to compute angular velocities when not
// directly retrieved?
DEBUG_GET_ONCE_BOOL_OPTION(oh_finite_diff, "OH_ALLOW_FINITE_DIFF", true)

// Define this if you have the appropriately hacked-up OpenHMD version.
#undef OHMD_HAVE_ANG_VEL

static void
oh_device_destroy(struct xrt_device *xdev)
{
	struct oh_device *ohd = oh_device(xdev);

	if (ohd->dev != NULL) {
		ohmd_close_device(ohd->dev);
		ohd->dev = NULL;
	}

	free(ohd);
}

static void
oh_device_update_inputs(struct xrt_device *xdev, struct time_state *timekeeping)
{
	// Empty
}

static void
oh_device_get_tracked_pose(struct xrt_device *xdev,
                           enum xrt_input_name name,
                           struct time_state *timekeeping,
                           int64_t *out_timestamp,
                           struct xrt_space_relation *out_relation)
{
	struct oh_device *ohd = oh_device(xdev);
	struct xrt_quat quat = {0.f, 0.f, 0.f, 1.f};

	if (name != XRT_INPUT_GENERIC_HEAD_RELATION) {
		OH_ERROR(ohd, "unknown input name");
		return;
	}

	ohmd_ctx_update(ohd->ctx);
	int64_t now = time_state_get_now(timekeeping);
	//! @todo adjust for latency here
	*out_timestamp = now;
	ohmd_device_getf(ohd->dev, OHMD_ROTATION_QUAT, &quat.x);
	out_relation->pose.orientation = quat;
	//! @todo assuming that orientation is actually currently tracked.
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

	bool have_ang_vel = false;
	struct xrt_vec3 ang_vel;
#ifdef OHMD_HAVE_ANG_VEL
	if (!ohd->skip_ang_vel) {
		if (0 == ohmd_device_getf(ohd->dev, OHMD_ANGULAR_VELOCITY,
		                          &ang_vel.x)) {
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
		*out_timestamp = ohd->last_update;
		*out_relation = ohd->last_relation;
		OH_SPEW(ohd, "GET_TRACKED_POSE - no new data");
		return;
	}

	/*!
	 * @todo possibly hoist this out of the driver level, to provide as a
	 * common service?
	 */
	if (ohd->enable_finite_difference && !have_ang_vel) {
		// No angular velocity
		float dt = time_ns_to_s(*out_timestamp - ohd->last_update);
		if (ohd->last_update == 0) {
			// This is the first report, so just print a warning
			// instead of estimating ang vel.
			OH_DEBUG(ohd,
			         "Will use finite differencing to estimate "
			         "angular velocity.");
		} else if (dt < 1.0f && dt > 0.0005) {
			// but we can compute it:
			// last report was not long ago but not
			// instantaneously (at least half a millisecond),
			// so approximately safe to do this.
			math_quat_finite_difference(&old_quat, &quat, dt,
			                            &ang_vel);
			have_ang_vel = true;
		}
	}

	if (have_ang_vel) {
		out_relation->angular_velocity = ang_vel;
		out_relation->relation_flags = (enum xrt_space_relation_flags)(
		    out_relation->relation_flags |
		    XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT);

		OH_SPEW(ohd, "GET_TRACKED_POSE (%f, %f, %f, %f) (%f, %f, %f)",
		        quat.x, quat.y, quat.z, quat.w, ang_vel.x, ang_vel.y,
		        ang_vel.z);
	} else {
		OH_SPEW(ohd, "GET_TRACKED_POSE (%f, %f, %f, %f)", quat.x,
		        quat.y, quat.z, quat.w);
	}

	// Update state within driver
	ohd->last_update = *out_timestamp;
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

	struct
	{
		bool rotate_lenses_right;
		bool rotate_lenses_inwards;
		bool video_see_through;
		bool video_distortion_none;
		bool video_distortion_vive;
		bool left_center_pano_scale;
		bool rotate_screen_right_after;
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
	}

	if (strcmp(prod, "PSVR") == 0) {
		info.quirks.video_distortion_none = true;
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

	//! @todo This is most definitely wrong!
	//!       3Glasses likes the opposite better.
	info.pano_warp_scale =
		(info.views[0].lens_center_x_meters > info.views[0].lens_center_x_meters) ?
			info.views[0].lens_center_x_meters :
			info.views[0].lens_center_x_meters;
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

struct oh_device *
oh_device_create(ohmd_context *ctx,
                 ohmd_device *dev,
                 const char *prod,
                 bool print_spew,
                 bool print_debug)
{
	enum u_device_alloc_flags flags = (enum u_device_alloc_flags)(
	    U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct oh_device *ohd =
	    U_DEVICE_ALLOCATE(struct oh_device, flags, 1, 0);
	ohd->base.update_inputs = oh_device_update_inputs;
	ohd->base.get_tracked_pose = oh_device_get_tracked_pose;
	ohd->base.get_view_pose = oh_device_get_view_pose;
	ohd->base.destroy = oh_device_destroy;
	ohd->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_RELATION;
	ohd->ctx = ctx;
	ohd->dev = dev;
	ohd->print_spew = print_spew;
	ohd->print_debug = print_debug;
	ohd->enable_finite_difference = debug_get_bool_option_oh_finite_diff();

	snprintf(ohd->base.name, XRT_DEVICE_NAME_LEN, "%s", prod);

	const struct device_info info = get_info(ohd, prod);

	{
		/* right eye */
		if (!math_compute_fovs(info.views[1].display.w_meters,
		                       info.views[1].lens_center_x_meters,
		                       info.views[1].fov,
		                       info.views[1].display.h_meters,
		                       info.views[1].lens_center_y_meters, 0,
		                       &ohd->base.hmd->views[1].fov)) {
			OH_ERROR(
			    ohd,
			    "Failed to compute the partial fields of view.");
			free(ohd);
			return NULL;
		}
	}
	{
		/* left eye - just mirroring right eye now */
		ohd->base.hmd->views[0].fov.angle_up =
		    ohd->base.hmd->views[1].fov.angle_up;
		ohd->base.hmd->views[0].fov.angle_down =
		    ohd->base.hmd->views[1].fov.angle_down;

		ohd->base.hmd->views[0].fov.angle_left =
		    -ohd->base.hmd->views[1].fov.angle_right;
		ohd->base.hmd->views[0].fov.angle_right =
		    -ohd->base.hmd->views[1].fov.angle_left;
	}

	// clang-format off
	// Main display.
	ohd->base.hmd->distortion.models = XRT_DISTORTION_MODEL_PANOTOOLS;
	ohd->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_PANOTOOLS;
	ohd->base.hmd->screens[0].w_pixels = info.display.w_pixels;
	ohd->base.hmd->screens[0].h_pixels = info.display.h_pixels;
	ohd->base.hmd->screens[0].nominal_frame_interval_ns = info.display.nominal_frame_interval_ns;
	ohd->base.hmd->distortion.pano.distortion_k[0] = info.pano_distortion_k[0];
	ohd->base.hmd->distortion.pano.distortion_k[1] = info.pano_distortion_k[1];
	ohd->base.hmd->distortion.pano.distortion_k[2] = info.pano_distortion_k[2];
	ohd->base.hmd->distortion.pano.distortion_k[3] = info.pano_distortion_k[3];
	ohd->base.hmd->distortion.pano.aberration_k[0] = info.pano_aberration_k[0];
	ohd->base.hmd->distortion.pano.aberration_k[1] = info.pano_aberration_k[1];
	ohd->base.hmd->distortion.pano.aberration_k[2] = info.pano_aberration_k[2];
	ohd->base.hmd->distortion.pano.warp_scale = info.pano_warp_scale;

	// Left
	ohd->base.hmd->views[0].display.w_meters = info.views[0].display.w_meters;
	ohd->base.hmd->views[0].display.h_meters = info.views[0].display.h_meters;
	ohd->base.hmd->views[0].lens_center.x_meters = info.views[0].lens_center_x_meters;
	ohd->base.hmd->views[0].lens_center.y_meters = info.views[0].lens_center_y_meters;
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
	ohd->base.hmd->views[1].lens_center.x_meters = info.views[1].lens_center_x_meters;
	ohd->base.hmd->views[1].lens_center.y_meters = info.views[1].lens_center_y_meters;
	ohd->base.hmd->views[1].display.w_pixels = info.views[1].display.w_pixels;
	ohd->base.hmd->views[1].display.h_pixels = info.views[1].display.h_pixels;
	ohd->base.hmd->views[1].viewport.x_pixels = info.views[0].display.w_pixels;
	ohd->base.hmd->views[1].viewport.y_pixels = 0;
	ohd->base.hmd->views[1].viewport.w_pixels = info.views[1].display.w_pixels;
	ohd->base.hmd->views[1].viewport.h_pixels = info.views[1].display.h_pixels;
	ohd->base.hmd->views[1].rot = u_device_rotation_ident;
	// clang-format on

	// Which blend modes does the device support.
	ohd->base.hmd->blend_mode = XRT_BLEND_MODE_OPAQUE;
	if (info.quirks.video_see_through) {
		ohd->base.hmd->blend_mode = (enum xrt_blend_mode)(
		    ohd->base.hmd->blend_mode | XRT_BLEND_MODE_ALPHA_BLEND);
	}

	if (info.quirks.video_distortion_vive) {
		ohd->base.hmd->distortion.models = (enum xrt_distortion_model)(
		    ohd->base.hmd->distortion.models |
		    XRT_DISTORTION_MODEL_VIVE);
		ohd->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_VIVE;

		// clang-format off
		// These need to be acquired from the vive config
		ohd->base.hmd->distortion.vive.aspect_x_over_y = 0.8999999761581421f;
		ohd->base.hmd->distortion.vive.grow_for_undistort = 0.6000000238418579f;
		ohd->base.hmd->distortion.vive.undistort_r2_cutoff[0] = 1.11622154712677f;
		ohd->base.hmd->distortion.vive.undistort_r2_cutoff[1] = 1.101870775222778f;
		ohd->base.hmd->distortion.vive.center[0][0] = 0.08946027017045266f;
		ohd->base.hmd->distortion.vive.center[0][1] = -0.009002181016260827f;
		ohd->base.hmd->distortion.vive.center[1][0] = -0.08933516629552526f;
		ohd->base.hmd->distortion.vive.center[1][1] = -0.006014565287238661f;

		// left
		// green
		ohd->base.hmd->distortion.vive.coefficients[0][0][0] = -0.188236068524731f;
		ohd->base.hmd->distortion.vive.coefficients[0][0][1] = -0.221086205321053f;
		ohd->base.hmd->distortion.vive.coefficients[0][0][2] = -0.2537849057915209f;

		// blue
		ohd->base.hmd->distortion.vive.coefficients[0][1][0] = -0.07316590815739493f;
		ohd->base.hmd->distortion.vive.coefficients[0][1][1] = -0.02332400789561968f;
		ohd->base.hmd->distortion.vive.coefficients[0][1][2] = 0.02469959434698275f;

		// red
		ohd->base.hmd->distortion.vive.coefficients[0][2][0] = -0.02223805567703767f;
		ohd->base.hmd->distortion.vive.coefficients[0][2][1] = -0.04931309279533211f;
		ohd->base.hmd->distortion.vive.coefficients[0][2][2] = -0.07862881939243466f;

		// right
		// green
		ohd->base.hmd->distortion.vive.coefficients[1][0][0] = -0.1906209981894497f;
		ohd->base.hmd->distortion.vive.coefficients[1][0][1] = -0.2248896677207884f;
		ohd->base.hmd->distortion.vive.coefficients[1][0][2] = -0.2721364516782803f;

		// blue
		ohd->base.hmd->distortion.vive.coefficients[1][1][0] = -0.07346071902951497f;
		ohd->base.hmd->distortion.vive.coefficients[1][1][1] = -0.02189527566250131f;
		ohd->base.hmd->distortion.vive.coefficients[1][1][2] = 0.0581378652359256f;

		// red
		ohd->base.hmd->distortion.vive.coefficients[1][2][0] = -0.01755850332081247f;
		ohd->base.hmd->distortion.vive.coefficients[1][2][1] = -0.04517245633373419f;
		ohd->base.hmd->distortion.vive.coefficients[1][2][2] = -0.0928909347763f;
		// clang-format on
	}

	if (info.quirks.video_distortion_none) {
		ohd->base.hmd->distortion.models = XRT_DISTORTION_MODEL_NONE;
		ohd->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_NONE;
	}

	if (info.quirks.left_center_pano_scale) {
		ohd->base.hmd->distortion.pano.warp_scale =
		    info.views[0].lens_center_x_meters;
	}

	if (info.quirks.rotate_lenses_right) {
		int w = info.display.w_pixels;
		int h = info.display.h_pixels;

		ohd->base.hmd->views[0].viewport.x_pixels = 0;
		ohd->base.hmd->views[0].viewport.y_pixels = 0;
		ohd->base.hmd->views[0].viewport.w_pixels = w;
		ohd->base.hmd->views[0].viewport.h_pixels = h / 2;
		ohd->base.hmd->views[0].rot = u_device_rotation_right;

		ohd->base.hmd->views[1].viewport.x_pixels = 0;
		ohd->base.hmd->views[1].viewport.y_pixels = h / 2;
		ohd->base.hmd->views[1].viewport.w_pixels = w;
		ohd->base.hmd->views[1].viewport.h_pixels = h / 2;
		ohd->base.hmd->views[1].rot = u_device_rotation_right;
	}

	if (info.quirks.rotate_lenses_inwards) {
		int w2 = info.display.w_pixels / 2;
		int h = info.display.h_pixels;

		ohd->base.hmd->views[0].display.w_pixels = h;
		ohd->base.hmd->views[0].display.h_pixels = w2;
		ohd->base.hmd->views[0].rot = u_device_rotation_right;

		ohd->base.hmd->views[1].display.w_pixels = h;
		ohd->base.hmd->views[1].display.h_pixels = w2;
		ohd->base.hmd->views[1].rot = u_device_rotation_left;
	}

	if (ohd->print_debug) {
		u_device_dump_config(&ohd->base, __func__, prod);
	}

	return ohd;
}
