// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Small cli application to control IPC service.
 * @author Pete Black <pblack@collabora.com>
 * @ingroup ipc
 */

#include "ipc_client.h"
#include "ipc_client_generated.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <ctype.h>
#include <unistd.h>

typedef enum op_mode
{
	MODE_GET,
	MODE_SET_PRIMARY,
	MODE_SET_FOCUSED,
} op_mode_t;

int
get_mode(struct ipc_connection *ipc_c)
{
	struct ipc_client_list clients;

	xrt_result_t r;

	r = ipc_call_system_get_clients(ipc_c, &clients);
	if (r != XRT_SUCCESS) {
		printf("failed to get client list.\n");
		exit(1);
	}

	for (uint32_t i = 0; i < IPC_MAX_CLIENTS; i++) {
		if (clients.ids[i] < 0) {
			continue;
		}

		struct ipc_app_state cs;
		r = ipc_call_system_get_client_info(ipc_c, i, &cs);
		if (r != XRT_SUCCESS) {
			printf(
			    "failed to get client info "
			    "for client %d.\n",
			    i);
			return 1;
		}

		printf(
		    "id: %d\tact: %d\tdisp: "
		    "%d\tfoc: %d\tovly: %d\tz: "
		    "%d\tpid: "
		    "%d\t %s\t\n",
		    clients.ids[i], cs.session_active, cs.session_visible,
		    cs.session_focused, cs.session_overlay, cs.z_order, cs.pid,
		    cs.info.application_name);
	}

	return 0;
}

int
set_primary(struct ipc_connection *ipc_c, int client_id)
{
	xrt_result_t r;

	r = ipc_call_system_set_primary_client(ipc_c, client_id);
	if (r != XRT_SUCCESS) {
		printf("failed to set active client to %d.\n", client_id);
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
		printf("failed to set focused client to %d.\n", client_id);
		return 1;
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	struct ipc_connection ipc_c;
	os_mutex_init(&ipc_c.mutex);

	op_mode_t op_mode = MODE_GET;

	// parse arguments
	int c;
	int s_val = 0;

	opterr = 0;
	while ((c = getopt(argc, argv, "p:f:")) != -1) {
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

		case '?':
			if (optopt == 's') {
				fprintf(stderr,
				        "Option -s requires an id to set.\n");
			} else if (isprint(optopt)) {
				fprintf(stderr, "Option `-%c' unknown.\n",
				        optopt);
			} else {
				fprintf(stderr, "Option `\\x%x' unknown.\n",
				        optopt);
			}
			exit(1);
		default: exit(0);
		}
	}

	bool socket_created = true;
	bool socket_connected = true;

	int fd;
	struct sockaddr_un addr;

	if ((fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
		printf("Socket Create Error!\n");
		socket_created = false;
	}

	if (socket_created) {
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strcpy(addr.sun_path, IPC_MSG_SOCK_FILE);
		if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
			printf("Socket Connect error!\n");
			socket_connected = false;
		}
	}

	if (socket_connected) {
		ipc_c.imc.socket_fd = fd;

		struct ipc_app_state cs;
		cs.pid = getpid();

		snprintf(cs.info.application_name,
		         sizeof(cs.info.application_name), "%s", "monado-ctl");

		xrt_result_t r = ipc_call_system_set_client_info(&ipc_c, &cs);
		if (r != XRT_SUCCESS) {
			printf("failed to set client info.\n");
			exit(1);
		}

		switch (op_mode) {
		case MODE_GET:
			exit(get_mode(&ipc_c));
			break;
		case MODE_SET_PRIMARY:
			exit(set_primary(&ipc_c, s_val));
			break;
		case MODE_SET_FOCUSED:
			exit(set_focused(&ipc_c, s_val));
			break;
		default: printf("Unrecognised operation mode.\n"); exit(1);
		}
	}

	close(fd);
}
