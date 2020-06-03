// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main file for Monado service.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc
 */


#include "target_lists.h"

int
ipc_server_main(int argc, char *argv[]);


int
main(int argc, char *argv[])
{
	return ipc_server_main(argc, argv);
}
