// Copyright 2019, Collabora, Ltd.
// Copyright 2014, Kevin M. Godby
// Copyright 2014-2018, Sensics, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Driver for an OSVR Hacker Dev Kit device.
 *
 * Based in part on the corresponding VRPN driver,
 * available under BSL-1.0.
 *
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Kevin M. Godby <kevin@godby.org>
 * @ingroup drv_hdk
 */


#include "math/m_mathinclude.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <type_traits>

#include "xrt/xrt_device.h"

#include "os/os_hid.h"
#include "os/os_time.h"

#include "math/m_api.h"

#include "util/u_debug.h"
#include "util/u_misc.h"
#include "util/u_device.h"
#include "util/u_time.h"
#include "util/u_distortion_mesh.h"

#include "hdk_device.h"

static constexpr uint8_t BITS_PER_BYTE = 8;

DEBUG_GET_ONCE_LOG_OPTION(hdk_log, "HDK_LOG", U_LOGGING_WARN)

/**
 * A fixed-point to float conversion function.
 *
 * Values are signed, two's-complement, if the supplied integer is.
 *
 * The conversion is effectively from the fixed-point arithmetic type known
 * "unambiguously" as Q INT_BITS.FRAC_BITS - the number of integer
 * bits is not inferred, though it is checked to ensure it adds up.
 *
 * @tparam INT_BITS The number of bits devoted to the integer part.
 * @tparam FRAC_BITS The number of bits devoted to the fractional
 * part.
 * @tparam IntegerType The input integer type, typically deduced (do not need to
 * specify explicitly)
 * @param v An input "integer" that is actually a fixed-point value.
 *
 * INT_BITS and FRAC_BITS must sum to 8 * sizeof(v), the bit width of
 * the input integer, for unsigned values, or to one less than that (for the
 * sign bit) for signed values.
 *
 * Based in part on the VRPN header vrpn_FixedPoint.h,
 * available under BSL-1.0.
 */
template <size_t INT_BITS, size_t FRAC_BITS, typename IntegerType>
static inline float
fromFixedPoint(IntegerType v)
{
	constexpr size_t SIGN_BIT = std::is_signed<IntegerType>::value ? 1 : 0;
	static_assert(INT_BITS + FRAC_BITS + SIGN_BIT == BITS_PER_BYTE * sizeof(IntegerType),
	              "INT_BITS and FRAC_BITS, plus 1 for a sign bit "
	              "if applicable, must sum to the input "
	              "integer width, but do not.");
	return static_cast<float>(v) / (1 << FRAC_BITS);
}

static inline uint16_t
hdk_get_le_uint16(uint8_t *&bufPtr)
{
	assert(bufPtr != nullptr);
	uint16_t ret = static_cast<uint16_t>(*bufPtr) | (static_cast<uint16_t>(*(bufPtr + 1)) << BITS_PER_BYTE);
	bufPtr += 2;
	return ret;
}

static inline int16_t
hdk_get_le_int16(uint8_t *&bufPtr)
{
	return static_cast<int16_t>(hdk_get_le_uint16(bufPtr));
}

static void
hdk_device_destroy(struct xrt_device *xdev)
{
	struct hdk_device *hd = hdk_device(xdev);

	os_thread_helper_destroy(&hd->imu_thread);

	if (hd->dev != NULL) {
		os_hid_destroy(hd->dev);
		hd->dev = NULL;
	}

	free(hd);
}

static void
hdk_device_update_inputs(struct xrt_device *xdev)
{
	// Empty
}

static constexpr uint8_t MSG_LEN_LARGE = 32;
static constexpr uint8_t MSG_LEN_SMALL = 16;

static int
hdk_device_update(struct hdk_device *hd)
{
	uint8_t buffer[MSG_LEN_LARGE];

	auto bytesRead = os_hid_read(hd->dev, buffer, sizeof(buffer), 0);
	if (bytesRead == -1) {
		if (!hd->disconnect_notified) {
			HDK_ERROR(hd,
			          "%s: HDK appeared to disconnect. Please "
			          "quit, reconnect, and try again.",
			          __func__);
			hd->disconnect_notified = true;
		}
		hd->quat_valid = false;
		return 0;
	}
	while (bytesRead > 0) {
		if (bytesRead != MSG_LEN_LARGE && bytesRead != MSG_LEN_SMALL) {
			HDK_DEBUG(hd, "Only got %d bytes", bytesRead);
			hd->quat_valid = false;
			return 1;
		}
		bytesRead = os_hid_read(hd->dev, buffer, sizeof(buffer), 0);
	}

	uint8_t *buf = &(buffer[0]);

#if 0
	uint8_t version = uint8_t(0x0f) & *buf;
	uint8_t hdmi_status = (uint8_t(0xf0) & *buf) >> 4;
#endif
	buf++;

	// HDMI status only valid in reports version 3.
	// Expecting either version 1 (100Hz) or 3 (400Hz):
	// https://github.com/OSVR/OSVR-HDK-MCU-Firmware/blob/master/Source%20code/Embedded/src/DeviceDrivers/BNO070_using_hostif.c#L511

	// Next byte is sequence number, ignore
	buf++;

	struct xrt_quat quat;
	static constexpr int INT_BITS = 1;
	static constexpr int FRAC_BITS = 14;
	quat.x = fromFixedPoint<INT_BITS, FRAC_BITS>(hdk_get_le_int16(buf));
	quat.z = fromFixedPoint<INT_BITS, FRAC_BITS>(hdk_get_le_int16(buf)) * -1;
	quat.y = fromFixedPoint<INT_BITS, FRAC_BITS>(hdk_get_le_int16(buf));
	quat.w = fromFixedPoint<INT_BITS, FRAC_BITS>(hdk_get_le_int16(buf));

// Used to produce 90 degree rotations
#define HDK_SIN_PI_OVER_4 0.7071068f
	struct xrt_quat rot_90_about_x
	{
		HDK_SIN_PI_OVER_4, 0, 0, HDK_SIN_PI_OVER_4
	};
	struct xrt_quat negative_90_about_y
	{
		0, -HDK_SIN_PI_OVER_4, 0, HDK_SIN_PI_OVER_4
	};
	// The flipping of components and this get us close, except we are
	// looking 90 to the right of where we want.
	math_quat_rotate(&quat, &rot_90_about_x, &quat);

	// Fix that 90
	math_quat_rotate(&negative_90_about_y, &quat, &quat);

	hd->quat = quat;

	/// @todo might not be accurate on some version 1 reports??

	// This is in the "world" coordinate system.

	// Note that we must "rotate" this velocity by the first transform above
	// (90 about x), hence putting it in a pure quat.
	struct xrt_quat ang_vel_quat;
	ang_vel_quat.x = fromFixedPoint<6, 9>(hdk_get_le_int16(buf));
	ang_vel_quat.z = fromFixedPoint<6, 9>(hdk_get_le_int16(buf)) * -1;
	ang_vel_quat.y = fromFixedPoint<6, 9>(hdk_get_le_int16(buf));
	ang_vel_quat.w = 0;

	// need the inverse rotation here
	struct xrt_quat negative_90_about_x
	{
		- HDK_SIN_PI_OVER_4, 0, 0, HDK_SIN_PI_OVER_4
	};
	math_quat_rotate(&ang_vel_quat, &rot_90_about_x, &ang_vel_quat);
	math_quat_rotate(&negative_90_about_x, &ang_vel_quat, &ang_vel_quat);

	hd->ang_vel_quat = ang_vel_quat;

	hd->quat_valid = true;
	return 1;
}

static void
hdk_device_get_tracked_pose(struct xrt_device *xdev,
                            enum xrt_input_name name,
                            uint64_t requested_timestamp_ns,
                            struct xrt_space_relation *out_relation)
{
	struct hdk_device *hd = hdk_device(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		HDK_ERROR(hd, "unknown input name");
		return;
	}

	uint64_t now = os_monotonic_get_ns();

	// Adjusting for latency - 14ms, found empirically.
	now -= 14000000;

	if (!hd->quat_valid) {
		out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
		HDK_TRACE(hd, "GET_TRACKED_POSE: No pose");
		return;
	}

	os_thread_helper_lock(&hd->imu_thread);

	out_relation->pose.orientation = hd->quat;

	out_relation->angular_velocity.x = hd->ang_vel_quat.x;
	out_relation->angular_velocity.y = hd->ang_vel_quat.y;
	out_relation->angular_velocity.z = hd->ang_vel_quat.z;

	out_relation->relation_flags = xrt_space_relation_flags(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                        XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT |
	                                                        XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

	os_thread_helper_unlock(&hd->imu_thread);

	HDK_TRACE(hd, "GET_TRACKED_POSE (%f, %f, %f, %f) ANG_VEL (%f, %f, %f)", hd->quat.x, hd->quat.y, hd->quat.z,
	          hd->quat.w, hd->ang_vel_quat.x, hd->ang_vel_quat.y, hd->ang_vel_quat.z);
}

static void
hdk_device_get_view_pose(struct xrt_device *xdev,
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

static void *
hdk_device_run_thread(void *ptr)
{
	struct hdk_device *hd = hdk_device((struct xrt_device *)ptr);

	os_thread_helper_lock(&hd->imu_thread);
	while (os_thread_helper_is_running_locked(&hd->imu_thread)) {
		os_thread_helper_unlock(&hd->imu_thread);

		if (!hdk_device_update(hd)) {
			return NULL;
		}

		// Just keep swimming.
		os_thread_helper_lock(&hd->imu_thread);
	}
	return NULL;
}

#define HDK_DEBUG_INT(hd, name, val) HDK_DEBUG(hd, "\t%s = %u", name, val)

#define HDK_DEBUG_MM(hd, name, val)                                                                                    \
	HDK_DEBUG(hd, "\t%s = %i.%02imm", name, (int32_t)(val * 1000.f), abs((int32_t)(val * 100000.f)) % 100)

#define HDK_DEBUG_ANGLE(hd, name, val) HDK_DEBUG(hd, "\t%s = %f (%i)", name, val, (int32_t)(val * (180 / M_PI)))

#define HDK_DEBUG_MAT2X2(hd, name, rot)                                                                                \
	HDK_DEBUG(hd, "\t%s = {%f, %f} {%f, %f}", name, rot.v[0], rot.v[1], rot.v[2], rot.v[3])

struct hdk_device *
hdk_device_create(struct os_hid_device *dev, enum HDK_VARIANT variant)
{
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct hdk_device *hd = U_DEVICE_ALLOCATE(struct hdk_device, flags, 1, 0);

	hd->base.hmd->blend_mode = XRT_BLEND_MODE_OPAQUE;
	hd->base.update_inputs = hdk_device_update_inputs;
	hd->base.get_tracked_pose = hdk_device_get_tracked_pose;
	hd->base.get_view_pose = hdk_device_get_view_pose;
	hd->base.destroy = hdk_device_destroy;
	hd->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;
	hd->base.name = XRT_DEVICE_GENERIC_HMD;
	hd->dev = dev;
	hd->ll = debug_get_log_option_hdk_log();

	snprintf(hd->base.str, XRT_DEVICE_NAME_LEN, "OSVR HDK-family Device");

	if (variant == HDK_UNKNOWN) {
		HDK_ERROR(hd, "Don't know which HDK variant this is.");
		hdk_device_destroy(&hd->base);
		return NULL;
	}

	double hFOV;
	double vFOV;
	double hCOP = 0.5;
	double vCOP = 0.5;

	switch (variant) {
	default:
	case HDK_UNKNOWN:
		HDK_ERROR(hd, "Don't know which HDK variant this is.");
		hdk_device_destroy(&hd->base);
		return NULL;

	case HDK_VARIANT_1_2:
		// Distortion optional - this is for no distortion.
		hFOV = 90;
		vFOV = 96.73;
		break;

	case HDK_VARIANT_1_3_1_4:
		// Non-mesh distortion.
		hFOV = 90;
		vFOV = 96.73;
		hCOP = 0.529;
		break;

	case HDK_VARIANT_2:
		// Mesh distortion (ideally)
		hFOV = vFOV = 92.0;
		break;
	}

	constexpr double DEGREES_TO_RADIANS = M_PI / 180.0;
	{
		/* right eye */
		math_compute_fovs(1.0, hCOP, hFOV * DEGREES_TO_RADIANS, 1, vCOP, vFOV * DEGREES_TO_RADIANS,
		                  &hd->base.hmd->views[1].fov);
	}
	{
		/* left eye - just mirroring right eye now */
		hd->base.hmd->views[0].fov.angle_up = hd->base.hmd->views[1].fov.angle_up;
		hd->base.hmd->views[0].fov.angle_down = hd->base.hmd->views[1].fov.angle_down;

		hd->base.hmd->views[0].fov.angle_left = -hd->base.hmd->views[1].fov.angle_right;
		hd->base.hmd->views[0].fov.angle_right = -hd->base.hmd->views[1].fov.angle_left;
	}

	switch (variant) {
	case HDK_UNKNOWN: assert(!"unknown device"); break;

	case HDK_VARIANT_2: {
		hd->base.hmd->screens[0].nominal_frame_interval_ns = time_s_to_ns(1.0f / 90.0f);
		constexpr int panel_w = 1080;
		constexpr int panel_h = 1200;
		// Padding needed horizontally per side.
		constexpr int vert_padding = (panel_h - panel_w) / 2;
		// HDK2 is upside down :facepalm:

		// clang-format off
		// Main display.
		hd->base.hmd->screens[0].w_pixels = panel_w * 2;
		hd->base.hmd->screens[0].h_pixels = panel_h;
#ifndef HDK_DO_NOT_FLIP_HDK2_SCREEN
		// Left
		hd->base.hmd->views[0].display.w_pixels = panel_w;
		hd->base.hmd->views[0].display.h_pixels = panel_h;
		hd->base.hmd->views[0].viewport.x_pixels = panel_w; // right half of display
		hd->base.hmd->views[0].viewport.y_pixels = vert_padding;
		hd->base.hmd->views[0].viewport.w_pixels = panel_w;
		hd->base.hmd->views[0].viewport.h_pixels = panel_w;
		hd->base.hmd->views[0].rot = u_device_rotation_180;

		// Right
		hd->base.hmd->views[1].display.w_pixels = panel_w;
		hd->base.hmd->views[1].display.h_pixels = panel_h;
		hd->base.hmd->views[1].viewport.x_pixels = 0;
		hd->base.hmd->views[1].viewport.y_pixels = vert_padding;
		hd->base.hmd->views[1].viewport.w_pixels = panel_w;
		hd->base.hmd->views[1].viewport.h_pixels = panel_w;
		hd->base.hmd->views[1].rot = u_device_rotation_180;
#else
		// Left
		hd->base.hmd->views[0].display.w_pixels = panel_w;
		hd->base.hmd->views[0].display.h_pixels = panel_h;
		hd->base.hmd->views[0].viewport.x_pixels = 0;
		hd->base.hmd->views[0].viewport.y_pixels = vert_padding;
		hd->base.hmd->views[0].viewport.w_pixels = panel_w;
		hd->base.hmd->views[0].viewport.h_pixels = panel_w;
		hd->base.hmd->views[0].rot = u_device_rotation_ident;

		// Right
		hd->base.hmd->views[1].display.w_pixels = panel_w;
		hd->base.hmd->views[1].display.h_pixels = panel_h;
		hd->base.hmd->views[1].viewport.x_pixels = panel_w;
		hd->base.hmd->views[1].viewport.y_pixels = vert_padding;
		hd->base.hmd->views[1].viewport.w_pixels = panel_w;
		hd->base.hmd->views[1].viewport.h_pixels = panel_w;
		hd->base.hmd->views[1].rot = u_device_rotation_ident;
#endif

		// clang-format on
		break;
	}
	case HDK_VARIANT_1_3_1_4:
		// fallthrough intentional
	case HDK_VARIANT_1_2: {
		// 1080x1920 screen, with the top at the left.
		hd->base.hmd->screens[0].nominal_frame_interval_ns = time_s_to_ns(1.0f / 60.0f);

		constexpr int panel_w = 1080;
		constexpr int panel_h = 1920;
		constexpr int panel_half_h = panel_h / 2;
		// clang-format off
		// Main display.
		hd->base.hmd->screens[0].w_pixels = panel_w;
		hd->base.hmd->screens[0].h_pixels = panel_h;

		// Left
		hd->base.hmd->views[0].display.w_pixels = panel_half_h;
		hd->base.hmd->views[0].display.h_pixels = panel_w;
		hd->base.hmd->views[0].viewport.x_pixels = 0;
		hd->base.hmd->views[0].viewport.y_pixels = 0;// top half of display
		hd->base.hmd->views[0].viewport.w_pixels = panel_w;
		hd->base.hmd->views[0].viewport.h_pixels = panel_half_h;
		hd->base.hmd->views[0].rot = u_device_rotation_left;

		// Right
		hd->base.hmd->views[1].display.w_pixels = panel_half_h;
		hd->base.hmd->views[1].display.h_pixels = panel_w;
		hd->base.hmd->views[1].viewport.x_pixels = 0;
		hd->base.hmd->views[1].viewport.y_pixels = panel_half_h; // bottom half of display
		hd->base.hmd->views[1].viewport.w_pixels = panel_w;
		hd->base.hmd->views[1].viewport.h_pixels = panel_half_h;
		hd->base.hmd->views[1].rot = u_device_rotation_left;
		// clang-format on
		break;
	}
	}

	// Distortion
	// "None" is correct or at least acceptable for 1.2.
	// We have coefficients for 1.3/1.4, though the mesh is better.
	// We only have a mesh for 2, so use "none" there until it's supported.
	// Distortion information, fills in xdev->compute_distortion().
	u_distortion_mesh_set_none(&hd->base);
	// if (variant == HDK_VARIANT_1_3_1_4) {
	// 	hd->base.hmd->distortion.models =
	// 	    xrt_distortion_model(hd->base.hmd->distortion.models |
	// 	                         XRT_DISTORTION_MODEL_PANOTOOLS);
	// 	hd->base.hmd->distortion.preferred =
	// XRT_DISTORTION_MODEL_PANOTOOLS;
	// }

	if (hd->dev) {
		int ret = os_thread_helper_start(&hd->imu_thread, hdk_device_run_thread, hd);
		if (ret != 0) {
			HDK_ERROR(hd, "Failed to start mainboard thread!");
			hdk_device_destroy((struct xrt_device *)hd);
			return 0;
		}
	}

	if (hd->ll <= U_LOGGING_DEBUG) {
		u_device_dump_config(&hd->base, __func__, hd->base.str);
	}

	hd->base.orientation_tracking_supported = true;
	hd->base.position_tracking_supported = false;
	hd->base.device_type = XRT_DEVICE_TYPE_HMD;

	return hd;
}
