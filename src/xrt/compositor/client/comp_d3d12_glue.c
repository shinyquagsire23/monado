// Copyright 2021-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to D3D12 client side code: expressing requirements and connecting `comp_` APIs to `xrt_gfx_`
 * interfaces.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup comp_client
 */

#include "client/comp_d3d12_client.h"

#include <stdlib.h>

struct xrt_compositor_d3d12 *
xrt_gfx_d3d12_provider_create(struct xrt_compositor_native *xcn, ID3D12Device *device, ID3D12CommandQueue *queue)
{
	return client_d3d12_compositor_create(xcn, device, queue);
}
