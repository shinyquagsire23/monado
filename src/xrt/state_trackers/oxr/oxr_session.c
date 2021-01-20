// Copyright 2018-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds session related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "xrt/xrt_device.h"
#include "xrt/xrt_config_build.h"
#include "xrt/xrt_config_have.h"

#ifdef XR_USE_PLATFORM_XLIB
#include "xrt/xrt_gfx_xlib.h"
#endif // XR_USE_PLATFORM_XLIB

#ifdef XRT_HAVE_VULKAN
#include "xrt/xrt_gfx_vk.h"
#endif // XRT_HAVE_VULKAN

#include "os/os_time.h"

#include "util/u_debug.h"
#include "util/u_misc.h"
#include "util/u_time.h"

#include "math/m_api.h"
#include "math/m_mathinclude.h"
#include "math/m_space.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_handle.h"
#include "oxr_chain.h"
#include "oxr_api_verify.h"
#include "oxr_chain.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>


DEBUG_GET_ONCE_NUM_OPTION(ipd, "OXR_DEBUG_IPD_MM", 63)
DEBUG_GET_ONCE_BOOL_OPTION(frame_timing_spew, "OXR_FRAME_TIMING_SPEW", false)

#define CALL_CHK(call)                                                                                                 \
	if ((call) == XRT_ERROR_IPC_FAILURE) {                                                                         \
		return oxr_error(log, XR_ERROR_INSTANCE_LOST, "Error in function call over IPC");                      \
	}

static bool
is_running(struct oxr_session *sess)
{
	return sess->has_begun;
}

static bool
should_render(XrSessionState state)
{
	switch (state) {
	case XR_SESSION_STATE_VISIBLE: return true;
	case XR_SESSION_STATE_FOCUSED: return true;
	case XR_SESSION_STATE_STOPPING: return true;
	default: return false;
	}
}

XRT_MAYBE_UNUSED static const char *
to_string(XrSessionState state)
{
	switch (state) {
	case XR_SESSION_STATE_UNKNOWN: return "XR_SESSION_STATE_UNKNOWN";
	case XR_SESSION_STATE_IDLE: return "XR_SESSION_STATE_IDLE";
	case XR_SESSION_STATE_READY: return "XR_SESSION_STATE_READY";
	case XR_SESSION_STATE_SYNCHRONIZED: return "XR_SESSION_STATE_SYNCHRONIZED";
	case XR_SESSION_STATE_VISIBLE: return "XR_SESSION_STATE_VISIBLE";
	case XR_SESSION_STATE_FOCUSED: return "XR_SESSION_STATE_FOCUSED";
	case XR_SESSION_STATE_STOPPING: return "XR_SESSION_STATE_STOPPING";
	case XR_SESSION_STATE_LOSS_PENDING: return "XR_SESSION_STATE_LOSS_PENDING";
	case XR_SESSION_STATE_EXITING: return "XR_SESSION_STATE_EXITING";
	case XR_SESSION_STATE_MAX_ENUM: return "XR_SESSION_STATE_MAX_ENUM";
	default: return "";
	}
}

static void
oxr_session_change_state(struct oxr_logger *log, struct oxr_session *sess, XrSessionState state)
{
	oxr_event_push_XrEventDataSessionStateChanged(log, sess, state, 0);
	sess->state = state;
}

XrResult
oxr_session_enumerate_formats(struct oxr_logger *log,
                              struct oxr_session *sess,
                              uint32_t formatCapacityInput,
                              uint32_t *formatCountOutput,
                              int64_t *formats)
{
	struct xrt_compositor *xc = sess->compositor;
	if (formatCountOutput == NULL) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE, "(formatCountOutput == NULL) can not be null");
	}
	if (xc == NULL) {
		if (formatCountOutput != NULL) {
			*formatCountOutput = 0;
		}
		return oxr_session_success_result(sess);
	}

	OXR_TWO_CALL_HELPER(log, formatCapacityInput, formatCountOutput, formats, xc->info.num_formats,
	                    xc->info.formats, oxr_session_success_result(sess));
}

XrResult
oxr_session_begin(struct oxr_logger *log, struct oxr_session *sess, const XrSessionBeginInfo *beginInfo)
{
	if (is_running(sess)) {
		return oxr_error(log, XR_ERROR_SESSION_RUNNING, "Session is already running");
	}

	struct xrt_compositor *xc = sess->compositor;
	if (xc != NULL) {
		XrViewConfigurationType view_type = beginInfo->primaryViewConfigurationType;

		if (view_type != sess->sys->view_config_type) {
			/*! @todo we only support a single view config type per
			 * system right now */
			return oxr_error(log, XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED,
			                 "(beginInfo->primaryViewConfigurationType == "
			                 "0x%08x) view configuration type not supported",
			                 view_type);
		}

		CALL_CHK(xrt_comp_begin_session(xc, (enum xrt_view_type)beginInfo->primaryViewConfigurationType));
	}

	sess->has_begun = true;

	return oxr_session_success_result(sess);
}

XrResult
oxr_session_end(struct oxr_logger *log, struct oxr_session *sess)
{
	struct xrt_compositor *xc = sess->compositor;

	if (!is_running(sess)) {
		return oxr_error(log, XR_ERROR_SESSION_NOT_RUNNING, "Session is not running");
	}
	if (sess->state != XR_SESSION_STATE_STOPPING) {
		return oxr_error(log, XR_ERROR_SESSION_NOT_STOPPING, "Session is not stopping");
	}

	if (xc != NULL) {
		if (sess->frame_id.waited > 0) {
			xrt_comp_discard_frame(xc, sess->frame_id.waited);
			sess->frame_id.waited = -1;
		}
		if (sess->frame_id.begun > 0) {
			xrt_comp_discard_frame(xc, sess->frame_id.begun);
			sess->frame_id.begun = -1;
		}
		sess->frame_started = false;

		CALL_CHK(xrt_comp_end_session(xc));
	}

	oxr_session_change_state(log, sess, XR_SESSION_STATE_IDLE);
	if (sess->exiting) {
		oxr_session_change_state(log, sess, XR_SESSION_STATE_EXITING);
	}

	oxr_session_change_state(log, sess, XR_SESSION_STATE_READY);

	sess->has_begun = false;

	return oxr_session_success_result(sess);
}

XrResult
oxr_session_request_exit(struct oxr_logger *log, struct oxr_session *sess)
{
	if (!is_running(sess)) {
		return oxr_error(log, XR_ERROR_SESSION_NOT_RUNNING, "Session is not running");
	}

	if (sess->state == XR_SESSION_STATE_FOCUSED) {
		oxr_session_change_state(log, sess, XR_SESSION_STATE_VISIBLE);
	}
	if (sess->state == XR_SESSION_STATE_VISIBLE) {
		oxr_session_change_state(log, sess, XR_SESSION_STATE_SYNCHRONIZED);
	}
	if (!sess->has_ended_once) {
		oxr_session_change_state(log, sess, XR_SESSION_STATE_SYNCHRONIZED);
		// Fake the synchronization.
		sess->has_ended_once = true;
	}

	//! @todo start fading out the app.
	oxr_session_change_state(log, sess, XR_SESSION_STATE_STOPPING);
	sess->exiting = true;
	return oxr_session_success_result(sess);
}

void
oxr_session_poll(struct oxr_logger *log, struct oxr_session *sess)
{
	struct xrt_compositor *xc = sess->compositor;
	if (xc == NULL) {
		return;
	}

	bool read_more_events = true;
	while (read_more_events) {
		union xrt_compositor_event xce = {0};
		xc->poll_events(xc, &xce);

		// dispatch based on event type
		switch (xce.type) {
		case XRT_COMPOSITOR_EVENT_NONE:
			// No more events.
			read_more_events = false;
			break;
		case XRT_COMPOSITOR_EVENT_STATE_CHANGE:
			sess->compositor_visible = xce.state.visible;
			sess->compositor_focused = xce.state.focused;
			break;
		case XRT_COMPOSITOR_EVENT_OVERLAY_CHANGE:
			oxr_event_push_XrEventDataMainSessionVisibilityChangedEXTX(log, sess, xce.overlay.visible);
			break;
		default: U_LOG_W("unhandled event type! %d", xce.type); break;
		}
	}

	if (sess->state == XR_SESSION_STATE_SYNCHRONIZED && sess->compositor_visible) {
		oxr_session_change_state(log, sess, XR_SESSION_STATE_VISIBLE);
	}

	if (sess->state == XR_SESSION_STATE_VISIBLE && sess->compositor_focused) {
		oxr_session_change_state(log, sess, XR_SESSION_STATE_FOCUSED);
	}
}

XrResult
oxr_session_get_view_relation_at(struct oxr_logger *log,
                                 struct oxr_session *sess,
                                 XrTime at_time,
                                 struct xrt_space_relation *out_relation)
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

	struct xrt_device *xdev = GET_XDEV_BY_ROLE(sess->sys, head);

	// Applies the offset in the function.
	struct xrt_space_graph xsg = {0};
	oxr_xdev_get_space_graph(log, sess->sys->inst, xdev, XRT_INPUT_GENERIC_HEAD_POSE, at_time, &xsg);
	m_space_graph_resolve(&xsg, out_relation);

	return oxr_session_success_result(sess);
}

void
print_view_fov(struct oxr_session *sess, uint32_t index, const struct xrt_fov *fov)
{
	if (!sess->sys->inst->debug_views) {
		return;
	}

	U_LOG_D("views[%i].fov = {%f, %f, %f, %f}", index, fov->angle_left, fov->angle_right, fov->angle_up,
	        fov->angle_down);
}

void
print_view_pose(struct oxr_session *sess, uint32_t index, const struct xrt_pose *pose)
{
	if (!sess->sys->inst->debug_views) {
		return;
	}

	U_LOG_D("views[%i].pose = {{%f, %f, %f, %f}, {%f, %f, %f}}", index, pose->orientation.x, pose->orientation.y,
	        pose->orientation.z, pose->orientation.w, pose->position.x, pose->position.y, pose->position.z);
}

static inline XrViewStateFlags
xrt_to_view_state_flags(enum xrt_space_relation_flags flags)
{
	XrViewStateFlags res = 0;
	if (flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) {
		res |= XR_VIEW_STATE_ORIENTATION_VALID_BIT;
	}
	if (flags & XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT) {
		res |= XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
	}
	if (flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) {
		res |= XR_VIEW_STATE_POSITION_VALID_BIT;
	}
	if (flags & XRT_SPACE_RELATION_POSITION_TRACKED_BIT) {
		res |= XR_VIEW_STATE_POSITION_TRACKED_BIT;
	}
	return res;
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
	struct xrt_device *xdev = GET_XDEV_BY_ROLE(sess->sys, head);
	struct oxr_space *baseSpc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, viewLocateInfo->space);
	uint32_t num_views = 2;

	// Does this apply for all calls?
	if (!baseSpc->is_reference) {
		viewState->viewStateFlags = 0;
		return oxr_session_success_result(sess);
	}

	// Start two call handling.
	if (viewCountOutput != NULL) {
		*viewCountOutput = num_views;
	}
	if (viewCapacityInput == 0) {
		return oxr_session_success_result(sess);
	}
	if (viewCapacityInput < num_views) {
		return oxr_error(log, XR_ERROR_SIZE_INSUFFICIENT, "(viewCapacityInput == %u) need %u",
		                 viewCapacityInput, num_views);
	}
	// End two call handling.

	if (sess->sys->inst->debug_views) {
		U_LOG_D("viewLocateInfo->displayTime %" PRIu64, viewLocateInfo->displayTime);
	}

	// Get the viewLocateInfo->space to view space relation.
	struct xrt_space_relation pure_relation;
	oxr_space_ref_relation(log, sess, XR_REFERENCE_SPACE_TYPE_VIEW, baseSpc->type, viewLocateInfo->displayTime,
	                       &pure_relation);

	// @todo the fov information that we get from xdev->hmd->views[i].fov is
	//       not properly filled out in oh_device.c, fix before wasting time
	//       on debugging weird rendering when adding stuff here.

	viewState->viewStateFlags = 0;

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
		struct xrt_space_relation result = {0};
		struct xrt_space_graph xsg = {0};
		m_space_graph_add_pose_if_not_identity(&xsg, &view_pose);
		m_space_graph_add_relation(&xsg, &pure_relation);
		m_space_graph_add_pose_if_not_identity(&xsg, &baseSpc->pose);
		m_space_graph_resolve(&xsg, &result);
		union {
			struct xrt_pose xrt;
			struct XrPosef oxr;
		} safe_copy_pose = {0};
		safe_copy_pose.xrt = result.pose;
		views[i].pose = safe_copy_pose.oxr;

		// Copy the fov information directly from the device.
		union {
			struct xrt_fov xrt;
			XrFovf oxr;
		} safe_copy_fov = {0};
		safe_copy_fov.xrt = xdev->hmd->views[i].fov;
		views[i].fov = safe_copy_fov.oxr;

		struct xrt_pose *pose = (struct xrt_pose *)&views[i].pose;
		if ((result.relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0 &&
		    !math_quat_ensure_normalized(&pose->orientation)) {
			struct xrt_quat *q = &pose->orientation;
			struct xrt_quat norm = *q;
			math_quat_normalize(&norm);
			return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
			                 "Quaternion %a %a %a %a (normalized %a %a %a %a) "
			                 "in xrLocateViews was invalid",
			                 q->x, q->y, q->z, q->w, norm.x, norm.y, norm.z, norm.w);
		}

		print_view_fov(sess, i, (struct xrt_fov *)&views[i].fov);
		print_view_pose(sess, i, (struct xrt_pose *)&views[i].pose);

		if (i == 0) {
			viewState->viewStateFlags = xrt_to_view_state_flags(result.relation_flags);
		} else {
			viewState->viewStateFlags &= xrt_to_view_state_flags(result.relation_flags);
		}
	}

	return oxr_session_success_result(sess);
}

static double
ns_to_ms(int64_t ns)
{
	double ms = ((double)ns) * 1. / 1000. * 1. / 1000.;
	return ms;
}

static double
ts_ms(struct oxr_session *sess)
{
	timepoint_ns now = time_state_get_now(sess->sys->inst->timekeeping);
	int64_t monotonic = time_state_ts_to_monotonic_ns(sess->sys->inst->timekeeping, now);
	return ns_to_ms(monotonic);
}

XrResult
oxr_session_frame_wait(struct oxr_logger *log, struct oxr_session *sess, XrFrameState *frameState)
{
	if (!is_running(sess)) {
		return oxr_error(log, XR_ERROR_SESSION_NOT_RUNNING, "Session is not running");
	}


	//! @todo this should be carefully synchronized, because there may be
	//! more than one session per instance.
	XRT_MAYBE_UNUSED timepoint_ns now = time_state_get_now_and_update(sess->sys->inst->timekeeping);

	struct xrt_compositor *xc = sess->compositor;
	if (xc == NULL) {
		frameState->shouldRender = XR_FALSE;
		return oxr_session_success_result(sess);
	}

	os_mutex_lock(&sess->active_wait_frames_lock);
	sess->active_wait_frames++;
	os_mutex_unlock(&sess->active_wait_frames_lock);

	if (sess->frame_timing_spew) {
		oxr_log(log, "Called at %8.3fms", ts_ms(sess));
	}

	// A subsequent xrWaitFrame call must: block until the previous frame
	// has been begun
	os_semaphore_wait(&sess->sem, 0);

	if (sess->frame_timing_spew) {
		oxr_log(log, "Finished waiting for previous frame begin at %8.3fms", ts_ms(sess));
	}

	uint64_t predicted_display_time;
	uint64_t predicted_display_period;
	CALL_CHK(xrt_comp_wait_frame(xc, &sess->frame_id.waited, &predicted_display_time, &predicted_display_period));

	if ((int64_t)predicted_display_time <= 0) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Got a negative display time '%" PRIi64 "'",
		                 (int64_t)predicted_display_time);
	}

	frameState->shouldRender = should_render(sess->state);
	frameState->predictedDisplayPeriod = predicted_display_period;
	frameState->predictedDisplayTime =
	    time_state_monotonic_to_ts_ns(sess->sys->inst->timekeeping, predicted_display_time);

	if (frameState->predictedDisplayTime <= 0) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Time_state_monotonic_to_ts_ns returned '%" PRIi64 "'",
		                 frameState->predictedDisplayTime);
	}

	if (sess->frame_timing_spew) {
		oxr_log(log,
		        "Waiting finished at %8.3fms. Predicted display time "
		        "%8.3fms, "
		        "period %8.3fms",
		        ts_ms(sess), ns_to_ms(predicted_display_time), ns_to_ms(predicted_display_period));
	}

	return oxr_session_success_result(sess);
}

XrResult
oxr_session_frame_begin(struct oxr_logger *log, struct oxr_session *sess)
{
	if (!is_running(sess)) {
		return oxr_error(log, XR_ERROR_SESSION_NOT_RUNNING, "Session is not running");
	}

	struct xrt_compositor *xc = sess->compositor;

	os_mutex_lock(&sess->active_wait_frames_lock);
	int active_wait_frames = sess->active_wait_frames;
	os_mutex_unlock(&sess->active_wait_frames_lock);

	XrResult ret;
	if (active_wait_frames == 0) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID, "xrBeginFrame without xrWaitFrame");
	}

	if (sess->frame_started) {
		// max 2 xrWaitFrame can be in flight so a second xrBeginFrame
		// is only valid if we have a second xrWaitFrame in flight
		if (active_wait_frames != 2) {
			return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID, "xrBeginFrame without xrWaitFrame");
		}


		ret = XR_FRAME_DISCARDED;
		if (xc != NULL) {
			CALL_CHK(xrt_comp_discard_frame(xc, sess->frame_id.begun));
			sess->frame_id.begun = -1;

			os_mutex_lock(&sess->active_wait_frames_lock);
			sess->active_wait_frames--;
			os_mutex_unlock(&sess->active_wait_frames_lock);
		}
	} else {
		ret = oxr_session_success_result(sess);
		sess->frame_started = true;
	}
	if (xc != NULL) {
		CALL_CHK(xrt_comp_begin_frame(xc, sess->frame_id.waited));
		sess->frame_id.begun = sess->frame_id.waited;
		sess->frame_id.waited = -1;
	}

	os_semaphore_release(&sess->sem);

	return ret;
}

static enum xrt_blend_mode
oxr_blend_mode_to_xrt(XrEnvironmentBlendMode blend_mode)
{
	switch (blend_mode) {
	case XR_ENVIRONMENT_BLEND_MODE_OPAQUE: return XRT_BLEND_MODE_OPAQUE;
	case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return XRT_BLEND_MODE_ADDITIVE;
	case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return XRT_BLEND_MODE_ALPHA_BLEND;
	default: return (enum xrt_blend_mode)0;
	}
}

static XrResult
verify_space(struct oxr_logger *log, uint32_t layer_index, XrSpace space)
{
	if (space == XR_NULL_HANDLE) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->space == "
		                 "XR_NULL_HANDLE) XrSpace must not be XR_NULL_HANDLE",
		                 layer_index);
	}

	return XR_SUCCESS;
}

static XrResult
is_rect_neg(const XrRect2Di *imageRect)
{
	if (imageRect->offset.x < 0 || imageRect->offset.y < 0) {
		return true;
	}

	return false;
}

static XrResult
is_rect_out_of_bounds(const XrRect2Di *imageRect, struct oxr_swapchain *sc)
{
	uint32_t total_width = imageRect->offset.x + imageRect->extent.width;
	if (total_width > sc->width) {
		return true;
	}
	uint32_t total_height = imageRect->offset.y + imageRect->extent.height;
	if (total_height > sc->height) {
		return true;
	}

	return false;
}

static XrResult
verify_quad_layer(struct xrt_compositor *xc,
                  struct oxr_logger *log,
                  uint32_t layer_index,
                  XrCompositionLayerQuad *quad,
                  struct xrt_device *head,
                  uint64_t timestamp)
{
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, quad->subImage.swapchain);

	if (sc == NULL) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage."
		                 "swapchain) swapchain is NULL!",
		                 layer_index);
	}

	XrResult ret = verify_space(log, layer_index, quad->space);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (!math_quat_validate_within_1_percent((struct xrt_quat *)&quad->pose.orientation)) {
		XrQuaternionf *q = &quad->pose.orientation;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.orientation "
		                 "== {%f %f %f %f}) is not a valid quat",
		                 layer_index, q->x, q->y, q->z, q->w);
	}

	if (!math_vec3_validate((struct xrt_vec3 *)&quad->pose.position)) {
		XrVector3f *p = &quad->pose.position;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.position "
		                 "== {%f %f %f}) is not valid",
		                 layer_index, p->x, p->y, p->z);
	}

	if (sc->num_array_layers <= quad->subImage.imageArrayIndex) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.imageArrayIndex == "
		                 "%u) Invalid swapchain array "
		                 "index for quad layer (%u).",
		                 layer_index, quad->subImage.imageArrayIndex, sc->num_array_layers);
	}

	if (!sc->released.yes) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage."
		                 "swapchain) swapchain has not been released!",
		                 layer_index);
	}

	if (sc->released.index >= (int)sc->swapchain->num_images) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) internal "
		                 "image index out of bounds",
		                 layer_index);
	}

	if (is_rect_neg(&quad->subImage.imageRect)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.imageRect.offset == "
		                 "{%i, %i}) has negative component(s)",
		                 layer_index, quad->subImage.imageRect.offset.x, quad->subImage.imageRect.offset.y);
	}

	if (is_rect_out_of_bounds(&quad->subImage.imageRect, sc)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.imageRect == {{%i, "
		                 "%i}, {%u, %u}}) imageRect out of image bounds (%u, %u)",
		                 layer_index, quad->subImage.imageRect.offset.x, quad->subImage.imageRect.offset.y,
		                 quad->subImage.imageRect.extent.width, quad->subImage.imageRect.extent.height,
		                 sc->width, sc->height);
	}

	return XR_SUCCESS;
}

static XrResult
verify_depth_layer(struct xrt_compositor *xc,
                   struct oxr_logger *log,
                   uint32_t layer_index,
                   uint32_t i,
                   const XrCompositionLayerDepthInfoKHR *depth)
{
	if (depth->subImage.swapchain == XR_NULL_HANDLE) {
		return oxr_error(log, XR_ERROR_HANDLE_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<"
		                 "XrCompositionLayerDepthInfoKHR>.subImage."
		                 "swapchain) is XR_NULL_HANDLE",
		                 layer_index, i);
	}

	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, depth->subImage.swapchain);

	if (!sc->released.yes) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<"
		                 "XrCompositionLayerDepthInfoKHR>.subImage."
		                 "swapchain) swapchain has not been released",
		                 layer_index, i);
	}

	if (sc->released.index >= (int)sc->swapchain->num_images) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<"
		                 "XrCompositionLayerDepthInfoKHR>.subImage."
		                 "swapchain) internal image index out of bounds",
		                 layer_index, i);
	}

	if (sc->num_array_layers <= depth->subImage.imageArrayIndex) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<"
		                 "XrCompositionLayerDepthInfoKHR>.subImage."
		                 "imageArrayIndex == %u) Invalid swapchain array "
		                 "index for projection layer (%u).",
		                 layer_index, i, depth->subImage.imageArrayIndex, sc->num_array_layers);
	}

	if (is_rect_neg(&depth->subImage.imageRect)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<"
		                 "XrCompositionLayerDepthInfoKHR>.subImage."
		                 "imageRect.offset == {%i, "
		                 "%i}) has negative component(s)",
		                 layer_index, i, depth->subImage.imageRect.offset.x,
		                 depth->subImage.imageRect.offset.y);
	}

	if (is_rect_out_of_bounds(&depth->subImage.imageRect, sc)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<"
		                 "XrCompositionLayerDepthInfoKHR>.subImage."
		                 "imageRect == {{%i, %i}, {%u, %u}}) imageRect out "
		                 "of image bounds (%u, %u)",
		                 layer_index, i, depth->subImage.imageRect.offset.x, depth->subImage.imageRect.offset.y,
		                 depth->subImage.imageRect.extent.width, depth->subImage.imageRect.extent.height,
		                 sc->width, sc->height);
	}

	if (depth->minDepth < 0.0f || depth->minDepth > 1.0f) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<"
		                 "XrCompositionLayerDepthInfoKHR>.minDepth) %f "
		                 "must be in [0.0,1.0]",
		                 layer_index, i, depth->minDepth);
	}

	if (depth->maxDepth < 0.0f || depth->maxDepth > 1.0f) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<"
		                 "XrCompositionLayerDepthInfoKHR>.maxDepth) %f "
		                 "must be in [0.0,1.0]",
		                 layer_index, i, depth->maxDepth);
	}

	if (depth->minDepth > depth->maxDepth) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<"
		                 "XrCompositionLayerDepthInfoKHR>.minDepth) %f "
		                 "must be <= maxDepth %f ",
		                 layer_index, i, depth->minDepth, depth->maxDepth);
	}

	if (depth->nearZ == depth->farZ) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<"
		                 "XrCompositionLayerDepthInfoKHR>.nearZ) %f "
		                 "must be != farZ %f ",
		                 layer_index, i, depth->nearZ, depth->farZ);
	}


	return XR_SUCCESS;
}

static XrResult
verify_projection_layer(struct xrt_compositor *xc,
                        struct oxr_logger *log,
                        uint32_t layer_index,
                        XrCompositionLayerProjection *proj,
                        struct xrt_device *head,
                        uint64_t timestamp)
{
	XrResult ret = verify_space(log, layer_index, proj->space);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (proj->viewCount != 2) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->viewCount == %u) must be 2 for "
		                 "projection layers and the current view configuration",
		                 layer_index, proj->viewCount);
	}

	// number of depth layers must be 0 or proj->viewCount
	uint32_t num_depth_layers = 0;

	// Check for valid swapchain states.
	for (uint32_t i = 0; i < proj->viewCount; i++) {
		const XrCompositionLayerProjectionView *view = &proj->views[i];

		//! @todo More validation?
		if (!math_quat_validate_within_1_percent((struct xrt_quat *)&view->pose.orientation)) {
			const XrQuaternionf *q = &view->pose.orientation;
			return oxr_error(log, XR_ERROR_POSE_INVALID,
			                 "(frameEndInfo->layers[%u]->views[%i]->pose."
			                 "orientation == {%f %f %f %f}) is not a valid quat",
			                 layer_index, i, q->x, q->y, q->z, q->w);
		}

		if (!math_vec3_validate((struct xrt_vec3 *)&view->pose.position)) {
			const XrVector3f *p = &view->pose.position;
			return oxr_error(log, XR_ERROR_POSE_INVALID,
			                 "(frameEndInfo->layers[%u]->views[%i]->pose."
			                 "position == {%f %f %f}) is not valid",
			                 layer_index, i, p->x, p->y, p->z);
		}

		if (view->subImage.swapchain == XR_NULL_HANDLE) {
			return oxr_error(log, XR_ERROR_HANDLE_INVALID,
			                 "(frameEndInfo->layers[%u]->views[%i]->subImage."
			                 "swapchain is XR_NULL_HANDLE",
			                 layer_index, i);
		}

		struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, view->subImage.swapchain);

		if (!sc->released.yes) {
			return oxr_error(log, XR_ERROR_LAYER_INVALID,
			                 "(frameEndInfo->layers[%u]->views[%i].subImage."
			                 "swapchain) swapchain has not been released",
			                 layer_index, i);
		}

		if (sc->released.index >= (int)sc->swapchain->num_images) {
			return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
			                 "(frameEndInfo->layers[%u]->views[%i].subImage."
			                 "swapchain) internal image index out of bounds",
			                 layer_index, i);
		}

		if (sc->num_array_layers <= view->subImage.imageArrayIndex) {
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
			                 "(frameEndInfo->layers[%u]->views[%i]->subImage."
			                 "imageArrayIndex == %u) Invalid swapchain array "
			                 "index for projection layer (%u).",
			                 layer_index, i, view->subImage.imageArrayIndex, sc->num_array_layers);
		}

		if (is_rect_neg(&view->subImage.imageRect)) {
			return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
			                 "(frameEndInfo->layers[%u]->views[%i]-"
			                 ">subImage.imageRect.offset == {%i, "
			                 "%i}) has negative component(s)",
			                 layer_index, i, view->subImage.imageRect.offset.x,
			                 view->subImage.imageRect.offset.y);
		}

		if (is_rect_out_of_bounds(&view->subImage.imageRect, sc)) {
			return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
			                 "(frameEndInfo->layers[%u]->views[%i]->subImage."
			                 "imageRect == {{%i, %i}, {%u, %u}}) imageRect out "
			                 "of image bounds (%u, %u)",
			                 layer_index, i, view->subImage.imageRect.offset.x,
			                 view->subImage.imageRect.offset.y, view->subImage.imageRect.extent.width,
			                 view->subImage.imageRect.extent.height, sc->width, sc->height);
		}

#ifdef XRT_FEATURE_OPENXR_LAYER_DEPTH
		const XrCompositionLayerDepthInfoKHR *depth_info = OXR_GET_INPUT_FROM_CHAIN(
		    view, XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR, XrCompositionLayerDepthInfoKHR);

		if (depth_info) {
			ret = verify_depth_layer(xc, log, layer_index, i, depth_info);
			if (ret != XR_SUCCESS) {
				return ret;
			}
			num_depth_layers++;
		}
#endif // XRT_FEATURE_OPENXR_LAYER_DEPTH
	}

#ifdef XRT_FEATURE_OPENXR_LAYER_DEPTH
	if (num_depth_layers > 0 && num_depth_layers != proj->viewCount) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u] projection layer must have %u "
		                 "depth layers or none, but has: %u)",
		                 layer_index, proj->viewCount, num_depth_layers);
	}
#endif // XRT_FEATURE_OPENXR_LAYER_DEPTH

	return XR_SUCCESS;
}

static XrResult
verify_cube_layer(struct xrt_compositor *xc,
                  struct oxr_logger *log,
                  uint32_t layer_index,
                  const XrCompositionLayerCubeKHR *cube,
                  struct xrt_device *head,
                  uint64_t timestamp)
{
#ifndef XRT_FEATURE_OPENXR_LAYER_CUBE
	return oxr_error(log, XR_ERROR_LAYER_INVALID,
	                 "(frameEndInfo->layers[%u]->type) layer type "
	                 "XrCompositionLayerCubeKHR not supported",
	                 layer_index);
#else
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, cube->swapchain);

	if (sc == NULL) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage."
		                 "swapchain) swapchain is NULL!",
		                 layer_index);
	}

	XrResult ret = verify_space(log, layer_index, cube->space);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (!math_quat_validate_within_1_percent((struct xrt_quat *)&cube->orientation)) {
		const XrQuaternionf *q = &cube->orientation;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.orientation "
		                 "== {%f %f %f %f}) is not a valid quat",
		                 layer_index, q->x, q->y, q->z, q->w);
	}

	if (sc->num_array_layers <= cube->imageArrayIndex) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->imageArrayIndex == %u) Invalid "
		                 "swapchain array index for cube layer (%u).",
		                 layer_index, cube->imageArrayIndex, sc->num_array_layers);
	}

	if (!sc->released.yes) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->swapchain) "
		                 "swapchain has not been released!",
		                 layer_index);
	}

	if (sc->released.index >= (int)sc->swapchain->num_images) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) internal "
		                 "image index out of bounds",
		                 layer_index);
	}

	return XR_SUCCESS;
#endif
}

static XrResult
verify_cylinder_layer(struct xrt_compositor *xc,
                      struct oxr_logger *log,
                      uint32_t layer_index,
                      const XrCompositionLayerCylinderKHR *cylinder,
                      struct xrt_device *head,
                      uint64_t timestamp)
{
#ifndef XRT_FEATURE_OPENXR_LAYER_CYLINDER
	return oxr_error(log, XR_ERROR_LAYER_INVALID,
	                 "(frameEndInfo->layers[%u]->type) layer type "
	                 "XrCompositionLayerCylinderKHR not supported",
	                 layer_index);
#else
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, cylinder->subImage.swapchain);

	if (sc == NULL) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage."
		                 "swapchain) swapchain is NULL!",
		                 layer_index);
	}

	XrResult ret = verify_space(log, layer_index, cylinder->space);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (!math_quat_validate_within_1_percent((struct xrt_quat *)&cylinder->pose.orientation)) {
		const XrQuaternionf *q = &cylinder->pose.orientation;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.orientation "
		                 "== {%f %f %f %f}) is not a valid quat",
		                 layer_index, q->x, q->y, q->z, q->w);
	}

	if (!math_vec3_validate((struct xrt_vec3 *)&cylinder->pose.position)) {
		const XrVector3f *p = &cylinder->pose.position;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.position == "
		                 "{%f %f %f}) is not valid",
		                 layer_index, p->x, p->y, p->z);
	}

	if (sc->num_array_layers <= cylinder->subImage.imageArrayIndex) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage."
		                 "imageArrayIndex == %u) Invalid swapchain "
		                 "array index for cylinder layer (%u).",
		                 layer_index, cylinder->subImage.imageArrayIndex, sc->num_array_layers);
	}

	if (!sc->released.yes) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage."
		                 "swapchain) swapchain has not been released!",
		                 layer_index);
	}

	if (sc->released.index >= (int)sc->swapchain->num_images) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) internal "
		                 "image index out of bounds",
		                 layer_index);
	}

	if (is_rect_neg(&cylinder->subImage.imageRect)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.imageRect.offset == "
		                 "{%i, %i}) has negative component(s)",
		                 layer_index, cylinder->subImage.imageRect.offset.x,
		                 cylinder->subImage.imageRect.offset.y);
	}

	if (is_rect_out_of_bounds(&cylinder->subImage.imageRect, sc)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.imageRect == {{%i, "
		                 "%i}, {%u, %u}}) imageRect out of image bounds (%u, %u)",
		                 layer_index, cylinder->subImage.imageRect.offset.x,
		                 cylinder->subImage.imageRect.offset.y, cylinder->subImage.imageRect.extent.width,
		                 cylinder->subImage.imageRect.extent.height, sc->width, sc->height);
	}

	if (cylinder->radius < 0.f) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->radius == %f) "
		                 "radius can not be negative",
		                 layer_index, cylinder->radius);
	}

	if (cylinder->centralAngle < 0.f || cylinder->centralAngle > (M_PI * 2)) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->centralAngle == "
		                 "%f) centralAngle out of bounds",
		                 layer_index, cylinder->centralAngle);
	}

	if (cylinder->aspectRatio <= 0.f) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->aspectRatio == "
		                 "%f) aspectRatio out of bounds",
		                 layer_index, cylinder->aspectRatio);
	}

	return XR_SUCCESS;
#endif
}

static XrResult
verify_equirect1_layer(struct xrt_compositor *xc,
                       struct oxr_logger *log,
                       uint32_t layer_index,
                       const XrCompositionLayerEquirectKHR *equirect,
                       struct xrt_device *head,
                       uint64_t timestamp)
{
#ifndef XRT_FEATURE_OPENXR_LAYER_EQUIRECT1
	return oxr_error(log, XR_ERROR_LAYER_INVALID,
	                 "(frameEndInfo->layers[%u]->type) layer type "
	                 "XrCompositionLayerEquirectKHR not supported",
	                 layer_index);
#else
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, equirect->subImage.swapchain);

	if (sc == NULL) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage."
		                 "swapchain) swapchain is NULL!",
		                 layer_index);
	}

	XrResult ret = verify_space(log, layer_index, equirect->space);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (!math_quat_validate_within_1_percent((struct xrt_quat *)&equirect->pose.orientation)) {
		const XrQuaternionf *q = &equirect->pose.orientation;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.orientation "
		                 "== {%f %f %f %f}) is not a valid quat",
		                 layer_index, q->x, q->y, q->z, q->w);
	}

	if (!math_vec3_validate((struct xrt_vec3 *)&equirect->pose.position)) {
		const XrVector3f *p = &equirect->pose.position;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.position == "
		                 "{%f %f %f}) is not valid",
		                 layer_index, p->x, p->y, p->z);
	}

	if (sc->num_array_layers <= equirect->subImage.imageArrayIndex) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage."
		                 "imageArrayIndex == %u) Invalid swapchain "
		                 "array index for equirect layer (%u).",
		                 layer_index, equirect->subImage.imageArrayIndex, sc->num_array_layers);
	}

	if (!sc->released.yes) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage."
		                 "swapchain) swapchain has not been released!",
		                 layer_index);
	}

	if (sc->released.index >= (int)sc->swapchain->num_images) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) internal "
		                 "image index out of bounds",
		                 layer_index);
	}

	if (is_rect_neg(&equirect->subImage.imageRect)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.imageRect.offset == "
		                 "{%i, %i}) has negative component(s)",
		                 layer_index, equirect->subImage.imageRect.offset.x,
		                 equirect->subImage.imageRect.offset.y);
	}

	if (is_rect_out_of_bounds(&equirect->subImage.imageRect, sc)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.imageRect == {{%i, "
		                 "%i}, {%u, %u}}) imageRect out of image bounds (%u, %u)",
		                 layer_index, equirect->subImage.imageRect.offset.x,
		                 equirect->subImage.imageRect.offset.y, equirect->subImage.imageRect.extent.width,
		                 equirect->subImage.imageRect.extent.height, sc->width, sc->height);
	}

	if (equirect->radius < .0f) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->radius == %f) "
		                 "radius out of bounds",
		                 layer_index, equirect->radius);
	}

	return XR_SUCCESS;
#endif
}

static XrResult
verify_equirect2_layer(struct xrt_compositor *xc,
                       struct oxr_logger *log,
                       uint32_t layer_index,
                       const XrCompositionLayerEquirect2KHR *equirect,
                       struct xrt_device *head,
                       uint64_t timestamp)
{
#ifndef XRT_FEATURE_OPENXR_LAYER_EQUIRECT2
	return oxr_error(log, XR_ERROR_LAYER_INVALID,
	                 "(frameEndInfo->layers[%u]->type) layer type "
	                 "XrCompositionLayerEquirect2KHR not supported",
	                 layer_index);
#else
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, equirect->subImage.swapchain);

	if (sc == NULL) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage."
		                 "swapchain) swapchain is NULL!",
		                 layer_index);
	}

	XrResult ret = verify_space(log, layer_index, equirect->space);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (!math_quat_validate_within_1_percent((struct xrt_quat *)&equirect->pose.orientation)) {
		const XrQuaternionf *q = &equirect->pose.orientation;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.orientation "
		                 "== {%f %f %f %f}) is not a valid quat",
		                 layer_index, q->x, q->y, q->z, q->w);
	}

	if (!math_vec3_validate((struct xrt_vec3 *)&equirect->pose.position)) {
		const XrVector3f *p = &equirect->pose.position;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.position == "
		                 "{%f %f %f}) is not valid",
		                 layer_index, p->x, p->y, p->z);
	}

	if (sc->num_array_layers <= equirect->subImage.imageArrayIndex) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage."
		                 "imageArrayIndex == %u) Invalid swapchain "
		                 "array index for equirect layer (%u).",
		                 layer_index, equirect->subImage.imageArrayIndex, sc->num_array_layers);
	}

	if (!sc->released.yes) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage."
		                 "swapchain) swapchain has not been released!",
		                 layer_index);
	}

	if (sc->released.index >= (int)sc->swapchain->num_images) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) internal "
		                 "image index out of bounds",
		                 layer_index);
	}

	if (is_rect_neg(&equirect->subImage.imageRect)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.imageRect.offset == "
		                 "{%i, %i}) has negative component(s)",
		                 layer_index, equirect->subImage.imageRect.offset.x,
		                 equirect->subImage.imageRect.offset.y);
	}

	if (is_rect_out_of_bounds(&equirect->subImage.imageRect, sc)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.imageRect == {{%i, "
		                 "%i}, {%u, %u}}) imageRect out of image bounds (%u, %u)",
		                 layer_index, equirect->subImage.imageRect.offset.x,
		                 equirect->subImage.imageRect.offset.y, equirect->subImage.imageRect.extent.width,
		                 equirect->subImage.imageRect.extent.height, sc->width, sc->height);
	}

	if (equirect->centralHorizontalAngle < .0f) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->centralHorizontalAngle == %f) "
		                 "centralHorizontalAngle out of bounds",
		                 layer_index, equirect->centralHorizontalAngle);
	}

	/*
	 * Accept all angle ranges here, since we are dealing with π
	 * and we don't want floating point errors to prevent the client
	 * to display the full sphere.
	 */

	return XR_SUCCESS;
#endif
}

static enum xrt_layer_composition_flags
convert_layer_flags(XrSwapchainUsageFlags xr_flags)
{
	enum xrt_layer_composition_flags flags = 0;

	if ((xr_flags & XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT) != 0) {
		flags |= XRT_LAYER_COMPOSITION_CORRECT_CHROMATIC_ABERRATION_BIT;
	}
	if ((xr_flags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) != 0) {
		flags |= XRT_LAYER_COMPOSITION_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
	}
	if ((xr_flags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT) != 0) {
		flags |= XRT_LAYER_COMPOSITION_UNPREMULTIPLIED_ALPHA_BIT;
	}

	return flags;
}

static enum xrt_layer_eye_visibility
convert_eye_visibility(XrSwapchainUsageFlags xr_visibility)
{
	enum xrt_layer_eye_visibility visibility = 0;

	if (xr_visibility == XR_EYE_VISIBILITY_BOTH) {
		visibility = XRT_LAYER_EYE_VISIBILITY_BOTH;
	}
	if (xr_visibility == XR_EYE_VISIBILITY_LEFT) {
		visibility = XRT_LAYER_EYE_VISIBILITY_LEFT_BIT;
	}
	if (xr_visibility == XR_EYE_VISIBILITY_RIGHT) {
		visibility = XRT_LAYER_EYE_VISIBILITY_RIGHT_BIT;
	}

	return visibility;
}

static bool
handle_space(struct oxr_logger *log,
             struct oxr_session *sess,
             struct oxr_space *spc,
             const struct xrt_pose *pose_ptr,
             const struct xrt_pose *inv_offset,
             uint64_t timestamp,
             struct xrt_pose *out_pose)
{
	struct xrt_pose pose = *pose_ptr;

	// The pose might be valid for OpenXR, but not good enough for math.
	if (!math_quat_validate(&pose.orientation)) {
		math_quat_normalize(&pose.orientation);
	}

	if (spc->is_reference && spc->type == XR_REFERENCE_SPACE_TYPE_VIEW) {
		// The space might have a pose, transform that in as well.
		math_pose_transform(&spc->pose, &pose, &pose);
	} else if (spc->is_reference) {
		// The space might have a pose, transform that in as well.
		math_pose_transform(&spc->pose, &pose, &pose);

		// Remove the tracking system origin offset.
		math_pose_transform(inv_offset, &pose, &pose);

		if (spc->type == XR_REFERENCE_SPACE_TYPE_LOCAL) {
			if (!initial_head_relation_valid(sess)) {
				return false;
			}
			math_pose_transform(&sess->initial_head_relation.pose, &pose, &pose);
		}

	} else {
		//! @todo Action space handling not very complete

		struct oxr_action_input *input = NULL;

		oxr_action_get_pose_input(log, sess, spc->act_key, &spc->subaction_paths, &input);

		// If the input isn't active.
		if (input == NULL) {
			//! @todo just don't render the quad here?
			return false;
		}


		struct xrt_space_relation out_relation;

		oxr_xdev_get_space_relation(log, sess->sys->inst, input->xdev, input->input->name, timestamp,
		                            &out_relation);

		struct xrt_pose device_pose = out_relation.pose;

		// The space might have a pose, transform that in as well.
		math_pose_transform(&spc->pose, &device_pose, &device_pose);

		math_pose_transform(&device_pose, &pose, &pose);

		// Remove the tracking system origin offset.
		math_pose_transform(inv_offset, &pose, &pose);
	}


	*out_pose = pose;

	return true;
}

static XrResult
submit_quad_layer(struct oxr_session *sess,
                  struct xrt_compositor *xc,
                  struct oxr_logger *log,
                  XrCompositionLayerQuad *quad,
                  struct xrt_device *head,
                  struct xrt_pose *inv_offset,
                  uint64_t timestamp)
{
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, quad->subImage.swapchain);
	struct oxr_space *spc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, quad->space);

	enum xrt_layer_composition_flags flags = convert_layer_flags(quad->layerFlags);

	struct xrt_pose *pose_ptr = (struct xrt_pose *)&quad->pose;

	struct xrt_pose pose;
	if (!handle_space(log, sess, spc, pose_ptr, inv_offset, timestamp, &pose)) {
		return XR_SUCCESS;
	}

	if (spc->is_reference && spc->type == XR_REFERENCE_SPACE_TYPE_VIEW) {
		flags |= XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT;
	}

	struct xrt_layer_data data;
	U_ZERO(&data);
	data.type = XRT_LAYER_QUAD;
	data.name = XRT_INPUT_GENERIC_HEAD_POSE;
	data.timestamp = timestamp;
	data.flags = flags;

	struct xrt_vec2 *size = (struct xrt_vec2 *)&quad->size;
	struct xrt_rect *rect = (struct xrt_rect *)&quad->subImage.imageRect;

	data.quad.visibility = convert_eye_visibility(quad->eyeVisibility);
	data.quad.sub.image_index = sc->released.index;
	data.quad.sub.array_index = quad->subImage.imageArrayIndex;
	data.quad.sub.rect = *rect;
	data.quad.pose = pose;
	data.quad.size = *size;

	CALL_CHK(xrt_comp_layer_quad(xc, head, sc->swapchain, &data));

	return XR_SUCCESS;
}

static XrResult
submit_projection_layer(struct xrt_compositor *xc,
                        struct oxr_logger *log,
                        XrCompositionLayerProjection *proj,
                        struct xrt_device *head,
                        struct xrt_pose *inv_offset,
                        uint64_t timestamp)
{
	struct oxr_space *spc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, proj->space);
	struct oxr_swapchain *d_scs[2] = {NULL, NULL};
	struct oxr_swapchain *scs[2];
	struct xrt_pose *pose_ptr[2];
	struct xrt_pose pose[2];

	enum xrt_layer_composition_flags flags = convert_layer_flags(proj->layerFlags);

	uint32_t num_chains = ARRAY_SIZE(scs);
	for (uint32_t i = 0; i < num_chains; i++) {
		scs[i] = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, proj->views[i].subImage.swapchain);
		pose_ptr[i] = (struct xrt_pose *)&proj->views[i].pose;
		pose[i] = *pose_ptr[i];

		// The pose might be valid for OpenXR, but not good enough for math.
		if (!math_quat_validate(&pose[i].orientation)) {
			math_quat_normalize(&pose[i].orientation);
		}
	}

	if (spc->is_reference && spc->type == XR_REFERENCE_SPACE_TYPE_VIEW) {
		flags |= XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT;
		// The space might have a pose, transform that in as well.
		math_pose_transform(&spc->pose, &pose[0], &pose[0]);
		math_pose_transform(&spc->pose, &pose[1], &pose[1]);
	} else {
		//! @todo Handle action spaces.

		// The space might have a pose, transform that in as well.
		math_pose_transform(&spc->pose, &pose[0], &pose[0]);
		math_pose_transform(&spc->pose, &pose[1], &pose[1]);

		// Remove the tracking system origin offset.
		math_pose_transform(inv_offset, &pose[0], &pose[0]);
		math_pose_transform(inv_offset, &pose[1], &pose[1]);
	}

	struct xrt_rect *l_rect = (struct xrt_rect *)&proj->views[0].subImage.imageRect;
	struct xrt_fov *l_fov = (struct xrt_fov *)&proj->views[0].fov;
	struct xrt_rect *r_rect = (struct xrt_rect *)&proj->views[1].subImage.imageRect;
	struct xrt_fov *r_fov = (struct xrt_fov *)&proj->views[1].fov;

	struct xrt_layer_data data;
	U_ZERO(&data);
	data.type = XRT_LAYER_STEREO_PROJECTION;
	data.name = XRT_INPUT_GENERIC_HEAD_POSE;
	data.timestamp = timestamp;
	data.flags = flags;

	data.stereo.l.sub.image_index = scs[0]->released.index;
	data.stereo.l.sub.array_index = proj->views[0].subImage.imageArrayIndex;
	data.stereo.l.sub.rect = *l_rect;
	data.stereo.l.fov = *l_fov;
	data.stereo.l.pose = pose[0];

	data.stereo.r.sub.image_index = scs[1]->released.index;
	data.stereo.r.sub.array_index = proj->views[1].subImage.imageArrayIndex;
	data.stereo.r.sub.rect = *r_rect;
	data.stereo.r.fov = *r_fov;
	data.stereo.r.pose = pose[1];



#ifdef XRT_FEATURE_OPENXR_LAYER_DEPTH
	const XrCompositionLayerDepthInfoKHR *d_l = OXR_GET_INPUT_FROM_CHAIN(
	    &proj->views[0], XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR, XrCompositionLayerDepthInfoKHR);
	if (d_l) {
		data.stereo_depth.l_d.far_z = d_l->farZ;
		data.stereo_depth.l_d.near_z = d_l->nearZ;
		data.stereo_depth.l_d.max_depth = d_l->maxDepth;
		data.stereo_depth.l_d.min_depth = d_l->minDepth;

		struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, d_l->subImage.swapchain);

		struct xrt_rect *d_l_rect = (struct xrt_rect *)&d_l->subImage.imageRect;
		data.stereo_depth.l_d.sub.image_index = sc->released.index;
		data.stereo_depth.l_d.sub.array_index = d_l->subImage.imageArrayIndex;
		data.stereo_depth.l_d.sub.rect = *d_l_rect;

		// Need to pass this in.
		d_scs[0] = sc;
	}

	const XrCompositionLayerDepthInfoKHR *d_r = OXR_GET_INPUT_FROM_CHAIN(
	    &proj->views[1], XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR, XrCompositionLayerDepthInfoKHR);

	if (d_r) {
		data.stereo_depth.r_d.far_z = d_r->farZ;
		data.stereo_depth.r_d.near_z = d_r->nearZ;
		data.stereo_depth.r_d.max_depth = d_r->maxDepth;
		data.stereo_depth.r_d.min_depth = d_r->minDepth;

		struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, d_r->subImage.swapchain);

		struct xrt_rect *d_l_rect = (struct xrt_rect *)&d_r->subImage.imageRect;
		data.stereo_depth.r_d.sub.image_index = sc->released.index;
		data.stereo_depth.r_d.sub.array_index = d_r->subImage.imageArrayIndex;
		data.stereo_depth.r_d.sub.rect = *d_l_rect;

		// Need to pass this in.
		d_scs[1] = sc;
	}
#endif // XRT_FEATURE_OPENXR_LAYER_DEPTH

	if (d_scs[0] != NULL && d_scs[1] != NULL) {
#ifdef XRT_FEATURE_OPENXR_LAYER_DEPTH
		data.type = XRT_LAYER_STEREO_PROJECTION_DEPTH;
		CALL_CHK(xrt_comp_layer_stereo_projection_depth(xc, head,
		                                                scs[0]->swapchain,   // Left
		                                                scs[1]->swapchain,   // Right
		                                                d_scs[0]->swapchain, // Left
		                                                d_scs[1]->swapchain, // Right
		                                                &data));
#else
		assert(false && "Should not get here");
#endif // XRT_FEATURE_OPENXR_LAYER_DEPTH
	} else {
		CALL_CHK(xrt_comp_layer_stereo_projection(xc, head,
		                                          scs[0]->swapchain, // Left
		                                          scs[1]->swapchain, // Right
		                                          &data));
	}

	return XR_SUCCESS;
}

static void
submit_cube_layer(struct oxr_session *sess,
                  struct xrt_compositor *xc,
                  struct oxr_logger *log,
                  const XrCompositionLayerCubeKHR *cube,
                  struct xrt_device *head,
                  struct xrt_pose *inv_offset,
                  uint64_t timestamp)
{
	// Not implemented
}

static XrResult
submit_cylinder_layer(struct oxr_session *sess,
                      struct xrt_compositor *xc,
                      struct oxr_logger *log,
                      const XrCompositionLayerCylinderKHR *cylinder,
                      struct xrt_device *head,
                      struct xrt_pose *inv_offset,
                      uint64_t timestamp)
{
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, cylinder->subImage.swapchain);
	struct oxr_space *spc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, cylinder->space);

	enum xrt_layer_composition_flags flags = convert_layer_flags(cylinder->layerFlags);
	enum xrt_layer_eye_visibility visibility = convert_eye_visibility(cylinder->eyeVisibility);

	struct xrt_pose *pose_ptr = (struct xrt_pose *)&cylinder->pose;

	struct xrt_pose pose;
	if (!handle_space(log, sess, spc, pose_ptr, inv_offset, timestamp, &pose)) {
		return XR_SUCCESS;
	}

	if (spc->is_reference && spc->type == XR_REFERENCE_SPACE_TYPE_VIEW) {
		flags |= XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT;
	}

	struct xrt_layer_data data;
	U_ZERO(&data);
	data.type = XRT_LAYER_CYLINDER;
	data.name = XRT_INPUT_GENERIC_HEAD_POSE;
	data.timestamp = timestamp;
	data.flags = flags;

	struct xrt_rect *rect = (struct xrt_rect *)&cylinder->subImage.imageRect;

	data.cylinder.visibility = visibility;
	data.cylinder.sub.image_index = sc->released.index;
	data.cylinder.sub.array_index = cylinder->subImage.imageArrayIndex;
	data.cylinder.sub.rect = *rect;
	data.cylinder.pose = pose;
	data.cylinder.radius = cylinder->radius;
	data.cylinder.central_angle = cylinder->centralAngle;
	data.cylinder.aspect_ratio = cylinder->aspectRatio;

	CALL_CHK(xrt_comp_layer_cylinder(xc, head, sc->swapchain, &data));

	return XR_SUCCESS;
}

static XrResult
submit_equirect1_layer(struct oxr_session *sess,
                       struct xrt_compositor *xc,
                       struct oxr_logger *log,
                       const XrCompositionLayerEquirectKHR *equirect,
                       struct xrt_device *head,
                       struct xrt_pose *inv_offset,
                       uint64_t timestamp)
{
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, equirect->subImage.swapchain);
	struct oxr_space *spc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, equirect->space);

	enum xrt_layer_composition_flags flags = convert_layer_flags(equirect->layerFlags);

	struct xrt_pose *pose_ptr = (struct xrt_pose *)&equirect->pose;

	struct xrt_pose pose;
	if (!handle_space(log, sess, spc, pose_ptr, inv_offset, timestamp, &pose)) {
		return XR_SUCCESS;
	}

	if (spc->is_reference && spc->type == XR_REFERENCE_SPACE_TYPE_VIEW) {
		flags |= XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT;
	}

	struct xrt_layer_data data;
	U_ZERO(&data);
	data.type = XRT_LAYER_EQUIRECT1;
	data.name = XRT_INPUT_GENERIC_HEAD_POSE;
	data.timestamp = timestamp;
	data.flags = flags;

	struct xrt_rect *rect = (struct xrt_rect *)&equirect->subImage.imageRect;

	data.equirect1.visibility = convert_eye_visibility(equirect->eyeVisibility);
	data.equirect1.sub.image_index = sc->released.index;
	data.equirect1.sub.array_index = equirect->subImage.imageArrayIndex;
	data.equirect1.sub.rect = *rect;
	data.equirect1.pose = pose;

	data.equirect1.radius = equirect->radius;

	struct xrt_vec2 *scale = (struct xrt_vec2 *)&equirect->scale;
	struct xrt_vec2 *bias = (struct xrt_vec2 *)&equirect->bias;

	data.equirect1.scale = *scale;
	data.equirect1.bias = *bias;

	CALL_CHK(xrt_comp_layer_equirect1(xc, head, sc->swapchain, &data));

	return XR_SUCCESS;
}

static void
do_synchronize_state_change(struct oxr_logger *log, struct oxr_session *sess)
{
	if (!sess->has_ended_once && sess->state < XR_SESSION_STATE_VISIBLE) {
		oxr_session_change_state(log, sess, XR_SESSION_STATE_SYNCHRONIZED);
		sess->has_ended_once = true;
	}
}

static XrResult
submit_equirect2_layer(struct oxr_session *sess,
                       struct xrt_compositor *xc,
                       struct oxr_logger *log,
                       const XrCompositionLayerEquirect2KHR *equirect,
                       struct xrt_device *head,
                       struct xrt_pose *inv_offset,
                       uint64_t timestamp)
{
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, equirect->subImage.swapchain);
	struct oxr_space *spc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, equirect->space);

	enum xrt_layer_composition_flags flags = convert_layer_flags(equirect->layerFlags);

	struct xrt_pose *pose_ptr = (struct xrt_pose *)&equirect->pose;

	struct xrt_pose pose;
	if (!handle_space(log, sess, spc, pose_ptr, inv_offset, timestamp, &pose)) {
		return XR_SUCCESS;
	}

	if (spc->is_reference && spc->type == XR_REFERENCE_SPACE_TYPE_VIEW) {
		flags |= XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT;
	}

	struct xrt_layer_data data;
	U_ZERO(&data);
	data.type = XRT_LAYER_EQUIRECT2;
	data.name = XRT_INPUT_GENERIC_HEAD_POSE;
	data.timestamp = timestamp;
	data.flags = flags;

	struct xrt_rect *rect = (struct xrt_rect *)&equirect->subImage.imageRect;

	data.equirect2.visibility = convert_eye_visibility(equirect->eyeVisibility);
	data.equirect2.sub.image_index = sc->released.index;
	data.equirect2.sub.array_index = equirect->subImage.imageArrayIndex;
	data.equirect2.sub.rect = *rect;
	data.equirect2.pose = pose;

	data.equirect2.radius = equirect->radius;
	data.equirect2.central_horizontal_angle = equirect->centralHorizontalAngle;
	data.equirect2.upper_vertical_angle = equirect->upperVerticalAngle;
	data.equirect2.lower_vertical_angle = equirect->lowerVerticalAngle;

	CALL_CHK(xrt_comp_layer_equirect2(xc, head, sc->swapchain, &data));

	return XR_SUCCESS;
}

XrResult
oxr_session_frame_end(struct oxr_logger *log, struct oxr_session *sess, const XrFrameEndInfo *frameEndInfo)
{
	/*
	 * Session state and call order.
	 */

	if (!is_running(sess)) {
		return oxr_error(log, XR_ERROR_SESSION_NOT_RUNNING, "Session is not running");
	}
	if (!sess->frame_started) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID, "Frame not begun with xrBeginFrame");
	}

	if (frameEndInfo->displayTime <= 0) {
		return oxr_error(log, XR_ERROR_TIME_INVALID,
		                 "(frameEndInfo->displayTime == %" PRIi64
		                 ") zero or a negative value is not a valid XrTime",
		                 frameEndInfo->displayTime);
	}

	int64_t timestamp = time_state_ts_to_monotonic_ns(sess->sys->inst->timekeeping, frameEndInfo->displayTime);
	if (sess->frame_timing_spew) {
		oxr_log(log, "End frame at %8.3fms with display time %8.3fms", ts_ms(sess), ns_to_ms(timestamp));
	}

	struct xrt_compositor *xc = sess->compositor;

	/*
	 * early out for headless sessions.
	 */
	if (xc == NULL) {
		sess->frame_started = false;

		os_mutex_lock(&sess->active_wait_frames_lock);
		sess->active_wait_frames--;
		os_mutex_unlock(&sess->active_wait_frames_lock);

		do_synchronize_state_change(log, sess);

		return oxr_session_success_result(sess);
	}


	/*
	 * Blend mode.
	 * XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED must always be reported,
	 * even with 0 layers.
	 */

	enum xrt_blend_mode blend_mode = oxr_blend_mode_to_xrt(frameEndInfo->environmentBlendMode);

	if (blend_mode == 0) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->environmentBlendMode == "
		                 "0x%08x) unknown environment blend mode",
		                 frameEndInfo->environmentBlendMode);
	}

	struct xrt_device *xdev = GET_XDEV_BY_ROLE(sess->sys, head);
	if ((blend_mode & xdev->hmd->blend_mode) == 0) {
		//! @todo Make integer print to string.
		return oxr_error(log, XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED,
		                 "(frameEndInfo->environmentBlendMode == %u) "
		                 "is not supported",
		                 frameEndInfo->environmentBlendMode);
	}

	/*
	 * Early out for discarded frame if layer count is 0.
	 */
	if (frameEndInfo->layerCount == 0) {

		os_mutex_lock(&sess->active_wait_frames_lock);
		sess->active_wait_frames--;
		os_mutex_unlock(&sess->active_wait_frames_lock);

		CALL_CHK(xrt_comp_discard_frame(xc, sess->frame_id.begun));
		sess->frame_id.begun = -1;
		sess->frame_started = false;

		do_synchronize_state_change(log, sess);

		return oxr_session_success_result(sess);
	}


	/*
	 * Layers.
	 */

	if (frameEndInfo->layers == NULL) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID, "(frameEndInfo->layers == NULL)");
	}

	for (uint32_t i = 0; i < frameEndInfo->layerCount; i++) {
		const XrCompositionLayerBaseHeader *layer = frameEndInfo->layers[i];
		if (layer == NULL) {
			return oxr_error(log, XR_ERROR_LAYER_INVALID,
			                 "(frameEndInfo->layers[%u] == NULL) "
			                 "layer can not be null",
			                 i);
		}

		XrResult res;

		switch (layer->type) {
		case XR_TYPE_COMPOSITION_LAYER_PROJECTION:
			res = verify_projection_layer(xc, log, i, (XrCompositionLayerProjection *)layer, xdev,
			                              frameEndInfo->displayTime);
			break;
		case XR_TYPE_COMPOSITION_LAYER_QUAD:
			res = verify_quad_layer(xc, log, i, (XrCompositionLayerQuad *)layer, xdev,
			                        frameEndInfo->displayTime);
			break;
		case XR_TYPE_COMPOSITION_LAYER_CUBE_KHR:
			res = verify_cube_layer(xc, log, i, (XrCompositionLayerCubeKHR *)layer, xdev,
			                        frameEndInfo->displayTime);
			break;
		case XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR:
			res = verify_cylinder_layer(xc, log, i, (XrCompositionLayerCylinderKHR *)layer, xdev,
			                            frameEndInfo->displayTime);
			break;
		case XR_TYPE_COMPOSITION_LAYER_EQUIRECT_KHR:
			res = verify_equirect1_layer(xc, log, i, (XrCompositionLayerEquirectKHR *)layer, xdev,
			                             frameEndInfo->displayTime);
			break;
		case XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR:
			res = verify_equirect2_layer(xc, log, i, (XrCompositionLayerEquirect2KHR *)layer, xdev,
			                             frameEndInfo->displayTime);
			break;
		default:
			return oxr_error(log, XR_ERROR_LAYER_INVALID,
			                 "(frameEndInfo->layers[%u]->type) "
			                 "layer type not supported",
			                 i);
		}

		if (res != XR_SUCCESS) {
			return res;
		}
	}


	/*
	 * Done verifying.
	 */

	// Do state change if needed.
	do_synchronize_state_change(log, sess);

	struct xrt_pose inv_offset = {0};
	math_pose_invert(&xdev->tracking_origin->offset, &inv_offset);

	CALL_CHK(xrt_comp_layer_begin(xc, sess->frame_id.begun, blend_mode));

	for (uint32_t i = 0; i < frameEndInfo->layerCount; i++) {
		const XrCompositionLayerBaseHeader *layer = frameEndInfo->layers[i];
		assert(layer != NULL);

		int64_t timestamp =
		    time_state_ts_to_monotonic_ns(sess->sys->inst->timekeeping, frameEndInfo->displayTime);

		switch (layer->type) {
		case XR_TYPE_COMPOSITION_LAYER_PROJECTION:
			submit_projection_layer(xc, log, (XrCompositionLayerProjection *)layer, xdev, &inv_offset,
			                        timestamp);
			break;
		case XR_TYPE_COMPOSITION_LAYER_QUAD:
			submit_quad_layer(sess, xc, log, (XrCompositionLayerQuad *)layer, xdev, &inv_offset, timestamp);
			break;
		case XR_TYPE_COMPOSITION_LAYER_CUBE_KHR:
			submit_cube_layer(sess, xc, log, (XrCompositionLayerCubeKHR *)layer, xdev, &inv_offset,
			                  timestamp);
			break;
		case XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR:
			submit_cylinder_layer(sess, xc, log, (XrCompositionLayerCylinderKHR *)layer, xdev, &inv_offset,
			                      timestamp);
			break;
		case XR_TYPE_COMPOSITION_LAYER_EQUIRECT_KHR:
			submit_equirect1_layer(sess, xc, log, (XrCompositionLayerEquirectKHR *)layer, xdev, &inv_offset,
			                       timestamp);
			break;
		case XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR:
			submit_equirect2_layer(sess, xc, log, (XrCompositionLayerEquirect2KHR *)layer, xdev,
			                       &inv_offset, timestamp);
			break;
		default: assert(false && "invalid layer type");
		}
	}

	CALL_CHK(xrt_comp_layer_commit(xc, sess->frame_id.begun, XRT_GRAPHICS_SYNC_HANDLE_INVALID));
	sess->frame_id.begun = -1;

	sess->frame_started = false;

	os_mutex_lock(&sess->active_wait_frames_lock);
	sess->active_wait_frames--;
	os_mutex_unlock(&sess->active_wait_frames_lock);

	return oxr_session_success_result(sess);
}

static XrResult
oxr_session_destroy(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	struct oxr_session *sess = (struct oxr_session *)hb;

	XrResult ret = oxr_event_remove_session_events(log, sess);

	for (size_t i = 0; i < sess->num_action_set_attachments; ++i) {
		oxr_action_set_attachment_teardown(&sess->act_set_attachments[i]);
	}
	free(sess->act_set_attachments);
	sess->act_set_attachments = NULL;
	sess->num_action_set_attachments = 0;

	// If we tore everything down correctly, these are empty now.
	assert(sess->act_sets_attachments_by_key == NULL || u_hashmap_int_empty(sess->act_sets_attachments_by_key));
	assert(sess->act_attachments_by_key == NULL || u_hashmap_int_empty(sess->act_attachments_by_key));

	u_hashmap_int_destroy(&sess->act_sets_attachments_by_key);
	u_hashmap_int_destroy(&sess->act_attachments_by_key);

	xrt_comp_destroy(&sess->compositor);
	xrt_comp_native_destroy(&sess->xcn);

	os_semaphore_destroy(&sess->sem);
	os_mutex_destroy(&sess->active_wait_frames_lock);

	free(sess);

	return ret;
}


#define OXR_ALLOCATE_NATIVE_COMPOSITOR(LOG, XSI, SESS)                                                                 \
	do {                                                                                                           \
		xrt_result_t xret = xrt_syscomp_create_native_compositor((SESS)->sys->xsysc, (XSI), &(SESS)->xcn);     \
		if (xret == XRT_ERROR_MULTI_SESSION_NOT_IMPLEMENTED) {                                                 \
			return oxr_error((LOG), XR_ERROR_LIMIT_REACHED, "Per instance multi-session not supported.");  \
		} else if (xret != XRT_SUCCESS) {                                                                      \
			return oxr_error((LOG), XR_ERROR_RUNTIME_FAILURE, "Failed to create native compositor! '%i'",  \
			                 xret);                                                                        \
		}                                                                                                      \
	} while (false)

#define OXR_SESSION_ALLOCATE(LOG, SYS, OUT)                                                                            \
	do {                                                                                                           \
		OXR_ALLOCATE_HANDLE_OR_RETURN(LOG, OUT, OXR_XR_DEBUG_SESSION, oxr_session_destroy,                     \
		                              &(SYS)->inst->handle);                                                   \
		(OUT)->sys = (SYS);                                                                                    \
	} while (0)


/* Just the allocation and populate part, so we can use early-returns to
 * simplify code flow and avoid weird if/else */
static XrResult
oxr_session_create_impl(struct oxr_logger *log,
                        struct oxr_system *sys,
                        const XrSessionCreateInfo *createInfo,
                        const struct xrt_session_info *xsi,
                        struct oxr_session **out_session)
{
#if defined(XR_USE_PLATFORM_XLIB) && defined(XR_USE_GRAPHICS_API_OPENGL)
	XrGraphicsBindingOpenGLXlibKHR const *opengl_xlib = OXR_GET_INPUT_FROM_CHAIN(
	    createInfo, XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR, XrGraphicsBindingOpenGLXlibKHR);
	if (opengl_xlib != NULL) {
		if (!sys->gotten_requirements) {
			return oxr_error(log, XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING,
			                 "Has not called "
			                 "xrGetOpenGL[ES]GraphicsRequirementsKHR");
		}

		OXR_SESSION_ALLOCATE(log, sys, *out_session);
		OXR_ALLOCATE_NATIVE_COMPOSITOR(log, xsi, *out_session);
		return oxr_session_populate_gl_xlib(log, sys, opengl_xlib, *out_session);
	}
#endif


#if defined(XR_USE_PLATFORM_ANDROID) && defined(XR_USE_GRAPHICS_API_OPENGL_ES)
	XrGraphicsBindingOpenGLESAndroidKHR const *opengles_android = OXR_GET_INPUT_FROM_CHAIN(
	    createInfo, XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR, XrGraphicsBindingOpenGLESAndroidKHR);
	if (opengles_android != NULL) {
		if (!sys->gotten_requirements) {
			return oxr_error(log, XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING,
			                 "Has not called "
			                 "xrGetOpenGLESGraphicsRequirementsKHR");
		}

		OXR_SESSION_ALLOCATE(log, sys, *out_session);
		OXR_ALLOCATE_NATIVE_COMPOSITOR(log, xsi, *out_session);
		return oxr_session_populate_gles_android(log, sys, opengles_android, *out_session);
	}
#endif

#ifdef XR_USE_GRAPHICS_API_VULKAN
	XrGraphicsBindingVulkanKHR const *vulkan =
	    OXR_GET_INPUT_FROM_CHAIN(createInfo, XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR, XrGraphicsBindingVulkanKHR);
	if (vulkan != NULL) {
		if (!sys->gotten_requirements) {
			return oxr_error(log, XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING,
			                 "Has not called "
			                 "xrGetVulkanGraphicsRequirementsKHR");
		}

		OXR_SESSION_ALLOCATE(log, sys, *out_session);
		OXR_ALLOCATE_NATIVE_COMPOSITOR(log, xsi, *out_session);
		return oxr_session_populate_vk(log, sys, vulkan, *out_session);
	}
#endif

#ifdef XR_USE_PLATFORM_EGL
	XrGraphicsBindingEGLMNDX const *egl =
	    OXR_GET_INPUT_FROM_CHAIN(createInfo, XR_TYPE_GRAPHICS_BINDING_EGL_MNDX, XrGraphicsBindingEGLMNDX);
	if (egl != NULL) {
		if (!sys->gotten_requirements) {
			return oxr_error(log, XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING,
			                 "Has not called "
			                 "xrGetOpenGL[ES]GraphicsRequirementsKHR");
		}

		OXR_SESSION_ALLOCATE(log, sys, *out_session);
		OXR_ALLOCATE_NATIVE_COMPOSITOR(log, xsi, *out_session);
		return oxr_session_populate_egl(log, sys, egl, *out_session);
	}
#endif

	/*
	 * Add any new graphics binding structs here - before the headless
	 * check. (order for non-headless checks not specified in standard.)
	 * Any new addition will also need to be added to
	 * oxr_verify_XrSessionCreateInfo and have its own associated verify
	 * function added.
	 */

	if (sys->inst->extensions.MND_headless) {
		OXR_SESSION_ALLOCATE(log, sys, *out_session);
		(*out_session)->compositor = NULL;
		(*out_session)->create_swapchain = NULL;
		return XR_SUCCESS;
	}
	return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
	                 "(createInfo->next->type) doesn't contain a valid "
	                 "graphics binding structs");
}

XrResult
oxr_session_create(struct oxr_logger *log,
                   struct oxr_system *sys,
                   const XrSessionCreateInfo *createInfo,
                   struct oxr_session **out_session)
{
	struct oxr_session *sess = NULL;

	struct xrt_session_info xsi = {0};
	const XrSessionCreateInfoOverlayEXTX *overlay_info = OXR_GET_INPUT_FROM_CHAIN(
	    createInfo, XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX, XrSessionCreateInfoOverlayEXTX);
	if (overlay_info) {
		xsi.is_overlay = true;
		xsi.flags = overlay_info->createFlags;
		xsi.z_order = overlay_info->sessionLayersPlacement;
	}

	/* Try allocating and populating. */
	XrResult ret = oxr_session_create_impl(log, sys, createInfo, &xsi, &sess);
	if (ret != XR_SUCCESS) {
		if (sess != NULL) {
			/* clean up allocation first */
			XrResult cleanup_result = oxr_handle_destroy(log, &sess->handle);
			assert(cleanup_result == XR_SUCCESS);
			(void)cleanup_result;
		}
		return ret;
	}

	// Init the begin/wait frame semaphore.
	os_semaphore_init(&sess->sem, 1);

	sess->active_wait_frames = 0;
	os_mutex_init(&sess->active_wait_frames_lock);

	sess->ipd_meters = debug_get_num_option_ipd() / 1000.0f;
	sess->frame_timing_spew = debug_get_bool_option_frame_timing_spew();

	oxr_session_change_state(log, sess, XR_SESSION_STATE_IDLE);
	oxr_session_change_state(log, sess, XR_SESSION_STATE_READY);

	u_hashmap_int_create(&sess->act_sets_attachments_by_key);
	u_hashmap_int_create(&sess->act_attachments_by_key);

	*out_session = sess;

	return ret;
}

void
xrt_to_xr_pose(struct xrt_pose *xrt_pose, XrPosef *xr_pose)
{
	xr_pose->orientation.x = xrt_pose->orientation.x;
	xr_pose->orientation.y = xrt_pose->orientation.y;
	xr_pose->orientation.z = xrt_pose->orientation.z;
	xr_pose->orientation.w = xrt_pose->orientation.w;

	xr_pose->position.x = xrt_pose->position.x;
	xr_pose->position.y = xrt_pose->position.y;
	xr_pose->position.z = xrt_pose->position.z;
}

XrResult
oxr_session_hand_joints(struct oxr_logger *log,
                        struct oxr_hand_tracker *hand_tracker,
                        const XrHandJointsLocateInfoEXT *locateInfo,
                        XrHandJointLocationsEXT *locations)
{
	struct oxr_space *baseSpc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, locateInfo->baseSpace);

	struct oxr_session *sess = hand_tracker->sess;

	XrHandJointVelocitiesEXT *vel =
	    OXR_GET_OUTPUT_FROM_CHAIN(locations, XR_TYPE_HAND_JOINT_VELOCITIES_EXT, XrHandJointVelocitiesEXT);

	struct xrt_device *xdev = hand_tracker->xdev;
	enum xrt_input_name name = hand_tracker->input_name;

	struct xrt_pose *tracking_origin_offset = &xdev->tracking_origin->offset;

	XrTime at_time = locateInfo->time;
	struct xrt_hand_joint_set value;

	oxr_xdev_get_hand_tracking_at(log, sess->sys->inst, xdev, name, at_time, &value);

	for (uint32_t i = 0; i < locations->jointCount; i++) {
		locations->jointLocations[i].locationFlags =
		    xrt_to_xr_space_location_flags(value.values.hand_joint_set_default[i].relation.relation_flags);
		locations->jointLocations[i].radius = value.values.hand_joint_set_default[i].radius;

		struct xrt_space_relation r = value.values.hand_joint_set_default[i].relation;

		struct xrt_space_relation result;
		struct xrt_space_graph graph = {0};
		m_space_graph_add_relation(&graph, &r);


		if (baseSpc->type == XR_REFERENCE_SPACE_TYPE_STAGE) {

			m_space_graph_add_relation(&graph, &value.hand_pose);
			m_space_graph_add_pose_if_not_identity(&graph, tracking_origin_offset);

		} else if (baseSpc->type == XR_REFERENCE_SPACE_TYPE_LOCAL) {

			// for local space, first do stage space and transform
			// result to local @todo: improve local space
			m_space_graph_add_relation(&graph, &value.hand_pose);
			m_space_graph_add_pose_if_not_identity(&graph, tracking_origin_offset);

		} else if (baseSpc->type == XR_REFERENCE_SPACE_TYPE_VIEW) {
			/*! @todo: testing, relating to view space unsupported
			 * in other parts of monado */

			struct xrt_device *head_xdev = GET_XDEV_BY_ROLE(sess->sys, head);

			struct xrt_space_relation view_relation;
			oxr_session_get_view_relation_at(log, sess, at_time, &view_relation);

			m_space_graph_add_relation(&graph, &value.hand_pose);
			m_space_graph_add_pose_if_not_identity(&graph, tracking_origin_offset);

			m_space_graph_add_inverted_relation(&graph, &view_relation);
			m_space_graph_add_inverted_pose_if_not_identity(&graph, &head_xdev->tracking_origin->offset);

		} else if (!baseSpc->is_reference) {
			// action space

			struct oxr_action_input *input = NULL;
			oxr_action_get_pose_input(log, sess, baseSpc->act_key, &baseSpc->subaction_paths, &input);

			// If the input isn't active.
			if (input == NULL) {
				locations->isActive = false;
				return XR_SUCCESS;
			}

			struct xrt_space_relation act_space_relation;

			oxr_xdev_get_space_relation(log, sess->sys->inst, input->xdev, input->input->name, at_time,
			                            &act_space_relation);


			m_space_graph_add_relation(&graph, &value.hand_pose);
			m_space_graph_add_pose_if_not_identity(&graph, tracking_origin_offset);

			m_space_graph_add_inverted_relation(&graph, &act_space_relation);
			m_space_graph_add_inverted_pose_if_not_identity(&graph, &input->xdev->tracking_origin->offset);
		}

		m_space_graph_add_inverted_pose_if_not_identity(&graph, &baseSpc->pose);
		m_space_graph_resolve(&graph, &result);

		if (baseSpc->type == XR_REFERENCE_SPACE_TYPE_LOCAL) {
			if (!global_to_local_space(sess, &result)) {
				locations->isActive = false;
				return XR_SUCCESS;
			}
		}

		xrt_to_xr_pose(&result.pose, &locations->jointLocations[i].pose);

		if (vel) {
			XrHandJointVelocityEXT *v = &vel->jointVelocities[i];

			v->velocityFlags = 0;
			if ((result.relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT)) {
				v->velocityFlags |= XR_SPACE_VELOCITY_LINEAR_VALID_BIT;
			}
			if ((result.relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT)) {
				v->velocityFlags |= XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
			}

			v->linearVelocity.x = result.linear_velocity.x;
			v->linearVelocity.y = result.linear_velocity.y;
			v->linearVelocity.z = result.linear_velocity.z;

			v->angularVelocity.x = result.angular_velocity.x;
			v->angularVelocity.y = result.angular_velocity.y;
			v->angularVelocity.z = result.angular_velocity.z;
		}
	}

	locations->isActive = true;

	return XR_SUCCESS;
}
