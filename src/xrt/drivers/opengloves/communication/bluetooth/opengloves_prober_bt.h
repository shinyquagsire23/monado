// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGloves bluetooth prober.
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#pragma once

#include "../opengloves_communication.h"

#define LUCIDGLOVES_BT_L_NAME "lucidgloves-left"
#define LUCIDGLOVES_BT_R_NAME "lucidgloves-right"

#ifdef __cplusplus
extern "C" {
#endif

int
opengloves_get_bt_devices(const char *bt_name, struct opengloves_communication_device **out_ocd);

#ifdef __cplusplus
}
#endif
