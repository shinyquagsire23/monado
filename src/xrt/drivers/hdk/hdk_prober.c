// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OSVR HDK prober code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup drv_hdk
 */

#include <stdio.h>
#include <stdlib.h>

#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "hdk_interface.h"
#include "hdk_device.h"


static const char HDK2_PRODUCT_STRING[] = "OSVR HDK 2";
static const char HDK13_PRODUCT_STRING[] = "OSVR HDK 1.3/1.4";
static const char HDK1_PRODUCT_STRING[] = "OSVR  HDK 1.x";
static const char HDK12_PRODUCT_STRING[] = "OSVR HDK 1.2";

int
hdk_found(struct xrt_prober *xp,
          struct xrt_prober_device **devices,
          size_t num_devices,
          size_t index,
          cJSON *attached_data,
          struct xrt_device **out_xdev)
{
	struct xrt_prober_device *dev = devices[index];

	unsigned char buf[256] = {0};
	int result = xrt_prober_get_string_descriptor(xp, dev, XRT_PROBER_STRING_PRODUCT, buf, sizeof(buf));

	enum HDK_VARIANT variant = HDK_UNKNOWN;
	const char *name = NULL;
	if (0 == strncmp(HDK2_PRODUCT_STRING, (const char *)buf, sizeof(buf))) {
		variant = HDK_VARIANT_2;
		name = HDK2_PRODUCT_STRING;
	} else if (0 == strncmp(HDK1_PRODUCT_STRING, (const char *)buf, sizeof(buf))) {
		variant = HDK_VARIANT_1_2;
		name = HDK12_PRODUCT_STRING;
	} else {
		//! @todo just assuming anything else is 1.3 for now
		variant = HDK_VARIANT_1_3_1_4;
		name = HDK13_PRODUCT_STRING;
	}

	U_LOG_I("%s - Found at least the tracker of some HDK (%s) -- opening\n", __func__, name);

	struct os_hid_device *hid = NULL;
	// Interface 2 is the HID interface.
	result = xrt_prober_open_hid_interface(xp, dev, 2, &hid);
	if (result != 0) {
		return -1;
	}
	struct hdk_device *hd = hdk_device_create(hid, variant);
	if (hd == NULL) {
		return -1;
	}
	*out_xdev = &hd->base;
	return 1;
}
