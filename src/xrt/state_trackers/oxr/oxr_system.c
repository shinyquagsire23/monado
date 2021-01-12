// Copyright 2018-2020, Collabora, Ltd.
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
#include "oxr_chain.h"


DEBUG_GET_ONCE_NUM_OPTION(scale_percentage, "OXR_VIEWPORT_SCALE_PERCENTAGE", 100)


static bool
oxr_system_matches(struct oxr_logger *log, struct oxr_system *sys, XrFormFactor form_factor)
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
		return oxr_error(log, XR_ERROR_FORM_FACTOR_UNSUPPORTED,
		                 "(getInfo->formFactor) no system available (given: %i)", form_factor);
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
oxr_system_verify_id(struct oxr_logger *log, const struct oxr_instance *inst, XrSystemId systemId)
{
	if (systemId != 1) {
		return oxr_error(log, XR_ERROR_SYSTEM_INVALID, "Invalid system %" PRIu64, systemId);
	}
	return XR_SUCCESS;
}

XrResult
oxr_system_get_by_id(struct oxr_logger *log, struct oxr_instance *inst, XrSystemId systemId, struct oxr_system **system)
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
oxr_system_fill_in(struct oxr_logger *log, struct oxr_instance *inst, XrSystemId systemId, struct oxr_system *sys)
{
	//! @todo handle other subaction paths?

	sys->inst = inst;
	sys->systemId = systemId;
	sys->form_factor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	sys->view_config_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

	sys->vulkan_enable2_instance = VK_NULL_HANDLE;
	sys->vulkan_enable2_physical_device = VK_NULL_HANDLE;

	// Headless.
	if (sys->xsysc == NULL) {
		sys->blend_modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		sys->num_blend_modes = 1;
		return XR_SUCCESS;
	}

	double scale = debug_get_num_option_scale_percentage() / 100.0;
	if (scale > 2.0) {
		scale = 2.0;
		oxr_log(log, "Clamped scale to 200%%\n");
	}

	struct xrt_system_compositor_info *info = &sys->xsysc->info;

	uint32_t w0 = (uint32_t)(info->views[0].recommended.width_pixels * scale);
	uint32_t h0 = (uint32_t)(info->views[0].recommended.height_pixels * scale);
	uint32_t w1 = (uint32_t)(info->views[1].recommended.width_pixels * scale);
	uint32_t h1 = (uint32_t)(info->views[1].recommended.height_pixels * scale);

	uint32_t w0_2 = info->views[0].max.width_pixels;
	uint32_t h0_2 = info->views[0].max.height_pixels;
	uint32_t w1_2 = info->views[1].max.width_pixels;
	uint32_t h1_2 = info->views[1].max.height_pixels;

#define imin(a, b) (a < b ? a : b)

	w0 = imin(w0, w0_2);
	h0 = imin(h0, h0_2);
	w1 = imin(w1, w1_2);
	h1 = imin(h1, h1_2);

#undef imin

	// clang-format off
	sys->views[0].recommendedImageRectWidth       = w0;
	sys->views[0].maxImageRectWidth               = w0_2;
	sys->views[0].recommendedImageRectHeight      = h0;
	sys->views[0].maxImageRectHeight              = h0_2;
	sys->views[0].recommendedSwapchainSampleCount = info->views[0].recommended.sample_count;
	sys->views[0].maxSwapchainSampleCount         = info->views[0].max.sample_count;

	sys->views[1].recommendedImageRectWidth       = w1;
	sys->views[1].maxImageRectWidth               = w1_2;
	sys->views[1].recommendedImageRectHeight      = h1;
	sys->views[1].maxImageRectHeight              = h1_2;
	sys->views[1].recommendedSwapchainSampleCount = info->views[1].recommended.sample_count;
	sys->views[1].maxSwapchainSampleCount         = info->views[1].max.sample_count;
	// clang-format on

	struct xrt_device *head = GET_XDEV_BY_ROLE(sys, head);

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

bool
oxr_system_get_hand_tracking_support(struct oxr_logger *log, struct oxr_instance *inst)
{
	struct oxr_system *sys = &inst->system;
	struct xrt_device *left = GET_XDEV_BY_ROLE(sys, left);
	struct xrt_device *right = GET_XDEV_BY_ROLE(sys, right);

	bool left_supported = left && left->hand_tracking_supported;
	bool right_supported = right && right->hand_tracking_supported;
	return left_supported || right_supported;
}

XrResult
oxr_system_get_properties(struct oxr_logger *log, struct oxr_system *sys, XrSystemProperties *properties)
{
	properties->vendorId = 42;
	properties->systemId = sys->systemId;

	struct xrt_device *xdev = GET_XDEV_BY_ROLE(sys, head);

	// The magical 247 number, is to silence warnings.
	snprintf(properties->systemName, XR_MAX_SYSTEM_NAME_SIZE, "Monado: %.*s", 247, xdev->str);

	// Get from compositor.
	struct xrt_system_compositor_info *info = &sys->xsysc->info;

	properties->graphicsProperties.maxLayerCount = info->max_layers;
	properties->graphicsProperties.maxSwapchainImageWidth = 1024 * 16;
	properties->graphicsProperties.maxSwapchainImageHeight = 1024 * 16;
	properties->trackingProperties.orientationTracking = xdev->orientation_tracking_supported;
	properties->trackingProperties.positionTracking = xdev->position_tracking_supported;

	XrSystemHandTrackingPropertiesEXT *hand_tracking_props = OXR_GET_OUTPUT_FROM_CHAIN(
	    properties, XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT, XrSystemHandTrackingPropertiesEXT);

	if (hand_tracking_props) {
		if (!sys->inst->extensions.EXT_hand_tracking) {
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE, "XR_EXT_hand_tracking is not enabled.");
		}
		hand_tracking_props->supportsHandTracking = oxr_system_get_hand_tracking_support(log, sys->inst);
	}

	return XR_SUCCESS;
}

XrResult
oxr_system_enumerate_view_confs(struct oxr_logger *log,
                                struct oxr_system *sys,
                                uint32_t viewConfigurationTypeCapacityInput,
                                uint32_t *viewConfigurationTypeCountOutput,
                                XrViewConfigurationType *viewConfigurationTypes)
{
	OXR_TWO_CALL_HELPER(log, viewConfigurationTypeCapacityInput, viewConfigurationTypeCountOutput,
	                    viewConfigurationTypes, 1, &sys->view_config_type, XR_SUCCESS);
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
	OXR_TWO_CALL_HELPER(log, environmentBlendModeCapacityInput, environmentBlendModeCountOutput,
	                    environmentBlendModes, sys->num_blend_modes, sys->blend_modes, XR_SUCCESS);
}

XrResult
oxr_system_get_view_conf_properties(struct oxr_logger *log,
                                    struct oxr_system *sys,
                                    XrViewConfigurationType viewConfigurationType,
                                    XrViewConfigurationProperties *configurationProperties)
{
	if (viewConfigurationType != sys->view_config_type) {
		return oxr_error(log, XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED, "Invalid view configuration type");
	}

	configurationProperties->viewConfigurationType = sys->view_config_type;
	configurationProperties->fovMutable = XR_FALSE;

	return XR_SUCCESS;
}

static void
view_configuration_view_fill_in(XrViewConfigurationView *target_view, XrViewConfigurationView *source_view)
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
oxr_system_enumerate_view_conf_views(struct oxr_logger *log,
                                     struct oxr_system *sys,
                                     XrViewConfigurationType viewConfigurationType,
                                     uint32_t viewCapacityInput,
                                     uint32_t *viewCountOutput,
                                     XrViewConfigurationView *views)
{
	if (viewConfigurationType != sys->view_config_type) {
		return oxr_error(log, XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED, "Invalid view configuration type");
	}

	OXR_TWO_CALL_FILL_IN_HELPER(log, viewCapacityInput, viewCountOutput, views, 2, view_configuration_view_fill_in,
	                            sys->views, XR_SUCCESS);
}
