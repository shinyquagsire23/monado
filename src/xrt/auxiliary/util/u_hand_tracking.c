// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Hand Tracking API interface.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup aux_tracking
 */

#include "u_hand_tracking.h"

#include "math/m_mathinclude.h"
#include "math/m_api.h"
#include "math/m_space.h"
#include "math/m_vec3.h"
#include "math/m_api.h"

#define DEG_TO_RAD(DEG) (DEG * M_PI / 180.)

#define VELOCIY_VALID_FLAGS                                                                                            \
	(XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT | XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT)

#define POSE_VALID_FLAGS                                                                                               \
	(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |                       \
	 XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT)

struct u_joint_curl_model
{
	enum xrt_hand_joint joint_id;
	// offset from hand origin (palm) in hand coordinates
	struct xrt_vec3 position_offset;
	// rotation always added to this joint
	float axis_angle_offset[3];
	// the length of the bone from this joint towards finger tips
	float bone_length;
	float radius;
};

//! @todo: Make this tunable by configuration
/* describes default configuration for a hand in rest position using the curl
 * model: Fingers are tracked with a singular curl value per finger.
 *
 * Coordinates are in "Hand coordinate system", i.e. a hand flat on a table has
 * y -> up, -z -> forward (direction of fingers), x -> right.
 *
 * Palm is always in the origin of the hand coordinate system.
 *
 * Finger Joints are rigidly connected to the bone towards the finger tips.
 *
 * metacarpal joints are connected to the wrist in the order
 * metacarpal, proximal, intermediate, distal, tip (thumb skips intermediate)
 *
 * Joint poses are calculated starting at the wrist. Iteratively joint poses are
 * calculated by rotating the joint by axis_angle_offset, then "following the
 * attached bone" to the next connected joint, and applying the next rotation
 * relative to the previous rotation.
 *
 * angles for left hand (right hand is mirrored), angles are clockwise.
 */
static struct u_joint_curl_model hand_joint_default_set_curl_model_defaults[XRT_HAND_JOINT_COUNT] = {
    // special cases: wrist and palm without bone lengths, offsets are
    // absolute, relative to hand origin (palm)
    [XRT_HAND_JOINT_PALM] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                             .axis_angle_offset = {0, 0, 0},
                             .bone_length = 0,
                             .radius = 0.018,
                             .joint_id = XRT_HAND_JOINT_PALM},

    [XRT_HAND_JOINT_WRIST] = {.position_offset = {.x = 0, .y = 0, .z = 0.07},
                              .axis_angle_offset = {0, 0, 0},
                              .bone_length = 0,
                              .radius = 0.03,
                              .joint_id = XRT_HAND_JOINT_WRIST},


    // fingers
    // metacarpal bones are angled outwards a little,
    // proximal bones copmpensate most of it, making fingers parallel again
    // finger position offsets are relative to previous bone
    // previous bone of metacarpal: wrist
    [XRT_HAND_JOINT_LITTLE_METACARPAL] = {.position_offset = {.x = -0.03, .y = 0, .z = -0.037},
                                          .axis_angle_offset = {0, DEG_TO_RAD(-25), 0},
                                          .bone_length = 0.048,
                                          .radius = 0.015,
                                          .joint_id = XRT_HAND_JOINT_LITTLE_METACARPAL},

    [XRT_HAND_JOINT_LITTLE_PROXIMAL] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                        .axis_angle_offset = {0, DEG_TO_RAD(20), 0},
                                        .bone_length = 0.035,
                                        .radius = 0.01,
                                        .joint_id = XRT_HAND_JOINT_LITTLE_PROXIMAL},

    [XRT_HAND_JOINT_LITTLE_INTERMEDIATE] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                            .axis_angle_offset = {0, 0, 0},
                                            .bone_length = 0.028,
                                            .radius = 0.009,
                                            .joint_id = XRT_HAND_JOINT_LITTLE_INTERMEDIATE},

    [XRT_HAND_JOINT_LITTLE_DISTAL] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                      .axis_angle_offset = {0, 0, 0},
                                      .bone_length = 0.022,
                                      .radius = 0.009,
                                      .joint_id = XRT_HAND_JOINT_LITTLE_DISTAL},

    [XRT_HAND_JOINT_LITTLE_TIP] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                   .axis_angle_offset = {0, 0, 0},
                                   .bone_length = 0,
                                   .radius = 0.012,
                                   .joint_id = XRT_HAND_JOINT_LITTLE_TIP},


    [XRT_HAND_JOINT_RING_METACARPAL] = {.position_offset = {.x = -0.015, .y = 0, .z = -0.038},
                                        .axis_angle_offset = {0, DEG_TO_RAD(-12), 0},
                                        .bone_length = 0.052,
                                        .radius = 0.015,
                                        .joint_id = XRT_HAND_JOINT_RING_METACARPAL},

    [XRT_HAND_JOINT_RING_PROXIMAL] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                      .axis_angle_offset = {0, DEG_TO_RAD(10), 0},
                                      .bone_length = 0.040,
                                      .radius = 0.012,
                                      .joint_id = XRT_HAND_JOINT_RING_PROXIMAL},

    [XRT_HAND_JOINT_RING_INTERMEDIATE] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                          .axis_angle_offset = {0, 0, 0},
                                          .bone_length = 0.031,
                                          .radius = 0.01,
                                          .joint_id = XRT_HAND_JOINT_RING_INTERMEDIATE},

    [XRT_HAND_JOINT_RING_DISTAL] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                    .axis_angle_offset = {0, 0, 0},
                                    .bone_length = 0.023,
                                    .radius = 0.01,
                                    .joint_id = XRT_HAND_JOINT_RING_DISTAL},

    [XRT_HAND_JOINT_RING_TIP] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                 .axis_angle_offset = {0, 0, 0},
                                 .bone_length = 0,
                                 .radius = 0.013,
                                 .joint_id = XRT_HAND_JOINT_RING_TIP},


    [XRT_HAND_JOINT_MIDDLE_METACARPAL] = {.position_offset = {.x = 0.00, .y = 0, .z = -0.039},
                                          .axis_angle_offset = {0, 0, 0},
                                          .bone_length = 0.045,
                                          .radius = 0.012,
                                          .joint_id = XRT_HAND_JOINT_MIDDLE_METACARPAL},

    [XRT_HAND_JOINT_MIDDLE_PROXIMAL] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                        .axis_angle_offset = {0, 0, 0},
                                        .bone_length = 0.042,
                                        .radius = 0.01,
                                        .joint_id = XRT_HAND_JOINT_MIDDLE_PROXIMAL},

    [XRT_HAND_JOINT_MIDDLE_INTERMEDIATE] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                            .axis_angle_offset = {0, 0, 0},
                                            .bone_length = 0.033,
                                            .radius = 0.01,
                                            .joint_id = XRT_HAND_JOINT_MIDDLE_INTERMEDIATE},

    [XRT_HAND_JOINT_MIDDLE_DISTAL] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                      .axis_angle_offset = {0, 0, 0},
                                      .bone_length = 0.024,
                                      .radius = 0.01,
                                      .joint_id = XRT_HAND_JOINT_MIDDLE_DISTAL},

    [XRT_HAND_JOINT_MIDDLE_TIP] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                   .axis_angle_offset = {0, 0, 0},
                                   .bone_length = 0,
                                   .radius = 0.01,
                                   .joint_id = XRT_HAND_JOINT_MIDDLE_TIP},


    [XRT_HAND_JOINT_INDEX_METACARPAL] = {.position_offset = {.x = 0.015, .y = 0, .z = -0.038},
                                         .axis_angle_offset = {0, DEG_TO_RAD(12), 0},
                                         .bone_length = 0.05,
                                         .radius = 0.012,
                                         .joint_id = XRT_HAND_JOINT_INDEX_METACARPAL},

    [XRT_HAND_JOINT_INDEX_PROXIMAL] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                       .axis_angle_offset = {0, DEG_TO_RAD(-10), 0},
                                       .bone_length = 0.040,
                                       .radius = 0.011,
                                       .joint_id = XRT_HAND_JOINT_INDEX_PROXIMAL},

    [XRT_HAND_JOINT_INDEX_INTERMEDIATE] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                           .axis_angle_offset = {0, 0, 0},
                                           .bone_length = 0.031,
                                           .radius = 0.01,
                                           .joint_id = XRT_HAND_JOINT_INDEX_INTERMEDIATE},

    [XRT_HAND_JOINT_INDEX_DISTAL] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                     .axis_angle_offset = {0, 0, 0},
                                     .bone_length = 0.023,
                                     .radius = 0.01,
                                     .joint_id = XRT_HAND_JOINT_INDEX_DISTAL},

    [XRT_HAND_JOINT_INDEX_TIP] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                  .axis_angle_offset = {0, 0, 0},
                                  .bone_length = 0,
                                  .radius = 0.01,
                                  .joint_id = XRT_HAND_JOINT_INDEX_TIP},


    [XRT_HAND_JOINT_THUMB_METACARPAL] = {.position_offset = {.x = 0.03, .y = 0, .z = -0.033},
                                         .axis_angle_offset = {0, DEG_TO_RAD(40), 0},
                                         .bone_length = 0.037,
                                         .radius = 0.0175,
                                         .joint_id = XRT_HAND_JOINT_THUMB_METACARPAL},

    [XRT_HAND_JOINT_THUMB_PROXIMAL] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                       .axis_angle_offset = {0, DEG_TO_RAD(-12), 0},
                                       .bone_length = 0.038,
                                       .radius = 0.017,
                                       .joint_id = XRT_HAND_JOINT_THUMB_PROXIMAL},
    // no intermediate

    [XRT_HAND_JOINT_THUMB_DISTAL] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                     .axis_angle_offset = {0, 0, 0},
                                     .bone_length = 0.028,
                                     .radius = 0.017,
                                     .joint_id = XRT_HAND_JOINT_THUMB_DISTAL},

    [XRT_HAND_JOINT_THUMB_TIP] = {.position_offset = {.x = 0, .y = 0, .z = 0},
                                  .axis_angle_offset = {0, 0, 0},
                                  .bone_length = 0,
                                  .radius = 0.016,
                                  .joint_id = XRT_HAND_JOINT_THUMB_TIP}};

inline static void
quat_from_angle_vector_clockwise(float angle_rads, const struct xrt_vec3 *vector, struct xrt_quat *result)
{
	math_quat_from_angle_vector(-angle_rads, vector, result);
}

bool
u_hand_joint_is_metacarpal(enum xrt_hand_joint joint)
{
	return joint == XRT_HAND_JOINT_LITTLE_METACARPAL || joint == XRT_HAND_JOINT_RING_METACARPAL ||
	       joint == XRT_HAND_JOINT_MIDDLE_METACARPAL || joint == XRT_HAND_JOINT_INDEX_METACARPAL ||
	       joint == XRT_HAND_JOINT_THUMB_METACARPAL;
}

bool
u_hand_joint_is_proximal(enum xrt_hand_joint joint)
{
	return joint == XRT_HAND_JOINT_LITTLE_PROXIMAL || joint == XRT_HAND_JOINT_RING_PROXIMAL ||
	       joint == XRT_HAND_JOINT_MIDDLE_PROXIMAL || joint == XRT_HAND_JOINT_INDEX_PROXIMAL ||
	       joint == XRT_HAND_JOINT_THUMB_PROXIMAL;
}

bool
u_hand_joint_is_intermediate(enum xrt_hand_joint joint)
{
	return joint == XRT_HAND_JOINT_LITTLE_INTERMEDIATE || joint == XRT_HAND_JOINT_RING_INTERMEDIATE ||
	       joint == XRT_HAND_JOINT_MIDDLE_INTERMEDIATE || joint == XRT_HAND_JOINT_INDEX_INTERMEDIATE;
}

bool
u_hand_joint_is_distal(enum xrt_hand_joint joint)
{
	return joint == XRT_HAND_JOINT_LITTLE_DISTAL || joint == XRT_HAND_JOINT_RING_DISTAL ||
	       joint == XRT_HAND_JOINT_MIDDLE_DISTAL || joint == XRT_HAND_JOINT_INDEX_DISTAL ||
	       joint == XRT_HAND_JOINT_THUMB_DISTAL;
}

bool
u_hand_joint_is_tip(enum xrt_hand_joint joint)
{
	return joint == XRT_HAND_JOINT_LITTLE_TIP || joint == XRT_HAND_JOINT_RING_TIP ||
	       joint == XRT_HAND_JOINT_MIDDLE_TIP || joint == XRT_HAND_JOINT_INDEX_TIP ||
	       joint == XRT_HAND_JOINT_THUMB_TIP;
}

bool
u_hand_joint_is_thumb(enum xrt_hand_joint joint)
{
	return joint == XRT_HAND_JOINT_THUMB_METACARPAL || joint == XRT_HAND_JOINT_THUMB_PROXIMAL ||
	       joint == XRT_HAND_JOINT_THUMB_DISTAL || joint == XRT_HAND_JOINT_THUMB_TIP;
}

static void
scale_model_param(struct u_joint_curl_model *param, float scale)
{
	param->bone_length *= scale;
	math_vec3_scalar_mul(scale, &param->position_offset);
	param->radius *= scale;
}

void
u_hand_joint_compute_next_by_curl(struct u_hand_tracking *set,
                                  struct u_joint_space_relation *prev,
                                  enum xrt_hand hand,
                                  uint64_t at_timestamp_ns,
                                  struct u_joint_space_relation *out_joint,
                                  float curl_value)
{
	struct xrt_space_relation old_relation = out_joint->relation;

	struct u_joint_curl_model prev_defaults = hand_joint_default_set_curl_model_defaults[prev->joint_id];
	struct u_joint_curl_model defaults = hand_joint_default_set_curl_model_defaults[out_joint->joint_id];

	scale_model_param(&prev_defaults, set->scale);
	scale_model_param(&defaults, set->scale);

	struct xrt_vec3 x_axis = {1, 0, 0};
	struct xrt_vec3 y_axis = {0, 1, 0};

	// prev joint pose is transformed to next joint pose by adding the bone
	// vector to the joint, and adding rotation based on finger curl
	struct xrt_pose pose = prev->relation.pose;

	// create bone vector with orientation of previous joint
	struct xrt_vec3 bone = {0, 0, -prev_defaults.bone_length};
	math_quat_rotate_vec3(&pose.orientation, &bone, &bone);

	// translate the bone to the previous joint
	math_vec3_accum(&bone, &pose.position);


	// curl and bone length alone doesn't give us an actual hand shape.
	// rotate first finger joints "outwards" to create a hand shape.
	// the offset rotation should not rotate the curl rotation, it rotates
	// the joint "around the finger axis", before the curl rotation.

	//! @todo more axis rotations & make sure order is right
	//! @todo handle velocities
	struct xrt_pose offset_pose;
	if (hand == XRT_HAND_LEFT) {
		quat_from_angle_vector_clockwise(defaults.axis_angle_offset[1], &y_axis, &offset_pose.orientation);
		offset_pose.position = defaults.position_offset;
	}
	if (hand == XRT_HAND_RIGHT) {
		quat_from_angle_vector_clockwise(-defaults.axis_angle_offset[1], &y_axis, &offset_pose.orientation);

		offset_pose.position = (struct xrt_vec3){.x = defaults.position_offset.x * -1,
		                                         .y = defaults.position_offset.y,
		                                         .z = defaults.position_offset.z};
	}
	math_pose_transform(&pose, &offset_pose, &pose);


	// proximal, intermediate, and distal joints (+ bones) will rotate
	//! @todo make this tunable
	// full curl angle for this joint at 1.0 value
	float full_curl_angle = 0;
	if (u_hand_joint_is_thumb(out_joint->joint_id)) {
		if (u_hand_joint_is_metacarpal(out_joint->joint_id)) {
			full_curl_angle = DEG_TO_RAD(10);
		} else if (u_hand_joint_is_proximal(out_joint->joint_id)) {
			full_curl_angle = DEG_TO_RAD(30);
		} else if (u_hand_joint_is_distal(out_joint->joint_id)) {
			full_curl_angle = DEG_TO_RAD(80);
		}
	} else {
		if (u_hand_joint_is_metacarpal(out_joint->joint_id)) {
			full_curl_angle = DEG_TO_RAD(0);
		} else if (u_hand_joint_is_proximal(out_joint->joint_id)) {
			full_curl_angle = DEG_TO_RAD(70);
		} else if (u_hand_joint_is_intermediate(out_joint->joint_id)) {
			full_curl_angle = DEG_TO_RAD(90);
		} else if (u_hand_joint_is_distal(out_joint->joint_id)) {
			full_curl_angle = DEG_TO_RAD(65);
		}
	}

	float curl_angle = curl_value * full_curl_angle;

	struct xrt_quat curl_rotation;
	math_quat_from_angle_vector(-curl_angle, &x_axis, &curl_rotation);
	math_quat_rotate(&pose.orientation, &curl_rotation, &pose.orientation);

	//! @todo: full relation with velocities
	out_joint->relation.pose = pose;

	double time_diff_s = time_ns_to_s(at_timestamp_ns - set->timestamp_ns);

	// linear velocity =
	// diff of current and old joint position, divided by timestamp diff
	out_joint->relation.linear_velocity = out_joint->relation.pose.position;
	math_vec3_subtract(&old_relation.pose.position, &out_joint->relation.linear_velocity);
	math_vec3_scalar_mul(1. / time_diff_s, &out_joint->relation.linear_velocity);

	if (time_diff_s > 0) {
		math_quat_finite_difference(&old_relation.pose.orientation, &out_joint->relation.pose.orientation,
		                            time_diff_s, &out_joint->relation.angular_velocity);
	} else {
		out_joint->relation.angular_velocity = (struct xrt_vec3){0, 0, 0};
	}

	out_joint->relation.relation_flags = POSE_VALID_FLAGS | VELOCIY_VALID_FLAGS;
}

void
u_hand_joints_update_curl(struct u_hand_tracking *set,
                          enum xrt_hand hand,
                          uint64_t at_timestamp_ns,
                          struct u_hand_tracking_curl_values *curl_values)
{
	float curl_little = curl_values->little;
	float curl_ring = curl_values->ring;
	float curl_middle = curl_values->middle;
	float curl_index = curl_values->index;
	float curl_thumb = curl_values->thumb;

	const struct xrt_quat identity_quat = {0, 0, 0, 1};

	//! @todo: full relations with velocities

	// wrist and palm mostly fixed poses
	set->joints.wrist.relation.pose = (struct xrt_pose){
	    .position = hand_joint_default_set_curl_model_defaults[XRT_HAND_JOINT_WRIST].position_offset,
	    .orientation = identity_quat};
	set->joints.wrist.relation.linear_velocity = (struct xrt_vec3){0, 0, 0};
	set->joints.wrist.relation.angular_velocity = (struct xrt_vec3){0, 0, 0};
	set->joints.wrist.relation.relation_flags = POSE_VALID_FLAGS | VELOCIY_VALID_FLAGS;

	set->joints.palm.relation.pose = (struct xrt_pose){
	    .position = hand_joint_default_set_curl_model_defaults[XRT_HAND_JOINT_PALM].position_offset,
	    .orientation = identity_quat};
	set->joints.palm.relation.linear_velocity = (struct xrt_vec3){0, 0, 0};
	set->joints.palm.relation.angular_velocity = (struct xrt_vec3){0, 0, 0};
	set->joints.palm.relation.relation_flags = POSE_VALID_FLAGS | VELOCIY_VALID_FLAGS;

	struct u_joint_space_relation *prev = &set->joints.wrist;
	for (int joint_num = 0; joint_num < set->joints.fingers[XRT_FINGER_LITTLE].num_joints; joint_num++) {
		struct u_joint_space_relation *joint = &set->joints.fingers[XRT_FINGER_LITTLE].joints[joint_num];
		u_hand_joint_compute_next_by_curl(set, prev, hand, at_timestamp_ns, joint, curl_little);
		prev = joint;
	}

	prev = &set->joints.wrist;
	for (int joint_num = 0; joint_num < set->joints.fingers[XRT_FINGER_RING].num_joints; joint_num++) {
		struct u_joint_space_relation *joint = &set->joints.fingers[XRT_FINGER_RING].joints[joint_num];
		u_hand_joint_compute_next_by_curl(set, prev, hand, at_timestamp_ns, joint, curl_ring);
		prev = joint;
	}

	prev = &set->joints.wrist;
	for (int joint_num = 0; joint_num < set->joints.fingers[XRT_FINGER_MIDDLE].num_joints; joint_num++) {
		struct u_joint_space_relation *joint = &set->joints.fingers[XRT_FINGER_MIDDLE].joints[joint_num];
		u_hand_joint_compute_next_by_curl(set, prev, hand, at_timestamp_ns, joint, curl_middle);
		prev = joint;
	}

	prev = &set->joints.wrist;
	for (int joint_num = 0; joint_num < set->joints.fingers[XRT_FINGER_INDEX].num_joints; joint_num++) {
		struct u_joint_space_relation *joint = &set->joints.fingers[XRT_FINGER_INDEX].joints[joint_num];
		u_hand_joint_compute_next_by_curl(set, prev, hand, at_timestamp_ns, joint, curl_index);
		prev = joint;
	}

	prev = &set->joints.wrist;
	for (int joint_num = 0; joint_num < set->joints.fingers[XRT_FINGER_THUMB].num_joints; joint_num++) {
		struct u_joint_space_relation *joint = &set->joints.fingers[XRT_FINGER_THUMB].joints[joint_num];
		u_hand_joint_compute_next_by_curl(set, prev, hand, at_timestamp_ns, joint, curl_thumb);
		prev = joint;
	}

	set->model_data.curl_values = *curl_values;
	set->timestamp_ns = at_timestamp_ns;
}

void
u_hand_joints_init_default_set(struct u_hand_tracking *set,
                               enum xrt_hand hand,
                               enum u_hand_tracking_model model,
                               float scale)
{
	struct xrt_space_relation identity;
	m_space_relation_ident(&identity);

	*set =
	    (struct u_hand_tracking){
	        .joints = {.palm = {.joint_id = XRT_HAND_JOINT_PALM, .relation = identity},
	                   .wrist = {.joint_id = XRT_HAND_JOINT_WRIST, .relation = identity},

	                   .fingers = {[XRT_FINGER_LITTLE] = {.num_joints = 5,
	                                                      .joints =
	                                                          {
	                                                              {
	                                                                  .joint_id = XRT_HAND_JOINT_LITTLE_METACARPAL,
	                                                                  .relation = identity,
	                                                              },
	                                                              {
	                                                                  .joint_id = XRT_HAND_JOINT_LITTLE_PROXIMAL,
	                                                                  .relation = identity,
	                                                              },
	                                                              {
	                                                                  .joint_id =
	                                                                      XRT_HAND_JOINT_LITTLE_INTERMEDIATE,
	                                                                  .relation = identity,
	                                                              },
	                                                              {
	                                                                  .joint_id = XRT_HAND_JOINT_LITTLE_DISTAL,
	                                                                  .relation = identity,
	                                                              },
	                                                              {
	                                                                  .joint_id = XRT_HAND_JOINT_LITTLE_TIP,
	                                                                  .relation = identity,
	                                                              },
	                                                          }},

	                               [XRT_FINGER_RING] = {.num_joints = 5,
	                                                    .joints =
	                                                        {
	                                                            {
	                                                                .joint_id = XRT_HAND_JOINT_RING_METACARPAL,
	                                                                .relation = identity,
	                                                            },
	                                                            {
	                                                                .joint_id = XRT_HAND_JOINT_RING_PROXIMAL,
	                                                                .relation = identity,
	                                                            },
	                                                            {
	                                                                .joint_id = XRT_HAND_JOINT_RING_INTERMEDIATE,
	                                                                .relation = identity,
	                                                            },
	                                                            {
	                                                                .joint_id = XRT_HAND_JOINT_RING_DISTAL,
	                                                                .relation = identity,
	                                                            },
	                                                            {
	                                                                .joint_id = XRT_HAND_JOINT_RING_TIP,
	                                                                .relation = identity,
	                                                            },
	                                                        }},

	                               [XRT_FINGER_MIDDLE] = {.num_joints = 5,
	                                                      .joints =
	                                                          {
	                                                              {
	                                                                  .joint_id = XRT_HAND_JOINT_MIDDLE_METACARPAL,
	                                                                  .relation = identity,
	                                                              },
	                                                              {
	                                                                  .joint_id = XRT_HAND_JOINT_MIDDLE_PROXIMAL,
	                                                                  .relation = identity,
	                                                              },
	                                                              {
	                                                                  .joint_id =
	                                                                      XRT_HAND_JOINT_MIDDLE_INTERMEDIATE,
	                                                                  .relation = identity,
	                                                              },
	                                                              {
	                                                                  .joint_id = XRT_HAND_JOINT_MIDDLE_DISTAL,
	                                                                  .relation = identity,
	                                                              },
	                                                              {
	                                                                  .joint_id = XRT_HAND_JOINT_MIDDLE_TIP,
	                                                                  .relation = identity,
	                                                              },
	                                                          }},

	                               [XRT_FINGER_INDEX] = {.num_joints = 5,
	                                                     .joints =
	                                                         {
	                                                             {
	                                                                 .joint_id = XRT_HAND_JOINT_INDEX_METACARPAL,
	                                                                 .relation = identity,
	                                                             },
	                                                             {
	                                                                 .joint_id = XRT_HAND_JOINT_INDEX_PROXIMAL,
	                                                                 .relation = identity,
	                                                             },
	                                                             {
	                                                                 .joint_id = XRT_HAND_JOINT_INDEX_INTERMEDIATE,
	                                                                 .relation = identity,
	                                                             },
	                                                             {
	                                                                 .joint_id = XRT_HAND_JOINT_INDEX_DISTAL,
	                                                                 .relation = identity,
	                                                             },
	                                                             {
	                                                                 .joint_id = XRT_HAND_JOINT_INDEX_TIP,
	                                                                 .relation = identity,
	                                                             },
	                                                         }},

	                               [XRT_FINGER_THUMB] = {.num_joints = 4,
	                                                     .joints = {
	                                                         {
	                                                             .joint_id = XRT_HAND_JOINT_THUMB_METACARPAL,
	                                                             .relation = identity,
	                                                         },
	                                                         {
	                                                             .joint_id = XRT_HAND_JOINT_THUMB_PROXIMAL,
	                                                             .relation = identity,
	                                                         },
	                                                         // has no intermediate
	                                                         {
	                                                             .joint_id = XRT_HAND_JOINT_THUMB_DISTAL,
	                                                             .relation = identity,
	                                                         },
	                                                         {
	                                                             .joint_id = XRT_HAND_JOINT_THUMB_TIP,
	                                                             .relation = identity,
	                                                         },
	                                                     }}}}};

	set->model = XRT_HAND_TRACKING_MODEL_FINGERL_CURL;
	set->scale = scale;

	struct u_hand_tracking_curl_values values = {0, 0, 0, 0, 0};
	u_hand_joints_update_curl(set, hand, 0, &values);
}

static struct u_joint_space_relation *
get_joint_data(struct u_hand_tracking *set, enum xrt_hand_joint joint_id)
{
	switch (joint_id) {
	case XRT_HAND_JOINT_WRIST: return &set->joints.wrist;
	case XRT_HAND_JOINT_PALM: return &set->joints.palm;

	case XRT_HAND_JOINT_LITTLE_METACARPAL: return &set->joints.fingers[XRT_FINGER_LITTLE].joints[0];
	case XRT_HAND_JOINT_LITTLE_PROXIMAL: return &set->joints.fingers[XRT_FINGER_LITTLE].joints[1];
	case XRT_HAND_JOINT_LITTLE_INTERMEDIATE: return &set->joints.fingers[XRT_FINGER_LITTLE].joints[2];
	case XRT_HAND_JOINT_LITTLE_DISTAL: return &set->joints.fingers[XRT_FINGER_LITTLE].joints[3];
	case XRT_HAND_JOINT_LITTLE_TIP: return &set->joints.fingers[XRT_FINGER_LITTLE].joints[4];

	case XRT_HAND_JOINT_RING_METACARPAL: return &set->joints.fingers[XRT_FINGER_RING].joints[0];
	case XRT_HAND_JOINT_RING_PROXIMAL: return &set->joints.fingers[XRT_FINGER_RING].joints[1];
	case XRT_HAND_JOINT_RING_INTERMEDIATE: return &set->joints.fingers[XRT_FINGER_RING].joints[2];
	case XRT_HAND_JOINT_RING_DISTAL: return &set->joints.fingers[XRT_FINGER_RING].joints[3];
	case XRT_HAND_JOINT_RING_TIP: return &set->joints.fingers[XRT_FINGER_RING].joints[4];

	case XRT_HAND_JOINT_MIDDLE_METACARPAL: return &set->joints.fingers[XRT_FINGER_MIDDLE].joints[0];
	case XRT_HAND_JOINT_MIDDLE_PROXIMAL: return &set->joints.fingers[XRT_FINGER_MIDDLE].joints[1];
	case XRT_HAND_JOINT_MIDDLE_INTERMEDIATE: return &set->joints.fingers[XRT_FINGER_MIDDLE].joints[2];
	case XRT_HAND_JOINT_MIDDLE_DISTAL: return &set->joints.fingers[XRT_FINGER_MIDDLE].joints[3];
	case XRT_HAND_JOINT_MIDDLE_TIP: return &set->joints.fingers[XRT_FINGER_MIDDLE].joints[4];

	case XRT_HAND_JOINT_INDEX_METACARPAL: return &set->joints.fingers[XRT_FINGER_INDEX].joints[0];
	case XRT_HAND_JOINT_INDEX_PROXIMAL: return &set->joints.fingers[XRT_FINGER_INDEX].joints[1];
	case XRT_HAND_JOINT_INDEX_INTERMEDIATE: return &set->joints.fingers[XRT_FINGER_INDEX].joints[2];
	case XRT_HAND_JOINT_INDEX_DISTAL: return &set->joints.fingers[XRT_FINGER_INDEX].joints[3];
	case XRT_HAND_JOINT_INDEX_TIP: return &set->joints.fingers[XRT_FINGER_INDEX].joints[4];

	case XRT_HAND_JOINT_THUMB_METACARPAL: return &set->joints.fingers[XRT_FINGER_THUMB].joints[0];
	case XRT_HAND_JOINT_THUMB_PROXIMAL: return &set->joints.fingers[XRT_FINGER_THUMB].joints[1];
	// no intermediate for thumb
	case XRT_HAND_JOINT_THUMB_DISTAL: return &set->joints.fingers[XRT_FINGER_THUMB].joints[2];
	case XRT_HAND_JOINT_THUMB_TIP: return &set->joints.fingers[XRT_FINGER_THUMB].joints[3];

	case XRT_HAND_JOINT_MAX_ENUM: return NULL;
	}
	return NULL;
}

void
u_hand_joints_set_out_data(struct u_hand_tracking *set,
                           enum xrt_hand hand,
                           struct xrt_space_relation *hand_relation,
                           struct xrt_pose *hand_offset,
                           struct xrt_hand_joint_set *out_value)
{

	struct xrt_hand_joint_value *l = out_value->values.hand_joint_set_default;

	for (int i = 0; i < XRT_HAND_JOINT_COUNT; i++) {
		struct u_joint_space_relation *data = get_joint_data(set, i);

		l[i].relation.relation_flags |= data->relation.relation_flags;
		l[i].radius = hand_joint_default_set_curl_model_defaults[i].radius;

		struct xrt_space_graph graph = {0};
		m_space_graph_add_relation(&graph, &data->relation);
		m_space_graph_add_pose(&graph, hand_offset);
		m_space_graph_resolve(&graph, &l[i].relation);
	}

	out_value->hand_pose = *hand_relation;
}

void
u_hand_joints_offset_valve_index_controller(enum xrt_hand hand, struct xrt_vec3 *static_offset, struct xrt_pose *offset)
{
	/* Controller space origin is at the very tip of the controller,
	 * handle pointing forward at -z.
	 *
	 * Transform joints into controller space by rotating "outwards" around
	 * -z "forward" by -75/75 deg. Then, rotate "forward" around x by 72
	 * deg.
	 *
	 * Then position everything at static_offset..
	 *
	 * Now the hand points "through the strap" like at normal use.
	 */
	struct xrt_vec3 x = {1, 0, 0};
	struct xrt_vec3 y = {0, 1, 0};
	struct xrt_vec3 z = {0, 0, -1};

	float hand_on_handle_x_rotation = DEG_TO_RAD(-72);
	float hand_on_handle_y_rotation = 0;
	float hand_on_handle_z_rotation = 0;
	if (hand == XRT_HAND_LEFT) {
		hand_on_handle_z_rotation = DEG_TO_RAD(-75);
	} else if (hand == XRT_HAND_RIGHT) {
		hand_on_handle_z_rotation = DEG_TO_RAD(75);
	}


	struct xrt_quat hand_rotation_y = {0, 0, 0, 1};
	math_quat_from_angle_vector(hand_on_handle_y_rotation, &y, &hand_rotation_y);

	struct xrt_quat hand_rotation_z = {0, 0, 0, 1};
	math_quat_from_angle_vector(hand_on_handle_z_rotation, &z, &hand_rotation_z);

	struct xrt_quat hand_rotation_x = {0, 0, 0, 1};
	math_quat_from_angle_vector(hand_on_handle_x_rotation, &x, &hand_rotation_x);

	struct xrt_quat hand_rotation;
	math_quat_rotate(&hand_rotation_x, &hand_rotation_z, &hand_rotation);

	struct xrt_pose hand_on_handle_pose = {.orientation = hand_rotation, .position = *static_offset};

	*offset = hand_on_handle_pose;
}
