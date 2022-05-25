// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helpers for @ref xrt_builder implementations.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "xrt/xrt_prober.h"
#include "u_builders.h"


/*
 *
 * 'Exported' function.
 *
 */

struct xrt_prober_device *
u_builder_find_prober_device(struct xrt_prober_device *const *xpdevs,
                             size_t xpdev_count,
                             uint16_t vendor_id,
                             uint16_t product_id,
                             enum xrt_bus_type bus_type)
{
	for (size_t i = 0; i < xpdev_count; i++) {
		struct xrt_prober_device *xpdev = xpdevs[i];
		if (xpdev->product_id != product_id || //
		    xpdev->vendor_id != vendor_id ||   //
		    xpdev->bus != bus_type) {
			continue;
		}

		return xpdev;
	}

	return NULL;
}

void
u_builder_search(struct xrt_prober *xp,
                 struct xrt_prober_device *const *xpdevs,
                 size_t xpdev_count,
                 const struct u_builder_search_filter *filters,
                 size_t filter_count,
                 struct u_builder_search_results *results)
{
	for (size_t i = 0; i < xpdev_count; i++) {
		struct xrt_prober_device *xpdev = xpdevs[i];
		bool match = false;

		for (size_t k = 0; k < filter_count; k++) {
			struct u_builder_search_filter f = filters[k];

			if (xpdev->product_id != f.product_id || //
			    xpdev->vendor_id != f.vendor_id ||   //
			    xpdev->bus != f.bus_type) {          //
				continue;
			}

			match = true;
			break;
		}

		if (!match) {
			continue;
		}

		results->xpdevs[results->xpdev_count++] = xpdev;

		// Exit if full.
		if (results->xpdev_count >= ARRAY_SIZE(results->xpdevs)) {
			return;
		}
	}
}
