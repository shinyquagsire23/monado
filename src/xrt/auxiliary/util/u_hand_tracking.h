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


/*!
 * The hand tracking model being used.
 *
 * XRT_HAND_TRACKING_MODEL_FINGERL_CURL uses one curl value per finger to
 * synthesize hand joint positions.
 *
 * @ingroup aux_util
 */
enum u_hand_tracking_model
{
	XRT_HAND_TRACKING_MODEL_FINGERL_CURL,
	XRT_HAND_TRACKING_MODEL_CAMERA,
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
	int num_joints;
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

	enum u_hand_tracking_model model;
	union {
		struct u_hand_tracking_curl_values curl_values;
	} model_data;

	struct u_hand_joint_default_set joints;

	uint64_t timestamp_ns;
};

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
u_hand_joint_is_tip(enum xrt_hand_joint joint);

/*!
 * @ingroup aux_util
 */
bool
u_hand_joint_is_thumb(enum xrt_hand_joint joint);

/*!
 * Initializes a hand tracking set with default data.
 *
 * @ingroup aux_util
 */
void
u_hand_joints_init_default_set(struct u_hand_tracking *set,
                               enum xrt_hand hand,
                               enum u_hand_tracking_model model,
                               float scale);

/*!
 * Helper function using hand_relation and hand_offset to transform joint
 * locations from an xrt_hand_tracking data in hand space
 * to an xrt_hand_joint_set in global space.
 *
 * @ingroup aux_util
 */
void
u_hand_joints_set_out_data(struct u_hand_tracking *set,
                           enum xrt_hand hand,
                           struct xrt_space_relation *hand_relation,
                           struct xrt_pose *hand_offset,
                           struct xrt_hand_joint_set *out_value);


/*
 *
 * Curl model specific functions
 *
 */

/*!
 * @ingroup aux_util
 */
void
u_hand_joint_compute_next_by_curl(struct u_hand_tracking *set,
                                  struct u_joint_space_relation *prev,
                                  enum xrt_hand hand,
                                  uint64_t at_timestamp_ns,
                                  struct u_joint_space_relation *out_joint,
                                  float curl_value);

/*!
 * @ingroup aux_util
 */
void
u_hand_joints_update_curl(struct u_hand_tracking *set,
                          enum xrt_hand hand,
                          uint64_t at_timestamp_ns,
                          struct u_hand_tracking_curl_values *curl_values);

/*!
 * Simple helper function for positioning hands on Valve Index controllers.
 *
 * @ingroup aux_util
 */
void
u_hand_joints_offset_valve_index_controller(enum xrt_hand hand,
                                            struct xrt_vec3 *static_offset,
                                            struct xrt_pose *offset);


#ifdef __cplusplus
}
#endif
