// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Small cli application to control IPC service.
 * @author Pete Black <pblack@collabora.com>
 * @ingroup ipc
 */

#include "client/ipc_client.h"
#include "ipc_client_generated.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#define P(...) fprintf(stdout, __VA_ARGS__)
#define PE(...) fprintf(stderr, __VA_ARGS__)

typedef enum op_mode
{
	MODE_GET,
	MODE_SET_PRIMARY,
	MODE_SET_FOCUSED,
	MODE_TOGGLE_IO,
} op_mode_t;

static int
do_connect(struct ipc_connection *ipc_c);


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
	for (uint32_t i = 0; i < ipc_c->ism->num_isdevs; i++) {
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

	r = ipc_call_system_toggle_io_device(ipc_c, client_id);
	if (r != XRT_SUCCESS) {
		PE("Failed to set focused client to %d.\n", client_id);
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
				PE("Option `-%c' unknown.\n", optopt);
			} else {
				PE("Option `\\x%x' unknown.\n", optopt);
			}
			exit(1);
		default: exit(0);
		}
	}

	struct ipc_connection ipc_c;
	os_mutex_init(&ipc_c.mutex);
	int ret = do_connect(&ipc_c);
	if (ret != 0) {
		return ret;
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

static int
do_connect(struct ipc_connection *ipc_c)
{
	int ret;


	/*
	 * Connenct.
	 */

	ipc_c->imc.socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (ipc_c->imc.socket_fd < 0) {
		ret = ipc_c->imc.socket_fd;
		PE("Socket create error '%i'!\n", ret);
		return -1;
	}

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, IPC_MSG_SOCK_FILE);

	ret = connect(ipc_c->imc.socket_fd,     // socket
	              (struct sockaddr *)&addr, // address
	              sizeof(addr));            // size
	if (ret < 0) {
		PE("Socket connect error '%i'!\n", ret);
		return -1;
	}


	/*
	 * Client info.
	 */

	struct ipc_app_state cs;
	cs.pid = getpid();
	snprintf(cs.info.application_name, sizeof(cs.info.application_name), "%s", "monado-ctl");

	xrt_result_t xret = ipc_call_system_set_client_info(ipc_c, &cs);
	if (xret != XRT_SUCCESS) {
		PE("Failed to set client info '%i'!\n", xret);
		return -1;
	}


	/*
	 * Shared memory.
	 */

	// get our xdev shm from the server and mmap it
	xret = ipc_call_instance_get_shm_fd(ipc_c, &ipc_c->ism_handle, 1);
	if (xret != XRT_SUCCESS) {
		PE("Failed to retrieve shm fd '%i'!\n", xret);
		return -1;
	}

	const int flags = MAP_SHARED;
	const int access = PROT_READ | PROT_WRITE;
	const size_t size = sizeof(struct ipc_shared_memory);

	ipc_c->ism = mmap(NULL, size, access, flags, ipc_c->ism_handle, 0);
	if (ipc_c->ism == NULL) {
		ret = errno;
		PE("Failed to mmap shm '%i'!\n", ret);
		return -1;
	}

	return 0;
}
