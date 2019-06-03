// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds session related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */


#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "util/u_debug.h"
#include "util/u_misc.h"
#include "math/m_api.h"
#include "util/u_time.h"

#include "xrt/xrt_device.h"
#include "xrt/xrt_gfx_xlib.h"
#include "xrt/xrt_gfx_vk.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_handle.h"


DEBUG_GET_ONCE_BOOL_OPTION(views, "OXR_DEBUG_VIEWS", false)
DEBUG_GET_ONCE_BOOL_OPTION(dynamic_prediction, "OXR_DYNAMIC_PREDICTION", true)
DEBUG_GET_ONCE_NUM_OPTION(ipd, "OXR_DEBUG_IPD_MM", 63)
DEBUG_GET_ONCE_NUM_OPTION(prediction_ms, "OXR_DEBUG_PREDICTION_MS", 11)

static bool
is_running(XrSessionState state)
{
	switch (state) {
	case XR_SESSION_STATE_RUNNING: return true;
	case XR_SESSION_STATE_VISIBLE: return true;
	case XR_SESSION_STATE_FOCUSED: return true;
	default: return false;
	}
}

XrResult
oxr_session_enumerate_formats(struct oxr_logger *log,
                              struct oxr_session *sess,
                              uint32_t formatCapacityInput,
                              uint32_t *formatCountOutput,
                              int64_t *formats)
{
	struct xrt_compositor *xc = sess->compositor;
	if (xc == NULL) {
		*formatCountOutput = 0;
		return XR_SUCCESS;
	}

	OXR_TWO_CALL_HELPER(log, formatCapacityInput, formatCountOutput,
	                    formats, xc->num_formats, xc->formats);
}

XrResult
oxr_session_begin(struct oxr_logger *log,
                  struct oxr_session *sess,
                  const XrSessionBeginInfo *beginInfo)
{
	if (is_running(sess->state)) {
		return oxr_error(log, XR_ERROR_SESSION_RUNNING,
		                 " session is already running");
	}
	XrViewConfigurationType view_type =
	    beginInfo->primaryViewConfigurationType;
	if (view_type != sess->sys->view_config_type) {
		/*! @todo we only support a single view config type per system
		 * right now */
		return oxr_error(log, XR_ERROR_SESSION_RUNNING,
		                 " view configuration type not supported");
	}

	struct xrt_compositor *xc = sess->compositor;

	if (xc != NULL) {
		xc->begin_session(xc, (enum xrt_view_type)beginInfo
		                          ->primaryViewConfigurationType);
	}

	oxr_event_push_XrEventDataSessionStateChanged(
	    log, sess, XR_SESSION_STATE_RUNNING, 0);
	oxr_event_push_XrEventDataSessionStateChanged(
	    log, sess, XR_SESSION_STATE_VISIBLE, 0);
	oxr_event_push_XrEventDataSessionStateChanged(
	    log, sess, XR_SESSION_STATE_FOCUSED, 0);

	sess->state = XR_SESSION_STATE_FOCUSED;

	return XR_SUCCESS;
}

XrResult
oxr_session_end(struct oxr_logger *log, struct oxr_session *sess)
{
	struct xrt_compositor *xc = sess->compositor;

	if (!is_running(sess->state)) {
		return oxr_error(log, XR_ERROR_SESSION_NOT_RUNNING,
		                 " session is not running");
	}

	if (xc != NULL) {
		if (sess->frame_started) {
			xc->discard_frame(xc);
			sess->frame_started = false;
		}

		xc->end_session(xc);
	}

	oxr_event_push_XrEventDataSessionStateChanged(
	    log, sess, XR_SESSION_STATE_STOPPING, 0);
	oxr_event_push_XrEventDataSessionStateChanged(log, sess,
	                                              XR_SESSION_STATE_IDLE, 0);
	oxr_event_push_XrEventDataSessionStateChanged(
	    log, sess, XR_SESSION_STATE_READY, 0);

	sess->state = XR_SESSION_STATE_READY;

	return XR_SUCCESS;
}

XrResult
oxr_session_get_view_pose_at(struct oxr_logger *log,
                             struct oxr_session *sess,
                             XrTime at_time,
                             struct xrt_pose *pose)
{
	// @todo This function needs to be massively expanded to support all
	//       use cases this drive. The main use of this function is to get
	//       either the predicted position of the headset device. Right now
	//       it only returns the current position. But it must also deal
	//       with past values are allowed by the spec. See displayTime
	//       argument on the xrLocateViews function. It will also drive
	//       the function xrLocateSpace view using the view space.
	// @todo If using orientation tracking only implement a neck model to
	//       get at least a slightly better position.

	struct xrt_device *xdev = sess->sys->device;
	struct xrt_space_relation relation;
	int64_t timestamp;
	xdev->get_tracked_pose(xdev, sess->sys->inst->timekeeping, &timestamp,
	                       &relation);
	if ((relation.relation_flags &
	     XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0) {
		pose->orientation = relation.pose.orientation;
	} else {
		pose->orientation.x = 0;
		pose->orientation.y = 0;
		pose->orientation.z = 0;
		pose->orientation.w = 1;
	}
	if ((relation.relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) !=
	    0) {
		pose->position = relation.pose.position;
	} else {
		// "nominal height" 1.6m
		pose->position.x = 0.0f;
		pose->position.y = 1.60f;
		pose->position.z = 0.0f;
	}

	if ((relation.relation_flags &
	     XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) != 0) {
		//! @todo Forcing a fixed amount of prediction for now since
		//! devices don't tell us timestamps yet.
		int64_t ns_diff = at_time - timestamp;
		float interval;
		if (debug_get_bool_option_dynamic_prediction()) {
			interval =
			    time_ns_to_s(ns_diff) + sess->static_prediction_s;
		} else {
			interval = sess->static_prediction_s;
		}

		struct xrt_quat predicted;
		math_quat_integrate_velocity(&pose->orientation,
		                             &relation.angular_velocity,
		                             interval, &predicted);
		if (debug_get_bool_option_views()) {

			fprintf(stderr,
			        "\toriginal quat = {%f, %f, %f, %f}   "
			        "(time requested: %li, Interval %li nsec, with "
			        "static interval %f s)\n",
			        pose->orientation.x, pose->orientation.y,
			        pose->orientation.z, pose->orientation.w,
			        at_time, ns_diff, interval);
		}
		pose->orientation = predicted;
	}

	return XR_SUCCESS;
}

void
print_view_fov(uint32_t index, const struct xrt_fov *fov)
{
	if (!debug_get_bool_option_views()) {
		return;
	}

	fprintf(stderr, "\tviews[%i].fov = {%f, %f, %f, %f}\n", index,
	        fov->angle_left, fov->angle_right, fov->angle_up,
	        fov->angle_down);
}

void
print_view_pose(uint32_t index, const struct xrt_pose *pose)
{
	if (!debug_get_bool_option_views()) {
		return;
	}

	fprintf(stderr, "\tviews[%i].pose = {{%f, %f, %f, %f}, {%f, %f, %f}}\n",
	        index, pose->orientation.x, pose->orientation.y,
	        pose->orientation.z, pose->orientation.w, pose->position.x,
	        pose->position.y, pose->position.z);
}


XrResult
oxr_session_views(struct oxr_logger *log,
                  struct oxr_session *sess,
                  const XrViewLocateInfo *viewLocateInfo,
                  XrViewState *viewState,
                  uint32_t viewCapacityInput,
                  uint32_t *viewCountOutput,
                  XrView *views)
{
	struct xrt_device *xdev = sess->sys->device;
	struct oxr_space *baseSpc = (struct oxr_space *)viewLocateInfo->space;
	uint32_t num_views = 2;

	// Does this apply for all calls?
	if (!baseSpc->is_reference) {
		viewState->viewStateFlags = 0;
		return XR_SUCCESS;
	}

	// Start two call handling.
	if (viewCountOutput != NULL) {
		*viewCountOutput = num_views;
	}
	if (viewCapacityInput == 0) {
		return XR_SUCCESS;
	}
	if (viewCapacityInput < num_views) {
		return oxr_error(log, XR_ERROR_SIZE_INSUFFICIENT,
		                 "(viewCapacityInput == %u) need %u",
		                 viewCapacityInput, num_views);
	}
	// End two call handling.

	if (debug_get_bool_option_views()) {
		fprintf(stderr, "%s\n", __func__);
		fprintf(stderr, "\tviewLocateInfo->displayTime %lu\n",
		        viewLocateInfo->displayTime);
	}

	// Get the viewLocateInfo->space to view space relation.
	struct xrt_space_relation pure_relation;
	oxr_space_ref_relation(log, sess, XR_REFERENCE_SPACE_TYPE_VIEW,
	                       baseSpc->type, viewLocateInfo->displayTime,
	                       &pure_relation);

	struct xrt_pose pure = pure_relation.pose;

	// @todo the fov information that we get from xdev->views[i].fov is not
	//       properly filled out in oh_device.c, fix before wasting time on
	//       debugging weird rendering when adding stuff here.

	for (uint32_t i = 0; i < num_views; i++) {
		//! @todo Do not hardcode IPD.
		struct xrt_vec3 eye_relation = {
		    sess->ipd_meters,
		    0.0f,
		    0.0f,
		};
		struct xrt_pose view_pose;

		// Get the per view pose from the device.
		xdev->get_view_pose(xdev, &eye_relation, i, &view_pose);

		// Do the magical space relation dance here.
		math_pose_openxr_locate(&view_pose, &pure, &baseSpc->pose,
		                        (struct xrt_pose *)&views[i].pose);

		// Copy the fov information directly from the device.
		views[i].fov = *(XrFovf *)&xdev->views[i].fov;

		print_view_fov(i, (struct xrt_fov *)&views[i].fov);
		print_view_pose(i, (struct xrt_pose *)&views[i].pose);
	}

	// @todo Add tracking bit once we have them.
	viewState->viewStateFlags = 0;
	viewState->viewStateFlags |= XR_VIEW_STATE_POSITION_VALID_BIT;
	viewState->viewStateFlags |= XR_VIEW_STATE_ORIENTATION_VALID_BIT;

	return XR_SUCCESS;
}

XrResult
oxr_session_frame_wait(struct oxr_logger *log,
                       struct oxr_session *sess,
                       XrFrameState *frameState)
{
	if (!is_running(sess->state)) {
		return oxr_error(log, XR_ERROR_SESSION_NOT_RUNNING,
		                 " session is not running");
	}

	//! @todo this should be carefully synchronized, because there may be
	//! more than one session per instance.
	XRT_MAYBE_UNUSED timepoint_ns now =
	    time_state_get_now_and_update(sess->sys->inst->timekeeping);


	struct xrt_compositor *xc = sess->compositor;
	xc->wait_frame(xc, &frameState->predictedDisplayTime,
	               &frameState->predictedDisplayPeriod);

	return XR_SUCCESS;
}

XrResult
oxr_session_frame_begin(struct oxr_logger *log, struct oxr_session *sess)
{
	if (!is_running(sess->state)) {
		return oxr_error(log, XR_ERROR_SESSION_NOT_RUNNING,
		                 " session is not running");
	}

	struct xrt_compositor *xc = sess->compositor;

	XrResult ret;
	if (sess->frame_started) {
		ret = XR_FRAME_DISCARDED;
		if (xc != NULL) {
			xc->discard_frame(xc);
		}
	} else {
		ret = XR_SUCCESS;
		sess->frame_started = true;
	}
	if (xc != NULL) {
		xc->begin_frame(xc);
	}

	return ret;
}

static enum xrt_blend_mode
oxr_blend_mode_to_xrt(XrEnvironmentBlendMode blend_mode)
{
	// clang-format off
	switch (blend_mode) {
	case XR_ENVIRONMENT_BLEND_MODE_OPAQUE: return XRT_BLEND_MODE_OPAQUE;
	case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return XRT_BLEND_MODE_ADDITIVE;
	case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return XRT_BLEND_MODE_ALPHA_BLEND;
	default: return (enum xrt_blend_mode)0;
	}
	// clang-format on
}

XrResult
oxr_session_frame_end(struct oxr_logger *log,
                      struct oxr_session *sess,
                      const XrFrameEndInfo *frameEndInfo)
{
	/*
	 * Session state and call order.
	 */

	if (!is_running(sess->state)) {
		return oxr_error(log, XR_ERROR_SESSION_NOT_RUNNING,
		                 " session is not running");
	}
	if (!sess->frame_started) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID,
		                 " frame not begun with xrBeginFrame");
	}

	struct xrt_compositor *xc = sess->compositor;

	/*
	 * early out for headless sessions.
	 */
	if (xc == NULL) {
		sess->frame_started = false;

		return XR_SUCCESS;
	}

	/*
	 * Early out for discarded frame if layer count is 0,
	 * since then blend mode, etc. doesn't matter.
	 */
	if (frameEndInfo->layerCount == 0) {
		xc->discard_frame(xc);
		sess->frame_started = false;

		return XR_SUCCESS;
	}

	/*
	 * Blend mode.
	 */

	enum xrt_blend_mode blend_mode =
	    oxr_blend_mode_to_xrt(frameEndInfo->environmentBlendMode);

	if (blend_mode == 0) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->environmentBlendMode) "
		                 "unknown environment blend mode");
	}

	if ((blend_mode & sess->sys->device->blend_mode) == 0) {
		return oxr_error(log,
		                 XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED,
		                 "(frameEndInfo->environmentBlendMode) "
		                 "is not supported");
	}


	/*
	 * Layers.
	 */

	if (frameEndInfo->layers == NULL) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers == NULL)");
	}
	if (frameEndInfo->layers[0]->type !=
	    XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[0]->type)");
	}

	XrCompositionLayerProjection *proj =
	    (XrCompositionLayerProjection *)frameEndInfo->layers[0];

	if (proj->viewCount != 2) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[0]->viewCount == %u)"
		                 " must be 2",
		                 proj->viewCount);
	}


	/*
	 * Doing the real work.
	 */

	struct xrt_swapchain *chains[2];
	uint32_t image_index[2];
	uint32_t layers[2];
	uint32_t num_chains = ARRAY_SIZE(chains);

	for (uint32_t i = 0; i < num_chains; i++) {
		//! @todo Validate this above.
		struct oxr_swapchain *sc =
		    (struct oxr_swapchain *)proj->views[i].subImage.swapchain;
		chains[i] = sc->swapchain;
		layers[i] = proj->views[i].subImage.imageArrayIndex;
		image_index[i] = sc->released_index;
	}

	xc->end_frame(xc, blend_mode, chains, image_index, layers, num_chains);

	sess->frame_started = false;

	return XR_SUCCESS;
}

static XrResult
oxr_session_destroy(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	struct oxr_session *sess = (struct oxr_session *)hb;
	if (sess->compositor != NULL) {
		sess->compositor->destroy(sess->compositor);
	}
	free(sess);

	return XR_SUCCESS;
}

#define OXR_SESSION_ALLOCATE(LOG, SYS, OUT)                                    \
	do {                                                                   \
		OXR_ALLOCATE_HANDLE_OR_RETURN(LOG, OUT, OXR_XR_DEBUG_SESSION,  \
		                              oxr_session_destroy,             \
		                              &(SYS)->inst->handle);           \
		(OUT)->sys = (SYS);                                            \
	} while (0)

XrResult
oxr_session_create(struct oxr_logger *log,
                   struct oxr_system *sys,
                   XrStructureType *next,
                   struct oxr_session **out_session)
{
	struct oxr_session *sess;
	XrResult ret;

	if (sys->inst->headless && next == NULL) {
		OXR_SESSION_ALLOCATE(log, sys, sess);
		ret = XR_SUCCESS;
		sess->compositor = NULL;
		sess->create_swapchain = NULL;
	} else
#ifdef XR_USE_PLATFORM_XLIB
	    if (*next == XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR) {
		OXR_SESSION_ALLOCATE(log, sys, sess);
		ret = oxr_session_populate_gl_xlib(
		    log, sys, (XrGraphicsBindingOpenGLXlibKHR *)next, sess);
	} else
#endif
#ifdef XR_USE_GRAPHICS_API_VULKAN
	    if (*next == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR) {
		OXR_SESSION_ALLOCATE(log, sys, sess);
		ret = oxr_session_populate_vk(
		    log, sys, (XrGraphicsBindingVulkanKHR *)next, sess);
	} else
#endif
	{
		ret = oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                "(createInfo->next->type)");
	}

	if (ret != XR_SUCCESS) {
		/* clean up allocation first */
		XrResult cleanup_result =
		    oxr_handle_destroy(log, &sess->handle);
		assert(cleanup_result == XR_SUCCESS);
		return ret;
	}

	sess->ipd_meters = debug_get_num_option_ipd() / 1000.0f;
	sess->static_prediction_s =
	    debug_get_num_option_prediction_ms() / 1000.0f;

	oxr_event_push_XrEventDataSessionStateChanged(log, sess,
	                                              XR_SESSION_STATE_IDLE, 0);
	oxr_event_push_XrEventDataSessionStateChanged(
	    log, sess, XR_SESSION_STATE_READY, 0);
	sess->state = XR_SESSION_STATE_READY;

	*out_session = sess;

	return ret;
}
