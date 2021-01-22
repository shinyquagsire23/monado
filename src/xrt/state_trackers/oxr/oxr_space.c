// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  So much space!
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "math/m_api.h"
#include "math/m_space.h"
#include "util/u_debug.h"
#include "util/u_misc.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_handle.h"


const struct xrt_pose origin = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};

static XrResult
check_reference_space_type(struct oxr_logger *log, XrReferenceSpaceType type)
{
	switch (type) {
	case XR_REFERENCE_SPACE_TYPE_VIEW: return XR_SUCCESS;
	case XR_REFERENCE_SPACE_TYPE_LOCAL: return XR_SUCCESS;
	case XR_REFERENCE_SPACE_TYPE_STAGE:
		// For now stage space is always supported.
		if (true) {
			return XR_SUCCESS;
		}
		return oxr_error(log, XR_ERROR_REFERENCE_SPACE_UNSUPPORTED,
		                 "(createInfo->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE)"
		                 " Stage space is unsupported on this device.");
	default:
		return oxr_error(log, XR_ERROR_REFERENCE_SPACE_UNSUPPORTED,
		                 "(createInfo->referenceSpaceType == 0x%08x)", type);
	}
}

static XrResult
oxr_space_destroy(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	struct oxr_space *spc = (struct oxr_space *)hb;
	free(spc);
	return XR_SUCCESS;
}

XrResult
oxr_space_action_create(struct oxr_logger *log,
                        struct oxr_session *sess,
                        uint64_t key,
                        const XrActionSpaceCreateInfo *createInfo,
                        struct oxr_space **out_space)
{
	struct oxr_instance *inst = sess->sys->inst;
	struct oxr_subaction_paths subaction_paths = {0};

	struct oxr_space *spc = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, spc, OXR_XR_DEBUG_SPACE, oxr_space_destroy, &sess->handle);

	oxr_classify_sub_action_paths(log, inst, 1, &createInfo->subactionPath, &subaction_paths);

	spc->sess = sess;
	spc->is_reference = false;
	spc->subaction_paths = subaction_paths;
	spc->act_key = key;
	memcpy(&spc->pose, &createInfo->poseInActionSpace, sizeof(spc->pose));

	*out_space = spc;

	return XR_SUCCESS;
}

XrResult
oxr_space_reference_create(struct oxr_logger *log,
                           struct oxr_session *sess,
                           const XrReferenceSpaceCreateInfo *createInfo,
                           struct oxr_space **out_space)
{
	XrResult ret;

	ret = check_reference_space_type(log, createInfo->referenceSpaceType);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (!math_pose_validate((struct xrt_pose *)&createInfo->poseInReferenceSpace)) {
		return oxr_error(log, XR_ERROR_POSE_INVALID, "(createInfo->poseInReferenceSpace)");
	}

	struct oxr_space *spc = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, spc, OXR_XR_DEBUG_SPACE, oxr_space_destroy, &sess->handle);
	spc->sess = sess;
	spc->is_reference = true;
	spc->type = createInfo->referenceSpaceType;
	memcpy(&spc->pose, &createInfo->poseInReferenceSpace, sizeof(spc->pose));

	*out_space = spc;

	return XR_SUCCESS;
}


static const char *
get_ref_space_type_short_str(struct oxr_space *spc)
{
	if (!spc->is_reference) {
		return "action";
	}

	switch (spc->type) {
	case XR_REFERENCE_SPACE_TYPE_VIEW: return "view";
	case XR_REFERENCE_SPACE_TYPE_LOCAL: return "local";
	case XR_REFERENCE_SPACE_TYPE_STAGE: return "stage";
	default: return "unknown";
	}
}

static bool
ensure_initial_head_relation(struct oxr_logger *log, struct oxr_session *sess, struct xrt_space_relation *head_relation)
{
	if ((head_relation->relation_flags & XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT) == 0) {
		return false;
	}

	if (!initial_head_relation_valid(sess)) {
		sess->initial_head_relation = *head_relation;

		// take only head rotation around y axis
		// https://stackoverflow.com/a/5783030
		sess->initial_head_relation.pose.orientation.x = 0;
		sess->initial_head_relation.pose.orientation.z = 0;
		math_quat_normalize(&sess->initial_head_relation.pose.orientation);

		//! @todo: Handle relation velocities if necessary
	}
	return true;
}

bool
initial_head_relation_valid(struct oxr_session *sess)
{
	return sess->initial_head_relation.relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;
}

bool
global_to_local_space(struct oxr_session *sess, struct xrt_space_relation *rel)
{
	if (!initial_head_relation_valid(sess)) {
		return false;
	}

	struct xrt_space_graph graph = {0};
	m_space_graph_add_relation(&graph, rel);
	m_space_graph_add_inverted_pose_if_not_identity(&graph, &sess->initial_head_relation.pose);
	m_space_graph_resolve(&graph, rel);

	return true;
}

/*!
 * This returns only the relation between two spaces without any of the app
 * given relations applied, assumes that both spaces are reference spaces.
 */
XrResult
oxr_space_ref_relation(struct oxr_logger *log,
                       struct oxr_session *sess,
                       XrReferenceSpaceType space,
                       XrReferenceSpaceType baseSpc,
                       XrTime time,
                       struct xrt_space_relation *out_relation)
{
	m_space_relation_ident(out_relation);


	if (space == baseSpc) {
		// m_space_relation_ident() sets to identity.
	} else if (space == XR_REFERENCE_SPACE_TYPE_VIEW) {
		oxr_session_get_view_relation_at(log, sess, time, out_relation);

		if (!ensure_initial_head_relation(log, sess, out_relation)) {
			out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
			return XR_SUCCESS;
		}

		if (baseSpc == XR_REFERENCE_SPACE_TYPE_STAGE) {
			// device poses are already in stage = "global" space
		} else if (baseSpc == XR_REFERENCE_SPACE_TYPE_LOCAL) {
			global_to_local_space(sess, out_relation);
		} else if (baseSpc == XR_REFERENCE_SPACE_TYPE_VIEW) {

		} else {
			OXR_WARN_ONCE(log, "unsupported base space in space_ref_relation");
			out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
			return XR_SUCCESS;
		}
	} else if (baseSpc == XR_REFERENCE_SPACE_TYPE_VIEW) {
		oxr_session_get_view_relation_at(log, sess, time, out_relation);

		if (!ensure_initial_head_relation(log, sess, out_relation)) {
			out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
			return XR_SUCCESS;
		}
		if (space == XR_REFERENCE_SPACE_TYPE_STAGE) {
			// device poses are already in stage = "global" space
		} else if (space == XR_REFERENCE_SPACE_TYPE_LOCAL) {
			global_to_local_space(sess, out_relation);
		} else if (space == XR_REFERENCE_SPACE_TYPE_VIEW) {

		} else {
			OXR_WARN_ONCE(log, "unsupported base space in space_ref_relation");
			out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
			return XR_SUCCESS;
		}
		math_pose_invert(&out_relation->pose, &out_relation->pose);

	} else if (space == XR_REFERENCE_SPACE_TYPE_STAGE) {
		if (baseSpc == XR_REFERENCE_SPACE_TYPE_LOCAL) {
			math_pose_invert(&sess->initial_head_relation.pose, &out_relation->pose);
		} else {
			OXR_WARN_ONCE(log, "unsupported base space in space_ref_relation");
			out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
			return XR_SUCCESS;
		}
	} else if (space == XR_REFERENCE_SPACE_TYPE_LOCAL) {
		if (baseSpc == XR_REFERENCE_SPACE_TYPE_STAGE) {
			out_relation->pose = sess->initial_head_relation.pose;
		} else {
			OXR_WARN_ONCE(log, "unsupported base space in space_ref_relation");
			out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
			return XR_SUCCESS;
		}
	} else {
		out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
		return XR_SUCCESS;
	}

	return XR_SUCCESS;
}

static void
remove_angular_and_linear_stuff(struct xrt_space_relation *out_relation)
{
	const enum xrt_space_relation_flags flags =
	    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT | XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT;

	out_relation->relation_flags &= ~flags;
	out_relation->linear_velocity = (struct xrt_vec3){0, 0, 0};
	out_relation->angular_velocity = (struct xrt_vec3){0, 0, 0};
}

/*!
 * This returns only the relation between two spaces without any of the app
 * given relations applied, assumes that only one is a action space.
 */
static XrResult
oxr_space_action_relation(struct oxr_logger *log,
                          struct oxr_session *sess,
                          struct oxr_space *spc,
                          struct oxr_space *baseSpc,
                          XrTime at_time,
                          struct xrt_space_relation *out_relation)
{
	struct oxr_action_input *input = NULL;
	struct oxr_space *act_spc, *ref_spc = NULL;
	bool invert = false;

	// Find the action space
	if (baseSpc->is_reference) {
		// Note spc, is assumed to be the action space.
		act_spc = spc;
		ref_spc = baseSpc;
	}

	// Find the action space.
	if (spc->is_reference) {
		// Note baseSpc, is assumed to be the action space.
		act_spc = baseSpc;
		ref_spc = spc;
		invert = true;
	}

	// Internal error check.
	if (act_spc == NULL || act_spc->is_reference || ref_spc == NULL || !ref_spc->is_reference) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "This is bad!");
	}

	// Reset so no relation is returned.
	m_space_relation_ident(out_relation);

	//! @todo Can not relate to the view space right now.
	if (baseSpc->type == XR_REFERENCE_SPACE_TYPE_VIEW) {
		//! @todo Error code?
		OXR_WARN_ONCE(log, "relating to view space unsupported");
		return XR_SUCCESS;
	}

	oxr_action_get_pose_input(log, sess, act_spc->act_key, &act_spc->subaction_paths, &input);

	// If the input isn't active.
	if (input == NULL) {
		out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
		return XR_SUCCESS;
	}

	oxr_xdev_get_space_relation(log, sess->sys->inst, input->xdev, input->input->name, at_time, out_relation);

	if (baseSpc->type == XR_REFERENCE_SPACE_TYPE_LOCAL) {
		global_to_local_space(sess, out_relation);
	}

	if (invert) {
		math_pose_invert(&out_relation->pose, &out_relation->pose);
		// Remove this since we can't (for now) invert the derivatives.
		remove_angular_and_linear_stuff(out_relation);
	}

	return XR_SUCCESS;
}

/*!
 * This returns only the relation between two directly-associated spaces without
 * any of the app given relations applied.
 */
static XrResult
get_pure_space_relation(struct oxr_logger *log,
                        struct oxr_space *spc,
                        struct oxr_space *baseSpc,
                        XrTime time,
                        struct xrt_space_relation *out_relation)
{
	struct oxr_session *sess = spc->sess;

	if (spc->is_reference && baseSpc->is_reference) {
		return oxr_space_ref_relation(log, sess, spc->type, baseSpc->type, time, out_relation);
	}
	if (!spc->is_reference && !baseSpc->is_reference) {
		// @todo Deal with action to action by keeping a true_space that
		//       we can always go via. Aka poor mans space graph.
		// WARNING order not thought through here!
		// struct xrt_pose pose1;
		// struct xrt_pose pose2;
		// get_pure_space_relation(log, session->true_space, baseSpc,
		//                         time, &pose1);
		// get_pure_space_relation(log, space, session->true_space,
		//                         time, &pose2);
		// math_pose_relate_2(&pose1, &pose2, out_pose);
		out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
		return XR_SUCCESS;
	}

	oxr_space_action_relation(log, sess, spc, baseSpc, time, out_relation);
	return XR_SUCCESS;
}

static void
print_pose(struct oxr_session *sess, const char *prefix, struct xrt_pose *pose)
{
	if (!sess->sys->inst->debug_spaces) {
		return;
	}

	struct xrt_vec3 *p = &pose->position;
	struct xrt_quat *q = &pose->orientation;

	U_LOG_D("%s (%f, %f, %f) (%f, %f, %f, %f)", prefix, p->x, p->y, p->z, q->x, q->y, q->z, q->w);
}

static void
print_space(const char *name, struct oxr_space *spc)
{
	if (!spc->sess->sys->inst->debug_spaces) {
		return;
	}

	const char *type_str = get_ref_space_type_short_str(spc);
	U_LOG_D("\t%s->type %s\n\t%s->pose", name, type_str, name);
	print_pose(spc->sess, "", &spc->pose);
}

XrSpaceLocationFlags
xrt_to_xr_space_location_flags(enum xrt_space_relation_flags relation_flags)
{
	// clang-format off
	bool valid_ori = (relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0;
	bool tracked_ori = (relation_flags & XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT) != 0;
	bool valid_pos = (relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) != 0;
	bool tracked_pos = (relation_flags & XRT_SPACE_RELATION_POSITION_TRACKED_BIT) != 0;

	bool linear_vel = (relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) != 0;
	bool angular_vel = (relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) != 0;
	// clang-format on

	XrSpaceLocationFlags location_flags = (XrSpaceLocationFlags)0;
	if (valid_ori) {
		location_flags |= XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	}
	if (tracked_ori) {
		location_flags |= XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
	}
	if (valid_pos) {
		location_flags |= XR_SPACE_LOCATION_POSITION_VALID_BIT;
	}
	if (tracked_pos) {
		location_flags |= XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	}
	if (linear_vel) {
		location_flags |= XR_SPACE_VELOCITY_LINEAR_VALID_BIT;
	}
	if (angular_vel) {
		location_flags |= XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
	}
	return location_flags;
}

XrResult
oxr_space_locate(
    struct oxr_logger *log, struct oxr_space *spc, struct oxr_space *baseSpc, XrTime time, XrSpaceLocation *location)
{
	if (spc->sess->sys->inst->debug_spaces) {
		U_LOG_D("%s", __func__);
	}
	print_space("space", spc);
	print_space("baseSpace", baseSpc);

	// Get the pure space relation.
	//! @todo for longer paths in "space graph" than one edge, this will be
	//! a loop.
	struct xrt_space_relation pure;
	XrResult ret = get_pure_space_relation(log, spc, baseSpc, time, &pure);
	if (ret != XR_SUCCESS) {
		location->locationFlags = 0;
		return ret;
	}

	// Combine space and base space poses with pure relation
	struct xrt_space_relation result;
	struct xrt_space_graph graph = {0};
	m_space_graph_add_pose_if_not_identity(&graph, &spc->pose);
	m_space_graph_add_relation(&graph, &pure);
	m_space_graph_add_inverted_pose_if_not_identity(&graph, &baseSpc->pose);
	m_space_graph_resolve(&graph, &result);

	// Copy
	union {
		struct xrt_pose xrt;
		XrPosef oxr;
	} safe_copy = {0};
	safe_copy.xrt = result.pose;

	location->pose = safe_copy.oxr;
	location->locationFlags = xrt_to_xr_space_location_flags(result.relation_flags);

	XrSpaceVelocity *vel = (XrSpaceVelocity *)location->next;
	if (vel) {
		vel->linearVelocity.x = result.linear_velocity.x;
		vel->linearVelocity.y = result.linear_velocity.y;
		vel->linearVelocity.z = result.linear_velocity.z;

		vel->angularVelocity.x = result.angular_velocity.x;
		vel->angularVelocity.y = result.angular_velocity.y;
		vel->angularVelocity.z = result.angular_velocity.z;

		vel->velocityFlags |= (location->locationFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT);
		vel->velocityFlags |= (location->locationFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT);
	}

#if 0
	location->linearVelocity = *(XrVector3f *)&result.linear_velocity;
	location->angularVelocity = *(XrVector3f *)&result.angular_velocity;
	location->linearAcceleration =
	    *(XrVector3f *)&result.linear_acceleration;
	location->angularAcceleration =
	    *(XrVector3f *)&result.angular_acceleration;
#endif

	print_pose(spc->sess, "\trelation->pose", (struct xrt_pose *)&location->pose);

	return oxr_session_success_result(spc->sess);
}
