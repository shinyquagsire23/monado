// Copyright 2023 Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Implementation of tunnelled controller connection,
 * that translates messages passing via an HP G2 or Sasmung Odyssey+ HMD
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_wmr
 */
#include "util/u_trace_marker.h"

#include "wmr_hmd_controller.h"
#include "wmr_controller.h"
#include "wmr_hmd.h"

#define WMR_TRACE(c, ...) U_LOG_IFL_T(c->log_level, __VA_ARGS__)
#define WMR_DEBUG(c, ...) U_LOG_IFL_D(c->log_level, __VA_ARGS__)
#define WMR_INFO(c, ...) U_LOG_IFL_I(c->log_level, __VA_ARGS__)
#define WMR_WARN(c, ...) U_LOG_IFL_W(c->log_level, __VA_ARGS__)
#define WMR_ERROR(c, ...) U_LOG_IFL_E(c->log_level, __VA_ARGS__)

/* A note:
 * This HMD controller connection object is used for controllers
 * where the communication is tunnelled through HMD packets. It
 * handles translating the controller packet IDs to raw
 * IDs when receiving data from the HMD, and back to HMD packet IDs
 * when sending data to the controller.
 *
 * Both the HMD and the controller hold a reference to this
 * connection object. The HMD can pass received packets at
 * any time, and will call wmr_controller_connection_disconnect()
 * when the HMD xrt_device is freed by the runtime.
 *
 * The controller may send packets based on calls from the
 * runtime (triggering haptics, for example), so can also
 * want to send packets at any time. It will also call
 * wmr_controller_connection_disconnect()
 * when the controller xrt_device is freed by the runtime.
 *
 * The conn_lock protects access to the HMD and controller
 * pointers while making calls to send/receive, to prevent
 * from invalid access if _disconnect() is called.
 */

static bool
send_bytes_to_controller(struct wmr_controller_connection *wcc, const uint8_t *buffer, uint32_t buf_size)
{
	struct wmr_hmd_controller_connection *conn = (struct wmr_hmd_controller_connection *)(wcc);
	bool res = false;

	assert(buf_size < 64);

	os_mutex_lock(&conn->lock);
	if (!conn->disconnected && buf_size > 0) {
		uint8_t outbuf[64];

		memcpy(outbuf, buffer, buf_size);
		outbuf[0] += conn->hmd_cmd_base;
		res = wmr_hmd_send_controller_packet(conn->hmd, outbuf, buf_size);
	}
	os_mutex_unlock(&conn->lock);

	return res;
}

static int
read_sync_from_controller(struct wmr_controller_connection *wcc, uint8_t *buffer, uint32_t buf_size, int timeout_ms)
{
	struct wmr_hmd_controller_connection *conn = (struct wmr_hmd_controller_connection *)(wcc);
	int res = -1;

	os_mutex_lock(&conn->lock);
	if (!conn->disconnected && buf_size > 0) {
		res = wmr_hmd_read_sync_from_controller(conn->hmd, buffer, buf_size, timeout_ms);
	}
	os_mutex_unlock(&conn->lock);

	return res;
}

static void
receive_bytes_from_controller(struct wmr_controller_connection *wcc,
                              uint64_t time_ns,
                              uint8_t *buffer,
                              uint32_t buf_size)
{
	struct wmr_hmd_controller_connection *conn = (struct wmr_hmd_controller_connection *)(wcc);
	os_mutex_lock(&conn->lock);
	if (!conn->disconnected && buf_size > 0) {
		buffer[0] -= conn->hmd_cmd_base;

		struct wmr_controller_base *wcb = wcc->wcb;
		assert(wcb->receive_bytes != NULL);
		wcb->receive_bytes(wcb, time_ns, buffer, buf_size);
	}
	os_mutex_unlock(&conn->lock);
}

static void
wmr_hmd_controller_connection_destroy(struct wmr_hmd_controller_connection *conn)
{
	DRV_TRACE_MARKER();

	os_mutex_destroy(&conn->lock);
	free(conn);
}

static void
wmr_hmd_controller_connection_disconnect(struct wmr_controller_connection *base)
{
	struct wmr_hmd_controller_connection *conn = (struct wmr_hmd_controller_connection *)(base);

	if (xrt_reference_dec(&conn->ref)) {
		wmr_hmd_controller_connection_destroy(conn);
	} else {
		os_mutex_lock(&conn->lock);
		conn->disconnected = true;
		os_mutex_unlock(&conn->lock);
	}
}

struct wmr_hmd_controller_connection *
wmr_hmd_controller_create(struct wmr_hmd *hmd,
                          uint8_t hmd_cmd_base,
                          enum xrt_device_type controller_type,
                          uint16_t vid,
                          uint16_t pid,
                          enum u_logging_level log_level)
{
	DRV_TRACE_MARKER();

	struct wmr_hmd_controller_connection *conn = calloc(1, sizeof(struct wmr_hmd_controller_connection));

	conn->log_level = log_level;

	conn->hmd = hmd;
	conn->hmd_cmd_base = hmd_cmd_base;

	conn->base.receive_bytes = receive_bytes_from_controller;
	conn->base.send_bytes = send_bytes_to_controller;
	conn->base.read_sync = read_sync_from_controller;
	conn->base.disconnect = wmr_hmd_controller_connection_disconnect;

	/* Init 2 references - one for the controller, one for the HMD */
	xrt_reference_inc(&conn->ref);
	xrt_reference_inc(&conn->ref);

	int ret = 0;

	ret = os_mutex_init(&conn->lock);
	if (ret != 0) {
		WMR_ERROR(conn, "WMR Controller (Tunnelled): Failed to init mutex!");
		wmr_hmd_controller_connection_destroy(conn);
		return NULL;
	}

	// Takes ownership of one reference to the connection, the other will
	// belong to the returned pointer
	struct wmr_controller_base *wcb = wmr_controller_create(&conn->base, controller_type, vid, pid, log_level);
	if (wcb == NULL) {
		WMR_ERROR(conn, "WMR Controller (Tunnelled): Failed to create controller");
		wmr_hmd_controller_connection_destroy(conn);
		return NULL;
	}

	// If the controller device was created, the connection belongs to
	// it now and will be cleaned up when it calls disconnect().
	conn->base.wcb = wcb;

	return conn;
}

struct xrt_device *
wmr_hmd_controller_connection_get_controller(struct wmr_hmd_controller_connection *wcc)
{
	struct wmr_controller_base *wcb = wcc->base.wcb;
	struct xrt_device *xdev = &wcb->base;

	return xdev;
}
