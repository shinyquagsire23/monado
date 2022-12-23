// Copyright 2022-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  @ref drv_wmr driver builder.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_logging.h"
#include "util/u_builders.h"
#include "util/u_config_json.h"
#include "util/u_system_helpers.h"

#include "target_builder_interface.h"

#include "wmr/wmr_interface.h"

#include <assert.h>

#ifndef XRT_BUILD_DRIVER_WMR
#error "Must only be built with XRT_BUILD_DRIVER_WMR set"
#endif


/*
 *
 * Various helper functions and lists.
 *
 */

static const char *driver_list[] = {
    "wmr",
};


/*
 *
 * Member functions.
 *
 */

static xrt_result_t
wmr_estimate_system(struct xrt_builder *xb,
                    cJSON *config,
                    struct xrt_prober *xp,
                    struct xrt_builder_estimate *out_estimate)
{
	return XRT_SUCCESS;
}

static xrt_result_t
wmr_open_system(struct xrt_builder *xb,
                cJSON *config,
                struct xrt_prober *xp,
                struct xrt_system_devices **out_xsysd,
                struct xrt_space_overseer **out_xso)
{
	return XRT_SUCCESS;
}

static void
wmr_destroy(struct xrt_builder *xb)
{
	free(xb);
}


/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_builder *
t_builder_wmr_create(void)
{
	struct xrt_builder *xb = U_TYPED_CALLOC(struct xrt_builder);
	xb->estimate_system = wmr_estimate_system;
	xb->open_system = wmr_open_system;
	xb->destroy = wmr_destroy;
	xb->identifier = "wmr";
	xb->name = "Windows Mixed Reality";
	xb->driver_identifiers = driver_list;
	xb->driver_identifier_count = ARRAY_SIZE(driver_list);

	return xb;
}
