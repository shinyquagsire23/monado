// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds instance related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "util/u_var.h"
#include "util/u_time.h"
#include "util/u_misc.h"
#include "util/u_debug.h"

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_prober.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_handle.h"
#include "oxr_extension_support.h"


DEBUG_GET_ONCE_BOOL_OPTION(debug_views, "OXR_DEBUG_VIEWS", false)
DEBUG_GET_ONCE_BOOL_OPTION(debug_spaces, "OXR_DEBUG_SPACES", false)
DEBUG_GET_ONCE_BOOL_OPTION(debug_bindings, "OXR_DEBUG_BINDINGS", false)
DEBUG_GET_ONCE_BOOL_OPTION(lifecycle_verbose, "OXR_LIFECYCLE_VERBOSE", false)

DEBUG_GET_ONCE_FLOAT_OPTION(lfov_left, "OXR_OVERRIDE_LFOV_LEFT", 0.0f)
DEBUG_GET_ONCE_FLOAT_OPTION(lfov_right, "OXR_OVERRIDE_LFOV_RIGHT", 0.0f)
DEBUG_GET_ONCE_FLOAT_OPTION(lfov_up, "OXR_OVERRIDE_LFOV_UP", 0.0f)
DEBUG_GET_ONCE_FLOAT_OPTION(lfov_down, "OXR_OVERRIDE_LFOV_DOWN", 0.0f)

/* ---- HACK ---- */
extern int
oxr_sdl2_hack_create(void **out_hack);

extern void
oxr_sdl2_hack_start(void *hack, struct xrt_prober *xp);

extern void
oxr_sdl2_hack_stop(void *hack);
/* ---- HACK ---- */

static inline int32_t
radtodeg_for_display(float radians)
{
	return (int32_t)(radians * 180 * M_1_PI);
}

static XrResult
oxr_instance_destroy(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	struct oxr_instance *inst = (struct oxr_instance *)hb;

	u_var_remove_root((void *)inst);

	oxr_binding_destroy_all(log, inst);
	oxr_path_destroy_all(log, inst);

	if (inst->path_store != NULL) {
		u_hashset_destroy(&inst->path_store);
	}

	for (size_t i = 0; i < inst->system.num_xdevs; i++) {
		oxr_xdev_destroy(&inst->system.xdevs[i]);
	}

	/* ---- HACK ---- */
	oxr_sdl2_hack_stop(inst->hack);
	/* ---- HACK ---- */

	xrt_prober_destroy(&inst->prober);

	time_state_destroy(inst->timekeeping);
	inst->timekeeping = NULL;

	free(inst);

	return XR_SUCCESS;
}

static void
cache_path(struct oxr_logger *log,
           struct oxr_instance *inst,
           const char *str,
           XrPath *out_path)
{
	oxr_path_get_or_create(log, inst, str, strlen(str), out_path);
}

#define NUM_XDEVS 16

XrResult
oxr_instance_create(struct oxr_logger *log,
                    const XrInstanceCreateInfo *createInfo,
                    struct oxr_instance **out_instance)
{
	struct oxr_instance *inst = NULL;
	struct xrt_device *xdevs[NUM_XDEVS] = {0};
	int h_ret, p_ret;

	OXR_ALLOCATE_HANDLE_OR_RETURN(log, inst, OXR_XR_DEBUG_INSTANCE,
	                              oxr_instance_destroy, NULL);

	inst->lifecycle_verbose = debug_get_bool_option_lifecycle_verbose();
	inst->debug_spaces = debug_get_bool_option_debug_spaces();
	inst->debug_views = debug_get_bool_option_debug_views();
	inst->debug_bindings = debug_get_bool_option_debug_bindings();

	/* ---- HACK ---- */
	oxr_sdl2_hack_create(&inst->hack);
	/* ---- HACK ---- */

	h_ret = u_hashset_create(&inst->path_store);
	if (h_ret != 0) {
		free(inst);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Failed to create hashset");
	}

	// Cache certain often looked up paths.
	// clang-format off
	cache_path(log, inst, "/user", &inst->path_cache.user);
	cache_path(log, inst, "/user/hand/head", &inst->path_cache.head);
	cache_path(log, inst, "/user/hand/left", &inst->path_cache.left);
	cache_path(log, inst, "/user/hand/right", &inst->path_cache.right);
	cache_path(log, inst, "/user/hand/gamepad", &inst->path_cache.gamepad);
	cache_path(log, inst, "/interaction_profiles/khr/simple_controller", &inst->path_cache.khr_simple_controller);
	cache_path(log, inst, "/interaction_profiles/google/daydream_controller", &inst->path_cache.google_daydream_controller);
	cache_path(log, inst, "/interaction_profiles/htc/vive_controller", &inst->path_cache.htc_vive_controller);
	cache_path(log, inst, "/interaction_profiles/htc/vive_pro", &inst->path_cache.htc_vive_pro);
	cache_path(log, inst, "/interaction_profiles/microsoft/motion_controller", &inst->path_cache.microsoft_motion_controller);
	cache_path(log, inst, "/interaction_profiles/microsoft/xbox_controller", &inst->path_cache.microsoft_xbox_controller);
	cache_path(log, inst, "/interaction_profiles/oculus/go_controller", &inst->path_cache.oculus_go_controller);
	cache_path(log, inst, "/interaction_profiles/oculus/touch_controller", &inst->path_cache.oculus_touch_controller);
	cache_path(log, inst, "/interaction_profiles/valve/index_controller", &inst->path_cache.valve_index_controller);
	cache_path(log, inst, "/interaction_profiles/mnd/ball_on_stick_controller", &inst->path_cache.mnd_ball_on_stick_controller);
	// clang-format on

	p_ret = xrt_prober_create(&inst->prober);
	if (p_ret != 0) {
		xrt_prober_destroy(&inst->prober);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Failed to create prober");
	}

	p_ret = xrt_prober_probe(inst->prober);
	if (p_ret != 0) {
		xrt_prober_destroy(&inst->prober);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Failed to probe device(s)");
	}

	p_ret = xrt_prober_select(inst->prober, xdevs, NUM_XDEVS);
	if (p_ret != 0) {
		xrt_prober_destroy(&inst->prober);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Failed to select device");
	}

	struct xrt_device *dev = xdevs[0];

	const float left_override = debug_get_float_option_lfov_left();
	if (left_override != 0.0f) {
		printf(
		    "Overriding left eye angle_left with %f radians (%i°), "
		    "and right eye angle_right with %f radians (%i°)\n",
		    left_override, radtodeg_for_display(left_override),
		    -left_override, radtodeg_for_display(-left_override));
		dev->hmd->views[0].fov.angle_left = left_override;
		dev->hmd->views[1].fov.angle_right = -left_override;
	}

	const float right_override = debug_get_float_option_lfov_right();
	if (right_override != 0.0f) {
		printf(
		    "Overriding left eye angle_right with %f radians (%i°), "
		    "and right eye angle_left with %f radians (%i°)\n",
		    right_override, radtodeg_for_display(right_override),
		    -right_override, radtodeg_for_display(-right_override));
		dev->hmd->views[0].fov.angle_right = right_override;
		dev->hmd->views[1].fov.angle_left = -right_override;
	}

	const float up_override = debug_get_float_option_lfov_up();
	if (up_override != 0.0f) {
		printf("Overriding both eyes angle_up with %f radians (%i°)\n",
		       up_override, radtodeg_for_display(up_override));
		dev->hmd->views[0].fov.angle_up = up_override;
		dev->hmd->views[1].fov.angle_up = up_override;
	}

	const float down_override = debug_get_float_option_lfov_down();
	if (down_override != 0.0f) {
		printf(
		    "Overriding both eyes angle_down with %f radians (%i°)\n",
		    down_override, radtodeg_for_display(down_override));
		dev->hmd->views[0].fov.angle_down = down_override;
		dev->hmd->views[1].fov.angle_down = down_override;
	}

	oxr_system_fill_in(log, inst, 1, &inst->system, xdevs, NUM_XDEVS);

	inst->timekeeping = time_state_create();


	U_ZERO(&inst->extensions);
	for (uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i) {

#define ENABLE_EXT(mixed_case, all_caps)                                       \
	if (strcmp(createInfo->enabledExtensionNames[i],                       \
	           XR_##all_caps##_EXTENSION_NAME) == 0) {                     \
		inst->extensions.mixed_case = true;                            \
		continue;                                                      \
	}
		OXR_EXTENSION_SUPPORT_GENERATE(ENABLE_EXT)
		// assert(false &&
		//        "Should not be reached - oxr_xrCreateInstance should "
		//        "have failed on unrecognized extension.");
	}

	//! @todo check if this (and other creates) failed?

	u_var_add_root((void *)inst, "XrInstance", true);

	/* ---- HACK ---- */
	oxr_sdl2_hack_start(inst->hack, inst->prober);
	/* ---- HACK ---- */

	*out_instance = inst;

	return XR_SUCCESS;
}


XrResult
oxr_instance_get_properties(struct oxr_logger *log,
                            struct oxr_instance *inst,
                            XrInstanceProperties *instanceProperties)
{
	instanceProperties->runtimeVersion = XR_MAKE_VERSION(0, 1, 42);
	strncpy(instanceProperties->runtimeName,
	        "Monado(XRT) by Collabora et al", XR_MAX_RUNTIME_NAME_SIZE - 1);

	return XR_SUCCESS;
}

#ifdef XR_USE_TIMESPEC

XrResult
oxr_instance_convert_time_to_timespec(struct oxr_logger *log,
                                      struct oxr_instance *inst,
                                      XrTime time,
                                      struct timespec *timespecTime)
{
	time_state_to_timespec(inst->timekeeping, time, timespecTime);
	return XR_SUCCESS;
}

XrResult
oxr_instance_convert_timespec_to_time(struct oxr_logger *log,
                                      struct oxr_instance *inst,
                                      const struct timespec *timespecTime,
                                      XrTime *time)
{
	*time = time_state_from_timespec(inst->timekeeping, timespecTime);
	return XR_SUCCESS;
}
#endif // XR_USE_TIMESPEC
