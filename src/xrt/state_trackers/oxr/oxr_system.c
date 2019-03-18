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
#include <assert.h>

#include "xrt/xrt_device.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"


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
	struct oxr_system *selected = NULL;
	for (uint32_t i = 0; i < num_systems; i++) {
		if (oxr_system_matches(log, systems[i], form_factor)) {
			selected = systems[i];
			break;
		}
	}

	if (selected == NULL) {
		return oxr_error(log, XR_ERROR_FORM_FACTOR_UNSUPPORTED,
		                 "(getInfo->formFactor) no matching system");
	}

	*out_selected = selected;

	return XR_SUCCESS;
}

XrResult
oxr_system_fill_in(struct oxr_logger *log,
                   struct oxr_instance *inst,
                   XrSystemId systemId,
                   struct oxr_system *sys,
                   struct xrt_device *xdev)
{
	if (xdev == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 " failed to probe device");
	}

	sys->device = xdev;
	sys->inst = inst;
	sys->systemId = systemId;

	// clang-format off
	sys->form_factor =      XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	sys->view_config_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

	sys->views[0].recommendedImageRectWidth       = xdev->views[0].display.w_pixels;
	sys->views[0].maxImageRectWidth               = xdev->views[0].display.w_pixels;
	sys->views[0].recommendedImageRectHeight      = xdev->views[0].display.h_pixels;
	sys->views[0].maxImageRectHeight              = xdev->views[0].display.h_pixels;
	sys->views[0].recommendedSwapchainSampleCount = 1;
	sys->views[0].maxSwapchainSampleCount         = 1;

	sys->views[1].recommendedImageRectWidth       = xdev->views[1].display.w_pixels;
	sys->views[1].maxImageRectWidth               = xdev->views[1].display.w_pixels;
	sys->views[1].recommendedImageRectHeight      = xdev->views[1].display.h_pixels;
	sys->views[1].maxImageRectHeight              = xdev->views[1].display.h_pixels;
	sys->views[1].recommendedSwapchainSampleCount = 1;
	sys->views[1].maxSwapchainSampleCount         = 1;

	uint32_t i = 0;
	if (xdev->blend_mode & XRT_BLEND_MODE_OPAQUE) {
		sys->blend_modes[i++] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	}
	if (xdev->blend_mode & XRT_BLEND_MODE_ADDITIVE) {
		sys->blend_modes[i++] = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
	}
	if (xdev->blend_mode & XRT_BLEND_MODE_ALPHA_BLEND) {
		sys->blend_modes[i++] = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
	}
	sys->num_blend_modes = i;

	assert(i < ARRAY_SIZE(sys->blend_modes));

	// clang-format on

	return XR_SUCCESS;
}

XrResult
oxr_system_get_properties(struct oxr_logger *log,
                          struct oxr_system *sys,
                          XrSystemProperties *properties)
{
	properties->vendorId = 42;
	properties->systemId = sys->systemId;
	properties->graphicsProperties.maxViewCount = 2;
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
	                    viewConfigurationTypes, 1, &sys->view_config_type);
}

XrResult
oxr_system_enumerate_blend_modes(struct oxr_logger *log,
                                 struct oxr_system *sys,
                                 uint32_t environmentBlendModeCapacityInput,
                                 uint32_t *environmentBlendModeCountOutput,
                                 XrEnvironmentBlendMode *environmentBlendModes)
{
	OXR_TWO_CALL_HELPER(log, environmentBlendModeCapacityInput,
	                    environmentBlendModeCountOutput,
	                    environmentBlendModes, sys->num_blend_modes,
	                    sys->blend_modes);
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
	configurationProperties->fovMutable = false;
	// clang-format on

	return XR_SUCCESS;
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

	OXR_TWO_CALL_HELPER(log, viewCapacityInput, viewCountOutput, views, 2,
	                    sys->views);
}
