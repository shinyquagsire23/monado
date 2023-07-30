// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Hand Tracking API interface.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_defines.h"
#include "util/u_misc.h"


#ifdef __cplusplus
extern "C" {
#endif


struct u_hand_tracking_finger_value
{
	float splay;

	float joint_curls[4];
	int joint_count;
};

struct u_hand_tracking_values
{
	struct u_hand_tracking_finger_value little;
	struct u_hand_tracking_finger_value ring;
	struct u_hand_tracking_finger_value middle;
	struct u_hand_tracking_finger_value index;
	struct u_hand_tracking_finger_value thumb;
};

/*!
 * Values used for the XRT_HAND_TRACKING_MODEL_FINGERL_CURL model.
 *
 * @ingroup aux_util
 */
struct u_hand_tracking_curl_values
{
	float little;
	float ring;
	float middle;
	float index;
	float thumb;
};

/*!
 * A space relation of a single joint.
 *
 * @ingroup aux_util
 */
struct u_joint_space_relation
{
	enum xrt_hand_joint joint_id;
	struct xrt_space_relation relation;
};

/*!
 * A set of joints in a single finger.
 *
 * @ingroup aux_util
 */
struct u_finger_joint_set
{
	struct u_joint_space_relation joints[5];
	int joint_count;
};

/*!
 * The set of joints in the XR_HAND_JOINT_SET_DEFAULT_EXT.
 *
 * @ingroup aux_util
 */
struct u_hand_joint_default_set
{
	struct u_joint_space_relation palm;
	struct u_joint_space_relation wrist;

	struct u_finger_joint_set fingers[XRT_FINGER_COUNT];
};

/*!
 * Main struct drivers can use to implement hand and finger tracking.
 *
 * @ingroup aux_util
 */
struct u_hand_tracking
{
	// scales dimensions like bone lengths
	float scale;

	union {
		struct u_hand_tracking_values finger_values;
	} model_data;

	struct u_hand_joint_default_set joints;

	uint64_t timestamp_ns;
};

/*!
 * Applies joint width to set.
 * @ingroup aux_util
 */
void
u_hand_joints_apply_joint_width(struct xrt_hand_joint_set *set);

/*!
 * @ingroup aux_util
 */
bool
u_hand_joint_is_tip(enum xrt_hand_joint joint);

/*!
 * @ingroup aux_util
 */
bool
u_hand_joint_is_metacarpal(enum xrt_hand_joint joint);

/*!
 * @ingroup aux_util
 */
bool
u_hand_joint_is_proximal(enum xrt_hand_joint joint);

/*!
 * @ingroup aux_util
 */
bool
u_hand_joint_is_intermediate(enum xrt_hand_joint joint);

/*!
 * @ingroup aux_util
 */
bool
u_hand_joint_is_distal(enum xrt_hand_joint joint);

/*!
 * @ingroup aux_util
 */
bool
u_hand_joint_is_thumb(enum xrt_hand_joint joint);

#ifdef __cplusplus
}
#endif
