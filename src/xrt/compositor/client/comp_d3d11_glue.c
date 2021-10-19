// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to D3D11 client side code: expressing requirements and connecting `comp_` APIs to `xrt_gfx_`
 * interfaces.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup comp_client
 */

#include "client/comp_d3d11_client.h"

#include <stdlib.h>

struct xrt_compositor_d3d11 *
xrt_gfx_d3d11_provider_create(struct xrt_compositor_native *xcn, ID3D11Device *device)
{
	return client_d3d11_compositor_create(xcn, device);
}
