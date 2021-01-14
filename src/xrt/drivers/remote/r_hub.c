// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main hub of the remote driver.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_remote
 */

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_logging.h"

#include "r_interface.h"
#include "r_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#ifndef __USE_MISC
#define __USE_MISC // SOL_TCP on C11
#endif

#include <netinet/tcp.h>

/*
 *
 * Function.
 *
 */

int
setup_accept_fd(struct r_hub *r)
{
	struct sockaddr_in server_address = {0};
	int ret;

	ret = socket(AF_INET, SOCK_STREAM, 0);
	if (ret < 0) {
		U_LOG_E("socket failed: %i", ret);
		return ret;
	}

	r->accept_fd = ret;

	int flag = 1;
	ret = setsockopt(r->accept_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
	if (ret < 0) {
		U_LOG_E("setsockopt failed: %i", ret);
		close(r->accept_fd);
		r->accept_fd = -1;
		return ret;
	}

	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	server_address.sin_port = htons(r->port);

	ret = bind(r->accept_fd, (struct sockaddr *)&server_address, sizeof(server_address));
	if (ret < 0) {
		U_LOG_E("bind failed: %i", ret);
		close(r->accept_fd);
		r->accept_fd = -1;
		return ret;
	}

	listen(r->accept_fd, 5);

	return 0;
}

int
do_accept(struct r_hub *r)
{
	struct sockaddr_in addr = {0};
	int ret, conn_fd;

	socklen_t addr_length = (socklen_t)sizeof(addr);
	ret = accept(r->accept_fd, (struct sockaddr *)&addr, &addr_length);
	if (ret < 0) {
		U_LOG_E("accept failed: %i", ret);
		return ret;
	}

	conn_fd = ret;

	int flags = 1;
	ret = setsockopt(conn_fd, SOL_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags));
	if (ret < 0) {
		U_LOG_E("setsockopt failed: %i", ret);
		close(conn_fd);
		return ret;
	}

	r->rc.fd = conn_fd;

	U_LOG_I("Connection received! %i", r->rc.fd);

	return 0;
}

void *
run_thread(void *ptr)
{
	struct r_hub *r = (struct r_hub *)ptr;
	int ret;

	ret = setup_accept_fd(r);
	if (ret < 0) {
		return NULL;
	}

	while (true) {
		U_LOG_I("Listening on port '%i'.", r->port);

		ret = do_accept(r);

		r_remote_connection_write_one(&r->rc, &r->reset);
		r_remote_connection_write_one(&r->rc, &r->latest);

		while (true) {
			struct r_remote_data data;

			ret = r_remote_connection_read_one(&r->rc, &data);
			if (ret < 0) {
				break;
			}

			r->latest = data;
		}
	}

	return NULL;
}

void
r_hub_destroy(struct r_hub *r)
{
	free(r);
}


/*!
 *
 *
 *
 */

int
r_create_devices(uint16_t port,
                 struct xrt_device **out_hmd,
                 struct xrt_device **out_controller_left,
                 struct xrt_device **out_controller_right)
{
	struct r_hub *r = U_TYPED_CALLOC(struct r_hub);
	int ret;

	r->base.type = XRT_TRACKING_TYPE_RGB;
	r->base.offset.orientation.w = 1.0f; // All other members are zero.
	r->reset.hmd.pose.position.y = 1.6f;
	r->reset.hmd.pose.orientation.w = 1.0f;
	r->reset.left.active = true;
	r->reset.left.pose.position.x = -0.2f;
	r->reset.left.pose.position.y = 1.3f;
	r->reset.left.pose.position.z = -0.5f;
	r->reset.left.pose.orientation.w = 1.0f;
	r->reset.right.active = true;
	r->reset.right.pose.position.x = 0.2f;
	r->reset.right.pose.position.y = 1.3f;
	r->reset.right.pose.position.z = -0.5f;
	r->reset.right.pose.orientation.w = 1.0f;
	r->latest = r->reset;
	r->gui.hmd = true;
	r->gui.left = true;
	r->gui.right = true;
	r->port = port;
	r->accept_fd = -1;
	r->rc.fd = -1;

	snprintf(r->base.name, sizeof(r->base.name), "Remote Simulator");

	ret = os_thread_helper_init(&r->oth);
	if (ret != 0) {
		U_LOG_E("Failed to init threading!");
		r_hub_destroy(r);
		return ret;
	}

	ret = os_thread_helper_start(&r->oth, run_thread, r);
	if (ret != 0) {
		U_LOG_E("Failed to start thread!");
		r_hub_destroy(r);
		return ret;
	}

	*out_hmd = r_hmd_create(r);
	*out_controller_left = r_device_create(r, true);
	*out_controller_right = r_device_create(r, false);

	// Setup variable tracker.
	u_var_add_root(r, "Remote Hub", true);
	// u_var_add_gui_header(r, &r->gui.hmd, "MHD");
	u_var_add_pose(r, &r->latest.hmd.pose, "hmd.pose");
	// u_var_add_gui_header(r, &r->gui.left, "Left");
	u_var_add_bool(r, &r->latest.left.active, "left.active");
	u_var_add_bool(r, &r->latest.left.select, "left.select");
	u_var_add_bool(r, &r->latest.left.menu, "left.menu");
	u_var_add_pose(r, &r->latest.left.pose, "left.pose");
	// u_var_add_gui_header(r, &r->gui.right, "Right");
	u_var_add_bool(r, &r->latest.right.active, "right.active");
	u_var_add_bool(r, &r->latest.right.select, "right.select");
	u_var_add_bool(r, &r->latest.right.menu, "right.menu");
	u_var_add_pose(r, &r->latest.right.pose, "right.pose");

	return 0;
}

int
r_remote_connection_init(struct r_remote_connection *rc, const char *ip_addr, uint16_t port)
{
	struct sockaddr_in addr = {0};
	int conn_fd;
	int ret;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	ret = inet_pton(AF_INET, ip_addr, &addr.sin_addr);
	if (ret < 0) {
		U_LOG_E("socket failed: %i", ret);
		return ret;
	}

	ret = socket(AF_INET, SOCK_STREAM, 0);
	if (ret < 0) {
		U_LOG_E("socket failed: %i", ret);
		return ret;
	}

	conn_fd = ret;

	ret = connect(conn_fd, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		U_LOG_E("connect failed: %i", ret);
		close(conn_fd);
		return ret;
	}

	int flags = 1;
	ret = setsockopt(conn_fd, SOL_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags));
	if (ret < 0) {
		U_LOG_E("setsockopt failed: %i", ret);
		close(conn_fd);
		return ret;
	}

	rc->fd = conn_fd;

	return 0;
}

int
r_remote_connection_read_one(struct r_remote_connection *rc, struct r_remote_data *data)
{
	const size_t size = sizeof(*data);
	size_t current = 0;

	while (current < size) {
		void *ptr = (uint8_t *)data + current;

		ssize_t ret = read(rc->fd, ptr, size - current);
		if (ret < 0) {
			return ret;
		} else if (ret > 0) {
			current += (size_t)ret;
		} else {
			U_LOG_I("Disconnected!");
			return -1;
		}
	}

	return 0;
}

int
r_remote_connection_write_one(struct r_remote_connection *rc, const struct r_remote_data *data)
{
	const size_t size = sizeof(*data);
	size_t current = 0;

	while (current < size) {
		const void *ptr = (const uint8_t *)data + current;

		ssize_t ret = write(rc->fd, ptr, size - current);
		if (ret < 0) {
			return ret;
		} else if (ret > 0) {
			current += (size_t)ret;
		} else {
			U_LOG_I("Disconnected!");
			return -1;
		}
	}

	return 0;
}
