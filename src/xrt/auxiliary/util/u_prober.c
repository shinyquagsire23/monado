// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helpers for prober related code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup aux_util
 */

#include "xrt/xrt_prober.h"

#include "u_prober.h"

#include <string.h>


/*
 *
 * Helpers.
 *
 */

#define ENUM_TO_STR(r)                                                                                                 \
	case r: return #r


/*
 *
 * 'Exported' functions.
 *
 */

const char *
u_prober_string_to_string(enum xrt_prober_string t)
{
	switch (t) {
		ENUM_TO_STR(XRT_PROBER_STRING_MANUFACTURER);
		ENUM_TO_STR(XRT_PROBER_STRING_PRODUCT);
		ENUM_TO_STR(XRT_PROBER_STRING_SERIAL_NUMBER);
	}
	return "XRT_PROBER_STRING_<INVALID>";
}

const char *
u_prober_bus_type_to_string(enum xrt_bus_type t)
{
	switch (t) {
		ENUM_TO_STR(XRT_BUS_TYPE_UNKNOWN);
		ENUM_TO_STR(XRT_BUS_TYPE_USB);
		ENUM_TO_STR(XRT_BUS_TYPE_BLUETOOTH);
	}
	return "XRT_BUS_TYPE_<INVALID>";
}

bool
u_prober_match_string(struct xrt_prober *xp,
                      struct xrt_prober_device *dev,
                      enum xrt_prober_string type,
                      const char *to_match)
{
	unsigned char s[256] = {0};
	int len = xrt_prober_get_string_descriptor(xp, dev, type, s, sizeof(s));
	if (len <= 0) {
		return false;
	}

	return 0 == strncmp(to_match, (const char *)s, sizeof(s));
}
