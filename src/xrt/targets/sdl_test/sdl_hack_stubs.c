// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Stubs needed to be able to link in the ipc layer.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_compiler.h"

struct xrt_instance;
struct xrt_system_devices;


int
u_debug_gui_create(void **out_hack)
{
	return 0;
}

void
u_debug_gui_start(void *hack, struct xrt_instance *xinst, struct xrt_system_devices *xsysd)
{
	// Noop
}

void
u_debug_gui_stop(void **hack)
{
	// Noop
}
