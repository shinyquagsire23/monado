// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  SDL2 Debug UI implementation
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 */

#pragma once

#include "xrt/xrt_compiler.h"


#ifdef __cplusplus
extern "C" {
#endif

struct xrt_instance;
struct xrt_system_devices;

struct u_debug_gui;

int
u_debug_gui_create(struct u_debug_gui **out_debug_gui);

void
u_debug_gui_start(struct u_debug_gui *debug_gui, struct xrt_instance *xinst, struct xrt_system_devices *xsysd);

void
u_debug_gui_stop(struct u_debug_gui **debug_gui);


#ifdef __cplusplus
}
#endif
