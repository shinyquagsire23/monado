// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helpers for system objects like @ref xrt_system_devices.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "xrt/xrt_device.h"
#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_device.h"
#include "util/u_system_helpers.h"

#include <assert.h>


/*
 *
 * Internal functions.
 *
 */

static void
destroy(struct xrt_system_devices *xsysd)
{
	struct u_system_devices *usysd = u_system_devices(xsysd);

	for (uint32_t i = 0; i < ARRAY_SIZE(usysd->base.xdevs); i++) {
		xrt_device_destroy(&usysd->base.xdevs[i]);
	}

	xrt_frame_context_destroy_nodes(&usysd->xfctx);

	free(usysd);
}


/*
 *
 * 'Exported' functions.
 *
 */

struct u_system_devices *
u_system_devices_allocate(void)
{
	struct u_system_devices *usysd = U_TYPED_CALLOC(struct u_system_devices);
	usysd->base.destroy = destroy;

	return usysd;
}

xrt_result_t
u_system_devices_create_from_prober(struct xrt_instance *xinst, struct xrt_system_devices **out_xsysd)
{
	xrt_result_t xret;

	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);


	/*
	 * Create the devices.
	 */

	struct xrt_prober *xp = NULL;
	xret = xrt_instance_get_prober(xinst, &xp);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	xret = xrt_prober_probe(xp);
	if (xret < 0) {
		return xret;
	}

	return xrt_prober_create_system(xp, out_xsysd);
}

struct xrt_device *
u_system_devices_get_ht_device(struct u_system_devices *usysd, enum xrt_input_name name)
{
	for (uint32_t i = 0; i < usysd->base.xdev_count; i++) {
		struct xrt_device *xdev = usysd->base.xdevs[i];

		if (xdev == NULL || !xdev->hand_tracking_supported) {
			continue;
		}

		for (uint32_t j = 0; j < xdev->input_count; j++) {
			struct xrt_input *input = &xdev->inputs[j];

			if (input->name == name) {
				return xdev;
			}
		}
	}

	return NULL;
}
