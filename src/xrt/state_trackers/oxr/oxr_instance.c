// Copyright 2018-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds instance related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "xrt/xrt_config_os.h"
#include "xrt/xrt_config_build.h"
#include "xrt/xrt_instance.h"

#include "math/m_mathinclude.h"
#include "util/u_var.h"
#include "util/u_time.h"
#include "util/u_misc.h"
#include "util/u_debug.h"

#ifdef XRT_OS_ANDROID
#include "android/android_globals.h"
#endif

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_handle.h"
#include "oxr_extension_support.h"
#include "oxr_subaction.h"
#include "oxr_chain.h"

#include <sys/types.h>
#ifdef XRT_OS_UNIX
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

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
oxr_sdl2_hack_start(void *hack, struct xrt_instance *xinst);

extern void
oxr_sdl2_hack_stop(void **hack_ptr);
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

	// Does a null-ptr check.
	xrt_comp_native_destroy(&inst->system.xcn);

	u_var_remove_root((void *)inst);

	oxr_binding_destroy_all(log, inst);

	oxr_path_destroy(log, inst);

	u_hashset_destroy(&inst->action_sets.name_store);
	u_hashset_destroy(&inst->action_sets.loc_store);

	for (size_t i = 0; i < inst->system.num_xdevs; i++) {
		oxr_xdev_destroy(&inst->system.xdevs[i]);
	}

	/* ---- HACK ---- */
	oxr_sdl2_hack_stop(&inst->hack);
	/* ---- HACK ---- */

	xrt_instance_destroy(&inst->xinst);

	// Does null checking and sets to null.
	time_state_destroy(&inst->timekeeping);

	// Mutex goes last.
	os_mutex_destroy(&inst->event.mutex);

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

static void
assign_xdev_roles(struct oxr_instance *inst)
{
	struct oxr_system *sys = &inst->system;
	for (size_t i = 0; i < NUM_XDEVS; i++) {
		if (sys->xdevs[i] == NULL) {
			continue;
		}

		if (sys->xdevs[i]->device_type == XRT_DEVICE_TYPE_HMD) {
			sys->role.head = i;
		} else if (sys->xdevs[i]->device_type ==
		           XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER) {
			if (sys->role.left == XRT_DEVICE_ROLE_UNASSIGNED) {
				sys->role.left = i;
			}
		} else if (sys->xdevs[i]->device_type ==
		           XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER) {
			if (sys->role.right == XRT_DEVICE_ROLE_UNASSIGNED) {
				sys->role.right = i;
			}
		} else if (sys->xdevs[i]->device_type ==
		           XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER) {
			if (sys->role.left == XRT_DEVICE_ROLE_UNASSIGNED) {
				sys->role.left = i;
			} else if (sys->role.right ==
			           XRT_DEVICE_ROLE_UNASSIGNED) {
				sys->role.right = i;
			} else {
				//! @todo: do something with unassigend devices?
			}
		}
	}
}

static inline size_t
min_size_t(size_t a, size_t b)
{
	return a < b ? a : b;
}

XrResult
oxr_instance_create(struct oxr_logger *log,
                    const XrInstanceCreateInfo *createInfo,
                    struct oxr_instance **out_instance)
{
	struct oxr_instance *inst = NULL;
	struct xrt_device *xdevs[NUM_XDEVS] = {0};
	int xinst_ret, m_ret, h_ret;
	xrt_result_t xret;
	XrResult ret;

	OXR_ALLOCATE_HANDLE_OR_RETURN(log, inst, OXR_XR_DEBUG_INSTANCE,
	                              oxr_instance_destroy, NULL);

	inst->lifecycle_verbose = debug_get_bool_option_lifecycle_verbose();
	inst->debug_spaces = debug_get_bool_option_debug_spaces();
	inst->debug_views = debug_get_bool_option_debug_views();
	inst->debug_bindings = debug_get_bool_option_debug_bindings();

	m_ret = os_mutex_init(&inst->event.mutex);
	if (m_ret < 0) {
		ret = oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                "Failed to init mutex");
		return ret;
	}

	/* ---- HACK ---- */
	oxr_sdl2_hack_create(&inst->hack);
	/* ---- HACK ---- */

	ret = oxr_path_init(log, inst);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	h_ret = u_hashset_create(&inst->action_sets.name_store);
	if (h_ret != 0) {
		oxr_instance_destroy(log, &inst->handle);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Failed to create name_store hashset");
	}

	h_ret = u_hashset_create(&inst->action_sets.loc_store);
	if (h_ret != 0) {
		oxr_instance_destroy(log, &inst->handle);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Failed to create loc_store hashset");
	}


	// Cache certain often looked up paths.


#define CACHE_SUBACTION_PATHS(NAME, NAME_CAPS, PATH)                           \
	cache_path(log, inst, PATH, &inst->path_cache.NAME);
	OXR_FOR_EACH_SUBACTION_PATH_DETAILED(CACHE_SUBACTION_PATHS)

#undef CACHE_SUBACTION_PATHS
	// clang-format off

	cache_path(log, inst, "/interaction_profiles/khr/simple_controller", &inst->path_cache.khr_simple_controller);
	cache_path(log, inst, "/interaction_profiles/google/daydream_controller", &inst->path_cache.google_daydream_controller);
	cache_path(log, inst, "/interaction_profiles/htc/vive_controller", &inst->path_cache.htc_vive_controller);
	cache_path(log, inst, "/interaction_profiles/htc/vive_pro", &inst->path_cache.htc_vive_pro);
	cache_path(log, inst, "/interaction_profiles/microsoft/motion_controller", &inst->path_cache.microsoft_motion_controller);
	cache_path(log, inst, "/interaction_profiles/microsoft/xbox_controller", &inst->path_cache.microsoft_xbox_controller);
	cache_path(log, inst, "/interaction_profiles/oculus/go_controller", &inst->path_cache.oculus_go_controller);
	cache_path(log, inst, "/interaction_profiles/oculus/touch_controller", &inst->path_cache.oculus_touch_controller);
	cache_path(log, inst, "/interaction_profiles/valve/index_controller", &inst->path_cache.valve_index_controller);
	cache_path(log, inst, "/interaction_profiles/mndx/ball_on_a_stick_controller", &inst->path_cache.mndx_ball_on_a_stick_controller);
	// clang-format on

	// fill in our application info - @todo - replicate all createInfo
	// fields?

	struct xrt_instance_info i_info = {0};
	snprintf(i_info.application_name,
	         sizeof(inst->xinst->instance_info.application_name), "%s",
	         createInfo->applicationInfo.applicationName);

#ifdef XRT_OS_ANDROID
	XrInstanceCreateInfoAndroidKHR const *create_info_android =
	    OXR_GET_INPUT_FROM_CHAIN(createInfo,
	                             XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR,
	                             XrInstanceCreateInfoAndroidKHR);
	android_globals_store_vm_and_activity(
	    (struct _JavaVM *)create_info_android->applicationVM,
	    create_info_android->applicationActivity);
#endif

	xinst_ret = xrt_instance_create(&i_info, &inst->xinst);
	if (xinst_ret != 0) {
		ret = oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                "Failed to create prober");
		oxr_instance_destroy(log, &inst->handle);
		return ret;
	}


	xinst_ret = xrt_instance_select(inst->xinst, xdevs, NUM_XDEVS);
	if (xinst_ret != 0) {
		ret = oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                "Failed to select device(s)");
		oxr_instance_destroy(log, &inst->handle);
		return ret;
	}

	struct oxr_system *sys = &inst->system;

#define POPULATE_ROLES(X) sys->role.X = XRT_DEVICE_ROLE_UNASSIGNED;
	OXR_FOR_EACH_VALID_SUBACTION_PATH(POPULATE_ROLES)
#undef POPULATE_ROLES

	sys->num_xdevs = min_size_t(ARRAY_SIZE(sys->xdevs), NUM_XDEVS);

	for (uint32_t i = 0; i < sys->num_xdevs; i++) {
		sys->xdevs[i] = xdevs[i];
	}
	for (size_t i = sys->num_xdevs; i < NUM_XDEVS; i++) {
		oxr_xdev_destroy(&xdevs[i]);
	}

	assign_xdev_roles(inst);

	// Did we find any HMD
	// @todo Headless with only controllers?
	struct xrt_device *dev = GET_XDEV_BY_ROLE(sys, head);
	if (dev == NULL) {
		ret = oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                "Failed to find any HMD device");
		oxr_instance_destroy(log, &inst->handle);
		return ret;
	}

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

	// Create the compositor, if we are not headless.
	if (!inst->extensions.MND_headless) {
		xret = xrt_instance_create_native_compositor(inst->xinst, dev,
		                                             &sys->xcn);
		if (ret < 0 || sys->xcn == NULL) {
			ret = oxr_error(
			    log, XR_ERROR_INITIALIZATION_FAILED,
			    "Failed to create a native compositor '%i'", xret);
			oxr_instance_destroy(log, &inst->handle);
			return ret;
		}

		// Make sure that the compositor we were given can do all the
		// things the build config promised.
#define CHECK_LAYER_TYPE(NAME, MEMBER_NAME)                                    \
	do {                                                                   \
		if (sys->xcn->base.MEMBER_NAME == NULL) {                      \
			ret = oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,   \
			                "Logic error: build config "           \
			                "advertised support for " NAME         \
			                " but compositor does not "            \
			                "implement " #MEMBER_NAME);            \
			oxr_instance_destroy(log, &inst->handle);              \
			assert(false &&                                        \
			       "Build configured with unsupported layers");    \
			return ret;                                            \
		}                                                              \
	} while (0)

		// Keep this list in sync with types in xrt_config_build.h
#ifdef XRT_FEATURE_OPENXR_LAYER_CUBE
		CHECK_LAYER_TYPE("cube layers", layer_cube);
#endif
#ifdef XRT_FEATURE_OPENXR_LAYER_CYLINDER
		CHECK_LAYER_TYPE("cylinder layers", layer_cylinder);
#endif
#ifdef XRT_FEATURE_OPENXR_LAYER_DEPTH
		CHECK_LAYER_TYPE("projection layers with depth images",
		                 layer_stereo_projection_depth);
#endif
#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT
		CHECK_LAYER_TYPE("equirect layers", layer_equirect);
#endif
#undef CHECK_LAYER_TYPE
	}

	ret = oxr_system_fill_in(log, inst, 1, &inst->system);
	if (ret != XR_SUCCESS) {
		oxr_instance_destroy(log, &inst->handle);
		return ret;
	}

	inst->timekeeping = time_state_create();


	//! @todo check if this (and other creates) failed?

	u_var_add_root((void *)inst, "XrInstance", true);

	/* ---- HACK ---- */
	oxr_sdl2_hack_start(inst->hack, inst->xinst);
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
