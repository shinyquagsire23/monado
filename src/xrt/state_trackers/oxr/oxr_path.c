// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds path related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "math/m_api.h"
#include "util/u_misc.h"

#include "oxr_objects.h"
#include "oxr_logger.h"


/*!
 * Internal representation of a path, item follows this struct in memory and
 * that in turn is followed by the string.
 *
 * @ingroup oxr_main
 */
struct oxr_path
{
	uint64_t debug;
	void *attached;
};


/*
 *
 * Helpers
 *
 */

static inline struct oxr_path *
oxr_path(XrPath path)
{
	return (struct oxr_path *)path;
}

static inline XrPath
to_xr_path(struct oxr_path *path)
{
	return (XrPath)path;
}

static inline struct u_hashset_item *
get_item(struct oxr_path *path)
{
	return (struct u_hashset_item *)&path[1];
}

static inline struct oxr_path *
from_item(struct u_hashset_item *item)
{
	return &((struct oxr_path *)item)[-1];
}


/*
 *
 * Static functions.
 *
 */

static XrResult
oxr_allocate_path(struct oxr_logger *log,
                  struct oxr_instance *inst,
                  const char *str,
                  size_t length,
                  struct oxr_path **out_path)
{
	struct u_hashset_item *item = NULL;
	struct oxr_path *path = NULL;
	size_t size = 0;
	int ret;

	size += sizeof(struct oxr_path);       // Main path object.
	size += sizeof(struct u_hashset_item); // Embedded hashset item.
	size += length;                        // String.
	size += 1;                             // Null terminate it.

	// Now allocate and setup the path.
	path = U_CALLOC_WITH_CAST(struct oxr_path, size);
	if (path == NULL) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Failed to allocate path");
	}
	path->debug = OXR_XR_DEBUG_PATH;

	// Setup the item.
	item = get_item(path);
	item->hash = math_hash_string(str, length);
	item->length = length;

	// Yes a const cast! D:
	char *store = (char *)item->c_str;
	for (size_t i = 0; i < length; i++) {
		store[i] = str[i];
	}
	store[length] = '\0';

	// Insert and return.
	ret = u_hashset_insert_item(inst->path_store, item);
	if (ret) {
		free(path);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Failed to insert item");
	}

	*out_path = path;

	return XR_SUCCESS;
}


/*
 *
 * "Exported" functions.
 *
 */

void *
oxr_path_get_attached(struct oxr_logger *log,
                      struct oxr_instance *inst,
                      XrPath xr_path)
{
	if (xr_path == XR_NULL_PATH) {
		return NULL;
	}

	return oxr_path(xr_path)->attached;
}

XrResult
oxr_path_get_or_create(struct oxr_logger *log,
                       struct oxr_instance *inst,
                       const char *str,
                       size_t length,
                       XrPath *out_path)
{
	struct u_hashset_item *item;
	struct oxr_path *path;
	XrResult ret;
	int h_ret;

	// Look it up the instance path store.
	h_ret = u_hashset_find_str(inst->path_store, str, length, &item);
	if (h_ret == 0) {
		*out_path = to_xr_path(from_item(item));
		return XR_SUCCESS;
	}

	// Create the path since it was not found.
	ret = oxr_allocate_path(log, inst, str, length, &path);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	*out_path = to_xr_path(path);

	return XR_SUCCESS;
}

XrResult
oxr_path_only_get(struct oxr_logger *log,
                  struct oxr_instance *inst,
                  const char *str,
                  size_t length,
                  XrPath *out_path)
{
	struct u_hashset_item *item;
	int h_ret;

	// Look it up the instance path store.
	h_ret = u_hashset_find_str(inst->path_store, str, length, &item);
	if (h_ret == 0) {
		*out_path = to_xr_path(from_item(item));
		return XR_SUCCESS;
	}

	*out_path = XR_NULL_PATH;
	return XR_SUCCESS;
}

XrResult
oxr_path_get_string(struct oxr_logger *log,
                    struct oxr_instance *inst,
                    XrPath xr_path,
                    const char **out_str,
                    size_t *out_length)
{
	struct oxr_path *path = oxr_path(xr_path);

	*out_str = get_item(path)->c_str;
	*out_length = get_item(path)->length;

	return XR_SUCCESS;
}

void
destroy_callback(struct u_hashset_item *item, void *priv)
{
	struct oxr_path *path = from_item(item);

	free(path);
}

void
oxr_path_destroy_all(struct oxr_logger *log,
                     struct oxr_instance *inst)
{
	u_hashset_clear_and_call_for_each(inst->path_store, destroy_callback,
	                                  inst);
}
