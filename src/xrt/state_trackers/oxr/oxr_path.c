// Copyright 2019-2020, Collabora, Ltd.
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
	XrPath id;
	void *attached;
};


/*
 *
 * Helpers
 *
 */

static inline XrPath
to_xr_path(struct oxr_path *path)
{
	return path->id;
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
oxr_ensure_array_length(struct oxr_logger *log, struct oxr_instance *inst, XrPath *out_id)
{
	size_t num = inst->path_num + 1;

	if (num < inst->path_array_length) {
		*out_id = inst->path_num++;
		return XR_SUCCESS;
	}

	size_t new_size = inst->path_array_length;
	while (new_size < num) {
		new_size += 64;
	}

	U_ARRAY_REALLOC_OR_FREE(inst->path_array, struct oxr_path *, new_size);
	inst->path_array_length = new_size;

	*out_id = inst->path_num++;

	return XR_SUCCESS;
}

static XrResult
oxr_allocate_path(
    struct oxr_logger *log, struct oxr_instance *inst, const char *str, size_t length, struct oxr_path **out_path)
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
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Failed to allocate path");
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
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Failed to insert item");
	}

	oxr_ensure_array_length(log, inst, &path->id);
	inst->path_array[path->id] = path;

	*out_path = path;

	return XR_SUCCESS;
}

struct oxr_path *
get_path_or_null(struct oxr_logger *log, struct oxr_instance *inst, XrPath xr_path)
{
	if (xr_path >= inst->path_array_length) {
		return NULL;
	}

	return inst->path_array[xr_path];
}


/*
 *
 * "Exported" functions.
 *
 */

bool
oxr_path_is_valid(struct oxr_logger *log, struct oxr_instance *inst, XrPath xr_path)
{
	return get_path_or_null(log, inst, xr_path) != NULL;
}

void *
oxr_path_get_attached(struct oxr_logger *log, struct oxr_instance *inst, XrPath xr_path)
{
	struct oxr_path *path = get_path_or_null(log, inst, xr_path);
	if (path == NULL) {
		return NULL;
	}

	return path->attached;
}

XrResult
oxr_path_get_or_create(
    struct oxr_logger *log, struct oxr_instance *inst, const char *str, size_t length, XrPath *out_path)
{
	struct u_hashset_item *item;
	struct oxr_path *path = NULL;
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
oxr_path_only_get(struct oxr_logger *log, struct oxr_instance *inst, const char *str, size_t length, XrPath *out_path)
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
oxr_path_get_string(
    struct oxr_logger *log, struct oxr_instance *inst, XrPath xr_path, const char **out_str, size_t *out_length)
{
	struct oxr_path *path = get_path_or_null(log, inst, xr_path);
	if (path == NULL) {
		return XR_ERROR_PATH_INVALID;
	}

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

XrResult
oxr_path_init(struct oxr_logger *log, struct oxr_instance *inst)
{
	int h_ret = u_hashset_create(&inst->path_store);
	if (h_ret != 0) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Failed to create hashset");
	}

	size_t new_size = 64;
	U_ARRAY_REALLOC_OR_FREE(inst->path_array, struct oxr_path *, new_size);
	inst->path_array_length = new_size;
	inst->path_num = 1; // Reserve space for XR_NULL_PATH

	return XR_SUCCESS;
}

void
oxr_path_destroy(struct oxr_logger *log, struct oxr_instance *inst)
{
	if (inst->path_array != NULL) {
		free(inst->path_array);
	}

	inst->path_array = NULL;
	inst->path_num = 0;
	inst->path_array_length = 0;

	if (inst->path_store == NULL) {
		return;
	}

	u_hashset_clear_and_call_for_each(inst->path_store, destroy_callback, inst);
	u_hashset_destroy(&inst->path_store);
}
