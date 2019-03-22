// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OSVR HDK prober code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include <hidapi.h>
#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "hdk_device.h"


DEBUG_GET_ONCE_BOOL_OPTION(hdk_spew, "HDK_PRINT_SPEW", false)
DEBUG_GET_ONCE_BOOL_OPTION(hdk_debug, "HDK_PRINT_DEBUG", false)

struct hdk_prober
{
	struct xrt_prober base;
};

static inline struct hdk_prober *
hdk_prober(struct xrt_prober *p)
{
	return (struct hdk_prober *)p;
}

static void
hdk_prober_destroy(struct xrt_prober *p)
{
	struct hdk_prober *hhp = hdk_prober(p);


	free(hhp);
}
#define HDK_MAKE_STRING(NAME, STR)                                             \
	static const char NAME[] = STR;                                        \
	static const wchar_t NAME##_W[] = L##STR

HDK_MAKE_STRING(HDK2_PRODUCT_STRING, "OSVR HDK 2");
HDK_MAKE_STRING(HDK13_PRODUCT_STRING, "OSVR HDK 1.3/1.4");
static const wchar_t HDK1_PRODUCT_STRING_W[] = L"OSVR  HDK 1.x";
static const char HDK12_PRODUCT_STRING[] = "OSVR HDK 1.2";

static const uint16_t HDK_VID = 0x1532;
static const uint16_t HDK_PID = 0x0b00;

static struct xrt_device *
hdk_prober_autoprobe(struct xrt_prober *p)
{
	struct hdk_prober *hhp = hdk_prober(p);

	(void)hhp;

	bool print_spew = debug_get_bool_option_hdk_spew();
	bool print_debug = debug_get_bool_option_hdk_debug();
	struct hid_device_info *devs = hid_enumerate(HDK_VID, HDK_PID);
	struct hid_device_info *current = devs;
	enum HDK_VARIANT variant = HDK_UNKNOWN;
	const char *name = NULL;

	// Just take the first one that responds correctly.
	while (current != NULL) {
		if (current->product_string == NULL ||
		    current->serial_number == NULL) {
			// Skip if the product string is null.
			if (print_debug) {
				fprintf(stderr,
				        "%s - skipping an apparent match with "
				        "null product string\n",
				        __func__);
			}
			continue;
		}
		if (current->serial_number == NULL) {
			// Skip if the serial number is null.
			if (print_debug) {
				fprintf(stderr,
				        "%s - skipping an apparent match with "
				        "null serial number\n",
				        __func__);
			}
			continue;
		}
		if (0 ==
		    wcscmp(HDK2_PRODUCT_STRING_W, current->product_string)) {
			variant = HDK_VARIANT_2;
			name = HDK2_PRODUCT_STRING;
			break;
		} else if (0 == wcscmp(HDK1_PRODUCT_STRING_W,
		                       current->product_string)) {
			variant = HDK_VARIANT_1_2;
			name = HDK12_PRODUCT_STRING;
			break;
		} else {
			//! @todo just assuming anything else is 1.3 for now
			(void)HDK13_PRODUCT_STRING_W;
			variant = HDK_VARIANT_1_3_1_4;
			name = HDK13_PRODUCT_STRING;
			break;
		}
	}
	if (current == NULL) {
		if (print_debug) {
			fprintf(stderr, "%s - no device found\n", __func__);
		}
		hid_free_enumeration(devs);
		return NULL;
	}


	hid_device *dev = hid_open(HDK_VID, HDK_PID, current->serial_number);
	struct hdk_device *hd =
	    hdk_device_create(dev, variant, print_spew, print_debug);
	hid_free_enumeration(devs);
	devs = NULL;

	printf("%s - Found at least the tracker of some HDK: %s\n", __func__,
	       name);

	return &hd->base;
}

struct xrt_prober *
hdk_create_prober()
{
	struct hdk_prober *hhp = U_TYPED_CALLOC(struct hdk_prober);
	hhp->base.destroy = hdk_prober_destroy;
	hhp->base.lelo_dallas_autoprobe = hdk_prober_autoprobe;

	return &hhp->base;
}
