// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  PSVR prober code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup drv_psvr
 */

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include <hidapi.h>
#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "psvr_interface.h"
#include "psvr_device.h"


/*
 *
 * Defines & structs.
 *
 */

// Should the experimental PSVR driver be enabled.
DEBUG_GET_ONCE_BOOL_OPTION(psvr_enable, "PSVR_ENABLE", true)
DEBUG_GET_ONCE_BOOL_OPTION(psvr_spew, "PSVR_PRINT_SPEW", false)
DEBUG_GET_ONCE_BOOL_OPTION(psvr_debug, "PSVR_PRINT_DEBUG", false)

/*!
 * PSVR prober struct.
 *
 * @ingroup drv_psvr
 */
struct psvr_prober
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

static inline struct psvr_prober *
psvr_prober(struct xrt_auto_prober *p)
{
	return (struct psvr_prober *)p;
}

static void
psvr_prober_destroy(struct xrt_auto_prober *p)
{
	struct psvr_prober *ppsvr = psvr_prober(p);

	free(ppsvr);
}

static struct xrt_device *
psvr_prober_autoprobe(struct xrt_auto_prober *p)
{
	struct psvr_prober *ppsvr = psvr_prober(p);
	struct hid_device_info *info_control = NULL;
	struct hid_device_info *info_handle = NULL;
	struct hid_device_info *cur_dev = NULL;
	struct hid_device_info *devs = NULL;
	struct xrt_device *dev = NULL;

	devs = hid_enumerate(PSVR_VID, PSVR_PID);
	cur_dev = devs;

	for (; cur_dev != NULL; cur_dev = cur_dev->next) {
		switch (cur_dev->interface_number) {
		case PSVR_HANDLE_IFACE: info_handle = cur_dev; break;
		case PSVR_CONTROL_IFACE: info_control = cur_dev; break;
		default: break;
		}
	}

	if (info_control != NULL && info_handle != NULL) {
		if (ppsvr->enabled) {
			dev = psvr_device_create(info_handle, info_control,
			                         ppsvr->print_spew,
			                         ppsvr->print_debug);
		} else {
			PSVR_DEBUG(ppsvr,
			           "Found a PSVR hmd but driver is disabled");
		}
	}

	hid_free_enumeration(devs);

	return dev;
}


/*
 *
 * Exported functions.
 *
 */

struct xrt_auto_prober *
psvr_create_auto_prober()
{
	struct psvr_prober *ppsvr = U_TYPED_CALLOC(struct psvr_prober);
	ppsvr->base.destroy = psvr_prober_destroy;
	ppsvr->base.lelo_dallas_autoprobe = psvr_prober_autoprobe;
	ppsvr->enabled = debug_get_bool_option_psvr_enable();
	ppsvr->print_spew = debug_get_bool_option_psvr_spew();
	ppsvr->print_debug = debug_get_bool_option_psvr_debug();

	return &ppsvr->base;
}
