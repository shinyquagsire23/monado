// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface for D3D11 client-side code.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup comp_client
 */

#pragma once

#include "xrt/xrt_compositor.h"
#include "xrt/xrt_gfx_d3d11.h"

#include <d3d11_4.h>

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Structs
 *
 */

struct client_d3d11_compositor;

/*!
 * Create a new client_d3d11_compositor.
 *
 * Takes ownership of provided xcn.
 *
 * @public @memberof client_d3d11_compositor
 * @see xrt_compositor_native
 */
struct xrt_compositor_d3d11 *
client_d3d11_compositor_create(struct xrt_compositor_native *xcn, ID3D11Device *device);


#ifdef __cplusplus
}
#endif
