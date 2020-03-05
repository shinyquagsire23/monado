// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Daydream prober code.
 * @author Pete Black <pete.black@collabora.com>
 * @ingroup drv_daydream
 */

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "daydream_interface.h"
#include "daydream_device.h"



/*
 *
 * Defines & structs.
 *
 */

// Should the experimental PSVR driver be enabled.
DEBUG_GET_ONCE_BOOL_OPTION(daydream_enable, "DAYDREAM_ENABLE", true)
DEBUG_GET_ONCE_BOOL_OPTION(daydream_spew, "DAYDREAM_PRINT_SPEW", false)
DEBUG_GET_ONCE_BOOL_OPTION(daydream_debug, "DAYDREAM_PRINT_DEBUG", false)

/*!
 * Daydream prober struct.
 *
 * @ingroup drv_daydream
 */
struct daydream_prober
{
	struct xrt_auto_prober base;

	bool print_spew;
	bool print_debug;
	bool enabled;
};


/*
 *
 * Static functions.
 *
 */

static inline struct daydream_prober *
daydream_prober(struct xrt_auto_prober *p)
{
	return (struct daydream_prober *)p;
}

static void
daydream_prober_destroy(struct xrt_auto_prober *p)
{
	struct daydream_prober *pdaydream = daydream_prober(p);

	free(pdaydream);
}

static struct xrt_device *
daydream_prober_autoprobe(struct xrt_auto_prober *xap,
                          bool no_hmds,
                          struct xrt_prober *xp)
{
	struct daydream_prober *pdaydream = daydream_prober(xap);
	if (!pdaydream->enabled) {
		return NULL;
	}

	const char *dev_uuid = "0000fe55-0000-1000-8000-00805f9b34fb";
	const char *char_uuid = "00000001-1000-1000-8000-00805f9b34fb";

	struct os_ble_device *ble = NULL;
	os_ble_notify_open(dev_uuid, char_uuid, &ble);
	if (ble == NULL) {
		return NULL;
	}

	struct daydream_device *dd = daydream_device_create(
	    ble, pdaydream->print_spew, pdaydream->print_debug);

	return &dd->base;
}


/*
 *
 * Exported functions.
 *
 */

struct xrt_auto_prober *
daydream_create_auto_prober()
{
	struct daydream_prober *pdaydream =
	    U_TYPED_CALLOC(struct daydream_prober);
	pdaydream->base.destroy = daydream_prober_destroy;
	pdaydream->base.lelo_dallas_autoprobe = daydream_prober_autoprobe;
	pdaydream->enabled = debug_get_bool_option_daydream_enable();
	pdaydream->print_spew = debug_get_bool_option_daydream_spew();
	pdaydream->print_debug = debug_get_bool_option_daydream_debug();

	return &pdaydream->base;
}
