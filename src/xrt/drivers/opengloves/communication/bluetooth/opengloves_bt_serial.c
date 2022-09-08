// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGloves bluetooth serial implementation
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#include <unistd.h>
#include <errno.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "opengloves_bt_serial.h"

#define OPENGLOVES_PROBER_LOG_LEVEL U_LOGGING_TRACE

#define OPENGLOVES_ERROR(...) U_LOG_IFL_E(OPENGLOVES_PROBER_LOG_LEVEL, __VA_ARGS__)
#define OPENGLOVES_WARN(...) U_LOG_IFL_W(OPENGLOVES_PROBER_LOG_LEVEL, __VA_ARGS__)
#define OPENGLOVES_INFO(...) U_LOG_IFL_I(OPENGLOVES_PROBER_LOG_LEVEL, __VA_ARGS__)

static void
opengloves_bt_close(struct opengloves_bt_device *btdev)
{
	if (btdev->sock != 0) {
		close(btdev->sock);
		btdev->sock = 0;
	}
}

static int
opengloves_bt_read(struct opengloves_communication_device *ocdev, char *data, size_t length)
{
	struct opengloves_bt_device *obdev = (struct opengloves_bt_device *)ocdev;

	return read(obdev->sock, data, length);
}

static int
opengloves_bt_write(struct opengloves_communication_device *ocdev, const char *data, size_t length)
{
	struct opengloves_bt_device *obdev = (struct opengloves_bt_device *)ocdev;

	return write(obdev->sock, data, length);
}

static void
opengloves_bt_destroy(struct opengloves_communication_device *ocdev)
{
	struct opengloves_bt_device *obdev = (struct opengloves_bt_device *)ocdev;

	opengloves_bt_close(obdev);
	free(obdev);
}

int
opengloves_bt_open(const char *btaddr, struct opengloves_communication_device **out_comm_dev)
{
	struct opengloves_bt_device *obdev = U_TYPED_CALLOC(struct opengloves_bt_device);

	obdev->base.read = opengloves_bt_read;
	obdev->base.write = opengloves_bt_write;
	obdev->base.destroy = opengloves_bt_destroy;

	// allocate a socket
	obdev->sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

	// set the connection parameters (who to connect to)
	struct sockaddr_rc addr = {0};
	addr.rc_family = AF_BLUETOOTH;
	addr.rc_channel = (uint8_t)1;
	str2ba(btaddr, &addr.rc_bdaddr);

	// connect to server
	int ret = connect(obdev->sock, (struct sockaddr *)&addr, sizeof(addr));

	if (ret < 0) {
		OPENGLOVES_ERROR("Failed to connect to device! %s", strerror(errno));
		return ret;
	}

	*out_comm_dev = &obdev->base;

	return 0;
}
