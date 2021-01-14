// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Contains handle-related functions and defines only required in a few
 * locations.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup oxr_main
 */

#pragma once

#include "oxr_objects.h"

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *
 * oxr_handle_base.c
 *
 */

/*!
 * Initialize a handle holder, and if a parent is specified, update its child
 * list to include this handle.
 *
 * @protected @memberof oxr_handle_base
 */
XrResult
oxr_handle_init(struct oxr_logger *log,
                struct oxr_handle_base *hb,
                uint64_t debug,
                oxr_handle_destroyer destroy,
                struct oxr_handle_base *parent);

/*!
 * Allocate some memory for use as a handle, and initialize it as a handle.
 *
 * Mainly for internal use - use OXR_ALLOCATE_HANDLE instead which wraps this.
 *
 * @relates oxr_handle_base
 */
XrResult
oxr_handle_allocate_and_init(struct oxr_logger *log,
                             size_t size,
                             uint64_t debug,
                             oxr_handle_destroyer destroy,
                             struct oxr_handle_base *parent,
                             void **out);
/*!
 * Allocates memory for a handle and evaluates to an XrResult.
 *
 * @param LOG pointer to struct oxr_logger
 * @param OUT the pointer to handle struct type you already created.
 * @param DEBUG Magic per-type debugging constant
 * @param DESTROY Handle destructor function
 * @param PARENT a parent handle, if any
 *
 * Use when you want to do something other than immediately returning in case of
 * failure. If returning immediately is OK, see OXR_ALLOCATE_HANDLE_OR_RETURN().
 *
 * @relates oxr_handle_base
 */
#define OXR_ALLOCATE_HANDLE(LOG, OUT, DEBUG, DESTROY, PARENT)                                                          \
	oxr_handle_allocate_and_init(LOG, sizeof(*OUT), DEBUG, DESTROY, PARENT, (void **)&OUT)

/*!
 * Allocate memory for a handle, returning in case of failure.
 *
 * @param LOG pointer to struct oxr_logger
 * @param OUT the pointer to handle struct type you already created.
 * @param DEBUG Magic per-type debugging constant
 * @param DESTROY Handle destructor function
 * @param PARENT a parent handle, if any
 *
 * Will return an XrResult from the current function if something fails.
 * If that's not OK, see OXR_ALLOCATE_HANDLE().
 *
 * @relates oxr_handle_base
 */
#define OXR_ALLOCATE_HANDLE_OR_RETURN(LOG, OUT, DEBUG, DESTROY, PARENT)                                                \
	do {                                                                                                           \
		XrResult allocResult = OXR_ALLOCATE_HANDLE(LOG, OUT, DEBUG, DESTROY, PARENT);                          \
		if (allocResult != XR_SUCCESS) {                                                                       \
			return allocResult;                                                                            \
		}                                                                                                      \
	} while (0)

#ifdef __cplusplus
}
#endif
