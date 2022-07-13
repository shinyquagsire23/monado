// Copyright 2019-2022, Collabora, Ltd.
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
#include "oxr_input_transform.h"
#include "oxr_chain.h"
#include "oxr_pretty_print.h"


const struct xrt_pose origin = XRT_POSE_IDENTITY;

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
                        uint32_t key,
                        const XrActionSpaceCreateInfo *createInfo,
                        struct oxr_space **out_space)
{
	struct oxr_instance *inst = sess->sys->inst;
	struct oxr_subaction_paths subaction_paths = {0};

	struct oxr_space *spc = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, spc, OXR_XR_DEBUG_SPACE, oxr_space_destroy, &sess->handle);

	oxr_classify_sub_action_paths(log, inst, 1, &createInfo->subactionPath, &subaction_paths);

	spc->sess = sess;
	spc->space_type = OXR_SPACE_TYPE_ACTION;
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
	spc->space_type = xr_ref_space_to_oxr(createInfo->referenceSpaceType);
	memcpy(&spc->pose, &createInfo->poseInReferenceSpace, sizeof(spc->pose));

	*out_space = spc;

	return XR_SUCCESS;
}

static void
print_pose(struct oxr_session *sess, const char *prefix, struct xrt_pose *pose);

static bool
set_up_local_space(struct oxr_logger *log, struct oxr_session *sess, XrTime time)
{
	struct xrt_device *head_xdev = GET_XDEV_BY_ROLE(sess->sys, head);
	struct xrt_space_relation head_relation;
	oxr_xdev_get_space_relation(log, sess->sys->inst, head_xdev, XRT_INPUT_GENERIC_HEAD_POSE, time, &head_relation);

	if ((head_relation.relation_flags & XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT) == 0) {
		return false;
	}

	if (!is_local_space_set_up(sess)) {
		sess->local_space_pure_relation = head_relation;

		// take only head rotation around y axis
		// https://stackoverflow.com/a/5783030
		sess->local_space_pure_relation.pose.orientation.x = 0;
		sess->local_space_pure_relation.pose.orientation.z = 0;
		math_quat_normalize(&sess->local_space_pure_relation.pose.orientation);

		print_pose(sess, "local space updated", &head_relation.pose);

		//! @todo: Handle relation velocities if necessary
	}
	return true;
}

XRT_CHECK_RESULT bool
is_local_space_set_up(struct oxr_session *sess)
{
	return (sess->local_space_pure_relation.relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0;
}


/*!
 * Returns the pure relation in global space of an oxr_space, meaning the tracking_origin offsets are already applied.
 *
 * @todo: Until a proper reference space system is implemented, the xdev assigned to the head role should be used as @p
 * ref_xdev for consistency.
 */
XRT_CHECK_RESULT static bool
oxr_space_ref_get_pure_relation(struct oxr_logger *log,
                                struct oxr_session *sess,
                                enum oxr_space_type space_type,
                                struct xrt_device *ref_xdev,
                                XrTime time,
                                struct xrt_space_relation *out_relation)
{
	switch (space_type) {
	case OXR_SPACE_TYPE_REFERENCE_LOCAL: {
		if (!is_local_space_set_up(sess)) {
			if (!set_up_local_space(log, sess, time)) {
				return false;
			}
		}

		*out_relation = sess->local_space_pure_relation;
		return true;
	}
	case OXR_SPACE_TYPE_REFERENCE_STAGE: {
		//! @todo: stage space origin assumed to be the same as HMD xdev space origin for now.
		m_space_relation_ident(out_relation);
		return true;
	}
	case OXR_SPACE_TYPE_REFERENCE_VIEW: {
		oxr_xdev_get_space_relation(log, sess->sys->inst, ref_xdev, XRT_INPUT_GENERIC_HEAD_POSE, time,
		                            out_relation);
		return true;
	}

	case OXR_SPACE_TYPE_REFERENCE_UNBOUNDED_MSFT:
	case OXR_SPACE_TYPE_REFERENCE_COMBINED_EYE_VARJO:
		// not implemented
		return oxr_error(log, false, "Reference Space type %d not implemented!", space_type);
	case OXR_SPACE_TYPE_ACTION: return oxr_error(log, false, "Space is not a reference space!");
	}
	return true;
}

XRT_CHECK_RESULT bool
oxr_space_pure_relation_in_space(struct oxr_logger *log,
                                 XrTime time,
                                 struct xrt_space_relation *relation,
                                 struct oxr_space *spc,
                                 bool apply_space_pose,
                                 struct xrt_space_relation *out_relation)
{
	struct xrt_space_relation pure_space_relation;
	struct xrt_device *xdev;
	if (!oxr_space_get_pure_relation(log, spc, time, &pure_space_relation, &xdev)) {
		return false;
	}

	struct xrt_relation_chain xrc = {0};

	m_relation_chain_push_relation(&xrc, relation);
	m_relation_chain_push_inverted_relation(&xrc, &pure_space_relation);

	if (apply_space_pose) {
		m_relation_chain_push_inverted_pose_if_not_identity(&xrc, &spc->pose);
	}

	m_relation_chain_resolve(&xrc, out_relation);
	return true;
}

XRT_CHECK_RESULT bool
oxr_space_pure_pose_in_space(struct oxr_logger *log,
                             XrTime time,
                             struct xrt_pose *pose,
                             struct oxr_space *spc,
                             bool apply_space_pose,
                             struct xrt_space_relation *out_relation)
{
	struct xrt_space_relation rel;
	m_space_relation_from_pose(pose, &rel);
	return oxr_space_pure_relation_in_space(log, time, &rel, spc, apply_space_pose, out_relation);
}

XRT_CHECK_RESULT bool
oxr_space_pure_relation_from_space(struct oxr_logger *log,
                                   XrTime time,
                                   struct xrt_space_relation *relation,
                                   struct oxr_space *spc,
                                   struct xrt_space_relation *out_relation)
{
	struct xrt_space_relation pure_space_relation;
	struct xrt_device *xdev;
	if (!oxr_space_get_pure_relation(log, spc, time, &pure_space_relation, &xdev)) {
		return false;
	}

	struct xrt_relation_chain xrc = {0};
	m_relation_chain_push_relation(&xrc, relation);
	m_relation_chain_push_pose_if_not_identity(&xrc, &spc->pose);
	m_relation_chain_push_relation(&xrc, &pure_space_relation);
	m_relation_chain_resolve(&xrc, out_relation);
	return true;
}

XRT_CHECK_RESULT bool
oxr_space_pure_pose_from_space(struct oxr_logger *log,
                               XrTime time,
                               struct xrt_pose *pose,
                               struct oxr_space *spc,
                               struct xrt_space_relation *out_relation)
{
	struct xrt_space_relation rel;
	m_space_relation_from_pose(pose, &rel);
	return oxr_space_pure_relation_from_space(log, time, &rel, spc, out_relation);
}

XRT_CHECK_RESULT bool
oxr_space_get_pure_relation(struct oxr_logger *log,
                            struct oxr_space *spc,
                            XrTime time,
                            struct xrt_space_relation *out_relation,
                            struct xrt_device **out_xdev)
{
	if (oxr_space_type_is_reference(spc->space_type)) {
		struct xrt_device *head_xdev = GET_XDEV_BY_ROLE(spc->sess->sys, head);
		*out_xdev = head_xdev;
		return oxr_space_ref_get_pure_relation(log, spc->sess, spc->space_type, head_xdev, time, out_relation);
	}
	if (spc->space_type == OXR_SPACE_TYPE_ACTION) {
		struct oxr_action_input *input = NULL;
		oxr_action_get_pose_input(spc->sess, spc->act_key, &spc->subaction_paths, &input);

		// If the input isn't active.
		if (input == NULL) {
			out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
			return false;
		}

		*out_xdev = input->xdev;
		oxr_xdev_get_space_relation(log, spc->sess->sys->inst, input->xdev, input->input->name, time,
		                            out_relation);

		return true;
	}

	return oxr_error(log, false, "Unknown space type");
}

XRT_CHECK_RESULT bool
global_to_local_space(struct oxr_logger *log, struct oxr_session *sess, XrTime time, struct xrt_space_relation *rel)
{
	if (!is_local_space_set_up(sess)) {
		if (!set_up_local_space(log, sess, time)) {
			return false;
		}
	}

	struct xrt_relation_chain xrc = {0};
	m_relation_chain_push_relation(&xrc, rel);
	m_relation_chain_push_inverted_pose_if_not_identity(&xrc, &sess->local_space_pure_relation.pose);
	m_relation_chain_resolve(&xrc, rel);

	return true;
}

/*!
 * This returns only the relation between two directly-associated spaces without
 * the app given offset pose for baseSpc applied.
 */
XRT_CHECK_RESULT static bool
get_pure_space_relation(struct oxr_logger *log,
                        struct oxr_space *spc,
                        struct oxr_space *baseSpc,
                        XrTime time,
                        struct xrt_space_relation *out_relation)
{
	struct xrt_space_relation space_pure_relation;
	struct xrt_device *space_xdev;
	if (!oxr_space_get_pure_relation(log, spc, time, &space_pure_relation, &space_xdev)) {
		return false;
	}

	struct xrt_space_relation base_space_pure_relation;
	struct xrt_device *base_space_xdev;
	if (!oxr_space_get_pure_relation(log, baseSpc, time, &base_space_pure_relation, &base_space_xdev)) {
		return false;
	}

	struct xrt_relation_chain xrc = {0};
	m_relation_chain_push_relation(&xrc, &space_pure_relation);
	m_relation_chain_push_inverted_relation(&xrc, &base_space_pure_relation);
	m_relation_chain_resolve(&xrc, out_relation);

	return true;
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
	struct oxr_sink_logger slog = {0};
	bool print = spc->sess->sys->inst->debug_spaces;
	if (print) {
		oxr_pp_space_indented(&slog, spc, "space");
		oxr_pp_space_indented(&slog, baseSpc, "baseSpace");
	}

	// Used in a lot of places.
	XrSpaceVelocity *vel = OXR_GET_OUTPUT_FROM_CHAIN(location->next, XR_TYPE_SPACE_VELOCITY, XrSpaceVelocity);

	// Get the pure space relation.
	struct xrt_space_relation pure;
	bool has_pure_relation = get_pure_space_relation(log, spc, baseSpc, time, &pure);
	if (!has_pure_relation) {
		location->locationFlags = 0;

		OXR_XRT_POSE_TO_XRPOSEF(XRT_POSE_IDENTITY, location->pose);

		if (vel) {
			vel->velocityFlags = 0;
			U_ZERO(&vel->linearVelocity);
			U_ZERO(&vel->angularVelocity);
		}

		if (print) {
			oxr_slog(&slog, "\n\tReturning invalid pose");
			oxr_log_slog(log, &slog);
		} else {
			oxr_slog_cancel(&slog);
		}

		return XR_SUCCESS;
	}


	/*
	 * Combine and copy
	 */

	// Combine space and base space poses with pure relation
	struct xrt_space_relation result;
	struct xrt_relation_chain xrc = {0};
	m_relation_chain_push_pose_if_not_identity(&xrc, &spc->pose);
	m_relation_chain_push_relation(&xrc, &pure);
	m_relation_chain_push_inverted_pose_if_not_identity(&xrc, &baseSpc->pose);
	m_relation_chain_resolve(&xrc, &result);

	OXR_XRT_POSE_TO_XRPOSEF(result.pose, location->pose);
	location->locationFlags = xrt_to_xr_space_location_flags(result.relation_flags);

	if (vel) {
		vel->velocityFlags = 0;
		if ((result.relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) != 0) {
			vel->linearVelocity.x = result.linear_velocity.x;
			vel->linearVelocity.y = result.linear_velocity.y;
			vel->linearVelocity.z = result.linear_velocity.z;
			vel->velocityFlags |= XR_SPACE_VELOCITY_LINEAR_VALID_BIT;
		} else {
			U_ZERO(&vel->linearVelocity);
		}

		if ((result.relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) != 0) {
			vel->angularVelocity.x = result.angular_velocity.x;
			vel->angularVelocity.y = result.angular_velocity.y;
			vel->angularVelocity.z = result.angular_velocity.z;
			vel->velocityFlags |= XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
		} else {
			U_ZERO(&vel->angularVelocity);
		}
	}


	/*
	 * Print
	 */

	if (print) {
		oxr_pp_pose_indented_as_object(&slog, (struct xrt_pose *)&pure.pose, "pure");
		oxr_pp_relation_indented(&slog, &result, "relation");
		oxr_log_slog(log, &slog);
	} else {
		oxr_slog_cancel(&slog);
	}

	return oxr_session_success_result(spc->sess);
}
