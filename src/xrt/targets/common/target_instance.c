// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared default implementation of the instance with compositor.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_gfx_native.h"
#include "xrt/xrt_system.h"

#include "util/u_trace_marker.h"
#include "util/u_system_helpers.h"

#include "target_instance_parts.h"

#include <assert.h>


/*
 *
 * Internal functions.
 *
 */

static xrt_result_t
t_instance_create_system(struct xrt_instance *xinst,
                         struct xrt_system_devices **out_xsysd,
                         struct xrt_system_compositor **out_xsysc)
{
	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);
	assert(out_xsysc == NULL || *out_xsysc == NULL);

	struct xrt_system_compositor *xsysc = NULL;
	struct xrt_system_devices *xsysd = NULL;
	xrt_result_t xret = XRT_SUCCESS;

	xret = u_system_devices_create_from_prober(xinst, &xsysd);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	// Early out if we only want devices.
	if (out_xsysc == NULL) {
		*out_xsysd = xsysd;
		return XRT_SUCCESS;
	}

	struct xrt_device *head = xsysd->roles.head;

	xret = xrt_gfx_provider_create_system(head, &xsysc);
	if (xret != XRT_SUCCESS) {
		xrt_system_devices_destroy(&xsysd);
		return xret;
	}

	*out_xsysd = xsysd;
	*out_xsysc = xsysc;

	return xret;
}


/*
 *
 * Exported function(s).
 *
 */

int
xrt_instance_create(struct xrt_instance_info *ii, struct xrt_instance **out_xinst)
{
	struct xrt_prober *xp = NULL;

	u_trace_marker_init();

	int ret = xrt_prober_create_with_lists(&xp, &target_lists);
	if (ret < 0) {
		return ret;
	}

	struct t_instance *tinst = U_TYPED_CALLOC(struct t_instance);
	tinst->base.create_system = t_instance_create_system;
	tinst->base.get_prober = t_instance_get_prober;
	tinst->base.destroy = t_instance_destroy;
	tinst->xp = xp;

	*out_xinst = &tinst->base;

	return 0;
}
