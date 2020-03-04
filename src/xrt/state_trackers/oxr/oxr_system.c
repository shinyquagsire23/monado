// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds system related entrypoints.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "xrt/xrt_device.h"
#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"

// clang-format off
DEBUG_GET_ONCE_NUM_OPTION(scale_percentage, "OXR_VIEWPORT_SCALE_PERCENTAGE", 140)
// clang-format on

static inline size_t
min_size_t(size_t a, size_t b)
{
	return a < b ? a : b;
}

static bool
oxr_system_matches(struct oxr_logger *log,
                   struct oxr_system *sys,
                   XrFormFactor form_factor)
{
	return form_factor == sys->form_factor;
}

XrResult
oxr_system_select(struct oxr_logger *log,
                  struct oxr_system **systems,
                  uint32_t num_systems,
                  XrFormFactor form_factor,
                  struct oxr_system **out_selected)
{
	if (num_systems == 0) {
		return oxr_error(
		    log, XR_ERROR_FORM_FACTOR_UNSUPPORTED,
		    "(getInfo->formFactor) no system available (given: %i)",
		    form_factor);
	}

	struct oxr_system *selected = NULL;
	for (uint32_t i = 0; i < num_systems; i++) {
		if (oxr_system_matches(log, systems[i], form_factor)) {
			selected = systems[i];
			break;
		}
	}

	if (selected == NULL) {
		return oxr_error(log, XR_ERROR_FORM_FACTOR_UNSUPPORTED,
		                 "(getInfo->formFactor) no matching system "
		                 "(given: %i, first: %i)",
		                 form_factor, systems[0]->form_factor);
	}

	*out_selected = selected;

	return XR_SUCCESS;
}

XrResult
oxr_system_verify_id(struct oxr_logger *log,
                     const struct oxr_instance *inst,
                     XrSystemId systemId)
{
	if (systemId != 1) {
		return oxr_error(log, XR_ERROR_SYSTEM_INVALID,
		                 "invalid system %" PRIu64, systemId);
	}
	return XR_SUCCESS;
}

XrResult
oxr_system_get_by_id(struct oxr_logger *log,
                     struct oxr_instance *inst,
                     XrSystemId systemId,
                     struct oxr_system **system)
{
	XrResult result = oxr_system_verify_id(log, inst, systemId);
	if (result != XR_SUCCESS) {
		return result;
	}

	/* right now only have one system. */
	*system = &inst->system;

	return XR_SUCCESS;
}

XrResult
oxr_system_fill_in(struct oxr_logger *log,
                   struct oxr_instance *inst,
                   XrSystemId systemId,
                   struct oxr_system *sys,
                   struct xrt_device **xdevs,
                   size_t num_xdevs)
{
	sys->num_xdevs = min_size_t(ARRAY_SIZE(sys->xdevs), num_xdevs);

	for (uint32_t i = 0; i < sys->num_xdevs; i++) {
		sys->xdevs[i] = xdevs[i];
	}
	for (size_t i = sys->num_xdevs; i < num_xdevs; i++) {
		oxr_xdev_destroy(&xdevs[i]);
	}

	struct xrt_device *head = sys->head;
	struct xrt_device *left = sys->left;
	struct xrt_device *right = sys->right;

	if (head == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 " failed to probe device");
	}

	if (head->tracking_origin->type == XRT_TRACKING_TYPE_NONE) {
		// "nominal height" 1.6m
		head->tracking_origin->offset.position.x = 0.0f;
		head->tracking_origin->offset.position.y = 1.6f;
		head->tracking_origin->offset.position.z = 0.0f;
	}

	if (left != NULL &&
	    left->tracking_origin->type == XRT_TRACKING_TYPE_NONE) {
		left->tracking_origin->offset.position.x = -0.2f;
		left->tracking_origin->offset.position.y = 1.3f;
		left->tracking_origin->offset.position.z = -0.5f;
	}

	if (right != NULL &&
	    right->tracking_origin->type == XRT_TRACKING_TYPE_NONE) {
		right->tracking_origin->offset.position.x = 0.2f;
		right->tracking_origin->offset.position.y = 1.3f;
		right->tracking_origin->offset.position.z = -0.5f;
	}

	// clang-format off
	sys->inst             = inst;
	sys->systemId         = systemId;
	sys->form_factor      = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	sys->view_config_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

	double scale = debug_get_num_option_scale_percentage() / 100.0;
	if (scale > 2.0) {
		scale = 2.0;
		oxr_log(log, "Clamped scale to 200%%\n");
	}


	uint32_t w0 = (uint32_t)(head->hmd->views[0].display.w_pixels * scale);
	uint32_t h0 = (uint32_t)(head->hmd->views[0].display.h_pixels * scale);
	uint32_t w1 = (uint32_t)(head->hmd->views[1].display.w_pixels * scale);
	uint32_t h1 = (uint32_t)(head->hmd->views[1].display.h_pixels * scale);

	uint32_t w0_2 = head->hmd->views[0].display.w_pixels * 2;
	uint32_t h0_2 = head->hmd->views[0].display.h_pixels * 2;
	uint32_t w1_2 = head->hmd->views[1].display.w_pixels * 2;
	uint32_t h1_2 = head->hmd->views[1].display.h_pixels * 2;

	sys->views[0].recommendedImageRectWidth       = w0;
	sys->views[0].maxImageRectWidth               = w0_2;
	sys->views[0].recommendedImageRectHeight      = h0;
	sys->views[0].maxImageRectHeight              = h0_2;
	sys->views[0].recommendedSwapchainSampleCount = 1;
	sys->views[0].maxSwapchainSampleCount         = 1;

	sys->views[1].recommendedImageRectWidth       = w1;
	sys->views[1].maxImageRectWidth               = w1_2;
	sys->views[1].recommendedImageRectHeight      = h1;
	sys->views[1].maxImageRectHeight              = h1_2;
	sys->views[1].recommendedSwapchainSampleCount = 1;
	sys->views[1].maxSwapchainSampleCount         = 1;
	// clang-format on

	uint32_t i = 0;
	if (head->hmd->blend_mode & XRT_BLEND_MODE_OPAQUE) {
		sys->blend_modes[i++] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	}
	if (head->hmd->blend_mode & XRT_BLEND_MODE_ADDITIVE) {
		sys->blend_modes[i++] = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
	}
	if (head->hmd->blend_mode & XRT_BLEND_MODE_ALPHA_BLEND) {
		sys->blend_modes[i++] = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
	}
	sys->num_blend_modes = i;

	assert(i < ARRAY_SIZE(sys->blend_modes));


	return XR_SUCCESS;
}

XrResult
oxr_system_get_properties(struct oxr_logger *log,
                          struct oxr_system *sys,
                          XrSystemProperties *properties)
{
	properties->vendorId = 42;
	properties->systemId = sys->systemId;

	// The magical 247 number, is to silence warnings.
	snprintf(properties->systemName, XR_MAX_SYSTEM_NAME_SIZE,
	         "Monado: %.*s", 247, sys->head->str);

	/*!
	 * @todo conforming implementations must support at
	 * leastXR_MIN_COMPOSITION_LAYERS_SUPPORTED layers.
	 */
	properties->graphicsProperties.maxLayerCount = 1;
	properties->graphicsProperties.maxSwapchainImageWidth = 1024 * 16;
	properties->graphicsProperties.maxSwapchainImageHeight = 1024 * 16;
	properties->trackingProperties.orientationTracking = XR_TRUE;
	properties->trackingProperties.positionTracking = XR_FALSE;

	return XR_SUCCESS;
}

XrResult
oxr_system_enumerate_view_confs(struct oxr_logger *log,
                                struct oxr_system *sys,
                                uint32_t viewConfigurationTypeCapacityInput,
                                uint32_t *viewConfigurationTypeCountOutput,
                                XrViewConfigurationType *viewConfigurationTypes)
{
	OXR_TWO_CALL_HELPER(log, viewConfigurationTypeCapacityInput,
	                    viewConfigurationTypeCountOutput,
	                    viewConfigurationTypes, 1, &sys->view_config_type,
	                    XR_SUCCESS);
}

XrResult
oxr_system_enumerate_blend_modes(struct oxr_logger *log,
                                 struct oxr_system *sys,
                                 XrViewConfigurationType viewConfigurationType,
                                 uint32_t environmentBlendModeCapacityInput,
                                 uint32_t *environmentBlendModeCountOutput,
                                 XrEnvironmentBlendMode *environmentBlendModes)
{
	//! @todo Take into account viewConfigurationType
	OXR_TWO_CALL_HELPER(log, environmentBlendModeCapacityInput,
	                    environmentBlendModeCountOutput,
	                    environmentBlendModes, sys->num_blend_modes,
	                    sys->blend_modes, XR_SUCCESS);
}

XrResult
oxr_system_get_view_conf_properties(
    struct oxr_logger *log,
    struct oxr_system *sys,
    XrViewConfigurationType viewConfigurationType,
    XrViewConfigurationProperties *configurationProperties)
{
	if (viewConfigurationType != sys->view_config_type) {
		return oxr_error(log,
		                 XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED,
		                 "invalid view configuration type");
	}

	// clang-format off
	configurationProperties->viewConfigurationType = sys->view_config_type;
	configurationProperties->fovMutable = XR_FALSE;
	// clang-format on

	return XR_SUCCESS;
}

static void
view_configuration_view_fill_in(XrViewConfigurationView *target_view,
                                XrViewConfigurationView *source_view)
{
	// clang-format off
	target_view->recommendedImageRectWidth       = source_view->recommendedImageRectWidth;
	target_view->maxImageRectWidth               = source_view->maxImageRectWidth;
	target_view->recommendedImageRectHeight      = source_view->recommendedImageRectHeight;
	target_view->maxImageRectHeight              = source_view->maxImageRectHeight;
	target_view->recommendedSwapchainSampleCount = source_view->recommendedSwapchainSampleCount;
	target_view->maxSwapchainSampleCount         = source_view->maxSwapchainSampleCount;
	// clang-format on
}

XrResult
oxr_system_enumerate_view_conf_views(
    struct oxr_logger *log,
    struct oxr_system *sys,
    XrViewConfigurationType viewConfigurationType,
    uint32_t viewCapacityInput,
    uint32_t *viewCountOutput,
    XrViewConfigurationView *views)
{
	if (viewConfigurationType != sys->view_config_type) {
		return oxr_error(log,
		                 XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED,
		                 "invalid view configuration type");
	}

	OXR_TWO_CALL_FILL_IN_HELPER(log, viewCapacityInput, viewCountOutput,
	                            views, 2, view_configuration_view_fill_in,
	                            sys->views, XR_SUCCESS);
}
