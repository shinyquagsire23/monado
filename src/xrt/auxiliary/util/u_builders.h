// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helpers for @ref xrt_builder implementations.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_prober.h"


#ifdef __cplusplus
extern "C" {
#endif

struct xrt_prober_device;

/*!
 * Max return of the number @ref xrt_prober_device.
 *
 * @ingroup aux_util
 */
#define U_BUILDER_SEARCH_MAX (16) // 16 Vive trackers

/*!
 * A filter to match the against.
 *
 * @ingroup aux_util
 */
struct u_builder_search_filter
{
	uint16_t vendor_id;
	uint16_t product_id;
	enum xrt_bus_type bus_type;
};

/*!
 * Results of a search of devices.
 *
 * @ingroup aux_util
 */
struct u_builder_search_results
{
	//! Out field of found @ref xrt_prober_device.
	struct xrt_prober_device *xpdevs[U_BUILDER_SEARCH_MAX];

	//! Number of found devices.
	size_t xpdev_count;
};

/*!
 * Find the first @ref xrt_prober_device in the prober list.
 *
 * @ingroup aux_util
 */
struct xrt_prober_device *
u_builder_find_prober_device(struct xrt_prober_device *const *xpdevs,
                             size_t xpdev_count,
                             uint16_t vendor_id,
                             uint16_t product_id,
                             enum xrt_bus_type bus_type);

/*!
 * Find all of the @ref xrt_prober_device that matches any in the given list of
 * @ref u_builder_search_filter filters.
 *
 * @ingroup aux_util
 */
void
u_builder_search(struct xrt_prober *xp,
                 struct xrt_prober_device *const *xpdevs,
                 size_t xpdev_count,
                 const struct u_builder_search_filter *filters,
                 size_t filter_count,
                 struct u_builder_search_results *results);


#ifdef __cplusplus
}
#endif
