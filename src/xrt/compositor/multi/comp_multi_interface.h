// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface for the multi-client layer code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "xrt/xrt_compositor.h"


#ifdef __cplusplus
extern "C" {
#endif

struct u_pacing_app_factory;


/*!
 * Create a "system compositor" that can handle multiple clients (each
 * through a "multi compositor") and that drives a single native compositor.
 * Both the native compositor and the pacing factory is owned by the system
 * compositor and destroyed by it.
 *
 * @param xcn           Native compositor that client are multi-plexed to.
 * @param upaf          App pacing factory, one pacer created per client.
 * @param xsci          Information to be exposed.
 * @param do_warm_start Should we always submit a frame at startup.
 * @param out_xsysc     Created @ref xrt_system_compositor.
 */
xrt_result_t
comp_multi_create_system_compositor(struct xrt_compositor_native *xcn,
                                    struct u_pacing_app_factory *upaf,
                                    const struct xrt_system_compositor_info *xsci,
                                    bool do_warm_start,
                                    struct xrt_system_compositor **out_xsysc);


#ifdef __cplusplus
}
#endif
