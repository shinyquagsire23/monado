// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  File for verifing app input into api functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_api
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#define _OXR_VERIFY_AND_SET_AND_INIT(log, thing, new_thing, THING, name,       \
                                     lookup)                                   \
	do {                                                                   \
		oxr_log_init(log, name);                                       \
		if (thing == NULL) {                                           \
			return oxr_error(log, XR_ERROR_HANDLE_INVALID,         \
			                 "(" #thing " == NULL)");              \
		}                                                              \
		new_thing = (__typeof__(new_thing))thing;                      \
		if (new_thing->handle.debug != OXR_XR_DEBUG_##THING) {         \
			return oxr_error(log, XR_ERROR_HANDLE_INVALID,         \
			                 "(" #thing " == %p)",                 \
			                 (void *)new_thing);                   \
		}                                                              \
		if (new_thing->handle.state != OXR_HANDLE_STATE_LIVE) {        \
			return oxr_error(log, XR_ERROR_HANDLE_INVALID,         \
			                 " state == %s (" #thing " == %p)",    \
			                 oxr_handle_state_to_string(           \
			                     new_thing->handle.state),         \
			                 (void *)new_thing);                   \
		}                                                              \
		oxr_log_set_instance(log, lookup);                             \
	} while (0)

#define _OXR_VERIFY_SET(log, arg, new_arg, THING)                              \
	do {                                                                   \
		if (arg == NULL) {                                             \
			return oxr_error(log, XR_ERROR_HANDLE_INVALID,         \
			                 "(" #arg " == NULL)");                \
		}                                                              \
		new_arg = (__typeof__(new_arg))arg;                            \
		if (new_arg->handle.debug != OXR_XR_DEBUG_##THING) {           \
			return oxr_error(log, XR_ERROR_HANDLE_INVALID,         \
			                 "(" #arg " == %p)", (void *)new_arg); \
		}                                                              \
	} while (0)


/*!
 * @ingroup oxr_api
 * @{
 */

// clang-format off
#define OXR_VERIFY_INSTANCE_AND_INIT_LOG(log, thing, new_thing, name) \
	_OXR_VERIFY_AND_SET_AND_INIT(log, thing, new_thing, INSTANCE, name, new_thing)
#define OXR_VERIFY_MESSENGER_AND_INIT_LOG(log, thing, new_thing, name) \
	_OXR_VERIFY_AND_SET_AND_INIT(log, thing, new_thing, MESSENGER, name, new_thing->inst)
#define OXR_VERIFY_SESSION_AND_INIT_LOG(log, thing, new_thing, name) \
	_OXR_VERIFY_AND_SET_AND_INIT(log, thing, new_thing, SESSION, name, new_thing->sys->inst)
#define OXR_VERIFY_SPACE_AND_INIT_LOG(log, thing, new_thing, name) \
	_OXR_VERIFY_AND_SET_AND_INIT(log, thing, new_thing, SPACE, name, new_thing->sess->sys->inst)
#define OXR_VERIFY_ACTION_AND_INIT_LOG(log, thing, new_thing, name) \
	_OXR_VERIFY_AND_SET_AND_INIT(log, thing, new_thing, ACTION, name, new_thing->act_set->inst)
#define OXR_VERIFY_SWAPCHAIN_AND_INIT_LOG(log, thing, new_thing, name) \
	_OXR_VERIFY_AND_SET_AND_INIT(log, thing, new_thing, SWAPCHAIN, name, new_thing->sess->sys->inst)
#define OXR_VERIFY_ACTIONSET_AND_INIT_LOG(log, thing, new_thing, name) \
	_OXR_VERIFY_AND_SET_AND_INIT(log, thing, new_thing, ACTIONSET, name, new_thing->inst)

#define OXR_VERIFY_INSTANCE_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, INSTANCE);
#define OXR_VERIFY_MESSENGER_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, MESSENGER);
#define OXR_VERIFY_SESSION_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, SESSION);
#define OXR_VERIFY_SPACE_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, SPACE);
#define OXR_VERIFY_ACTION_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, ACTION);
#define OXR_VERIFY_SWAPCHAIN_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, SWAPCHAIN);
#define OXR_VERIFY_ACTIONSET_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, ACTIONSET);
// clang-format on

/*!
 * Checks if a required extension is enabled.
 *
 * mixed_case_name should be the extension name without the XR_ prefix.
 */
#define OXR_VERIFY_EXTENSION(log, inst, mixed_case_name)                       \
	do {                                                                   \
		if (!(inst)->extensions.mixed_case_name) {                     \
			return oxr_error((log), XR_ERROR_FUNCTION_UNSUPPORTED, \
			                 " Requires XR_" #mixed_case_name      \
			                 " extension enabled");                \
		}                                                              \
	} while (false)

#define OXR_VERIFY_ARG_NOT_NULL(log, arg)                                      \
	do {                                                                   \
		if (arg == NULL) {                                             \
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,     \
			                 "(" #arg " == NULL)");                \
		}                                                              \
	} while (false)

#define OXR_VERIFY_ARG_NOT_ZERO(log, arg)                                      \
	do {                                                                   \
		if (arg == 0) {                                                \
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,     \
			                 "(" #arg " == 0) must be non-zero");  \
		}                                                              \
	} while (false)

#define OXR_VERIFY_ARG_TYPE_CAN_BE_NULL(log, arg, type_enum)                   \
	do {                                                                   \
		if (arg != NULL && arg->type != type_enum) {                   \
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,     \
			                 "(" #arg "->type == %u)", arg->type); \
		}                                                              \
	} while (false)

#define OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(log, arg, type_enum)                  \
	do {                                                                   \
		if (arg == NULL) {                                             \
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,     \
			                 "(" #arg " == NULL)");                \
		}                                                              \
		OXR_VERIFY_ARG_TYPE_CAN_BE_NULL(log, arg, type_enum);          \
	} while (false)

#define OXR_VERIFY_SUBACTION_PATHS(log, count, paths)                          \
	do {                                                                   \
		if (count > 0 && paths == NULL) {                              \
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,     \
			                 " " #count " is not zero but " #paths \
			                 " is NULL");                          \
		}                                                              \
	} while (false)

#define OXR_VERIFY_ARG_SINGLE_LEVEL_FIXED_LENGTH_PATH(log, path)               \
	do {                                                                   \
		XrResult verify_ret = oxr_verify_fixed_size_single_level_path( \
		    log, path, ARRAY_SIZE(path), #path);                       \
		if (verify_ret != XR_SUCCESS) {                                \
			return verify_ret;                                     \
		}                                                              \
	} while (false)

#define OXR_VERIFY_ARG_LOCALIZED_NAME(log, string)                             \
	do {                                                                   \
		XrResult verify_ret = oxr_verify_localized_name(               \
		    log, string, ARRAY_SIZE(string), #string);                 \
		if (verify_ret != XR_SUCCESS) {                                \
			return verify_ret;                                     \
		}                                                              \
	} while (false)

#define OXR_VERIFY_POSE(log, p)                                                \
	do {                                                                   \
		if (!math_quat_validate((struct xrt_quat *)&p.orientation)) {  \
			return oxr_error(log, XR_ERROR_POSE_INVALID,           \
			                 "(" #p                                \
			                 ".orientation) is not a valid quat"); \
		}                                                              \
                                                                               \
		if (!math_vec3_validate((struct xrt_vec3 *)&p.position)) {     \
			return oxr_error(log, XR_ERROR_POSE_INVALID,           \
			                 "(" #p ".position) is not valid");    \
		}                                                              \
	} while (false)

/*
 *
 * Implementation in oxr_verify.cpp
 *
 */

XrResult
oxr_verify_full_path_c(struct oxr_logger *log,
                       const char *path,
                       const char *name);

/*!
 * Verify a full path.
 *
 * Length not including zero terminator character but must be there.
 */
XrResult
oxr_verify_full_path(struct oxr_logger *log,
                     const char *path,
                     size_t length,
                     const char *name);

/*!
 * Verify a single path level that sits inside of a fixed sized array.
 */
XrResult
oxr_verify_fixed_size_single_level_path(struct oxr_logger *,
                                        const char *path,
                                        uint32_t array_size,
                                        const char *name);

/*!
 * Verify an arbitrary UTF-8 string that sits inside of a fixed sized array.
 */
XrResult
oxr_verify_localized_name(struct oxr_logger *,
                          const char *string,
                          uint32_t array_size,
                          const char *name);

/*!
 * Verify a set of subaction paths for action creation.
 */
XrResult
oxr_verify_subaction_paths_create(struct oxr_logger *log,
                                  struct oxr_instance *inst,
                                  uint32_t countSubactionPaths,
                                  const XrPath *subactionPaths,
                                  const char *variable);

/*!
 * Verify a set of subaction paths for action sync.
 */
XrResult
oxr_verify_subaction_path_sync(struct oxr_logger *log,
                               struct oxr_instance *inst,
                               XrPath path,
                               uint32_t index);

/*!
 * Verify a set of subaction paths for action state get.
 */
XrResult
oxr_verify_subaction_path_get(struct oxr_logger *log,
                              struct oxr_instance *inst,
                              XrPath path,
                              const struct oxr_sub_paths *act_sub_paths,
                              struct oxr_sub_paths *out_sub_paths,
                              const char *variable);

XrResult
oxr_verify_XrSessionCreateInfo(struct oxr_logger *,
                               const struct oxr_instance *,
                               const XrSessionCreateInfo *);

#if defined(XR_USE_PLATFORM_XLIB) && defined(XR_USE_GRAPHICS_API_OPENGL)
XrResult
oxr_verify_XrGraphicsBindingOpenGLXlibKHR(
    struct oxr_logger *, const XrGraphicsBindingOpenGLXlibKHR *);
#endif // defined(XR_USE_PLATFORM_XLIB) && defined(XR_USE_GRAPHICS_API_OPENGL)

#if defined(XR_USE_GRAPHICS_API_VULKAN)
XrResult
oxr_verify_XrGraphicsBindingVulkanKHR(struct oxr_logger *,
                                      const XrGraphicsBindingVulkanKHR *);
#endif // defined(XR_USE_GRAPHICS_API_VULKAN)

#if defined(XR_USE_PLATFORM_EGL) && defined(XR_USE_GRAPHICS_API_OPENGL)
XrResult
oxr_verify_XrGraphicsBindingEGLMND(struct oxr_logger *log,
                                   const XrGraphicsBindingEGLMND *next);
#endif // defined(XR_USE_PLATFORM_EGL) && defined(XR_USE_GRAPHICS_API_OPENGL)

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
