// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief DXGI formats shared between D3D11 and D3D12 tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#pragma once

#include <dxgi.h>
#include <initializer_list>
#include <algorithm>


static constexpr std::initializer_list<DXGI_FORMAT> colorFormats = {
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM,     DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_R16G16B16A16_UNORM,  DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_UNORM,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM,
};

static constexpr std::initializer_list<DXGI_FORMAT> depthStencilFormats = {
    DXGI_FORMAT_D16_UNORM,
    DXGI_FORMAT_D24_UNORM_S8_UINT,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
    DXGI_FORMAT_D32_FLOAT,
};
static constexpr std::initializer_list<DXGI_FORMAT> formats = {
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM,       DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_R16G16B16A16_UNORM,  DXGI_FORMAT_R16G16B16A16_FLOAT,   DXGI_FORMAT_R16G16B16A16_UNORM,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM,       DXGI_FORMAT_D16_UNORM,
    DXGI_FORMAT_D24_UNORM_S8_UINT,   DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_D32_FLOAT,
};

static inline bool
isDepthStencilFormat(DXGI_FORMAT format)
{
	const auto b = depthStencilFormats.begin();
	const auto e = depthStencilFormats.end();
	auto it = std::find(b, e, format);
	return it != e;
}
