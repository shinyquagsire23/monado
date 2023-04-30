// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  So much space!
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */


#include "xrt/xrt_space.h"

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
#include "oxr_conversions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 *
 * Helper functions.
 *
 */

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


/*
 *
 * To xrt_space functions.
 *
 */

static XrResult
get_xrt_space_action(struct oxr_logger *log, struct oxr_space *spc, struct xrt_space **out_xspace)
{

	struct oxr_action_input *input = NULL;

	XrResult ret = oxr_action_get_pose_input(spc->sess, spc->act_key, &spc->subaction_paths, &input);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	// Clear the cache.
	if (input == NULL) {
		xrt_space_reference(&spc->action.xs, NULL);
		spc->action.name = 0;
		spc->action.xdev = NULL;
		return XR_SUCCESS;
	}

	struct xrt_device *xdev = input->xdev;
	enum xrt_input_name name = input->input->name;

	assert(xdev != NULL);
	assert(name != 0);

	if (xdev != spc->action.xdev || name != spc->action.name) {
		xrt_space_reference(&spc->action.xs, NULL);

		xrt_result_t xret = xrt_space_overseer_create_pose_space( //
		    spc->sess->sys->xso,                                  //
		    xdev,                                                 //
		    name,                                                 //
		    &spc->action.xs);                                     //
		if (xret != XRT_SUCCESS) {
			oxr_warn(log, "Failed to create pose space");
		} else {
			spc->action.xdev = xdev;
			spc->action.name = name;
		}
	}

	*out_xspace = spc->action.xs;

	return XR_SUCCESS;
}

static XrResult
get_xrt_space(struct oxr_logger *log, struct oxr_space *spc, struct xrt_space **out_xspace)
{
	assert(out_xspace != NULL);
	assert(*out_xspace == NULL);

	struct xrt_space *xspace = NULL;
	switch (spc->space_type) {
	case OXR_SPACE_TYPE_ACTION: return get_xrt_space_action(log, spc, out_xspace);
	case OXR_SPACE_TYPE_REFERENCE_VIEW: xspace = spc->sess->sys->xso->semantic.view; break;
	case OXR_SPACE_TYPE_REFERENCE_LOCAL: xspace = spc->sess->sys->xso->semantic.local; break;
	case OXR_SPACE_TYPE_REFERENCE_LOCAL_FLOOR: xspace = NULL; break;
	case OXR_SPACE_TYPE_REFERENCE_STAGE: xspace = spc->sess->sys->xso->semantic.stage; break;
	case OXR_SPACE_TYPE_REFERENCE_UNBOUNDED_MSFT: xspace = spc->sess->sys->xso->semantic.unbounded; break;
	case OXR_SPACE_TYPE_REFERENCE_COMBINED_EYE_VARJO: xspace = NULL; break;
	}

	if (xspace == NULL) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Reference space without internal semantic space!");
	}

	*out_xspace = xspace;

	return XR_SUCCESS;
}


/*
 *
 * Space creation and destroy functions.
 *
 */

static XrResult
oxr_space_destroy(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	struct oxr_space *spc = (struct oxr_space *)hb;

	xrt_space_reference(&spc->action.xs, NULL);
	spc->action.xdev = NULL;
	spc->action.name = 0;

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

	oxr_classify_subaction_paths(log, inst, 1, &createInfo->subactionPath, &subaction_paths);

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


/*
 *
 * OpenXR API functions.
 *
 */

XrResult
oxr_space_locate(
    struct oxr_logger *log, struct oxr_space *spc, struct oxr_space *baseSpc, XrTime time, XrSpaceLocation *location)
{
	struct oxr_sink_logger slog = {0};
	struct oxr_system *sys = spc->sess->sys;
	bool print = sys->inst->debug_spaces;
	if (print) {
		oxr_pp_space_indented(&slog, spc, "space");
		oxr_pp_space_indented(&slog, baseSpc, "baseSpace");
	}

	// Used in a lot of places.
	XrSpaceVelocity *vel = OXR_GET_OUTPUT_FROM_CHAIN(location->next, XR_TYPE_SPACE_VELOCITY, XrSpaceVelocity);


	/*
	 * Seek knowledge about the spaces from the space overseer.
	 */

	struct xrt_space *xtarget = NULL;
	struct xrt_space *xbase = NULL;
	XrResult ret;

	ret = get_xrt_space(log, spc, &xtarget);
	// Make sure not to overwrite error return
	if (ret == XR_SUCCESS) {
		ret = get_xrt_space(log, baseSpc, &xbase);
	}

	// Only fill this out if the above succeeded.
	struct xrt_space_relation result = XRT_SPACE_RELATION_ZERO;
	if (xtarget != NULL && xbase != NULL) {
		// Convert at_time to monotonic and give to device.
		uint64_t at_timestamp_ns = time_state_ts_to_monotonic_ns(sys->inst->timekeeping, time);

		// Ask the space overseer to locate the spaces.
		xrt_space_overseer_locate_space( //
		    sys->xso,                    //
		    xbase,                       //
		    &baseSpc->pose,              //
		    at_timestamp_ns,             //
		    xtarget,                     //
		    &spc->pose,                  //
		    &result);                    //
	}


	/*
	 * Validate results
	 */

	if (result.relation_flags == 0) {
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

		return ret; // Return any error.
	}


	/*
	 * Combine and copy
	 */

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
		oxr_pp_relation_indented(&slog, &result, "relation");
		oxr_log_slog(log, &slog);
	} else {
		oxr_slog_cancel(&slog);
	}

	return oxr_session_success_result(spc->sess);
}


/*
 *
 * 'Exported' functions.
 *
 */

XrResult
oxr_space_locate_device(struct oxr_logger *log,
                        struct xrt_device *xdev,
                        struct oxr_space *baseSpc,
                        XrTime time,
                        struct xrt_space_relation *out_relation)
{
	struct oxr_system *sys = baseSpc->sess->sys;

	struct xrt_space *xbase = NULL;
	XrResult ret;

	ret = get_xrt_space(log, baseSpc, &xbase);
	if (xbase == NULL) {
		return ret;
	}

	// Convert at_time to monotonic and give to device.
	uint64_t at_timestamp_ns = time_state_ts_to_monotonic_ns(sys->inst->timekeeping, time);

	// Ask the space overseer to locate the spaces.
	xrt_space_overseer_locate_device( //
	    sys->xso,                     //
	    xbase,                        //
	    &baseSpc->pose,               //
	    at_timestamp_ns,              //
	    xdev,                         //
	    out_relation);                //

	return ret;
}
