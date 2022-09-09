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
#include "util/u_verify.h"

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
                  uint32_t system_count,
                  XrFormFactor form_factor,
                  struct oxr_system **out_selected)
{
	if (system_count == 0) {
		return oxr_error(log, XR_ERROR_FORM_FACTOR_UNSUPPORTED,
		                 "(getInfo->formFactor) no system available (given: %i)", form_factor);
	}

	struct oxr_system *selected = NULL;
	for (uint32_t i = 0; i < system_count; i++) {
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

#ifdef XR_USE_GRAPHICS_API_VULKAN
	sys->vulkan_enable2_instance = VK_NULL_HANDLE;
	sys->suggested_vulkan_physical_device = VK_NULL_HANDLE;
#endif
#if defined(XR_USE_GRAPHICS_API_D3D11) || defined(XR_USE_GRAPHICS_API_D3D12)
	U_ZERO(&(sys->suggested_d3d_luid));
	sys->suggested_d3d_luid_valid = false;
#endif

	// Headless.
	if (sys->xsysc == NULL) {
		sys->blend_modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		sys->blend_mode_count = 1;
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

	assert(info->supported_blend_mode_count <= ARRAY_SIZE(sys->blend_modes));
	assert(info->supported_blend_mode_count != 0);

	for (uint8_t i = 0; i < info->supported_blend_mode_count; i++) {
		assert(u_verify_blend_mode_valid(info->supported_blend_modes[i]));
		sys->blend_modes[i] = (XrEnvironmentBlendMode)info->supported_blend_modes[i];
	}
	sys->blend_mode_count = (uint32_t)info->supported_blend_mode_count;

	return XR_SUCCESS;
}

bool
oxr_system_get_hand_tracking_support(struct oxr_logger *log, struct oxr_instance *inst)
{
	struct oxr_system *sys = &inst->system;
	struct xrt_device *ht_left = GET_XDEV_BY_ROLE(sys, hand_tracking.left);
	struct xrt_device *ht_right = GET_XDEV_BY_ROLE(sys, hand_tracking.right);

	bool left_supported = ht_left && ht_left->hand_tracking_supported;
	bool right_supported = ht_right && ht_right->hand_tracking_supported;

	return left_supported || right_supported;
}

bool
oxr_system_get_force_feedback_support(struct oxr_logger *log, struct oxr_instance *inst)
{
	struct oxr_system *sys = &inst->system;
	struct xrt_device *ffb_left = GET_XDEV_BY_ROLE(sys, hand_tracking.left);
	struct xrt_device *ffb_right = GET_XDEV_BY_ROLE(sys, hand_tracking.right);

	bool left_supported = ffb_left && ffb_left->force_feedback_supported;
	bool right_supported = ffb_right && ffb_right->force_feedback_supported;

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
	struct xrt_system_compositor_info *info = sys->xsysc ? &sys->xsysc->info : NULL;

	if (info) {
		properties->graphicsProperties.maxLayerCount = info->max_layers;
	} else {
		// probably using the headless extension, but the extension does not modify the 16 layer minimum.
		properties->graphicsProperties.maxLayerCount = 16;
	}
	properties->graphicsProperties.maxSwapchainImageWidth = 1024 * 16;
	properties->graphicsProperties.maxSwapchainImageHeight = 1024 * 16;
	properties->trackingProperties.orientationTracking = xdev->orientation_tracking_supported;
	properties->trackingProperties.positionTracking = xdev->position_tracking_supported;

	XrSystemHandTrackingPropertiesEXT *hand_tracking_props = NULL;
	// We should only be looking for extension structs if the extension has been enabled.
	if (sys->inst->extensions.EXT_hand_tracking) {
		hand_tracking_props = OXR_GET_OUTPUT_FROM_CHAIN(properties, XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT,
		                                                XrSystemHandTrackingPropertiesEXT);
	}

	if (hand_tracking_props) {
		hand_tracking_props->supportsHandTracking = oxr_system_get_hand_tracking_support(log, sys->inst);
	}

	XrSystemForceFeedbackCurlPropertiesMNDX *force_feedback_props = NULL;
	if (sys->inst->extensions.MNDX_force_feedback_curl) {
		force_feedback_props =
		    OXR_GET_OUTPUT_FROM_CHAIN(properties, XR_TYPE_SYSTEM_FORCE_FEEDBACK_CURL_PROPERTIES_MNDX,
		                              XrSystemForceFeedbackCurlPropertiesMNDX);
	}

	if (force_feedback_props) {
		force_feedback_props->supportsForceFeedbackCurl = oxr_system_get_force_feedback_support(log, sys->inst);
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
	                    environmentBlendModes, sys->blend_mode_count, sys->blend_modes, XR_SUCCESS);
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
