// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Small cli application to control IPC service.
 * @author Pete Black <pblack@collabora.com>
 * @ingroup ipc
 */

#include "util/u_file.h"

#include "client/ipc_client.h"
#include "client/ipc_client_connection.h"

#include "ipc_client_generated.h"

#include <ctype.h>


#define P(...) fprintf(stdout, __VA_ARGS__)
#define PE(...) fprintf(stderr, __VA_ARGS__)

typedef enum op_mode
{
	MODE_GET,
	MODE_SET_PRIMARY,
	MODE_SET_FOCUSED,
	MODE_TOGGLE_IO,
} op_mode_t;


int
get_mode(struct ipc_connection *ipc_c)
{
	struct ipc_client_list clients;

	xrt_result_t r;

	r = ipc_call_system_get_clients(ipc_c, &clients);
	if (r != XRT_SUCCESS) {
		PE("Failed to get client list.\n");
		exit(1);
	}

	P("Clients:\n");
	for (uint32_t i = 0; i < IPC_MAX_CLIENTS; i++) {
		if (clients.ids[i] < 0) {
			continue;
		}

		struct ipc_app_state cs;
		r = ipc_call_system_get_client_info(ipc_c, i, &cs);
		if (r != XRT_SUCCESS) {
			PE("Failed to get client info for client %d.\n", i);
			return 1;
		}

		P("\tid: %d"
		  "\tact: %d"
		  "\tdisp: %d"
		  "\tfoc: %d"
		  "\tio: %d"
		  "\tovly: %d"
		  "\tz: %d"
		  "\tpid: %d"
		  "\t%s\n",
		  clients.ids[i],     //
		  cs.session_active,  //
		  cs.session_visible, //
		  cs.session_focused, //
		  cs.io_active,       //
		  cs.session_overlay, //
		  cs.z_order,         //
		  cs.pid,             //
		  cs.info.application_name);
	}

	P("\nDevices:\n");
	for (uint32_t i = 0; i < ipc_c->ism->isdev_count; i++) {
		struct ipc_shared_device *isdev = &ipc_c->ism->isdevs[i];
		P("\tid: %d"
		  "\tname: %d"
		  "\t\"%s\"\n",
		  i,           //
		  isdev->name, //
		  isdev->str); //
	}

	return 0;
}

int
set_primary(struct ipc_connection *ipc_c, int client_id)
{
	xrt_result_t r;

	r = ipc_call_system_set_primary_client(ipc_c, client_id);
	if (r != XRT_SUCCESS) {
		PE("Failed to set active client to %d.\n", client_id);
		return 1;
	}

	return 0;
}

int
set_focused(struct ipc_connection *ipc_c, int client_id)
{
	xrt_result_t r;

	r = ipc_call_system_set_focused_client(ipc_c, client_id);
	if (r != XRT_SUCCESS) {
		PE("Failed to set focused client to %d.\n", client_id);
		return 1;
	}

	return 0;
}

int
toggle_io(struct ipc_connection *ipc_c, int client_id)
{
	xrt_result_t r;

	r = ipc_call_system_toggle_io_client(ipc_c, client_id);
	if (r != XRT_SUCCESS) {
		PE("Failed to toggle io for client %d.\n", client_id);
		return 1;
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	op_mode_t op_mode = MODE_GET;

	// parse arguments
	int c;
	int s_val = 0;

	opterr = 0;
	while ((c = getopt(argc, argv, "p:f:i:")) != -1) {
		switch (c) {
		case 'p':
			s_val = atoi(optarg);
			if (s_val >= 0 && s_val < IPC_MAX_CLIENTS) {
				op_mode = MODE_SET_PRIMARY;
			}
			break;
		case 'f':
			s_val = atoi(optarg);
			if (s_val >= 0 && s_val < IPC_MAX_CLIENTS) {
				op_mode = MODE_SET_FOCUSED;
			}
			break;
		case 'i':
			s_val = atoi(optarg);
			if (s_val >= 0 && s_val < IPC_MAX_CLIENTS) {
				op_mode = MODE_TOGGLE_IO;
			}
			break;
		case '?':
			if (optopt == 's') {
				PE("Option -s requires an id to set.\n");
			} else if (isprint(optopt)) {
				PE("Option `-%c' unknown. Usage:\n", optopt);
				PE("    -f <id>: Set focused client\n");
				PE("    -p <id>: Set primary client\n");
				PE("    -i <id>: Toggle whether client receives input\n");
			} else {
				PE("Option `\\x%x' unknown.\n", optopt);
			}
			exit(1);
		default: exit(0);
		}
	}

	// Connection struct on the stack, super simple.
	struct ipc_connection ipc_c = {0};

	struct xrt_instance_info info = {
	    .application_name = "monado-ctl",
	};

	xrt_result_t xret = ipc_client_connection_init(&ipc_c, U_LOGGING_INFO, &info);
	if (xret != XRT_SUCCESS) {
		U_LOG_E("ipc_client_connection_init: %u", xret);
		return -1;
	}

	switch (op_mode) {
	case MODE_GET: exit(get_mode(&ipc_c)); break;
	case MODE_SET_PRIMARY: exit(set_primary(&ipc_c, s_val)); break;
	case MODE_SET_FOCUSED: exit(set_focused(&ipc_c, s_val)); break;
	case MODE_TOGGLE_IO: exit(toggle_io(&ipc_c, s_val)); break;
	default: P("Unrecognised operation mode.\n"); exit(1);
	}

	return 0;
}
