// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common defines and enums for XRT.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "util/u_time.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * A base class for reference counted objects.
 *
 * @ingroup xrt_iface
 */
struct xrt_reference
{
	uint32_t count;
};

/*!
 * Which blend mode does the device support, used as both a bitfield and value.
 *
 * @ingroup xrt_iface
 */
enum xrt_blend_mode
{
	// clang-format off
	XRT_BLEND_MODE_OPAQUE      = 1 << 0,
	XRT_BLEND_MODE_ADDITIVE    = 1 << 1,
	XRT_BLEND_MODE_ALPHA_BLEND = 1 << 2,
	// clang-format on
};

/*!
 * Which distortion model does the device expose,
 * used both as a bitfield and value.
 */
enum xrt_distortion_model
{
	// clang-format off
	XRT_DISTORTION_MODEL_NONE      = 1 << 0,
	XRT_DISTORTION_MODEL_PANOTOOLS = 1 << 1,
	XRT_DISTORTION_MODEL_VIVE      = 1 << 2,
	XRT_DISTORTION_MODEL_MESHUV    = 1 << 3,
	// clang-format on
};

/*!
 * Common formats, use `u_format_*` functions to reason about them.
 */
enum xrt_format
{
	XRT_FORMAT_R8G8B8X8,
	XRT_FORMAT_R8G8B8A8,
	XRT_FORMAT_R8G8B8,
	XRT_FORMAT_R8G8,
	XRT_FORMAT_R8,

	XRT_FORMAT_L8, // Luminence, R = L, G = L, B = L.

	XRT_FORMAT_BITMAP_8X1, // One bit format tiled in 8x1 blocks.
	XRT_FORMAT_BITMAP_8X8, // One bit format tiled in 8X8 blocks.

	XRT_FORMAT_YUV888,
	XRT_FORMAT_YUV422,

	XRT_FORMAT_MJPEG,
};

/*!
 * What type of stereo format a frame has.
 *
 * @ingroup xrt_iface
 */
enum xrt_stereo_format
{
	XRT_STEREO_FORMAT_NONE,
	XRT_STEREO_FORMAT_SBS,         //!< Side by side.
	XRT_STEREO_FORMAT_INTERLEAVED, //!< Interleaved pixels.
	XRT_STEREO_FORMAT_OAU,         //!< Over & Under.
};

/*!
 * A quaternion with single floats.
 *
 * @ingroup xrt_iface math
 */
struct xrt_quat
{
	float x;
	float y;
	float z;
	float w;
};

/*!
 * A 1 element vector with single floats.
 *
 * @ingroup xrt_iface math
 */
struct xrt_vec1
{
	float x;
};

/*!
 * A 2 element vector with single floats.
 *
 * @ingroup xrt_iface math
 */
struct xrt_vec2
{
	float x;
	float y;
};

/*!
 * A 3 element vector with single floats.
 *
 * @ingroup xrt_iface math
 */
struct xrt_vec3
{
	float x;
	float y;
	float z;
};

/*!
 * A 3 element vector with 32 bit integers.
 *
 * @ingroup xrt_iface math
 */
struct xrt_vec3_i32
{
	int32_t x;
	int32_t y;
	int32_t z;
};

/*!
 * A 2 element vector with 32 bit integers.
 *
 * @ingroup xrt_iface math
 */
struct xrt_vec2_i32
{
	int32_t x;
	int32_t y;
};

/*!
 * A 3 element colour with 8 bits per channel.
 *
 * @ingroup xrt_iface math
 */
struct xrt_colour_rgb_u8
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

/*!
 * A 4 element colour with 8 bits per channel.
 *
 * @ingroup xrt_iface math
 */
struct xrt_colour_rgba_u8
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

/*!
 * A 3 element colour with floating point channels.
 *
 * @ingroup xrt_iface math
 */
struct xrt_colour_rgb_f32
{
	float r;
	float g;
	float b;
};

/*!
 * A 4 element colour with floating point channels.
 *
 * @ingroup xrt_iface math
 */
struct xrt_colour_rgba_f32
{
	float r;
	float g;
	float b;
	float a;
};

/*!
 * Image size.
 *
 * @ingroup xrt_iface math
 */
struct xrt_size
{
	int w;
	int h;
};

/*!
 * A pose composed of a position and orientation.
 *
 * @see xrt_qaut
 * @see xrt_vec3
 * @ingroup xrt_iface math
 */
struct xrt_pose
{
	struct xrt_quat orientation;
	struct xrt_vec3 position;
};

/*!
 * Describes a projection matrix fov.
 *
 * @ingroup xrt_iface math
 */
struct xrt_fov
{
	float angle_left;
	float angle_right;
	float angle_up;
	float angle_down;
};

/*!
 * A tightly packed 2x2 matrix of floats.
 *
 * @ingroup xrt_iface math
 */
struct xrt_matrix_2x2
{
	union {
		float v[4];
		struct xrt_vec2 vecs[2];
	};
};

/*!
 * A tightly packed 3x3 matrix of floats.
 *
 * @ingroup xrt_iface math
 */
struct xrt_matrix_3x3
{
	float v[9];
};

/*!
 * A tightly packed 4x4 matrix of floats.
 *
 * @ingroup xrt_iface math
 */
struct xrt_matrix_4x4
{
	float v[16];
};

/*!
 * A range of API versions supported.
 *
 * @ingroup xrt_iface math
 */
struct xrt_api_requirements
{
	uint32_t min_major;
	uint32_t min_minor;
	uint32_t min_patch;

	uint32_t max_major;
	uint32_t max_minor;
	uint32_t max_patch;
};

/*!
 * Flags of which components of a @ref xrt_space_relation is valid.
 *
 * @see xrt_space_relation
 * @ingroup xrt_iface math
 */
enum xrt_space_relation_flags
{
	XRT_SPACE_RELATION_ORIENTATION_VALID_BIT = 0x00000001,
	XRT_SPACE_RELATION_POSITION_VALID_BIT = 0x00000002,
	XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT = 0x00000004,
	XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT = 0x00000008,
	XRT_SPACE_RELATION_LINEAR_ACCELERATION_VALID_BIT = 0x00000010,
	XRT_SPACE_RELATION_ANGULAR_ACCELERATION_VALID_BIT = 0x00000020,
	XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT = 0x00000040,
	XRT_SPACE_RELATION_POSITION_TRACKED_BIT = 0x00000080,
	XRT_SPACE_RELATION_BITMASK_ALL =
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT |
	    XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT |
	    XRT_SPACE_RELATION_LINEAR_ACCELERATION_VALID_BIT |
	    XRT_SPACE_RELATION_ANGULAR_ACCELERATION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	    XRT_SPACE_RELATION_POSITION_TRACKED_BIT,
	XRT_SPACE_RELATION_BITMASK_NONE = 0
};

/*!
 * A relation with two spaces, includes velocity and acceleration.
 *
 * @see xrt_quat
 * @see xrt_vec3
 * @see xrt_pose
 * @see xrt_space_relation_flags
 * @ingroup xrt_iface math
 */
struct xrt_space_relation
{
	enum xrt_space_relation_flags relation_flags;
	struct xrt_pose pose;
	struct xrt_vec3 linear_velocity;
	struct xrt_vec3 angular_velocity;
	struct xrt_vec3 linear_acceleration;
	struct xrt_vec3 angular_acceleration;
};


/*
 *
 * Input related enums and structs.
 *
 */

/*!
 * A enum that is used to name devices so that the
 * state trackers can reason about the devices easier.
 */
enum xrt_device_name
{
	XRT_DEVICE_GENERIC_HMD = 1,

	XRT_DEVICE_PSMV = 2,
	XRT_DEVICE_HYDRA = 3,
	XRT_DEVICE_DAYDREAM = 4,
	XRT_DEVICE_INDEX_CONTROLLER = 5,
	XRT_DEVICE_VIVE_WAND = 6,
};

/*!
 * Base type of this inputs.
 *
 * @ingroup xrt_iface
 */
enum xrt_input_type
{
	// clang-format off
	//! Float input in [0, 1]
	XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE      = 0x00,
	//! Float input in [-1, 1]
	XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE = 0x01,
	//! Vec2 input, components in [-1, 1]
	XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE = 0x02,
	//! Vec3 input, components in [-1, 1]
	XRT_INPUT_TYPE_VEC3_MINUS_ONE_TO_ONE = 0x03,
	//! Boolean (digital, binary) input
	XRT_INPUT_TYPE_BOOLEAN               = 0x04,
	//! A tracked pose
	XRT_INPUT_TYPE_POSE                  = 0x05,
	// clang-format on
};

/*!
 * @brief Create an enum value for xrt_input_name that packs an ID and input
 * type.
 *
 * @param id an integer
 * @param type The suffix of an xrt_input_type value name: `XRT_INPUT_TYPE_` is
 * prepended automatically.
 *
 * @see xrt_input_name
 * @ingroup xrt_iface
 */
#define XRT_INPUT_NAME(id, type) ((id << 8) | XRT_INPUT_TYPE_##type)

/*!
 * @brief Extract the xrt_input_type from an xrt_input_name.
 *
 * @param name A xrt_input_name value
 *
 * @see xrt_input_name
 * @see xrt_input_type
 * @ingroup xrt_iface
 */
#define XRT_GET_INPUT_TYPE(name) (name & 0xff)

/*!
 * Name of a input with a baked in type.
 *
 * @see xrt_input_type
 * @ingroup xrt_iface
 */
enum xrt_input_name
{
	// clang-format off
	XRT_INPUT_GENERIC_HEAD_POSE                  = XRT_INPUT_NAME(0x0000, POSE),
	XRT_INPUT_GENERIC_HEAD_DETECT                = XRT_INPUT_NAME(0x0001, BOOLEAN),

	XRT_INPUT_PSMV_PS_CLICK                      = XRT_INPUT_NAME(0x0020, BOOLEAN),
	XRT_INPUT_PSMV_MOVE_CLICK                    = XRT_INPUT_NAME(0x0021, BOOLEAN),
	XRT_INPUT_PSMV_START_CLICK                   = XRT_INPUT_NAME(0x0022, BOOLEAN),
	XRT_INPUT_PSMV_SELECT_CLICK                  = XRT_INPUT_NAME(0x0023, BOOLEAN),
	XRT_INPUT_PSMV_SQUARE_CLICK                  = XRT_INPUT_NAME(0x0024, BOOLEAN),
	XRT_INPUT_PSMV_CROSS_CLICK                   = XRT_INPUT_NAME(0x0025, BOOLEAN),
	XRT_INPUT_PSMV_CIRCLE_CLICK                  = XRT_INPUT_NAME(0x0026, BOOLEAN),
	XRT_INPUT_PSMV_TRIANGLE_CLICK                = XRT_INPUT_NAME(0x0027, BOOLEAN),
	XRT_INPUT_PSMV_TRIGGER_VALUE                 = XRT_INPUT_NAME(0x0028, VEC1_ZERO_TO_ONE),
	XRT_INPUT_PSMV_BODY_CENTER_POSE              = XRT_INPUT_NAME(0x0029, POSE),
	XRT_INPUT_PSMV_BALL_CENTER_POSE              = XRT_INPUT_NAME(0x002A, POSE),
	XRT_INPUT_PSMV_BALL_TIP_POSE                 = XRT_INPUT_NAME(0x002B, POSE),

	XRT_INPUT_HYDRA_1_CLICK                      = XRT_INPUT_NAME(0x0030, BOOLEAN),
	XRT_INPUT_HYDRA_2_CLICK                      = XRT_INPUT_NAME(0x0031, BOOLEAN),
	XRT_INPUT_HYDRA_3_CLICK                      = XRT_INPUT_NAME(0x0032, BOOLEAN),
	XRT_INPUT_HYDRA_4_CLICK                      = XRT_INPUT_NAME(0x0033, BOOLEAN),
	XRT_INPUT_HYDRA_MIDDLE_CLICK                 = XRT_INPUT_NAME(0x0034, BOOLEAN),
	XRT_INPUT_HYDRA_BUMPER_CLICK                 = XRT_INPUT_NAME(0x0035, BOOLEAN),
	XRT_INPUT_HYDRA_JOYSTICK_CLICK               = XRT_INPUT_NAME(0x0036, BOOLEAN),
	XRT_INPUT_HYDRA_JOYSTICK_VALUE               = XRT_INPUT_NAME(0x0037, VEC2_MINUS_ONE_TO_ONE),
	XRT_INPUT_HYDRA_TRIGGER_VALUE                = XRT_INPUT_NAME(0x0038, VEC1_ZERO_TO_ONE),
	XRT_INPUT_HYDRA_POSE                         = XRT_INPUT_NAME(0x0039, POSE),

	XRT_INPUT_DAYDREAM_TOUCHPAD_CLICK            = XRT_INPUT_NAME(0x0040, BOOLEAN),
	XRT_INPUT_DAYDREAM_BAR_CLICK                 = XRT_INPUT_NAME(0x0041, BOOLEAN),
	XRT_INPUT_DAYDREAM_CIRCLE_CLICK              = XRT_INPUT_NAME(0x0042, BOOLEAN),
	XRT_INPUT_DAYDREAM_VOLUP_CLICK               = XRT_INPUT_NAME(0x0043, BOOLEAN),
	XRT_INPUT_DAYDREAM_VOLDN_CLICK               = XRT_INPUT_NAME(0x0044, BOOLEAN),
	//! @todo This should be merged and be tagged as a touchpad, maybe.
	XRT_INPUT_DAYDREAM_TOUCHPAD_VALUE_X          = XRT_INPUT_NAME(0x0045, VEC1_ZERO_TO_ONE),
	XRT_INPUT_DAYDREAM_TOUCHPAD_VALUE_Y          = XRT_INPUT_NAME(0x0046, VEC1_ZERO_TO_ONE),
	XRT_INPUT_DAYDREAM_POSE                      = XRT_INPUT_NAME(0x0047, POSE),

	XRT_INPUT_INDEX_SYSTEM_CLICK                 = XRT_INPUT_NAME(0x0050, BOOLEAN),
	XRT_INPUT_INDEX_SYSTEM_TOUCH                 = XRT_INPUT_NAME(0x0051, BOOLEAN),
	XRT_INPUT_INDEX_A_CLICK                      = XRT_INPUT_NAME(0x0052, BOOLEAN),
	XRT_INPUT_INDEX_A_TOUCH                      = XRT_INPUT_NAME(0x0053, BOOLEAN),
	XRT_INPUT_INDEX_B_CLICK                      = XRT_INPUT_NAME(0x0054, BOOLEAN),
	XRT_INPUT_INDEX_B_TOUCH                      = XRT_INPUT_NAME(0x0055, BOOLEAN),
	XRT_INPUT_INDEX_SQUEEZE_VALUE                = XRT_INPUT_NAME(0x0056, VEC1_ZERO_TO_ONE),
	XRT_INPUT_INDEX_SQUEEZE_FORCE                = XRT_INPUT_NAME(0x0057, VEC1_ZERO_TO_ONE),
	XRT_INPUT_INDEX_TRIGGER_CLICK                = XRT_INPUT_NAME(0x0058, BOOLEAN),
	XRT_INPUT_INDEX_TRIGGER_VALUE                = XRT_INPUT_NAME(0x0059, VEC1_ZERO_TO_ONE),
	XRT_INPUT_INDEX_TRIGGER_TOUCH                = XRT_INPUT_NAME(0x005A, BOOLEAN),
	XRT_INPUT_INDEX_THUMBSTICK_X                 = XRT_INPUT_NAME(0x005B, VEC1_ZERO_TO_ONE),
	XRT_INPUT_INDEX_THUMBSTICK_Y                 = XRT_INPUT_NAME(0x005C, VEC1_ZERO_TO_ONE),
	XRT_INPUT_INDEX_THUMBSTICK_CLICK             = XRT_INPUT_NAME(0x005D, BOOLEAN),
	XRT_INPUT_INDEX_THUMBSTICK_TOUCH             = XRT_INPUT_NAME(0x005E, BOOLEAN),
	XRT_INPUT_INDEX_TRACKPAD_X                   = XRT_INPUT_NAME(0x005F, VEC1_ZERO_TO_ONE),
	XRT_INPUT_INDEX_TRACKPAD_Y                   = XRT_INPUT_NAME(0x0060, VEC1_ZERO_TO_ONE),
	XRT_INPUT_INDEX_TRACKPAD_FORCE               = XRT_INPUT_NAME(0x0061, VEC1_ZERO_TO_ONE),
	XRT_INPUT_INDEX_TRACKPAD_TOUCH               = XRT_INPUT_NAME(0x0062, BOOLEAN),
	XRT_INPUT_INDEX_GRIP_POSE                    = XRT_INPUT_NAME(0x0063, POSE),
	XRT_INPUT_INDEX_AIM_POSE                     = XRT_INPUT_NAME(0x0064, POSE),

	XRT_INPUT_VIVE_SYSTEM_CLICK                  = XRT_INPUT_NAME(0x0070, BOOLEAN),
	XRT_INPUT_VIVE_SQUEEZE_CLICK                 = XRT_INPUT_NAME(0x0071, BOOLEAN),
	XRT_INPUT_VIVE_MENU_CLICK                    = XRT_INPUT_NAME(0x0072, BOOLEAN),
	XRT_INPUT_VIVE_TRIGGER_CLICK                 = XRT_INPUT_NAME(0x0073, BOOLEAN),
	XRT_INPUT_VIVE_TRIGGER_VALUE                 = XRT_INPUT_NAME(0x0074, VEC1_ZERO_TO_ONE),
	XRT_INPUT_VIVE_TRACKPAD_X                    = XRT_INPUT_NAME(0x0075, VEC1_ZERO_TO_ONE),
	XRT_INPUT_VIVE_TRACKPAD_Y                    = XRT_INPUT_NAME(0x0076, VEC1_ZERO_TO_ONE),
	XRT_INPUT_VIVE_TRACKPAD_CLICK                = XRT_INPUT_NAME(0x0077, BOOLEAN),
	XRT_INPUT_VIVE_TRACKPAD_TOUCH                = XRT_INPUT_NAME(0x0077, BOOLEAN),
	XRT_INPUT_VIVE_GRIP_POSE                     = XRT_INPUT_NAME(0x0078, POSE),
	XRT_INPUT_VIVE_AIM_POSE                      = XRT_INPUT_NAME(0x0079, POSE),

	// clang-format on
};

/*!
 * A union of all input types.
 *
 * @see xrt_input_type
 * @ingroup xrt_iface math
 */
union xrt_input_value {
	struct xrt_vec1 vec1;
	struct xrt_vec2 vec2;
	struct xrt_vec3 vec3;
	bool boolean;
};


/*!
 * Base type of this output.
 *
 * @ingroup xrt_iface
 */
enum xrt_output_type
{
	// clang-format off
	XRT_OUTPUT_TYPE_VIBRATION             = 0x00,
	// clang-format on
};

#define XRT_OUTPUT_NAME(id, type) ((id << 8) | XRT_OUTPUT_TYPE_##type)

/*!
 * Name of a output with a baked in type.
 *
 * @see xrt_output_type
 * @ingroup xrt_iface
 */
enum xrt_output_name
{
	// clang-format off
	XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION       = XRT_OUTPUT_NAME(0x0020, VIBRATION),
	XRT_OUTPUT_NAME_INDEX_HAPTIC                = XRT_OUTPUT_NAME(0x0030, VIBRATION),
	XRT_OUTPUT_NAME_VIVE_HAPTIC                 = XRT_OUTPUT_NAME(0x0040, VIBRATION),
	// clang-format on
};

/*!
 * A union of all output types.
 *
 * @see xrt_output_type
 * @ingroup xrt_iface math
 */
union xrt_output_value {
	struct
	{
		float frequency;
		float amplitude;
		time_duration_ns duration;
	} vibration;
};


/*
 *
 * Inline functions
 *
 */

static inline bool
xrt_reference_dec(struct xrt_reference *xref)
{
	int count = xrt_atomic_dec_return(&xref->count);
	return count == 0;
}

static inline void
xrt_reference_inc(struct xrt_reference *xref)
{
	xrt_atomic_inc_return(&xref->count);
}


#ifdef __cplusplus
}
#endif
