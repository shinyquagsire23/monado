// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#include "oxr_handle.h"

#include "util/u_debug.h"
#include "util/u_misc.h"

#include "oxr_objects.h"
#include "oxr_logger.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>


#define HANDLE_LIFECYCLE_LOG(log, ...)                                                                                 \
	if (log->inst != NULL && log->inst->lifecycle_verbose) {                                                       \
		oxr_log(log, " Handle Lifecycle: " __VA_ARGS__);                                                       \
	}

// Variation of HANDLE_LIFECYCLE_LOG() to wrap a handle free() which might
// potentially free the instance (in which logger info is stored).
#define HANDLE_LIFECYCLE_LOG_SCOPED_BEGIN(log)                                                                         \
	{                                                                                                              \
		const bool _log_lifecycle_verbose = log->inst != NULL && log->inst->lifecycle_verbose;
#define HANDLE_LIFECYCLE_LOG_SCOPED_END                                                                                \
	}                                                                                                              \
	(void)0
#define HANDLE_LIFECYCLE_LOG_SCOPED(log, ...)                                                                          \
	if (_log_lifecycle_verbose) {                                                                                  \
		oxr_log(log, " Handle Lifecycle: " __VA_ARGS__);                                                       \
	}


const char *
oxr_handle_state_to_string(enum oxr_handle_state state)
{
	switch (state) {
	case OXR_HANDLE_STATE_UNINITIALIZED: return "UNINITIALIZED";
	case OXR_HANDLE_STATE_LIVE: return "LIVE";
	case OXR_HANDLE_STATE_DESTROYED: return "DESTROYED";
	default: return "<UNKNOWN>";
	}
}


XrResult
oxr_handle_init(struct oxr_logger *log,
                struct oxr_handle_base *hb,
                uint64_t debug,
                oxr_handle_destroyer destroy,
                struct oxr_handle_base *parent)
{
	assert(log != NULL);
	assert(hb != NULL);
	assert(destroy != NULL);
	assert(debug != 0);

	HANDLE_LIFECYCLE_LOG(log, "[init %p] Initializing handle, parent handle = %p", (void *)hb, (void *)parent);


	hb->state = OXR_HANDLE_STATE_UNINITIALIZED;

	if (parent != NULL) {
		if (parent->state != OXR_HANDLE_STATE_LIVE) {
			return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
			                 "Handle %p given parent %p in invalid state: %s", (void *)parent, (void *)hb,
			                 oxr_handle_state_to_string(parent->state));
		}

		bool placed = false;
		for (int i = 0; i < XRT_MAX_HANDLE_CHILDREN; ++i) {
			if (parent->children[i] == NULL) {
				HANDLE_LIFECYCLE_LOG(log,
				                     "[init %p] Assigned to "
				                     "child slot %d in parent",
				                     (void *)hb, i);
				parent->children[i] = hb;
				placed = true;
				break;
			}
		}
		if (!placed) {
			return oxr_error(log, XR_ERROR_LIMIT_REACHED,
			                 "Parent handle has no more room for "
			                 "child handles");
		}
	}
	U_ZERO(hb);
	hb->debug = debug;
	hb->parent = parent;
	hb->state = OXR_HANDLE_STATE_LIVE;
	hb->destroy = destroy;
	return XR_SUCCESS;
}

XrResult
oxr_handle_allocate_and_init(struct oxr_logger *log,
                             size_t size,
                             uint64_t debug,
                             oxr_handle_destroyer destroy,
                             struct oxr_handle_base *parent,
                             void **out)
{
	/*
	 * This allocation call, taking a size, not a type, is why this
	 * function isn't recommended for direct use.
	 */
	struct oxr_handle_base *hb = U_CALLOC_WITH_CAST(struct oxr_handle_base, size);
	XrResult result = oxr_handle_init(log, hb, debug, destroy, parent);
	if (result != XR_SUCCESS) {
		free(hb);
		return result;
	}
	*out = (void *)hb;
	return result;
}

/*!
 * This is the actual recursive call that destroys handles.
 *
 * oxr_handle_destroy wraps this to provide some extra output and start `level`
 * at 0. `level`, which is reported in debug output, is the current depth of
 * recursion.
 */
static XrResult
oxr_handle_do_destroy(struct oxr_logger *log, struct oxr_handle_base *hb, int level)
{

	HANDLE_LIFECYCLE_LOG(log,
	                     "[%d: destroying %p] Destroying handle and all "
	                     "contained handles (recursively)",
	                     level, (void *)hb);

	/* Remove from parent, if any. */
	if (hb->parent != NULL) {
		bool found = false;
		struct oxr_handle_base *parent = hb->parent;

		for (int i = 0; i < XRT_MAX_HANDLE_CHILDREN; ++i) {
			if (parent->children[i] == hb) {
				HANDLE_LIFECYCLE_LOG(log,
				                     "[%d: destroying %p] Removing handle from "
				                     "child slot %d in parent %p",
				                     level, (void *)hb, i, (void *)hb->parent);

				parent->children[i] = NULL;
				found = true;
				break;
			}
		}
		if (!found) {
			return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Parent handle does not refer to this handle");
		}

		/* clear parent pointer */
		hb->parent = NULL;
	}

	/* Destroy child handles */
	for (size_t i = 0; i < XRT_MAX_HANDLE_CHILDREN; ++i) {
		struct oxr_handle_base *child = hb->children[i];

		if (child != NULL) {
			XrResult result = oxr_handle_do_destroy(log, child, level + 1);
			if (result != XR_SUCCESS) {
				return result;
			}
		}
	}

	/* Might destroy instance, which log needs, so use secured variant */
	HANDLE_LIFECYCLE_LOG_SCOPED_BEGIN(log)
	{
		/* Destroy self */
		HANDLE_LIFECYCLE_LOG_SCOPED(log, "[%d: destroying %p] Calling handle object destructor", level,
		                            (void *)hb);
		hb->state = OXR_HANDLE_STATE_DESTROYED;
		XrResult result = hb->destroy(log, hb);
		if (result != XR_SUCCESS) {
			return result;
		}
		HANDLE_LIFECYCLE_LOG_SCOPED(log, "r%d: destroying %p] Done", level, (void *)hb);
	}
	HANDLE_LIFECYCLE_LOG_SCOPED_END;

	return XR_SUCCESS;
}

XrResult
oxr_handle_destroy(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	assert(log != NULL);
	assert(hb != NULL);

	/* Might destroy instance, which log needs, so use secured variant */
	HANDLE_LIFECYCLE_LOG_SCOPED_BEGIN(log)
	{
		HANDLE_LIFECYCLE_LOG_SCOPED(log, "[~: destroying %p] oxr_handle_destroy starting", (void *)hb);

		XrResult result = oxr_handle_do_destroy(log, hb, 0);

		HANDLE_LIFECYCLE_LOG_SCOPED(log, "[~: destroying %p] oxr_handle_destroy finished", (void *)hb);

		return result;
	}
	HANDLE_LIFECYCLE_LOG_SCOPED_END;
}
