// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Small file to allow the prober to start.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include <stdio.h>

#include "target_lists.h"
#include "cli_common.h"


static int
ps3_eye_found(struct xrt_prober *xp,
              struct xrt_prober_device **devices,
              size_t num_devices,
              size_t index,
              struct xrt_device **out_xdev)
{
	printf("Found PS3 Eye!\n");
	return 0;
}

struct xrt_prober_entry quirks_list[] = {
    {0x1415, 0x2000, ps3_eye_found, "PS3 Eye"},
    {0x0000, 0x0000, NULL, NULL}, // Terminate
};

struct xrt_prober_entry *entry_lists[] = {
    quirks_list, target_entry_list,
    NULL, // Terminate
};

struct xrt_prober_entry_lists list = {
    entry_lists,
    target_auto_list,
    NULL,
};

int
xrt_prober_create(struct xrt_prober **out_xp)
{
	return xrt_prober_create_with_lists(out_xp, &list);
}
