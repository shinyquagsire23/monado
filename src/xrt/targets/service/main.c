// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main file for Monado service.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc
 */

#include "util/u_trace_marker.h"

#include "target_lists.h"


// Insert the on load constructor to init trace marker.
U_TRACE_TARGET_SETUP(U_TRACE_WHICH_SERVICE)

int
ipc_server_main(int argc, char *argv[]);


int
main(int argc, char *argv[])
{
	u_trace_marker_init();

	return ipc_server_main(argc, argv);
}
