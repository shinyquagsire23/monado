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

#define MAKE_PAIR(ENUM)                                                                                                \
	{                                                                                                              \
#ENUM, ENUM                                                                                            \
	}
static constexpr std::initializer_list<std::pair<const char *, DXGI_FORMAT>> colorNamesAndFormats = {
    MAKE_PAIR(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB), MAKE_PAIR(DXGI_FORMAT_B8G8R8A8_UNORM),
    MAKE_PAIR(DXGI_FORMAT_R16G16B16A16_FLOAT),  MAKE_PAIR(DXGI_FORMAT_R16G16B16A16_UNORM),
    MAKE_PAIR(DXGI_FORMAT_R16G16B16A16_FLOAT),  MAKE_PAIR(DXGI_FORMAT_R16G16B16A16_UNORM),
    MAKE_PAIR(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB), MAKE_PAIR(DXGI_FORMAT_R8G8B8A8_UNORM),
    MAKE_PAIR(DXGI_FORMAT_R32_FLOAT),
};

static constexpr std::initializer_list<DXGI_FORMAT> depthStencilFormats = {
    DXGI_FORMAT_D16_UNORM,
    DXGI_FORMAT_D24_UNORM_S8_UINT,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
    DXGI_FORMAT_D32_FLOAT,
};
static constexpr std::initializer_list<std::pair<const char *, DXGI_FORMAT>> namesAndFormats = {
    MAKE_PAIR(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB), MAKE_PAIR(DXGI_FORMAT_B8G8R8A8_UNORM),
    MAKE_PAIR(DXGI_FORMAT_R16G16B16A16_FLOAT),  MAKE_PAIR(DXGI_FORMAT_R16G16B16A16_UNORM),
    MAKE_PAIR(DXGI_FORMAT_R16G16B16A16_FLOAT),  MAKE_PAIR(DXGI_FORMAT_R16G16B16A16_UNORM),
    MAKE_PAIR(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB), MAKE_PAIR(DXGI_FORMAT_R8G8B8A8_UNORM),
    MAKE_PAIR(DXGI_FORMAT_R32_FLOAT),           MAKE_PAIR(DXGI_FORMAT_D16_UNORM),
    MAKE_PAIR(DXGI_FORMAT_D24_UNORM_S8_UINT),   MAKE_PAIR(DXGI_FORMAT_D32_FLOAT_S8X24_UINT),
    MAKE_PAIR(DXGI_FORMAT_D32_FLOAT),
};

static inline bool
isDepthStencilFormat(DXGI_FORMAT format)
{
	const auto b = depthStencilFormats.begin();
	const auto e = depthStencilFormats.end();
	auto it = std::find(b, e, format);
	return it != e;
}
