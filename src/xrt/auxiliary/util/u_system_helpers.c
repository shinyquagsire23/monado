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

static struct xrt_device *
get_ht_device(struct u_system_devices *usysd, enum xrt_input_name name)
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
	struct u_system_devices *usysd = u_system_devices_allocate();
	int ret = 0;

	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);


	/*
	 * Create the devices.
	 */

	struct xrt_prober *xp = NULL;
	ret = xrt_instance_get_prober(xinst, &xp);
	if (ret < 0) {
		return XRT_ERROR_ALLOCATION;
	}

	ret = xrt_prober_probe(xp);
	if (ret < 0) {
		return XRT_ERROR_ALLOCATION;
	}

	ret = xrt_prober_select(xp, usysd->base.xdevs, ARRAY_SIZE(usysd->base.xdevs));
	if (ret < 0) {
		u_system_devices_destroy(&usysd);
	}

	// Count the xdevs.
	for (uint32_t i = 0; i < ARRAY_SIZE(usysd->base.xdevs); i++) {
		if (usysd->base.xdevs[i] == NULL) {
			break;
		}

		usysd->base.xdev_count++;
	}


	/*
	 * Setup the roles.
	 */

	int head, left, right;
	u_device_assign_xdev_roles(usysd->base.xdevs, usysd->base.xdev_count, &head, &left, &right);

	if (head >= 0) {
		usysd->base.roles.head = usysd->base.xdevs[head];
	}
	if (left >= 0) {
		usysd->base.roles.left = usysd->base.xdevs[left];
	}
	if (right >= 0) {
		usysd->base.roles.right = usysd->base.xdevs[right];
	}

	// Find hand tracking devices.
	usysd->base.roles.hand_tracking.left = get_ht_device(usysd, XRT_INPUT_GENERIC_HAND_TRACKING_LEFT);
	usysd->base.roles.hand_tracking.right = get_ht_device(usysd, XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT);


	/*
	 * Done.
	 */

	*out_xsysd = &usysd->base;

	return XRT_SUCCESS;
}
