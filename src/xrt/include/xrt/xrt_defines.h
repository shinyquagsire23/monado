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

#include "xrt/xrt_results.h"

#ifdef __cplusplus
extern "C" {
#endif

//! internal define for VK_UUID_SIZE
#define XRT_GPU_UUID_SIZE 16

/*!
 * A base class for reference counted objects.
 *
 * @ingroup xrt_iface
 */
struct xrt_reference
{
	xrt_atomic_s32_t count;
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
	XRT_DISTORTION_MODEL_COMPUTE   = 1 << 1,
	XRT_DISTORTION_MODEL_MESHUV    = 1 << 2,
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

	XRT_FORMAT_BAYER_GR8,

	XRT_FORMAT_L8, // Luminence, R = L, G = L, B = L.

	XRT_FORMAT_BITMAP_8X1, // One bit format tiled in 8x1 blocks.
	XRT_FORMAT_BITMAP_8X8, // One bit format tiled in 8X8 blocks.

	XRT_FORMAT_YUV888,
	XRT_FORMAT_YUYV422,
	XRT_FORMAT_UYVY422,

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
 * Represents a uv triplet for distortion, basically just three xrt_vec2.
 *
 * @ingroup xrt_iface_math
 */
struct xrt_uv_triplet
{
	struct xrt_vec2 r, g, b;
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
 * Image offset.
 *
 * @ingroup xrt_iface math
 */
struct xrt_offset
{
	int w, h;
};

/*!
 * Image rectangle.
 *
 * @ingroup xrt_iface math
 */
struct xrt_rect
{
	struct xrt_offset offset;
	struct xrt_size extent;
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
	// clang-format off
	XRT_SPACE_RELATION_ORIENTATION_VALID_BIT =          (1 << 0),
	XRT_SPACE_RELATION_POSITION_VALID_BIT =             (1 << 1),
	XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT =      (1 << 2),
	XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT =     (1 << 3),
	XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT =        (1 << 4),
	XRT_SPACE_RELATION_POSITION_TRACKED_BIT =           (1 << 5),
	// clang-format on
	XRT_SPACE_RELATION_BITMASK_ALL =
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT | XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT,
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
};

/*!
 * The maximum number of steps that can be in a space graph chain.
 *
 * @see xrt_space_graph::steps
 * @ingroup xrt_iface math
 */
#define XRT_SPACE_GRAPHS_MAX 8

/*!
 * A graph of space relations, technically more of a chain of transformation
 * since it's not branching, but a flat graph is still a graph. Functions for
 * manipulating this are available in `math/m_space.h`.
 *
 * @see xrt_space_relation
 * @ingroup xrt_iface math
 */
struct xrt_space_graph
{
	struct xrt_space_relation steps[XRT_SPACE_GRAPHS_MAX];
	uint32_t num_steps;
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

	XRT_DEVICE_SIMPLE_CONTROLLER,

	XRT_DEVICE_PSMV,
	XRT_DEVICE_HYDRA,
	XRT_DEVICE_DAYDREAM,
	XRT_DEVICE_INDEX_CONTROLLER,
	XRT_DEVICE_VIVE_WAND,
	XRT_DEVICE_VIVE_TRACKER_GEN1,
	XRT_DEVICE_VIVE_TRACKER_GEN2,

	XRT_DEVICE_VIVE_PRO,
	XRT_DEVICE_WMR_CONTROLLER,
	XRT_DEVICE_XBOX_CONTROLLER,
	XRT_DEVICE_GO_CONTROLLER,
	XRT_DEVICE_TOUCH_CONTROLLER,

	XRT_DEVICE_HAND_INTERACTION,

	XRT_DEVICE_HAND_TRACKER,
};

/*!
 * How an xrt_device can be used.
 *
 * @ingroup xrt_iface
 */
enum xrt_device_type
{
	XRT_DEVICE_TYPE_UNKNOWN = 0,
	XRT_DEVICE_TYPE_HMD,
	XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER,
	XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER,
	XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER,
	XRT_DEVICE_TYPE_GENERIC_TRACKER,
	XRT_DEVICE_TYPE_HAND_TRACKER,
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
	//! A tracked hand
	XRT_INPUT_TYPE_HAND_TRACKING         = 0x06,
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
 * @relates xrt_input_name
 * @returns @ref xrt_input_type
 * @ingroup xrt_iface
 */
#define XRT_GET_INPUT_TYPE(name) ((enum xrt_input_type)(name & 0xff))

/*!
 * Every internal input source known to monado with a baked in type.
 *
 * @see xrt_input_type
 * @ingroup xrt_iface
 */
enum xrt_input_name
{
	// clang-format off
	XRT_INPUT_GENERIC_HEAD_POSE                  = XRT_INPUT_NAME(0x0000, POSE),
	XRT_INPUT_GENERIC_HEAD_DETECT                = XRT_INPUT_NAME(0x0001, BOOLEAN),
	XRT_INPUT_GENERIC_HAND_TRACKING_LEFT         = XRT_INPUT_NAME(0x0002, HAND_TRACKING),
	XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT        = XRT_INPUT_NAME(0x0004, HAND_TRACKING),

	XRT_INPUT_SIMPLE_SELECT_CLICK                = XRT_INPUT_NAME(0x0010, BOOLEAN),
	XRT_INPUT_SIMPLE_MENU_CLICK                  = XRT_INPUT_NAME(0x0011, BOOLEAN),
	XRT_INPUT_SIMPLE_GRIP_POSE                   = XRT_INPUT_NAME(0x0012, POSE),
	XRT_INPUT_SIMPLE_AIM_POSE                    = XRT_INPUT_NAME(0x0013, POSE),

	XRT_INPUT_PSMV_PS_CLICK                      = XRT_INPUT_NAME(0x0020, BOOLEAN),
	XRT_INPUT_PSMV_MOVE_CLICK                    = XRT_INPUT_NAME(0x0021, BOOLEAN),
	XRT_INPUT_PSMV_START_CLICK                   = XRT_INPUT_NAME(0x0022, BOOLEAN),
	XRT_INPUT_PSMV_SELECT_CLICK                  = XRT_INPUT_NAME(0x0023, BOOLEAN),
	XRT_INPUT_PSMV_SQUARE_CLICK                  = XRT_INPUT_NAME(0x0024, BOOLEAN),
	XRT_INPUT_PSMV_CROSS_CLICK                   = XRT_INPUT_NAME(0x0025, BOOLEAN),
	XRT_INPUT_PSMV_CIRCLE_CLICK                  = XRT_INPUT_NAME(0x0026, BOOLEAN),
	XRT_INPUT_PSMV_TRIANGLE_CLICK                = XRT_INPUT_NAME(0x0027, BOOLEAN),
	XRT_INPUT_PSMV_TRIGGER_VALUE                 = XRT_INPUT_NAME(0x0028, VEC1_ZERO_TO_ONE),
	XRT_INPUT_PSMV_GRIP_POSE                     = XRT_INPUT_NAME(0x0029, POSE),
	XRT_INPUT_PSMV_AIM_POSE                      = XRT_INPUT_NAME(0x002A, POSE),
	XRT_INPUT_PSMV_BODY_CENTER_POSE              = XRT_INPUT_NAME(0x002B, POSE),
	XRT_INPUT_PSMV_BALL_CENTER_POSE              = XRT_INPUT_NAME(0x002C, POSE),

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
	XRT_INPUT_DAYDREAM_TOUCHPAD                  = XRT_INPUT_NAME(0x0045, VEC2_MINUS_ONE_TO_ONE),
	XRT_INPUT_DAYDREAM_POSE                      = XRT_INPUT_NAME(0x0046, POSE),
	XRT_INPUT_DAYDREAM_TOUCHPAD_TOUCH            = XRT_INPUT_NAME(0x0047, BOOLEAN),

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
	XRT_INPUT_INDEX_THUMBSTICK                   = XRT_INPUT_NAME(0x005B, VEC2_MINUS_ONE_TO_ONE),
	XRT_INPUT_INDEX_THUMBSTICK_CLICK             = XRT_INPUT_NAME(0x005D, BOOLEAN),
	XRT_INPUT_INDEX_THUMBSTICK_TOUCH             = XRT_INPUT_NAME(0x005E, BOOLEAN),
	XRT_INPUT_INDEX_TRACKPAD                     = XRT_INPUT_NAME(0x005F, VEC2_MINUS_ONE_TO_ONE),
	XRT_INPUT_INDEX_TRACKPAD_FORCE               = XRT_INPUT_NAME(0x0061, VEC1_ZERO_TO_ONE),
	XRT_INPUT_INDEX_TRACKPAD_TOUCH               = XRT_INPUT_NAME(0x0062, BOOLEAN),
	XRT_INPUT_INDEX_GRIP_POSE                    = XRT_INPUT_NAME(0x0063, POSE),
	XRT_INPUT_INDEX_AIM_POSE                     = XRT_INPUT_NAME(0x0064, POSE),

	XRT_INPUT_VIVE_SYSTEM_CLICK                  = XRT_INPUT_NAME(0x0070, BOOLEAN),
	XRT_INPUT_VIVE_SQUEEZE_CLICK                 = XRT_INPUT_NAME(0x0071, BOOLEAN),
	XRT_INPUT_VIVE_MENU_CLICK                    = XRT_INPUT_NAME(0x0072, BOOLEAN),
	XRT_INPUT_VIVE_TRIGGER_CLICK                 = XRT_INPUT_NAME(0x0073, BOOLEAN),
	XRT_INPUT_VIVE_TRIGGER_VALUE                 = XRT_INPUT_NAME(0x0074, VEC1_ZERO_TO_ONE),
	XRT_INPUT_VIVE_TRACKPAD                      = XRT_INPUT_NAME(0x0075, VEC2_MINUS_ONE_TO_ONE),
	XRT_INPUT_VIVE_TRACKPAD_CLICK                = XRT_INPUT_NAME(0x0076, BOOLEAN),
	XRT_INPUT_VIVE_TRACKPAD_TOUCH                = XRT_INPUT_NAME(0x0077, BOOLEAN),
	XRT_INPUT_VIVE_GRIP_POSE                     = XRT_INPUT_NAME(0x0078, POSE),
	XRT_INPUT_VIVE_AIM_POSE                      = XRT_INPUT_NAME(0x0079, POSE),

	XRT_INPUT_VIVEPRO_SYSTEM_CLICK               = XRT_INPUT_NAME(0x0080, BOOLEAN),
	XRT_INPUT_VIVEPRO_VOLUP_CLICK                = XRT_INPUT_NAME(0x0081, BOOLEAN),
	XRT_INPUT_VIVEPRO_VOLDN_CLICK                = XRT_INPUT_NAME(0x0082, BOOLEAN),
	XRT_INPUT_VIVEPRO_MUTE_MIC_CLICK             = XRT_INPUT_NAME(0x0083, BOOLEAN),

	XRT_INPUT_WMR_MENU_CLICK                     = XRT_INPUT_NAME(0x0090, BOOLEAN),
	XRT_INPUT_WMR_SQUEEZE_CLICK                  = XRT_INPUT_NAME(0x0091, BOOLEAN),
	XRT_INPUT_WMR_TRIGGER_VALUE                  = XRT_INPUT_NAME(0x0092, VEC1_ZERO_TO_ONE),
	XRT_INPUT_WMR_THUMBSTICK_CLICK               = XRT_INPUT_NAME(0x0093, BOOLEAN),
	XRT_INPUT_WMR_THUMBSTICK                     = XRT_INPUT_NAME(0x0094, VEC2_MINUS_ONE_TO_ONE),
	XRT_INPUT_WMR_TRACKPAD_CLICK                 = XRT_INPUT_NAME(0x0095, BOOLEAN),
	XRT_INPUT_WMR_TRACKPAD_TOUCH                 = XRT_INPUT_NAME(0x0096, BOOLEAN),
	XRT_INPUT_WMR_TRACKPAD                       = XRT_INPUT_NAME(0x0097, VEC2_MINUS_ONE_TO_ONE),
	XRT_INPUT_WMR_GRIP_POSE                      = XRT_INPUT_NAME(0x0098, POSE),
	XRT_INPUT_WMR_AIM_POSE                       = XRT_INPUT_NAME(0x0099, POSE),

	XRT_INPUT_XBOX_MENU_CLICK                    = XRT_INPUT_NAME(0x00A0, BOOLEAN),
	XRT_INPUT_XBOX_VIEW_CLICK                    = XRT_INPUT_NAME(0x00A1, BOOLEAN),
	XRT_INPUT_XBOX_A_CLICK                       = XRT_INPUT_NAME(0x00A2, BOOLEAN),
	XRT_INPUT_XBOX_B_CLICK                       = XRT_INPUT_NAME(0x00A3, BOOLEAN),
	XRT_INPUT_XBOX_X_CLICK                       = XRT_INPUT_NAME(0x00A4, BOOLEAN),
	XRT_INPUT_XBOX_Y_CLICK                       = XRT_INPUT_NAME(0x00A5, BOOLEAN),
	XRT_INPUT_XBOX_DPAD_DOWN_CLICK               = XRT_INPUT_NAME(0x00A6, BOOLEAN),
	XRT_INPUT_XBOX_DPAD_RIGHT_CLICK              = XRT_INPUT_NAME(0x00A7, BOOLEAN),
	XRT_INPUT_XBOX_DPAD_UP_CLICK                 = XRT_INPUT_NAME(0x00A8, BOOLEAN),
	XRT_INPUT_XBOX_DPAD_LEFT_CLICK               = XRT_INPUT_NAME(0x00A9, BOOLEAN),
	XRT_INPUT_XBOX_SHOULDER_LEFT_CLICK           = XRT_INPUT_NAME(0x00AA, BOOLEAN),
	XRT_INPUT_XBOX_SHOULDER_RIGHT_CLICK          = XRT_INPUT_NAME(0x00AB, BOOLEAN),
	XRT_INPUT_XBOX_THUMBSTICK_LEFT_CLICK         = XRT_INPUT_NAME(0x00AC, BOOLEAN),
	XRT_INPUT_XBOX_THUMBSTICK_LEFT               = XRT_INPUT_NAME(0x00AD, VEC2_MINUS_ONE_TO_ONE),
	XRT_INPUT_XBOX_THUMBSTICK_RIGHT_CLICK        = XRT_INPUT_NAME(0x00AE, BOOLEAN),
	XRT_INPUT_XBOX_THUMBSTICK_RIGHT              = XRT_INPUT_NAME(0x00AF, VEC2_MINUS_ONE_TO_ONE),
	XRT_INPUT_XBOX_LEFT_TRIGGER_VALUE            = XRT_INPUT_NAME(0x00B0, VEC1_ZERO_TO_ONE),
	XRT_INPUT_XBOX_RIGHT_TRIGGER_VALUE           = XRT_INPUT_NAME(0x00B1, VEC1_ZERO_TO_ONE),

	XRT_INPUT_GO_SYSTEM_CLICK                    = XRT_INPUT_NAME(0x00B0, BOOLEAN),
	XRT_INPUT_GO_TRIGGER_CLICK                   = XRT_INPUT_NAME(0x00B1, BOOLEAN),
	XRT_INPUT_GO_BACK_CLICK                      = XRT_INPUT_NAME(0x00B2, BOOLEAN),
	XRT_INPUT_GO_TRACKPAD_CLICK                  = XRT_INPUT_NAME(0x00B3, BOOLEAN),
	XRT_INPUT_GO_TRACKPAD_TOUCH                  = XRT_INPUT_NAME(0x00B4, BOOLEAN),
	XRT_INPUT_GO_TRACKPAD                        = XRT_INPUT_NAME(0x00B5, VEC2_MINUS_ONE_TO_ONE),
	XRT_INPUT_GO_GRIP_POSE                       = XRT_INPUT_NAME(0x00B6, POSE),
	XRT_INPUT_GO_AIM_POSE                        = XRT_INPUT_NAME(0x00B7, POSE),

	XRT_INPUT_TOUCH_X_CLICK                      = XRT_INPUT_NAME(0x00C0, BOOLEAN),
	XRT_INPUT_TOUCH_X_TOUCH                      = XRT_INPUT_NAME(0x00C1, BOOLEAN),
	XRT_INPUT_TOUCH_Y_CLICK                      = XRT_INPUT_NAME(0x00C2, BOOLEAN),
	XRT_INPUT_TOUCH_Y_TOUCH                      = XRT_INPUT_NAME(0x00C3, BOOLEAN),
	XRT_INPUT_TOUCH_MENU_CLICK                   = XRT_INPUT_NAME(0x00C4, BOOLEAN),
	XRT_INPUT_TOUCH_A_CLICK                      = XRT_INPUT_NAME(0x00C5, BOOLEAN),
	XRT_INPUT_TOUCH_A_TOUCH                      = XRT_INPUT_NAME(0x00C6, BOOLEAN),
	XRT_INPUT_TOUCH_B_CLICK                      = XRT_INPUT_NAME(0x00C7, BOOLEAN),
	XRT_INPUT_TOUCH_B_TOUCH                      = XRT_INPUT_NAME(0x00C8, BOOLEAN),
	XRT_INPUT_TOUCH_SYSTEM_CLICK                 = XRT_INPUT_NAME(0x00C9, BOOLEAN),
	XRT_INPUT_TOUCH_SQUEEZE_VALUE                = XRT_INPUT_NAME(0x00CA, VEC1_ZERO_TO_ONE),
	XRT_INPUT_TOUCH_TRIGGER_TOUCH                = XRT_INPUT_NAME(0x00CB, BOOLEAN),
	XRT_INPUT_TOUCH_TRIGGER_VALUE                = XRT_INPUT_NAME(0x00CC, VEC1_ZERO_TO_ONE),
	XRT_INPUT_TOUCH_THUMBSTICK_CLICK             = XRT_INPUT_NAME(0x00CD, BOOLEAN),
	XRT_INPUT_TOUCH_THUMBSTICK_TOUCH             = XRT_INPUT_NAME(0x00CE, BOOLEAN),
	XRT_INPUT_TOUCH_THUMBSTICK                   = XRT_INPUT_NAME(0x00CF, VEC2_MINUS_ONE_TO_ONE),
	XRT_INPUT_TOUCH_THUMBREST_TOUCH              = XRT_INPUT_NAME(0x00D0, BOOLEAN),
	XRT_INPUT_TOUCH_GRIP_POSE                    = XRT_INPUT_NAME(0x00D1, POSE),
	XRT_INPUT_TOUCH_AIM_POSE                     = XRT_INPUT_NAME(0x00D2, POSE),

	XRT_INPUT_HAND_SELECT_VALUE                  = XRT_INPUT_NAME(0x00E0, VEC1_ZERO_TO_ONE),
	XRT_INPUT_HAND_SQUEEZE_VALUE                 = XRT_INPUT_NAME(0x00E1, VEC1_ZERO_TO_ONE),
	XRT_INPUT_HAND_GRIP_POSE                     = XRT_INPUT_NAME(0x00E2, POSE),
	XRT_INPUT_HAND_AIM_POSE                      = XRT_INPUT_NAME(0x00E3, POSE),

	// clang-format on
};

/*!
 * Number of joints in a hand. Corresponds to XR_HAND_JOINT_COUNT_EXT.
 *
 * @see xrt_hand_joint
 * @ingroup xrt_iface
 */
#define XRT_HAND_JOINT_COUNT 26

/*!
 * Number of joints in a hand. Corresponds to XrHandJointEXT.
 *
 * @ingroup xrt_iface
 */
enum xrt_hand_joint
{
	XRT_HAND_JOINT_PALM = 0,
	XRT_HAND_JOINT_WRIST = 1,
	XRT_HAND_JOINT_THUMB_METACARPAL = 2,
	XRT_HAND_JOINT_THUMB_PROXIMAL = 3,
	XRT_HAND_JOINT_THUMB_DISTAL = 4,
	XRT_HAND_JOINT_THUMB_TIP = 5,
	XRT_HAND_JOINT_INDEX_METACARPAL = 6,
	XRT_HAND_JOINT_INDEX_PROXIMAL = 7,
	XRT_HAND_JOINT_INDEX_INTERMEDIATE = 8,
	XRT_HAND_JOINT_INDEX_DISTAL = 9,
	XRT_HAND_JOINT_INDEX_TIP = 10,
	XRT_HAND_JOINT_MIDDLE_METACARPAL = 11,
	XRT_HAND_JOINT_MIDDLE_PROXIMAL = 12,
	XRT_HAND_JOINT_MIDDLE_INTERMEDIATE = 13,
	XRT_HAND_JOINT_MIDDLE_DISTAL = 14,
	XRT_HAND_JOINT_MIDDLE_TIP = 15,
	XRT_HAND_JOINT_RING_METACARPAL = 16,
	XRT_HAND_JOINT_RING_PROXIMAL = 17,
	XRT_HAND_JOINT_RING_INTERMEDIATE = 18,
	XRT_HAND_JOINT_RING_DISTAL = 19,
	XRT_HAND_JOINT_RING_TIP = 20,
	XRT_HAND_JOINT_LITTLE_METACARPAL = 21,
	XRT_HAND_JOINT_LITTLE_PROXIMAL = 22,
	XRT_HAND_JOINT_LITTLE_INTERMEDIATE = 23,
	XRT_HAND_JOINT_LITTLE_DISTAL = 24,
	XRT_HAND_JOINT_LITTLE_TIP = 25,
	XRT_HAND_JOINT_MAX_ENUM = 0x7FFFFFFF
};

/*!
 * Enumeration for left and right hand.
 *
 * @ingroup xrt_iface
 */
enum xrt_hand
{
	XRT_HAND_LEFT = 0,
	XRT_HAND_RIGHT = 1
};

/*!
 * Location of a single hand joint. Corresponds to XrHandJointLocationEXT.
 *
 * @ingroup xrt_iface
 */
struct xrt_hand_joint_value
{
	struct xrt_space_relation relation;
	float radius;
};

/*!
 * Number of fingers on a hand.
 *
 * @ingroup xrt_iface
 */
#define XRT_FINGER_COUNT 5

/*!
 * Names for fingers on a hand.
 *
 * @ingroup xrt_iface
 */
enum xrt_finger
{
	XRT_FINGER_LITTLE = 0,
	XRT_FINGER_RING,
	XRT_FINGER_MIDDLE,
	XRT_FINGER_INDEX,
	XRT_FINGER_THUMB
};

/*!
 * Joint set type used for hand tracking. Corresponds to XrHandJointSetEXT.
 *
 * @ingroup xrt_iface
 */
struct xrt_hand_joint_set
{
	union {
		struct xrt_hand_joint_value hand_joint_set_default[XRT_HAND_JOINT_COUNT];
	} values;

	// in driver global space, without tracking_origin offset
	struct xrt_space_relation hand_pose;
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
	XRT_OUTPUT_NAME_SIMPLE_VIBRATION            = XRT_OUTPUT_NAME(0x0010, VIBRATION),
	XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION       = XRT_OUTPUT_NAME(0x0020, VIBRATION),
	XRT_OUTPUT_NAME_INDEX_HAPTIC                = XRT_OUTPUT_NAME(0x0030, VIBRATION),
	XRT_OUTPUT_NAME_VIVE_HAPTIC                 = XRT_OUTPUT_NAME(0x0040, VIBRATION),
	XRT_OUTPUT_NAME_WMR_HAPTIC                  = XRT_OUTPUT_NAME(0x0050, VIBRATION),

	XRT_OUTPUT_NAME_XBOX_HAPTIC_LEFT            = XRT_OUTPUT_NAME(0x0060, VIBRATION),
	XRT_OUTPUT_NAME_XBOX_HAPTIC_RIGHTT          = XRT_OUTPUT_NAME(0x0061, VIBRATION),
	XRT_OUTPUT_NAME_XBOX_HAPTIC_LEFT_TRIGGER    = XRT_OUTPUT_NAME(0x0062, VIBRATION),
	XRT_OUTPUT_NAME_XBOX_HAPTIC_RIGHT_TRIGGER   = XRT_OUTPUT_NAME(0x0063, VIBRATION),

	XRT_OUTPUT_NAME_TOUCH_HAPTIC                = XRT_OUTPUT_NAME(0x0070, VIBRATION),

	// clang-format on
};

/*!
 * Value used to indicate a haptic pulse of the minimal supported duration.
 *
 * @ingroup xrt_iface
 */
#define XRT_MIN_HAPTIC_DURATION -1

/*!
 * Value used to indicate a haptic pulse of some runtime defined optimal
 * frequency.
 *
 * @ingroup xrt_iface
 */

#define XRT_FREQUENCY_UNSPECIFIED 0

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
	int32_t count = xrt_atomic_s32_dec_return(&xref->count);
	return count == 0;
}

static inline void
xrt_reference_inc(struct xrt_reference *xref)
{
	xrt_atomic_s32_inc_return(&xref->count);
}


#ifdef __cplusplus
}
#endif
