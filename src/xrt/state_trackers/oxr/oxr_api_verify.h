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
			                 (void*)new_thing);                    \
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
			                 "(" #arg " == %p)", (void*)new_arg);  \
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
	_OXR_VERIFY_AND_SET_AND_INIT(log, thing, new_thing, ACTION, name, new_thing->act_set->sess->sys->inst)
#define OXR_VERIFY_SWAPCHAIN_AND_INIT_LOG(log, thing, new_thing, name) \
	_OXR_VERIFY_AND_SET_AND_INIT(log, thing, new_thing, SWAPCHAIN, name, new_thing->sess->sys->inst)
#define OXR_VERIFY_ACTIONSET_AND_INIT_LOG(log, thing, new_thing, name) \
	_OXR_VERIFY_AND_SET_AND_INIT(log, thing, new_thing, ACTIONSET, name, new_thing->sess->sys->inst)

#define OXR_VERIFY_INSTANCE_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, INSTANCE);
#define OXR_VERIFY_MESSENGER_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, MESSENGER);
#define OXR_VERIFY_SESSION_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, SESSION);
#define OXR_VERIFY_SPACE_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, SPACE);
#define OXR_VERIFY_ACTION_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, ACTION);
#define OXR_VERIFY_SWAPCHAIN_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, SWAPCHAIN);
#define OXR_VERIFY_ACTIONSET_NOT_NULL(log, arg, new_arg) _OXR_VERIFY_SET(log, arg, new_arg, ACTIONSET);
// clang-format on


#define OXR_VERIFY_ARG_NOT_NULL(log, arg)                                      \
	do {                                                                   \
		if (arg == NULL) {                                             \
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,     \
			                 "(" #arg " == NULL)");                \
		}                                                              \
	} while (false)

#define OXR_VERIFY_ARG_TYPE_AND_NULL(log, arg, type_enum)                      \
	do {                                                                   \
		if (arg == NULL) {                                             \
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,     \
			                 "(" #arg "== NULL)");                 \
		}                                                              \
		if (arg->type != type_enum) {                                  \
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,     \
			                 "(" #arg "->type = %u)", arg->type);  \
		}                                                              \
		if (arg->next != NULL) {                                       \
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,     \
			                 "(" #arg "->next = %p)", arg->next);  \
		}                                                              \
	} while (false)

#define OXR_VERIFY_SUBACTION_PATHS(log, count, paths)                          \
	do {                                                                   \
		if (count == 0 && paths != NULL) {                             \
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,     \
			                 " " #count " is zero but " #paths     \
			                 " is not NULL");                      \
		}                                                              \
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

/*
 *
 * Implementation in oxr_verify.cpp
 *
 */

XrResult
oxr_verify_fixed_size_single_level_path(struct oxr_logger*,
                                        const char* path,
                                        uint32_t size,
                                        const char* name);

XrResult
oxr_verify_XrSessionCreateInfo(struct oxr_logger*,
                               const struct oxr_instance*,
                               const XrSessionCreateInfo*);

XrResult
oxr_verify_XrGraphicsBindingOpenGLXlibKHR(
    struct oxr_logger*, const XrGraphicsBindingOpenGLXlibKHR*);

XrResult
oxr_verify_XrGraphicsBindingVulkanKHR(struct oxr_logger*,
                                      const XrGraphicsBindingVulkanKHR*);

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
