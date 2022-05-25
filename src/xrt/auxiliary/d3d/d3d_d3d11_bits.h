// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Usage bits for D3D11.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_d3d
 */

#pragma once

#include "xrt/xrt_compositor.h"
#include "xrt/xrt_windows.h"

#include "d3d11.h"


#ifdef __cplusplus
extern "C" {
#endif


static inline UINT
d3d_convert_usage_bits_to_d3d11_bind_flags(enum xrt_swapchain_usage_bits xsub)
{
	int ret = 0;
	if ((xsub & XRT_SWAPCHAIN_USAGE_COLOR) != 0) {
		ret |= D3D11_BIND_RENDER_TARGET;
	}
	if ((xsub & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) != 0) {
		ret |= D3D11_BIND_DEPTH_STENCIL;
	}
	if ((xsub & XRT_SWAPCHAIN_USAGE_UNORDERED_ACCESS) != 0) {
		ret |= D3D11_BIND_UNORDERED_ACCESS;
	}
	if ((xsub & XRT_SWAPCHAIN_USAGE_SAMPLED) != 0) {
		ret |= D3D11_BIND_SHADER_RESOURCE;
	}
	return ret;
}

#ifdef __cplusplus
}
#endif
