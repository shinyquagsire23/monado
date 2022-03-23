// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Hack for making it easy to run our tracking with DepthAI cameras.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#include "ht_interface.h"

#include "util/u_debug.h"


DEBUG_GET_ONCE_BOOL_OPTION(ht_use_depthai, "HT_USE_DEPTHAI", false)

static void
ht_prober_destroy(struct xrt_auto_prober *p)
{
	free(p);
}

static int
ht_prober_autoprobe(struct xrt_auto_prober *xap,
                    cJSON *attached_data,
                    bool no_hmds,
                    struct xrt_prober *xp,
                    struct xrt_device **out_xdevs)
{
	if (!debug_get_bool_option_ht_use_depthai()) {
		return 0;
	}

	struct xrt_device *ht = ht_device_create_depthai_ov9282();

	if (ht == NULL) {
		return 0;
	}

	int out_idx = 0;

	out_xdevs[out_idx++] = ht;
	return out_idx;
}



struct xrt_auto_prober *
ht_create_auto_prober()
{
	struct xrt_auto_prober *xap = U_TYPED_CALLOC(struct xrt_auto_prober);
	xap->name = "ht_depthai";
	xap->destroy = ht_prober_destroy;
	xap->lelo_dallas_autoprobe = ht_prober_autoprobe;

	return xap;
}
