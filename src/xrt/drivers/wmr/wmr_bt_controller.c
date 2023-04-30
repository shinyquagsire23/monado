// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// Copyright 2020-2023, Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Driver for Bluetooth based WMR Controller.
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @ingroup drv_wmr
 */

#include "os/os_time.h"
#include "os/os_hid.h"

#include "util/u_trace_marker.h"

#include "wmr_common.h"
#include "wmr_bt_controller.h"
#include "wmr_controller.h"
#include "wmr_config_key.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#define WMR_TRACE(c, ...) U_LOG_IFL_T(c->log_level, __VA_ARGS__)
#define WMR_DEBUG(c, ...) U_LOG_IFL_D(c->log_level, __VA_ARGS__)
#define WMR_INFO(c, ...) U_LOG_IFL_I(c->log_level, __VA_ARGS__)
#define WMR_WARN(c, ...) U_LOG_IFL_W(c->log_level, __VA_ARGS__)
#define WMR_ERROR(c, ...) U_LOG_IFL_E(c->log_level, __VA_ARGS__)

static inline struct wmr_bt_connection *
wmr_bt_connection(struct wmr_controller_connection *p)
{
	return (struct wmr_bt_connection *)p;
}

static bool
read_packets(struct wmr_bt_connection *conn)
{
	DRV_TRACE_MARKER();

	unsigned char buffer[WMR_MOTION_CONTROLLER_MSG_BUFFER_SIZE];

	// Better cpu efficiency with blocking reads instead of multiple reads.
	os_mutex_lock(&conn->hid_lock);
	int size = os_hid_read(conn->controller_hid, buffer, sizeof(buffer), 500);

	// Get the timing as close to reading packet as possible.
	uint64_t now_ns = os_monotonic_get_ns();
	os_mutex_unlock(&conn->hid_lock);

	DRV_TRACE_IDENT(read_packets_got);

	if (size < 0) {
		WMR_ERROR(conn, "WMR Controller (Bluetooth): Error reading from device");
		return false;
	}
	if (size == 0) {
		WMR_TRACE(conn, "WMR Controller (Bluetooth): No data to read from device");
		return true; // No more messages, return.
	}

	WMR_TRACE(conn, "WMR Controller (Bluetooth): Read %u bytes from device", size);

	struct wmr_controller_connection *wcc = (struct wmr_controller_connection *)conn;
	wmr_controller_connection_receive_bytes(wcc, now_ns, buffer, size);

	return true;
}

static bool
send_bytes(struct wmr_controller_connection *wcc, const uint8_t *buffer, uint32_t buf_size)
{
	struct wmr_bt_connection *conn = (struct wmr_bt_connection *)(wcc);

	os_mutex_lock(&conn->hid_lock);
	int ret = os_hid_write(conn->controller_hid, buffer, buf_size);
	os_mutex_unlock(&conn->hid_lock);

	return ret != -1 && (uint32_t)(ret) == buf_size;
}

/* Synchronously read a buffer from the HID connection.
 * This is only used for reading firmware during startup,
 * before the hid reading loop is running */
static int
read_sync(struct wmr_controller_connection *wcc, uint8_t *buffer, uint32_t buf_size, int timeout_ms)
{
	struct wmr_bt_connection *conn = (struct wmr_bt_connection *)(wcc);

	os_mutex_lock(&conn->hid_lock);
	int res = os_hid_read(conn->controller_hid, buffer, buf_size, timeout_ms);
	os_mutex_unlock(&conn->hid_lock);

	return res;
}

static void *
wmr_bt_connection_run_thread(void *ptr)
{
	U_TRACE_SET_THREAD_NAME("WMR: BT-Controller");

	struct wmr_bt_connection *conn = wmr_bt_connection(ptr);

	os_thread_helper_lock(&conn->controller_thread);
	while (os_thread_helper_is_running_locked(&conn->controller_thread)) {
		os_thread_helper_unlock(&conn->controller_thread);

		// Does not block.
		if (!read_packets(conn)) {
			break;
		}
	}

	WMR_DEBUG(conn, "WMR Controller (Bluetooth): Exiting reading thread.");

	return NULL;
}

static void
wmr_bt_connection_destroy(struct wmr_controller_connection *base)
{
	struct wmr_bt_connection *conn = (struct wmr_bt_connection *)base;

	DRV_TRACE_MARKER();

	// Destroy the thread object.
	os_thread_helper_destroy(&conn->controller_thread);

	if (conn->controller_hid != NULL) {
		os_hid_destroy(conn->controller_hid);
		conn->controller_hid = NULL;
	}

	os_mutex_destroy(&conn->hid_lock);

	free(conn);
}

/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_device *
wmr_bt_controller_create(struct os_hid_device *controller_hid,
                         enum xrt_device_type controller_type,
                         uint16_t vid,
                         uint16_t pid,
                         enum u_logging_level log_level)
{
	DRV_TRACE_MARKER();

	struct wmr_bt_connection *conn = calloc(1, sizeof(struct wmr_bt_connection));

	conn->log_level = log_level;
	conn->controller_hid = controller_hid;

	conn->base.send_bytes = send_bytes;
	conn->base.read_sync = read_sync;
	conn->base.disconnect = wmr_bt_connection_destroy;

	int ret = 0;

	ret = os_mutex_init(&conn->hid_lock);
	if (ret != 0) {
		WMR_ERROR(conn, "WMR Controller (Bluetooth): Failed to init mutex!");
		wmr_bt_connection_destroy(&conn->base);
		return NULL;
	}

	// Thread and other state.
	ret = os_thread_helper_init(&conn->controller_thread);
	if (ret != 0) {
		WMR_ERROR(conn, "WMR Controller (Bluetooth): Failed to init controller threading!");
		wmr_bt_connection_destroy(&conn->base);
		return NULL;
	}

	// Takes ownership of the connection
	struct wmr_controller_base *wcb = wmr_controller_create(&conn->base, controller_type, vid, pid, log_level);
	if (wcb == NULL) {
		WMR_ERROR(conn, "WMR Controller (Bluetooth): Failed to create controller");
		return NULL;
	}

	// If the controller device was created, the connection belongs to
	// it now and will be cleaned up when it calls disconnect().
	conn->base.wcb = wcb;

	struct xrt_device *xdev = &wcb->base;

	// Hand over controller device to reading thread.
	ret = os_thread_helper_start(&conn->controller_thread, wmr_bt_connection_run_thread, conn);
	if (ret != 0) {
		WMR_ERROR(conn, "WMR Controller (Bluetooth): Failed to start controller thread!");
		xrt_device_destroy(&xdev);
		return NULL;
	}

	return xdev;
}
