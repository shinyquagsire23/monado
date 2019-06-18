// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenHMD prober code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ohmd
 */

#include <stdio.h>
#include <stdlib.h>

#include "openhmd.h"
#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "oh_interface.h"
#include "oh_device.h"


DEBUG_GET_ONCE_BOOL_OPTION(oh_spew, "OH_PRINT_SPEW", false)
DEBUG_GET_ONCE_BOOL_OPTION(oh_debug, "OH_PRINT_DEBUG", false)

struct oh_prober
{
	struct xrt_auto_prober base;
	ohmd_context *ctx;
	bool print_spew;
	bool print_debug;
};

static inline struct oh_prober *
oh_prober(struct xrt_auto_prober *p)
{
	return (struct oh_prober *)p;
}

static void
oh_prober_destroy(struct xrt_auto_prober *p)
{
	struct oh_prober *ohp = oh_prober(p);

	if (ohp->ctx != NULL) {
		ohmd_ctx_destroy(ohp->ctx);
		ohp->ctx = NULL;
	}

	free(ohp);
}

static struct xrt_device *
oh_prober_autoprobe(struct xrt_auto_prober *p)
{
	struct oh_prober *ohp = oh_prober(p);

	int device_idx = -1;

	/* Probe for devices */
	int num_devices = ohmd_ctx_probe(ohp->ctx);

	/* Then loop */
	for (int i = 0; i < num_devices; i++) {
		int device_class = 0, device_flags = 0;

		ohmd_list_geti(ohp->ctx, i, OHMD_DEVICE_CLASS, &device_class);
		ohmd_list_geti(ohp->ctx, i, OHMD_DEVICE_FLAGS, &device_flags);

		if (device_class != OHMD_DEVICE_CLASS_HMD) {
			OH_DEBUG(ohp, "Rejecting device idx %i, is not a HMD.",
			         i);
			continue;
		}

		if (device_flags & OHMD_DEVICE_FLAGS_NULL_DEVICE) {
			OH_DEBUG(ohp,
			         "Rejecting device idx %i, is a NULL device.",
			         i);
			continue;
		}

		OH_DEBUG(ohp, "Selecting device idx %i", i);
		device_idx = i;
		break;
	}

	if (device_idx < 0) {
		return NULL;
	}

	const char *prod = ohmd_list_gets(ohp->ctx, device_idx, OHMD_PRODUCT);
	ohmd_device *dev = ohmd_list_open_device(ohp->ctx, device_idx);
	if (dev == NULL) {
		return NULL;
	}

	struct oh_device *ohd = oh_device_create(
	    ohp->ctx, dev, prod, ohp->print_spew, ohp->print_debug);
	return &ohd->base;
}

struct xrt_auto_prober *
oh_create_auto_prober()
{
	struct oh_prober *ohp = U_TYPED_CALLOC(struct oh_prober);
	ohp->base.destroy = oh_prober_destroy;
	ohp->base.lelo_dallas_autoprobe = oh_prober_autoprobe;
	ohp->ctx = ohmd_ctx_create();
	ohp->print_spew = debug_get_bool_option_oh_spew();
	ohp->print_debug = debug_get_bool_option_oh_debug();

	return &ohp->base;
}
