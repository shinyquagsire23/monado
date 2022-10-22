// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Stub BLE functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_os
 */

#include "os_ble.h"


int
os_ble_notify_open(const char *dev_uuid, const char *char_uuid, struct os_ble_device **out_ble)
{
	return -1;
}

int
os_ble_broadcast_write_value(const char *service_uuid, const char *char_uuid, uint8_t value)
{
	return -1;
}
