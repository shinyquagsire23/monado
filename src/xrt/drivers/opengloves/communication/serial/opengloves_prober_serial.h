// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Serial prober interface for OpenGloves.
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#pragma once

#include "../opengloves_communication.h"

#define LUCIDGLOVES_USB_VID 0x1a86
#define LUCIDGLOVES_USB_L_PID 0x7523 // left hand pid
#define LUCIDGLOVES_USB_R_PID 0x7524 // right hand pid
#ifdef __cplusplus
extern "C" {
#endif


int
opengloves_get_serial_devices(uint16_t vid, uint16_t pid, struct opengloves_communication_device **out_ocd);

#ifdef __cplusplus
}
#endif
