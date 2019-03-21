// Copyright 2019, Collabora, Ltd.
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
#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"


DEBUG_GET_ONCE_BOOL_OPTION(space, "OXR_DEBUG_SPACE", false)

const struct xrt_pose origin = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};

static XrResult
check_reference_space_type(struct oxr_logger *log, XrReferenceSpaceType type)
{
	switch (type) {
	case XR_REFERENCE_SPACE_TYPE_VIEW: return XR_SUCCESS;
	case XR_REFERENCE_SPACE_TYPE_LOCAL: return XR_SUCCESS;
	case XR_REFERENCE_SPACE_TYPE_STAGE: return XR_SUCCESS;
#if 0
		return oxr_error(log, XR_ERROR_REFERENCE_SPACE_UNSUPPORTED,
		                 "(createInfo->referenceSpaceType = "
		                 "XR_REFERENCE_SPACE_TYPE_STAGE)");
#endif
	default:
		return oxr_error(log, XR_ERROR_REFERENCE_SPACE_UNSUPPORTED,
		                 "(createInfo->referenceSpaceType = "
		                 "<UNKNOWN>)");
	}
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

	if (!math_pose_validate(
	        (struct xrt_pose *)&createInfo->poseInReferenceSpace)) {
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(createInfo->poseInReferenceSpace)");
	}

	struct oxr_space *spc =
	    (struct oxr_space *)calloc(1, sizeof(struct oxr_space));
	spc->debug = OXR_XR_DEBUG_SPACE;
	spc->sess = sess;
	spc->is_reference = true;
	spc->type = createInfo->referenceSpaceType;
	memcpy(&spc->pose, &createInfo->poseInReferenceSpace,
	       sizeof(spc->pose));

	*out_space = spc;

	return XR_SUCCESS;
}

XrResult
oxr_space_destroy(struct oxr_logger *log, struct oxr_space *spc)
{
	free(spc);
	return XR_SUCCESS;
}

static const char *
get_ref_space_type_short_str(struct oxr_space *spc)
{
	if (!spc->is_reference) {
		return "action?";
	}

	switch (spc->type) {
	case XR_REFERENCE_SPACE_TYPE_VIEW: return "view";
	case XR_REFERENCE_SPACE_TYPE_LOCAL: return "local";
	case XR_REFERENCE_SPACE_TYPE_STAGE: return "stage";
	default: return "unknown";
	}
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
	// Treat stage space as the local space.
	if (space == XR_REFERENCE_SPACE_TYPE_STAGE) {
		space = XR_REFERENCE_SPACE_TYPE_LOCAL;
	}

	// Treat stage space as the local space.
	if (baseSpc == XR_REFERENCE_SPACE_TYPE_STAGE) {
		baseSpc = XR_REFERENCE_SPACE_TYPE_LOCAL;
	}

	math_relation_reset(out_relation);

	if (space == XR_REFERENCE_SPACE_TYPE_VIEW &&
	    baseSpc == XR_REFERENCE_SPACE_TYPE_LOCAL) {
		oxr_session_get_view_pose_at(log, sess, time,
		                             &out_relation->pose);

		out_relation->relation_flags = (enum xrt_space_relation_flags)(
		    XRT_SPACE_RELATION_POSITION_VALID_BIT |
		    XRT_SPACE_RELATION_POSITION_TRACKED_BIT |
		    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
		    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

	} else if (space == XR_REFERENCE_SPACE_TYPE_LOCAL &&
	           baseSpc == XR_REFERENCE_SPACE_TYPE_VIEW) {
		oxr_session_get_view_pose_at(log, sess, time,
		                             &out_relation->pose);
		math_pose_invert(&out_relation->pose, &out_relation->pose);

		out_relation->relation_flags = (enum xrt_space_relation_flags)(
		    XRT_SPACE_RELATION_POSITION_VALID_BIT |
		    XRT_SPACE_RELATION_POSITION_TRACKED_BIT |
		    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
		    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

	} else if (space == baseSpc) {
		// math_relation_reset() sets to identity.

	} else {
		out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
		return XR_SUCCESS;
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
		return oxr_space_ref_relation(
		    log, sess, spc->type, baseSpc->type, time, out_relation);
	} else if (!spc->is_reference && !baseSpc->is_reference) {
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
	} else {
		// @todo deal with action space poses.
		out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
		return XR_SUCCESS;
	}
}

static void
print_pose(const char *prefix, struct xrt_pose *pose)
{
	if (!debug_get_bool_option_space()) {
		return;
	}

	struct xrt_vec3 *p = &pose->position;
	struct xrt_quat *q = &pose->orientation;

	fprintf(stderr, "%s (%f, %f, %f) (%f, %f, %f, %f)\n", prefix, p->x,
	        p->y, p->z, q->x, q->y, q->z, q->w);
}

static void
print_space(const char *name, struct oxr_space *spc)
{
	if (!debug_get_bool_option_space()) {
		return;
	}

	const char *type_str = get_ref_space_type_short_str(spc);
	fprintf(stderr, "\t%s->type %s\n\t%s->pose", name, type_str, name);
	print_pose("", &spc->pose);
}

XrResult
oxr_space_locate(struct oxr_logger *log,
                 struct oxr_space *spc,
                 struct oxr_space *baseSpc,
                 XrTime time,
                 XrSpaceRelation *relation)
{
	if (debug_get_bool_option_space()) {
		fprintf(stderr, "%s\n", __func__);
	}
	print_space("space", spc);
	print_space("baseSpace", baseSpc);

	// Get the pure space relation.
	//! @todo for longer paths in "space graph" than one edge, this will be
	//! a loop.
	struct xrt_space_relation pure;
	XrResult ret = get_pure_space_relation(log, spc, baseSpc, time, &pure);
	if (ret != XR_SUCCESS) {
		relation->relationFlags = 0;
		return ret;
	}

	// Combine space and base space poses with pure relation
	struct xrt_space_relation result;
	math_relation_openxr_locate(&spc->pose, &pure, &baseSpc->pose, &result);

	// Copy
	relation->pose = *(XrPosef *)&result.pose;
	relation->linearVelocity = *(XrVector3f *)&result.linear_velocity;
	relation->angularVelocity = *(XrVector3f *)&result.angular_velocity;
	relation->linearAcceleration =
	    *(XrVector3f *)&result.linear_acceleration;
	relation->angularAcceleration =
	    *(XrVector3f *)&result.angular_acceleration;
	relation->relationFlags = result.relation_flags;

	print_pose("\trelation->pose", (struct xrt_pose *)&relation->pose);

	return XR_SUCCESS;
}
