// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Usage bits for D3D12.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_d3d
 */

#pragma once

#include "xrt/xrt_compositor.h"
#include "xrt/xrt_windows.h"
#include "xrt/xrt_config_have.h"

#include "d3d12.h"

#include <assert.h>

#ifdef XRT_HAVE_D3D12

#ifdef __cplusplus
extern "C" {
#endif

static inline D3D12_RESOURCE_FLAGS
d3d_convert_usage_bits_to_d3d12_resource_flags(enum xrt_swapchain_usage_bits xsub)
{
	int ret = 0;
	if ((xsub & XRT_SWAPCHAIN_USAGE_COLOR) != 0) {
		ret |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	}
	if ((xsub & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) != 0) {
		ret |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	}
	if ((xsub & XRT_SWAPCHAIN_USAGE_UNORDERED_ACCESS) != 0) {
		ret |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	// if this is omitted, D3D12 asks to add a bit to **deny** shader access.
	// it's a bold api design strategy Cotton, let's see if it pays off for them. ;)
	if ((xsub & XRT_SWAPCHAIN_USAGE_SAMPLED) == 0) {
		// per https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_flags
		// this also depends on depth stencil
		assert((xsub & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) != 0);
		ret |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
	}
	return (D3D12_RESOURCE_FLAGS)ret;
}

/**
 * Given swapchain usage bits, determine the resource usage state expected by the app
 *
 * @param xsub
 * @return D3D12_RESOURCE_STATES
 */
static inline D3D12_RESOURCE_STATES
d3d_convert_usage_bits_to_d3d12_app_resource_state(enum xrt_swapchain_usage_bits xsub)
{
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATES(0);

	if ((xsub & XRT_SWAPCHAIN_USAGE_UNORDERED_ACCESS) != 0) {
		state |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	if ((xsub & XRT_SWAPCHAIN_USAGE_COLOR) != 0) {
		// since we are treating these as mutually exclusive
		assert((xsub & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) == 0);
		state |= D3D12_RESOURCE_STATE_RENDER_TARGET;
	}
	if ((xsub & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) != 0) {
		state |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
	}
	return state;
}

/**
 * Given swapchain usage bits, determine the resource usage state expected by the compositor
 *
 * @param xsub
 * @return D3D12_RESOURCE_STATES
 */
static inline D3D12_RESOURCE_STATES
d3d_convert_usage_bits_to_d3d12_compositor_resource_state(enum xrt_swapchain_usage_bits xsub)
{
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	if ((xsub & XRT_SWAPCHAIN_USAGE_UNORDERED_ACCESS) != 0) {
		state |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	// if ((xsub & XRT_SWAPCHAIN_USAGE_COLOR) != 0) {
	// 	// since we are treating these as mutually exclusive
	// 	assert((xsub & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) == 0);
	// 	state |= D3D12_RESOURCE_STATE_RENDER_TARGET;
	// }
	if ((xsub & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) != 0) {
		state |= D3D12_RESOURCE_STATE_DEPTH_READ;
	}
	return state;
}

#ifdef __cplusplus
}
#endif

#endif // XRT_HAVE_D3D12
