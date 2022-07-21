// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Bluetooth Serial interface for OpenGloves.
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#pragma once

#include "../opengloves_communication.h"


#ifdef __cplusplus
extern "C" {
#endif


struct opengloves_bt_device
{
	struct opengloves_communication_device base;
	int sock;
};

int
opengloves_bt_open(const char *btaddr, struct opengloves_communication_device **out_comm_dev);

#ifdef __cplusplus
}
#endif
