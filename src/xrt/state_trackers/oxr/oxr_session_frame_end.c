// Copyright 2018-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds session end frame functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <mosesturner@protonmail.com>
 * @ingroup oxr_main
 */

#include "xrt/xrt_device.h"
#include "xrt/xrt_config_build.h"
#include "xrt/xrt_config_have.h"

#include "os/os_time.h"

#include "util/u_debug.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_verify.h"

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


/*
 *
 * Helper functions and defines.
 *
 */

#define CALL_CHK(call)                                                                                                 \
	if ((call) == XRT_ERROR_IPC_FAILURE) {                                                                         \
		return oxr_error(log, XR_ERROR_INSTANCE_LOST, "Error in function call over IPC");                      \
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

static bool
is_session_running(struct oxr_session *sess)
{
	return sess->has_begun;
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

static enum xrt_blend_mode
convert_blend_mode(XrEnvironmentBlendMode blend_mode)
{
	switch (blend_mode) {
	case XR_ENVIRONMENT_BLEND_MODE_OPAQUE: return XRT_BLEND_MODE_OPAQUE;
	case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return XRT_BLEND_MODE_ADDITIVE;
	case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return XRT_BLEND_MODE_ALPHA_BLEND;
	default: return (enum xrt_blend_mode)0;
	}
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

static void
fill_in_sub_image(const struct oxr_swapchain *sc, const XrSwapchainSubImage *oxr_sub, struct xrt_sub_image *xsub)
{
	const struct xrt_rect *rect = (const struct xrt_rect *)&oxr_sub->imageRect;

	xsub->image_index = sc->released.index;
	xsub->array_index = oxr_sub->imageArrayIndex;
	xsub->rect = *rect;
	xsub->norm_rect.w = (float)(rect->extent.w / (double)sc->width);
	xsub->norm_rect.h = (float)(rect->extent.h / (double)sc->height);
	xsub->norm_rect.x = (float)(rect->offset.w / (double)sc->width);
	xsub->norm_rect.y = (float)(rect->offset.h / (double)sc->height);
}


/*
 *
 * Verify functions.
 *
 */

static XrResult
verify_space(struct oxr_logger *log, uint32_t layer_index, XrSpace space)
{
	if (space == XR_NULL_HANDLE) {
		return oxr_error(
		    log, XR_ERROR_VALIDATION_FAILURE,
		    "(frameEndInfo->layers[%u]->space == XR_NULL_HANDLE) XrSpace must not be XR_NULL_HANDLE",
		    layer_index);
	}

	return XR_SUCCESS;
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
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) swapchain is NULL!", layer_index);
	}

	XrResult ret = verify_space(log, layer_index, quad->space);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (!math_quat_validate_within_1_percent((struct xrt_quat *)&quad->pose.orientation)) {
		XrQuaternionf *q = &quad->pose.orientation;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.orientation == {%f %f %f %f}) is not a valid quat",
		                 layer_index, q->x, q->y, q->z, q->w);
	}

	if (!math_vec3_validate((struct xrt_vec3 *)&quad->pose.position)) {
		XrVector3f *p = &quad->pose.position;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.position == {%f %f %f}) is not valid", layer_index,
		                 p->x, p->y, p->z);
	}

	if (sc->array_layer_count <= quad->subImage.imageArrayIndex) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.imageArrayIndex == %u) Invalid swapchain array "
		                 "index for quad layer (%u).",
		                 layer_index, quad->subImage.imageArrayIndex, sc->array_layer_count);
	}

	if (sc->face_count != 1) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) Invalid swapchain face count "
		                 "(expected 1, got %u)",
		                 layer_index, sc->face_count);
	}

	if (!sc->released.yes) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) swapchain has not been released!",
		                 layer_index);
	}

	if (sc->released.index >= (int)sc->swapchain->image_count) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) internal image index out of bounds",
		                 layer_index);
	}

	if (is_rect_neg(&quad->subImage.imageRect)) {
		return oxr_error(
		    log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		    "(frameEndInfo->layers[%u]->subImage.imageRect.offset == {%i, %i}) has negative component(s)",
		    layer_index, quad->subImage.imageRect.offset.x, quad->subImage.imageRect.offset.y);
	}

	if (is_rect_out_of_bounds(&quad->subImage.imageRect, sc)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.imageRect == {{%i, %i}, {%u, %u}}) imageRect out "
		                 "of image bounds (%u, %u)",
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
		                 "(frameEndInfo->layers[%u]->views[%i]->next<XrCompositionLayerDepthInfoKHR>.subImage."
		                 "swapchain) is XR_NULL_HANDLE",
		                 layer_index, i);
	}

	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, depth->subImage.swapchain);

	if (!sc->released.yes) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<XrCompositionLayerDepthInfoKHR>.subImage."
		                 "swapchain) swapchain has not been released",
		                 layer_index, i);
	}

	if (sc->released.index >= (int)sc->swapchain->image_count) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<XrCompositionLayerDepthInfoKHR>.subImage."
		                 "swapchain) internal image index out of bounds",
		                 layer_index, i);
	}

	if (sc->array_layer_count <= depth->subImage.imageArrayIndex) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<XrCompositionLayerDepthInfoKHR>.subImage."
		                 "imageArrayIndex == %u) Invalid swapchain array index for projection layer (%u).",
		                 layer_index, i, depth->subImage.imageArrayIndex, sc->array_layer_count);
	}

	if (sc->face_count != 1) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) Invalid swapchain face count "
		                 "(expected 1, got %u)",
		                 layer_index, sc->face_count);
	}

	if (is_rect_neg(&depth->subImage.imageRect)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<XrCompositionLayerDepthInfoKHR>.subImage."
		                 "imageRect.offset == {%i, %i}) has negative component(s)",
		                 layer_index, i, depth->subImage.imageRect.offset.x,
		                 depth->subImage.imageRect.offset.y);
	}

	if (is_rect_out_of_bounds(&depth->subImage.imageRect, sc)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<XrCompositionLayerDepthInfoKHR>.subImage."
		                 "imageRect == {{%i, %i}, {%u, %u}}) imageRect out of image bounds (%u, %u)",
		                 layer_index, i, depth->subImage.imageRect.offset.x, depth->subImage.imageRect.offset.y,
		                 depth->subImage.imageRect.extent.width, depth->subImage.imageRect.extent.height,
		                 sc->width, sc->height);
	}

	if (depth->minDepth < 0.0f || depth->minDepth > 1.0f) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<XrCompositionLayerDepthInfoKHR>.minDepth) "
		                 "%f must be in [0.0,1.0]",
		                 layer_index, i, depth->minDepth);
	}

	if (depth->maxDepth < 0.0f || depth->maxDepth > 1.0f) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<XrCompositionLayerDepthInfoKHR>.maxDepth) "
		                 "%f must be in [0.0,1.0]",
		                 layer_index, i, depth->maxDepth);
	}

	if (depth->minDepth > depth->maxDepth) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<XrCompositionLayerDepthInfoKHR>.minDepth) "
		                 "%f must be <= maxDepth %f ",
		                 layer_index, i, depth->minDepth, depth->maxDepth);
	}

	if (depth->nearZ == depth->farZ) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->views[%i]->next<XrCompositionLayerDepthInfoKHR>.nearZ) %f "
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
		                 "(frameEndInfo->layers[%u]->viewCount == %u) must be 2 for projection layers and the "
		                 "current view configuration",
		                 layer_index, proj->viewCount);
	}

	// number of depth layers must be 0 or proj->viewCount
	uint32_t depth_layer_count = 0;

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

		if (sc->released.index >= (int)sc->swapchain->image_count) {
			return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
			                 "(frameEndInfo->layers[%u]->views[%i].subImage."
			                 "swapchain) internal image index out of bounds",
			                 layer_index, i);
		}

		if (sc->array_layer_count <= view->subImage.imageArrayIndex) {
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
			                 "(frameEndInfo->layers[%u]->views[%i]->subImage."
			                 "imageArrayIndex == %u) Invalid swapchain array "
			                 "index for projection layer (%u).",
			                 layer_index, i, view->subImage.imageArrayIndex, sc->array_layer_count);
		}

		if (sc->face_count != 1) {
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
			                 "(frameEndInfo->layers[%u]->views[%i]->subImage.swapchain) Invalid swapchain "
			                 "face count (expected 1, got %u)",
			                 layer_index, i, sc->face_count);
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
			depth_layer_count++;
		}
#endif // XRT_FEATURE_OPENXR_LAYER_DEPTH
	}

#ifdef XRT_FEATURE_OPENXR_LAYER_DEPTH
	if (depth_layer_count > 0 && depth_layer_count != proj->viewCount) {
		return oxr_error(
		    log, XR_ERROR_VALIDATION_FAILURE,
		    "(frameEndInfo->layers[%u] projection layer must have %u depth layers or none, but has: %u)",
		    layer_index, proj->viewCount, depth_layer_count);
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
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) swapchain is NULL!", layer_index);
	}

	XrResult ret = verify_space(log, layer_index, cube->space);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (!math_quat_validate_within_1_percent((struct xrt_quat *)&cube->orientation)) {
		const XrQuaternionf *q = &cube->orientation;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.orientation == {%f %f %f %f}) is not a valid quat",
		                 layer_index, q->x, q->y, q->z, q->w);
	}

	if (sc->array_layer_count <= cube->imageArrayIndex) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->imageArrayIndex == %u) Invalid swapchain array index for "
		                 "cube layer (%u).",
		                 layer_index, cube->imageArrayIndex, sc->array_layer_count);
	}

	if (sc->face_count != 6) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) Invalid swapchain face count "
		                 "(expected 6, got %u)",
		                 layer_index, sc->face_count);
	}

	if (!sc->released.yes) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->swapchain) swapchain has not been released!", layer_index);
	}

	if (sc->released.index >= (int)sc->swapchain->image_count) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) internal image index out of bounds",
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
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) swapchain is NULL!", layer_index);
	}

	XrResult ret = verify_space(log, layer_index, cylinder->space);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (!math_quat_validate_within_1_percent((struct xrt_quat *)&cylinder->pose.orientation)) {
		const XrQuaternionf *q = &cylinder->pose.orientation;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.orientation == {%f %f %f %f}) is not a valid quat",
		                 layer_index, q->x, q->y, q->z, q->w);
	}

	if (!math_vec3_validate((struct xrt_vec3 *)&cylinder->pose.position)) {
		const XrVector3f *p = &cylinder->pose.position;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.position == {%f %f %f}) is not valid", layer_index,
		                 p->x, p->y, p->z);
	}

	if (sc->array_layer_count <= cylinder->subImage.imageArrayIndex) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.imageArrayIndex == %u) Invalid swapchain array "
		                 "index for cylinder layer (%u).",
		                 layer_index, cylinder->subImage.imageArrayIndex, sc->array_layer_count);
	}

	if (sc->face_count != 1) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) Invalid swapchain face count "
		                 "(expected 1, got %u)",
		                 layer_index, sc->face_count);
	}

	if (!sc->released.yes) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) swapchain has not been released!",
		                 layer_index);
	}

	if (sc->released.index >= (int)sc->swapchain->image_count) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) internal image index out of bounds",
		                 layer_index);
	}

	if (is_rect_neg(&cylinder->subImage.imageRect)) {
		return oxr_error(
		    log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		    "(frameEndInfo->layers[%u]->subImage.imageRect.offset == {%i, %i}) has negative component(s)",
		    layer_index, cylinder->subImage.imageRect.offset.x, cylinder->subImage.imageRect.offset.y);
	}

	if (is_rect_out_of_bounds(&cylinder->subImage.imageRect, sc)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.imageRect == {{%i, %i}, {%u, %u}}) imageRect out "
		                 "of image bounds (%u, %u)",
		                 layer_index, cylinder->subImage.imageRect.offset.x,
		                 cylinder->subImage.imageRect.offset.y, cylinder->subImage.imageRect.extent.width,
		                 cylinder->subImage.imageRect.extent.height, sc->width, sc->height);
	}

	if (cylinder->radius < 0.f) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->radius == %f) radius can not be negative", layer_index,
		                 cylinder->radius);
	}

	if (cylinder->centralAngle < 0.f || cylinder->centralAngle > (M_PI * 2)) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->centralAngle == %f) centralAngle out of bounds",
		                 layer_index, cylinder->centralAngle);
	}

	if (cylinder->aspectRatio <= 0.f) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->aspectRatio == %f) aspectRatio out of bounds", layer_index,
		                 cylinder->aspectRatio);
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
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) swapchain is NULL!", layer_index);
	}

	XrResult ret = verify_space(log, layer_index, equirect->space);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (!math_quat_validate_within_1_percent((struct xrt_quat *)&equirect->pose.orientation)) {
		const XrQuaternionf *q = &equirect->pose.orientation;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.orientation == {%f %f %f %f}) is not a valid quat",
		                 layer_index, q->x, q->y, q->z, q->w);
	}

	if (!math_vec3_validate((struct xrt_vec3 *)&equirect->pose.position)) {
		const XrVector3f *p = &equirect->pose.position;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.position == {%f %f %f}) is not valid", layer_index,
		                 p->x, p->y, p->z);
	}

	if (sc->array_layer_count <= equirect->subImage.imageArrayIndex) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.imageArrayIndex == %u) Invalid swapchain array "
		                 "index for equirect layer (%u).",
		                 layer_index, equirect->subImage.imageArrayIndex, sc->array_layer_count);
	}

	if (sc->face_count != 1) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) Invalid swapchain face count "
		                 "(expected 1, got %u)",
		                 layer_index, sc->face_count);
	}

	if (!sc->released.yes) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) swapchain has not been released!",
		                 layer_index);
	}

	if (sc->released.index >= (int)sc->swapchain->image_count) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) internal image index out of bounds",
		                 layer_index);
	}

	if (is_rect_neg(&equirect->subImage.imageRect)) {
		return oxr_error(
		    log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		    "(frameEndInfo->layers[%u]->subImage.imageRect.offset == {%i, %i}) has negative component(s)",
		    layer_index, equirect->subImage.imageRect.offset.x, equirect->subImage.imageRect.offset.y);
	}

	if (is_rect_out_of_bounds(&equirect->subImage.imageRect, sc)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.imageRect == {{%i, %i}, {%u, %u}}) imageRect out "
		                 "of image bounds (%u, %u)",
		                 layer_index, equirect->subImage.imageRect.offset.x,
		                 equirect->subImage.imageRect.offset.y, equirect->subImage.imageRect.extent.width,
		                 equirect->subImage.imageRect.extent.height, sc->width, sc->height);
	}

	if (equirect->radius < .0f) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->radius == %f) radius out of bounds", layer_index,
		                 equirect->radius);
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
	                 "(frameEndInfo->layers[%u]->type) layer type XrCompositionLayerEquirect2KHR not supported",
	                 layer_index);
#else
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, equirect->subImage.swapchain);

	if (sc == NULL) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) swapchain is NULL!", layer_index);
	}

	XrResult ret = verify_space(log, layer_index, equirect->space);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (!math_quat_validate_within_1_percent((struct xrt_quat *)&equirect->pose.orientation)) {
		const XrQuaternionf *q = &equirect->pose.orientation;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.orientation == {%f %f %f %f}) is not a valid quat",
		                 layer_index, q->x, q->y, q->z, q->w);
	}

	if (!math_vec3_validate((struct xrt_vec3 *)&equirect->pose.position)) {
		const XrVector3f *p = &equirect->pose.position;
		return oxr_error(log, XR_ERROR_POSE_INVALID,
		                 "(frameEndInfo->layers[%u]->pose.position == {%f %f %f}) is not valid", layer_index,
		                 p->x, p->y, p->z);
	}

	if (sc->array_layer_count <= equirect->subImage.imageArrayIndex) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.imageArrayIndex == %u) Invalid swapchain array "
		                 "index for equirect layer (%u).",
		                 layer_index, equirect->subImage.imageArrayIndex, sc->array_layer_count);
	}

	if (sc->face_count != 1) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) Invalid swapchain face count "
		                 "(expected 1, got %u)",
		                 layer_index, sc->face_count);
	}

	if (!sc->released.yes) {
		return oxr_error(log, XR_ERROR_LAYER_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) swapchain has not been released!",
		                 layer_index);
	}

	if (sc->released.index >= (int)sc->swapchain->image_count) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(frameEndInfo->layers[%u]->subImage.swapchain) internal image index out of bounds",
		                 layer_index);
	}

	if (is_rect_neg(&equirect->subImage.imageRect)) {
		return oxr_error(
		    log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		    "(frameEndInfo->layers[%u]->subImage.imageRect.offset == {%i, %i}) has negative component(s)",
		    layer_index, equirect->subImage.imageRect.offset.x, equirect->subImage.imageRect.offset.y);
	}

	if (is_rect_out_of_bounds(&equirect->subImage.imageRect, sc)) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_RECT_INVALID,
		                 "(frameEndInfo->layers[%u]->subImage.imageRect == {{%i, %i}, {%u, %u}}) imageRect out "
		                 "of image bounds (%u, %u)",
		                 layer_index, equirect->subImage.imageRect.offset.x,
		                 equirect->subImage.imageRect.offset.y, equirect->subImage.imageRect.extent.width,
		                 equirect->subImage.imageRect.extent.height, sc->width, sc->height);
	}

	if (equirect->centralHorizontalAngle < .0f) {
		return oxr_error(
		    log, XR_ERROR_VALIDATION_FAILURE,
		    "(frameEndInfo->layers[%u]->centralHorizontalAngle == %f) centralHorizontalAngle out of bounds",
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


/*
 *
 * Submit functions.
 *
 */

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

	/*
	 * poses in view space are already in the space the compositor expects
	 */
	if (spc->space_type == OXR_SPACE_TYPE_REFERENCE_VIEW) {
		struct xrt_space_relation rel;
		struct xrt_relation_chain xrc = {0};
		m_relation_chain_push_pose(&xrc, &pose);
		m_relation_chain_push_pose_if_not_identity(&xrc, &spc->pose);
		m_relation_chain_resolve(&xrc, &rel);
		*out_pose = rel.pose;
		return true;
	}

	struct xrt_space_relation rel;
	if (!oxr_space_pure_pose_from_space(log, timestamp, &pose, spc, &rel)) {
		return false;
	}


	// The compositor doesn't know about tracking origins, transform into the "raw" HMD tracking space.
	struct xrt_device *head_xdev = GET_XDEV_BY_ROLE(sess->sys, head);
	struct xrt_relation_chain xrc = {0};
	m_relation_chain_push_relation(&xrc, &rel);
	m_relation_chain_push_inverted_pose_if_not_identity(&xrc, &head_xdev->tracking_origin->offset);
	m_relation_chain_resolve(&xrc, &rel);

	*out_pose = rel.pose;

	return true;
}

static XrResult
submit_quad_layer(struct oxr_session *sess,
                  struct xrt_compositor *xc,
                  struct oxr_logger *log,
                  XrCompositionLayerQuad *quad,
                  struct xrt_device *head,
                  struct xrt_pose *inv_offset,
                  uint64_t oxr_timestamp,
                  uint64_t xrt_timestamp)
{
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, quad->subImage.swapchain);
	struct oxr_space *spc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, quad->space);

	enum xrt_layer_composition_flags flags = convert_layer_flags(quad->layerFlags);

	struct xrt_pose *pose_ptr = (struct xrt_pose *)&quad->pose;

	struct xrt_pose pose;
	if (!handle_space(log, sess, spc, pose_ptr, inv_offset, oxr_timestamp, &pose)) {
		return XR_SUCCESS;
	}

	if (spc->space_type == OXR_SPACE_TYPE_REFERENCE_VIEW) {
		flags |= XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT;
	}

	struct xrt_layer_data data;
	U_ZERO(&data);
	data.type = XRT_LAYER_QUAD;
	data.name = XRT_INPUT_GENERIC_HEAD_POSE;
	data.timestamp = xrt_timestamp;
	data.flags = flags;

	struct xrt_vec2 *size = (struct xrt_vec2 *)&quad->size;

	data.quad.visibility = convert_eye_visibility(quad->eyeVisibility);
	data.quad.pose = pose;
	data.quad.size = *size;
	fill_in_sub_image(sc, &quad->subImage, &data.quad.sub);

	CALL_CHK(xrt_comp_layer_quad(xc, head, sc->swapchain, &data));

	return XR_SUCCESS;
}

static XrResult
submit_projection_layer(struct oxr_session *sess,
                        struct xrt_compositor *xc,
                        struct oxr_logger *log,
                        XrCompositionLayerProjection *proj,
                        struct xrt_device *head,
                        struct xrt_pose *inv_offset,
                        uint64_t oxr_timestamp,
                        uint64_t xrt_timestamp)
{
	struct oxr_space *spc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, proj->space);
	struct oxr_swapchain *d_scs[2] = {NULL, NULL};
	struct oxr_swapchain *scs[2];
	struct xrt_pose *pose_ptr;
	struct xrt_pose pose[2];

	enum xrt_layer_composition_flags flags = convert_layer_flags(proj->layerFlags);

	uint32_t swapchain_count = ARRAY_SIZE(scs);
	for (uint32_t i = 0; i < swapchain_count; i++) {
		scs[i] = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, proj->views[i].subImage.swapchain);
		pose_ptr = (struct xrt_pose *)&proj->views[i].pose;

		if (!handle_space(log, sess, spc, pose_ptr, inv_offset, oxr_timestamp, &pose[i])) {
			return XR_SUCCESS;
		}
	}

	if (spc->space_type == OXR_SPACE_TYPE_REFERENCE_VIEW) {
		flags |= XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT;
	}

	struct xrt_fov *l_fov = (struct xrt_fov *)&proj->views[0].fov;
	struct xrt_fov *r_fov = (struct xrt_fov *)&proj->views[1].fov;

	struct xrt_layer_data data;
	U_ZERO(&data);
	data.type = XRT_LAYER_STEREO_PROJECTION;
	data.name = XRT_INPUT_GENERIC_HEAD_POSE;
	data.timestamp = xrt_timestamp;
	data.flags = flags;
	data.stereo.l.fov = *l_fov;
	data.stereo.l.pose = pose[0];
	data.stereo.r.fov = *r_fov;
	data.stereo.r.pose = pose[1];
	fill_in_sub_image(scs[0], &proj->views[0].subImage, &data.stereo.l.sub);
	fill_in_sub_image(scs[1], &proj->views[1].subImage, &data.stereo.r.sub);

#ifdef XRT_FEATURE_OPENXR_LAYER_DEPTH
	const XrCompositionLayerDepthInfoKHR *d_l = OXR_GET_INPUT_FROM_CHAIN(
	    &proj->views[0], XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR, XrCompositionLayerDepthInfoKHR);
	if (d_l) {
		data.stereo_depth.l_d.far_z = d_l->farZ;
		data.stereo_depth.l_d.near_z = d_l->nearZ;
		data.stereo_depth.l_d.max_depth = d_l->maxDepth;
		data.stereo_depth.l_d.min_depth = d_l->minDepth;

		struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, d_l->subImage.swapchain);

		fill_in_sub_image(sc, &d_l->subImage, &data.stereo_depth.l_d.sub);

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

		fill_in_sub_image(sc, &d_r->subImage, &data.stereo_depth.r_d.sub);

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

static XrResult
submit_cube_layer(struct oxr_session *sess,
                  struct xrt_compositor *xc,
                  struct oxr_logger *log,
                  const XrCompositionLayerCubeKHR *cube,
                  struct xrt_device *head,
                  struct xrt_pose *inv_offset,
                  uint64_t oxr_timestamp,
                  uint64_t xrt_timestamp)
{
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, cube->swapchain);
	struct oxr_space *spc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, cube->space);

	struct xrt_layer_data data = {0};

	data.type = XRT_LAYER_CUBE;
	data.name = XRT_INPUT_GENERIC_HEAD_POSE;
	data.timestamp = xrt_timestamp;
	data.flags = convert_layer_flags(cube->layerFlags);

	if (spc->space_type == OXR_SPACE_TYPE_REFERENCE_VIEW) {
		data.flags |= XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT;
	}

	data.cube.visibility = convert_eye_visibility(cube->eyeVisibility);

	data.cube.sub.image_index = sc->released.index;
	data.cube.sub.array_index = cube->imageArrayIndex;

	struct xrt_pose pose = {
	    .orientation =
	        {
	            .x = cube->orientation.x,
	            .y = cube->orientation.y,
	            .z = cube->orientation.z,
	            .w = cube->orientation.w,
	        },
	    .position = XRT_VEC3_ZERO,
	};

	if (!handle_space(log, sess, spc, &pose, inv_offset, oxr_timestamp, &data.cube.pose)) {
		return XR_SUCCESS;
	}

	CALL_CHK(xrt_comp_layer_cube(xc, head, sc->swapchain, &data));

	return XR_SUCCESS;
}

static XrResult
submit_cylinder_layer(struct oxr_session *sess,
                      struct xrt_compositor *xc,
                      struct oxr_logger *log,
                      const XrCompositionLayerCylinderKHR *cylinder,
                      struct xrt_device *head,
                      struct xrt_pose *inv_offset,
                      uint64_t oxr_timestamp,
                      uint64_t xrt_timestamp)
{
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, cylinder->subImage.swapchain);
	struct oxr_space *spc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, cylinder->space);

	enum xrt_layer_composition_flags flags = convert_layer_flags(cylinder->layerFlags);
	enum xrt_layer_eye_visibility visibility = convert_eye_visibility(cylinder->eyeVisibility);

	struct xrt_pose *pose_ptr = (struct xrt_pose *)&cylinder->pose;

	struct xrt_pose pose;
	if (!handle_space(log, sess, spc, pose_ptr, inv_offset, oxr_timestamp, &pose)) {
		return XR_SUCCESS;
	}

	if (spc->space_type == OXR_SPACE_TYPE_REFERENCE_VIEW) {
		flags |= XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT;
	}

	struct xrt_layer_data data;
	U_ZERO(&data);
	data.type = XRT_LAYER_CYLINDER;
	data.name = XRT_INPUT_GENERIC_HEAD_POSE;
	data.timestamp = xrt_timestamp;
	data.flags = flags;

	data.cylinder.visibility = visibility;
	data.cylinder.pose = pose;
	data.cylinder.radius = cylinder->radius;
	data.cylinder.central_angle = cylinder->centralAngle;
	data.cylinder.aspect_ratio = cylinder->aspectRatio;
	fill_in_sub_image(sc, &cylinder->subImage, &data.cylinder.sub);

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
                       uint64_t oxr_timestamp,
                       uint64_t xrt_timestamp)
{
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, equirect->subImage.swapchain);
	struct oxr_space *spc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, equirect->space);

	enum xrt_layer_composition_flags flags = convert_layer_flags(equirect->layerFlags);

	struct xrt_pose *pose_ptr = (struct xrt_pose *)&equirect->pose;

	struct xrt_pose pose;
	if (!handle_space(log, sess, spc, pose_ptr, inv_offset, oxr_timestamp, &pose)) {
		return XR_SUCCESS;
	}

	if (spc->space_type == OXR_SPACE_TYPE_REFERENCE_VIEW) {
		flags |= XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT;
	}

	struct xrt_layer_data data;
	U_ZERO(&data);
	data.type = XRT_LAYER_EQUIRECT1;
	data.name = XRT_INPUT_GENERIC_HEAD_POSE;
	data.timestamp = xrt_timestamp;
	data.flags = flags;
	data.equirect1.visibility = convert_eye_visibility(equirect->eyeVisibility);
	data.equirect1.pose = pose;
	data.equirect1.radius = equirect->radius;
	fill_in_sub_image(sc, &equirect->subImage, &data.equirect1.sub);


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
                       uint64_t oxr_timestamp,
                       uint64_t xrt_timestamp)
{
	struct oxr_swapchain *sc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_swapchain *, equirect->subImage.swapchain);
	struct oxr_space *spc = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_space *, equirect->space);

	enum xrt_layer_composition_flags flags = convert_layer_flags(equirect->layerFlags);

	struct xrt_pose *pose_ptr = (struct xrt_pose *)&equirect->pose;

	struct xrt_pose pose;
	if (!handle_space(log, sess, spc, pose_ptr, inv_offset, oxr_timestamp, &pose)) {
		return XR_SUCCESS;
	}

	if (spc->space_type == OXR_SPACE_TYPE_REFERENCE_VIEW) {
		flags |= XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT;
	}

	struct xrt_layer_data data;
	U_ZERO(&data);
	data.type = XRT_LAYER_EQUIRECT2;
	data.name = XRT_INPUT_GENERIC_HEAD_POSE;
	data.timestamp = xrt_timestamp;
	data.flags = flags;
	data.equirect2.visibility = convert_eye_visibility(equirect->eyeVisibility);
	data.equirect2.pose = pose;
	data.equirect2.radius = equirect->radius;
	data.equirect2.central_horizontal_angle = equirect->centralHorizontalAngle;
	data.equirect2.upper_vertical_angle = equirect->upperVerticalAngle;
	data.equirect2.lower_vertical_angle = equirect->lowerVerticalAngle;
	fill_in_sub_image(sc, &equirect->subImage, &data.equirect2.sub);

	CALL_CHK(xrt_comp_layer_equirect2(xc, head, sc->swapchain, &data));

	return XR_SUCCESS;
}

XrResult
oxr_session_frame_end(struct oxr_logger *log, struct oxr_session *sess, const XrFrameEndInfo *frameEndInfo)
{
	/*
	 * Session state and call order.
	 */

	if (!is_session_running(sess)) {
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

	int64_t xrt_display_time_ns =
	    time_state_ts_to_monotonic_ns(sess->sys->inst->timekeeping, frameEndInfo->displayTime);
	if (sess->frame_timing_spew) {
		oxr_log(log, "End frame at %8.3fms with display time %8.3fms", ts_ms(sess),
		        ns_to_ms(xrt_display_time_ns));
	}

	struct xrt_compositor *xc = sess->compositor;


	/*
	 * Early out for headless sessions.
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
	 * XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED must always be reported, even with 0 layers.
	 */

	enum xrt_blend_mode blend_mode = convert_blend_mode(frameEndInfo->environmentBlendMode);
	struct xrt_device *xdev = GET_XDEV_BY_ROLE(sess->sys, head);

	if (!u_verify_blend_mode_valid(blend_mode)) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(frameEndInfo->environmentBlendMode == 0x%08x) unknown environment blend mode",
		                 frameEndInfo->environmentBlendMode);
	}

	if (!u_verify_blend_mode_supported(xdev, blend_mode)) {
		//! @todo Make integer print to string.
		return oxr_error(log, XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED,
		                 "(frameEndInfo->environmentBlendMode == %u) is not supported",
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
			                 "(frameEndInfo->layers[%u] == NULL) layer can not be null", i);
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
			                 "(frameEndInfo->layers[%u]->type) layer type not supported (%u)", i,
			                 layer->type);
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

	CALL_CHK(xrt_comp_layer_begin(xc, sess->frame_id.begun, xrt_display_time_ns, blend_mode));

	for (uint32_t i = 0; i < frameEndInfo->layerCount; i++) {
		const XrCompositionLayerBaseHeader *layer = frameEndInfo->layers[i];
		assert(layer != NULL);

		switch (layer->type) {
		case XR_TYPE_COMPOSITION_LAYER_PROJECTION:
			submit_projection_layer(sess, xc, log, (XrCompositionLayerProjection *)layer, xdev, &inv_offset,
			                        frameEndInfo->displayTime, xrt_display_time_ns);
			break;
		case XR_TYPE_COMPOSITION_LAYER_QUAD:
			submit_quad_layer(sess, xc, log, (XrCompositionLayerQuad *)layer, xdev, &inv_offset,
			                  frameEndInfo->displayTime, xrt_display_time_ns);
			break;
		case XR_TYPE_COMPOSITION_LAYER_CUBE_KHR:
			submit_cube_layer(sess, xc, log, (XrCompositionLayerCubeKHR *)layer, xdev, &inv_offset,
			                  frameEndInfo->displayTime, xrt_display_time_ns);
			break;
		case XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR:
			submit_cylinder_layer(sess, xc, log, (XrCompositionLayerCylinderKHR *)layer, xdev, &inv_offset,
			                      frameEndInfo->displayTime, xrt_display_time_ns);
			break;
		case XR_TYPE_COMPOSITION_LAYER_EQUIRECT_KHR:
			submit_equirect1_layer(sess, xc, log, (XrCompositionLayerEquirectKHR *)layer, xdev, &inv_offset,
			                       frameEndInfo->displayTime, xrt_display_time_ns);
			break;
		case XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR:
			submit_equirect2_layer(sess, xc, log, (XrCompositionLayerEquirect2KHR *)layer, xdev,
			                       &inv_offset, frameEndInfo->displayTime, xrt_display_time_ns);
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
