// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Fallback builder the old method of probing devices.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_device.h"
#include "util/u_system_helpers.h"

#include "target_builder_interface.h"

#include <assert.h>


/*
 *
 * Helper functions.
 *
 */

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

static const char *driver_list[] = {
#ifdef XRT_BUILD_DRIVER_HYDRA
    "hydra",
#endif

#ifdef XRT_BUILD_DRIVER_HDK
    "hdk",
#endif

#ifdef XRT_BUILD_DRIVER_VIVE
    "vive",
#endif

#ifdef XRT_BUILD_DRIVER_ULV2
    "ulv2",
#endif

#ifdef XRT_BUILD_DRIVER_DEPTHAI
    "depthai",
#endif

#ifdef XRT_BUILD_DRIVER_WMR
    "wmr",
#endif

#ifdef XRT_BUILD_DRIVER_ARDUINO
    "arduino",
#endif

#ifdef XRT_BUILD_DRIVER_DAYDREAM
    "daydream",
#endif

#ifdef XRT_BUILD_DRIVER_SURVIVE
    "survive",
#endif

#ifdef XRT_BUILD_DRIVER_OHMD
    "oh",
#endif

#ifdef XRT_BUILD_DRIVER_NS
    "ns",
#endif

#ifdef XRT_BUILD_DRIVER_ANDROID
    "android",
#endif

#ifdef XRT_BUILD_DRIVER_ILLIXR
    "illixr",
#endif

#ifdef XRT_BUILD_DRIVER_REALSENSE
    "rs",
#endif

#ifdef XRT_BUILD_DRIVER_EUROC
    "euroc",
#endif

#ifdef XRT_BUILD_DRIVER_QWERTY
    "qwerty",
#endif

#if defined(XRT_BUILD_DRIVER_HANDTRACKING) && defined(XRT_BUILD_DRIVER_DEPTHAI)
    "ht",
#endif

#if defined(XRT_BUILD_DRIVER_SIMULATED)
    "simulated",
#endif
};


/*
 *
 * Member functions.
 *
 */

static xrt_result_t
legacy_estimate_system(struct xrt_builder *xb,
                       cJSON *config,
                       struct xrt_prober *xp,
                       struct xrt_builder_estimate *estimate)
{
	estimate->maybe.head = true;
	estimate->maybe.left = true;
	estimate->maybe.right = true;
	estimate->priority = -20;

	return XRT_SUCCESS;
}

static xrt_result_t
legacy_open_system(struct xrt_builder *xb, cJSON *config, struct xrt_prober *xp, struct xrt_system_devices **out_xsysd)
{
	struct u_system_devices *usysd = u_system_devices_allocate();
	xrt_result_t xret;
	int ret;

	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);


	/*
	 * Create the devices.
	 */

	xret = xrt_prober_probe(xp);
	if (xret != XRT_SUCCESS) {
		return xret;
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

static void
legacy_destroy(struct xrt_builder *xb)
{
	free(xb);
}


/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_builder *
t_builder_legacy_create(void)
{
	struct xrt_builder *xb = U_TYPED_CALLOC(struct xrt_builder);
	xb->estimate_system = legacy_estimate_system;
	xb->open_system = legacy_open_system;
	xb->destroy = legacy_destroy;
	xb->identifier = "legacy";
	xb->name = "Legacy probing system";
	xb->driver_identifiers = driver_list;
	xb->driver_identifier_count = ARRAY_SIZE(driver_list);

	return xb;
}
