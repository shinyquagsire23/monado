// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main hub of the remote driver.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_remote
 */

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"

#include "r_interface.h"
#include "r_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(XRT_OS_WINDOWS)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#ifndef _BSD_SOURCE
#define _BSD_SOURCE // same, but for musl // NOLINT
#endif
#endif

#ifndef SOCKET
#define SOCKET int
#endif

#ifndef __USE_MISC
#define __USE_MISC // SOL_TCP on C11
#endif


/*
 *
 * Small helpers.
 *
 */

DEBUG_GET_ONCE_LOG_OPTION(remote_log, "REMOTE_LOG", U_LOGGING_INFO)

#define R_TRACE(R, ...) U_LOG_IFL_T((R)->rc.log_level, __VA_ARGS__)
#define R_DEBUG(R, ...) U_LOG_IFL_D((R)->rc.log_level, __VA_ARGS__)
#define R_INFO(R, ...) U_LOG_IFL_I((R)->rc.log_level, __VA_ARGS__)
#define R_WARN(R, ...) U_LOG_IFL_W((R)->rc.log_level, __VA_ARGS__)
#define R_ERROR(R, ...) U_LOG_IFL_E((R)->rc.log_level, __VA_ARGS__)

#define RC_TRACE(RC, ...) U_LOG_IFL_T((RC)->log_level, __VA_ARGS__)
#define RC_DEBUG(RC, ...) U_LOG_IFL_D((RC)->log_level, __VA_ARGS__)
#define RC_INFO(RC, ...) U_LOG_IFL_I((RC)->log_level, __VA_ARGS__)
#define RC_WARN(RC, ...) U_LOG_IFL_W((RC)->log_level, __VA_ARGS__)
#define RC_ERROR(RC, ...) U_LOG_IFL_E((RC)->log_level, __VA_ARGS__)


/*
 *
 * Socket functions.
 *
 */



#if defined(XRT_OS_WINDOWS)
static void
socket_close(SOCKET id)
{
	closesocket(id);
}

static SOCKET
socket_create(void)
{
	return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

static int
socket_set_opt(SOCKET id, int flag)
{
	return setsockopt(id, SOL_SOCKET, SO_REUSEADDR, (const char *)&flag, sizeof(flag));
}

static ssize_t
socket_read(SOCKET id, void *ptr, size_t size, size_t current)
{
	return recv(id, (char *)ptr, size - current, 0);
}

static size_t
socket_write(SOCKET id, void *ptr, size_t size, size_t current)
{
	return send(id, (const char *)ptr, size - current, 0);
}

#else
static void
socket_close(SOCKET id)
{
	close(id);
}

static SOCKET
socket_create(void)
{
	return socket(AF_INET, SOCK_STREAM, 0);
}

static int
socket_set_opt(SOCKET id, int flag)
{
	return setsockopt(id, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
}

static ssize_t
socket_read(SOCKET id, void *ptr, size_t size, size_t current)
{
	return read(id, ptr, size - current);
}

static ssize_t
socket_write(SOCKET id, void *ptr, size_t size, size_t current)
{
	return write(id, ptr, size - current);
}
#endif

static int
setup_accept_fd(struct r_hub *r)
{
	struct sockaddr_in server_address = {0};
#if defined(XRT_OS_WINDOWS)
	// Initialize Winsock.
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		int error = WSAGetLastError();
		R_ERROR(r, "Failed to do WSAStartup %ld", error);
		return error;
	}
#endif
	SOCKET ret = socket_create();

	if (ret < 0) {
		R_ERROR(r, "socket: %i", ret);
		goto cleanup;
	}

	r->accept_fd = ret;

	int flag = 1;
	ret = socket_set_opt(r->accept_fd, flag);
	if (ret < 0) {
		R_ERROR(r, "setsockopt: %i", ret);
		socket_close(r->accept_fd);
		r->accept_fd = -1;
		goto cleanup;
	}

	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	server_address.sin_port = htons(r->port);

	ret = bind(r->accept_fd, (struct sockaddr *)&server_address, sizeof(server_address));
	if (ret < 0) {
		R_ERROR(r, "bind: %i", ret);
		socket_close(r->accept_fd);
		r->accept_fd = -1;
		goto cleanup;
	}

	R_INFO(r, "Listen address %s on port %d", inet_ntoa(server_address.sin_addr), r->port);

	listen(r->accept_fd, 5);

	return 0;
cleanup:
#if defined(XRT_OS_WINDOWS)
	WSACleanup();
#endif
	return ret;
}

static bool
wait_for_read_and_to_continue(struct r_hub *r, SOCKET socket)
{
	fd_set set;
	int ret = 0;

	// To be more roboust
	if (socket < 0) {
		return false;
	}

	while (os_thread_helper_is_running(&r->oth) && ret == 0) {
		// Select can modify timeout, reset each loop.
		struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};

		// Reset each loop.
		FD_ZERO(&set);
		FD_SET(socket, &set);

		ret = select(socket + 1, &set, NULL, NULL, &timeout);
	}

	if (ret < 0) {
		R_ERROR(r, "select: %i", ret);
		return false;
	} else if (ret > 0) {
		return true;
	} else {
		return false;
	}
}

static int
do_accept(struct r_hub *r)
{
	struct sockaddr_in addr = {0};
	int ret = 0;
	if (!wait_for_read_and_to_continue(r, r->accept_fd)) {
		R_ERROR(r, "Failed to wait for id %d", r->accept_fd);
		return -1;
	}

	socklen_t addr_length = (socklen_t)sizeof(addr);
	ret = accept(r->accept_fd, (struct sockaddr *)&addr, &addr_length);
	if (ret < 0) {
		R_ERROR(r, "accept: %i", ret);
		return ret;
	}

	SOCKET conn_fd = ret;

	int flags = 1;
	ret = socket_set_opt(r->accept_fd, flags);
	if (ret < 0) {
		R_ERROR(r, "setsockopt: %i", ret);
		socket_close(conn_fd);
		return ret;
	}

	r->rc.fd = conn_fd;

	R_INFO(r, "Connection received! %i", r->rc.fd);

	return 0;
}

static int
read_one(struct r_hub *r, struct r_remote_data *data)
{
	struct r_remote_connection *rc = &r->rc;

	const size_t size = sizeof(*data);
	size_t current = 0;

	while (current < size) {
		void *ptr = (uint8_t *)data + current;

		if (!wait_for_read_and_to_continue(r, rc->fd)) {
			return -1;
		}

		ssize_t ret = socket_read(rc->fd, ptr, size, current);
		if (ret < 0) {
#if defined(XRT_OS_WINDOWS)
			RC_ERROR(rc, "recv: %zi", WSAGetLastError());
#else
			RC_ERROR(rc, "read: %zi", ret);
#endif
			return ret;
		} else if (ret > 0) {
			current += (size_t)ret;
		} else {
			R_INFO(r, "Disconnected!");
			return -1;
		}
	}

	return 0;
}

static void *
run_thread(void *ptr)
{
	struct r_hub *r = (struct r_hub *)ptr;
	int ret;

	ret = setup_accept_fd(r);
	if (ret < 0) {
		R_INFO(r, "Leaving thread");
		return NULL;
	}

	while (os_thread_helper_is_running(&r->oth)) {
		R_INFO(r, "Listening on port '%i'.", r->port);

		ret = do_accept(r);
		if (ret < 0) {
			R_INFO(r, "Leaving thread");
			return NULL;
		}

		r_remote_connection_write_one(&r->rc, &r->reset);
		r_remote_connection_write_one(&r->rc, &r->latest);

		while (true) {
			struct r_remote_data data;

			ret = read_one(r, &data);
			if (ret < 0) {
				break;
			}

			r->latest = data;
		}
	}

	R_INFO(r, "Leaving thread");

	return NULL;
}

static void
r_hub_system_devices_destroy(struct xrt_system_devices *xsysd)
{
	struct r_hub *r = (struct r_hub *)xsysd;

	R_DEBUG(r, "Destroying");

	// Stop the thread first.
	os_thread_helper_stop_and_wait(&r->oth);

	// Destroy all of the devices now.
	for (uint32_t i = 0; i < ARRAY_SIZE(r->base.xdevs); i++) {
		xrt_device_destroy(&r->base.xdevs[i]);
	}

	// Should be safe to destroy the sockets now.
	if (r->accept_fd >= 0) {
		socket_close(r->accept_fd);
		r->accept_fd = -1;
	}

	if (r->rc.fd >= 0) {
		close(r->rc.fd);
		r->rc.fd = -1;
	}

	free(r);

#if defined(XRT_OS_WINDOWS)
	// Clean up Winsock.
	WSACleanup();
#endif
}


/*
 *
 * 'Exported' create function.
 *
 */

int
r_create_devices(uint16_t port, struct xrt_system_devices **out_xsysd)
{
	struct r_hub *r = U_TYPED_CALLOC(struct r_hub);
	int ret;

	r->base.destroy = r_hub_system_devices_destroy;
	r->origin.type = XRT_TRACKING_TYPE_RGB;
	r->origin.offset = (struct xrt_pose)XRT_POSE_IDENTITY;
	r->reset.head.center = (struct xrt_pose)XRT_POSE_IDENTITY;
	r->reset.head.center.position.y = 1.6f;
	r->reset.left.active = true;
	r->reset.left.hand_tracking_active = true;
	r->reset.left.pose.position.x = -0.2f;
	r->reset.left.pose.position.y = 1.3f;
	r->reset.left.pose.position.z = -0.5f;
	r->reset.left.pose.orientation.w = 1.0f;

	r->reset.right.active = true;
	r->reset.right.hand_tracking_active = true;
	r->reset.right.pose.position.x = 0.2f;
	r->reset.right.pose.position.y = 1.3f;
	r->reset.right.pose.position.z = -0.5f;
	r->reset.right.pose.orientation.w = 1.0f;
	r->latest = r->reset;
	r->rc.log_level = debug_get_log_option_remote_log();
	r->gui.hmd = true;
	r->gui.left = true;
	r->gui.right = true;
	r->port = port;
	r->accept_fd = -1;
	r->rc.fd = -1;

	snprintf(r->origin.name, sizeof(r->origin.name), "Remote Simulator");

	ret = os_thread_helper_init(&r->oth);
	if (ret != 0) {
		R_ERROR(r, "Failed to init threading!");
		r_hub_system_devices_destroy(&r->base);
		return XRT_ERROR_ALLOCATION;
	}

	ret = os_thread_helper_start(&r->oth, run_thread, r);
	if (ret != 0) {
		R_ERROR(r, "Failed to start thread!");
		r_hub_system_devices_destroy(&r->base);
		return XRT_ERROR_ALLOCATION;
	}


	/*
	 * Setup system devices.
	 */

	struct xrt_device *head = r_hmd_create(r);
	struct xrt_device *left = r_device_create(r, true);
	struct xrt_device *right = r_device_create(r, false);

	r->base.xdevs[r->base.xdev_count++] = head;
	r->base.xdevs[r->base.xdev_count++] = left;
	r->base.xdevs[r->base.xdev_count++] = right;

	r->base.roles.head = head;
	r->base.roles.left = left;
	r->base.roles.right = right;
	r->base.roles.hand_tracking.left = left;
	r->base.roles.hand_tracking.right = right;


	/*
	 * Setup variable tracker.
	 */

	u_var_add_root(r, "Remote Hub", true);
	// u_var_add_gui_header(r, &r->gui.hmd, "MHD");
	u_var_add_pose(r, &r->latest.head.center, "head.center");
	// u_var_add_gui_header(r, &r->gui.left, "Left");
	u_var_add_bool(r, &r->latest.left.active, "left.active");
	u_var_add_pose(r, &r->latest.left.pose, "left.pose");
	// u_var_add_gui_header(r, &r->gui.right, "Right");
	u_var_add_bool(r, &r->latest.right.active, "right.active");
	u_var_add_pose(r, &r->latest.right.pose, "right.pose");

	/*
	 * Done now.
	 */

	*out_xsysd = &r->base;

	return XRT_SUCCESS;
}


/*
 *
 * 'Exported' connection functions.
 *
 */

int
r_remote_connection_init(struct r_remote_connection *rc, const char *ip_addr, uint16_t port)
{
	struct sockaddr_in addr = {0};
	int conn_fd;
	int ret;

	// Set log level.
	rc->log_level = debug_get_log_option_remote_log();

#if defined(XRT_OS_WINDOWS)
	// Initialize Winsock.
	WSADATA wsaData;
	ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (ret != 0) {
		RC_ERROR(rc, "Failed to do WSAStartup %ld", WSAGetLastError());
		return ret;
	}
#endif

	// Address
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	// inet_pton/InetPton resolves "localhost" as 0.0.0.0 or 255.255.255.255, and it causes connection error. To
	// avoid this issue, the following logic converts "localhost" to "127.0.0.1" first.
	if (strcmp("localhost", ip_addr) == 0) {
		ret = inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
	} else {
		ret = inet_pton(AF_INET, ip_addr, &addr.sin_addr);
	}
	if (ret < 0) {
		RC_ERROR(rc, "Failed to do inet pton for %s: %i", ip_addr, ret);
		goto cleanup;
	}

	ret = socket_create();
	if (ret < 0) {
#if defined(XRT_OS_WINDOWS)
		RC_ERROR(rc, "Failed to create socket %ld", WSAGetLastError());
#else
		RC_ERROR(rc, "Failed to create socket: %i", ret);
#endif
		goto cleanup;
	}

	conn_fd = ret;

	ret = connect(conn_fd, (struct sockaddr *)&addr, sizeof(addr));
	// If connect operation succeed, both Windows and POSIX returns 0.
	if (ret != 0) {
#if defined(XRT_OS_WINDOWS)
		RC_ERROR(rc, "Failed to connect id %d and addr %s with failure %d", conn_fd, inet_ntoa(addr.sin_addr),
		         WSAGetLastError());
#else
		RC_ERROR(rc, "Failed to connect id %d and addr %s with failure %d", conn_fd, inet_ntoa(addr.sin_addr),
		         ret);
#endif
		socket_close(conn_fd);
		goto cleanup;
	}

	int flags = 1;
	ret = socket_set_opt(conn_fd, flags);
	if (ret < 0) {
		RC_ERROR(rc, "Failed to setsockopt: %i", ret);
		socket_close(conn_fd);
		goto cleanup;
	}

	rc->fd = conn_fd;

	return 0;

cleanup:
#if defined(XRT_OS_WINDOWS)
	WSACleanup();
#endif
	return ret;
}

int
r_remote_connection_read_one(struct r_remote_connection *rc, struct r_remote_data *data)
{
	const size_t size = sizeof(*data);
	size_t current = 0;

	while (current < size) {
		void *ptr = (uint8_t *)data + current;
		ssize_t ret = socket_read(rc->fd, ptr, size, current);
		if (ret < 0) {
			RC_ERROR(rc, "read: %zi", ret);
			return ret;
		}
		if (ret > 0) {
			current += (size_t)ret;
		} else {
			RC_INFO(rc, "Disconnected!");
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
		void *ptr = (uint8_t *)data + current;

		ssize_t ret = socket_write(rc->fd, ptr, size, current);
		if (ret < 0) {
			RC_ERROR(rc, "write: %zi", ret);
			return ret;
		}
		if (ret > 0) {
			current += (size_t)ret;
		} else {
			RC_INFO(rc, "Disconnected!");
			return -1;
		}
	}

	return 0;
}
