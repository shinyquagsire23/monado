// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGloves bluetooth prober implementation.
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#include <stdlib.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <unistd.h>

#include "util/u_debug.h"
#include "xrt/xrt_defines.h"

#include "opengloves_bt_serial.h"
#include "opengloves_prober_bt.h"
#include "util/u_misc.h"

#define OPENGLOVES_PROBER_LOG_LEVEL U_LOGGING_TRACE

#define OPENGLOVES_ERROR(...) U_LOG_IFL_E(OPENGLOVES_PROBER_LOG_LEVEL, __VA_ARGS__)
#define OPENGLOVES_WARN(...) U_LOG_IFL_W(OPENGLOVES_PROBER_LOG_LEVEL, __VA_ARGS__)
#define OPENGLOVES_INFO(...) U_LOG_IFL_I(OPENGLOVES_PROBER_LOG_LEVEL, __VA_ARGS__)

#define OPENGLOVES_BT_MAX_ADDRESS_LEN 19
#define OPENGLOVES_BT_MAX_NAME_LEN 248
#define OPENGLOVES_BT_MAX_DEVICES 255

int
opengloves_get_bt_devices(const char *bt_name, struct opengloves_communication_device **out_ocd)
{
	char addr[OPENGLOVES_BT_MAX_ADDRESS_LEN] = {0};
	char name[OPENGLOVES_BT_MAX_NAME_LEN] = {0};

	int dev_id = hci_get_route(NULL);
	int sock = hci_open_dev(dev_id);

	if (dev_id < 0 || sock < 0) {
		OPENGLOVES_ERROR("Failed to open socket!");

		return -1;
	}

	int max_rsp = OPENGLOVES_BT_MAX_DEVICES; // maximum devices to find
	inquiry_info *ii = U_TYPED_ARRAY_CALLOC(inquiry_info, max_rsp);

	int num_rsp = hci_inquiry(dev_id, 1, max_rsp, NULL, &ii, IREQ_CACHE_FLUSH);

	if (num_rsp < 0) {
		OPENGLOVES_ERROR("device inquiry failed!");

		free(ii);
		close(sock);

		return -1;
	}

	for (int i = 0; i < num_rsp; i++) {
		ba2str(&(ii + i)->bdaddr, addr);
		memset(name, 0, sizeof(name));

		hci_read_remote_name(sock, &(ii + i)->bdaddr, sizeof(name), name, 0);

		if (!strcmp(name, bt_name) && *out_ocd == NULL) {
			OPENGLOVES_INFO("Found bt device! %s", name);

			opengloves_bt_open(addr, out_ocd);

			break;
		}
	}

	free(ii);
	close(sock);
	return 0;
}
