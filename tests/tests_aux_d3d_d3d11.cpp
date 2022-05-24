// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Direct3D 11 tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */
#include "aux_d3d_dxgi_formats.hpp"

#include <iostream>

#include "catch/catch.hpp"

#include <d3d/d3d_dxgi_helpers.hpp>
#include <d3d/d3d_d3d11_helpers.hpp>
#include <d3d/d3d_d3d11_allocator.hpp>
#include <util/u_win32_com_guard.hpp>

#include <d3d11_4.h>

using namespace xrt::auxiliary::d3d;
using namespace xrt::auxiliary::d3d::d3d11;
using namespace xrt::auxiliary::util;

TEST_CASE("dxgi_adapter", "[.][needgpu]")
{
	ComGuard comGuard;
	wil::com_ptr<IDXGIAdapter> adapter;

	CHECK_NOTHROW(adapter = getAdapterByIndex(0, U_LOGGING_TRACE));
	CHECK(adapter);
	auto adapter1 = adapter.query<IDXGIAdapter1>();
	DXGI_ADAPTER_DESC1 desc{};
	REQUIRE(SUCCEEDED(adapter1->GetDesc1(&desc)));

	xrt_luid_t luid{};
	memcpy(&luid, &(desc.AdapterLuid), sizeof(luid));


	wil::com_ptr<IDXGIAdapter> adapterFromLuid;
	CHECK_NOTHROW(adapterFromLuid = getAdapterByLUID(luid, U_LOGGING_TRACE));
	CHECK(adapterFromLuid);
}

TEST_CASE("d3d11_device", "[.][needgpu]")
{
	ComGuard comGuard;
	wil::com_ptr<IDXGIAdapter> adapter;

	CHECK_NOTHROW(adapter = getAdapterByIndex(0, U_LOGGING_TRACE));
	CHECK(adapter);
	wil::com_ptr<ID3D11Device> device;
	wil::com_ptr<ID3D11DeviceContext> context;
	CHECK_NOTHROW(std::tie(device, context) = createDevice(adapter, U_LOGGING_TRACE));
	CHECK(device);
	CHECK(context);
}

TEST_CASE("d3d11_allocate", "[.][needgpu]")
{
	ComGuard comGuard;
	wil::com_ptr<ID3D11Device> device;
	wil::com_ptr<ID3D11DeviceContext> context;
	std::tie(device, context) = createDevice();
	auto device5 = device.query<ID3D11Device5>();
	std::vector<wil::com_ptr<ID3D11Texture2D1>> images;
	std::vector<wil::unique_handle> handles;

	static constexpr bool kKeyedMutex = true;
	size_t imageCount = 3;

	xrt_swapchain_create_info xsci{};
	xsci.sample_count = 1;
	xsci.width = 800;
	xsci.height = 600;
	xsci.face_count = 1;
	xsci.array_size = 1;
	xsci.mip_count = 1;
	SECTION("create images")
	{
		auto format = GENERATE(values(colorFormats));
		if (isDepthStencilFormat(format)) {

			xsci.bits = XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL;
		} else {
			xsci.bits = XRT_SWAPCHAIN_USAGE_COLOR;
		}
		xsci.bits = (xrt_swapchain_usage_bits)(xsci.bits | XRT_SWAPCHAIN_USAGE_SAMPLED);

		INFO("Format: " << format) xsci.format = format;
		images.clear();
		handles.clear();
		SECTION("invalid array size 0")
		{
			xsci.array_size = 0;
			CHECK(XRT_SUCCESS !=
			      allocateSharedImages(*device5, xsci, imageCount, kKeyedMutex, images, handles));
			CHECK(images.empty());
			CHECK(handles.empty());
		}
		SECTION("not array")
		{
			CHECK(XRT_SUCCESS ==
			      allocateSharedImages(*device5, xsci, imageCount, kKeyedMutex, images, handles));
			CHECK(images.size() == imageCount);
			CHECK(handles.size() == imageCount);
		}
		SECTION("array of 2")
		{
			xsci.array_size = 2;
			CHECK(XRT_SUCCESS ==
			      allocateSharedImages(*device5, xsci, imageCount, kKeyedMutex, images, handles));
			CHECK(images.size() == imageCount);
			CHECK(handles.size() == imageCount);
		}
		SECTION("cubemaps not implemented")
		{
			xsci.face_count = 6;
			CHECK(XRT_ERROR_ALLOCATION ==
			      allocateSharedImages(*device5, xsci, imageCount, kKeyedMutex, images, handles));
			CHECK(images.empty());
			CHECK(handles.empty());
		}
		SECTION("protected content not implemented")
		{
			xsci.create = XRT_SWAPCHAIN_CREATE_PROTECTED_CONTENT;
			CHECK(XRT_ERROR_SWAPCHAIN_FLAG_VALID_BUT_UNSUPPORTED ==
			      allocateSharedImages(*device5, xsci, imageCount, kKeyedMutex, images, handles));
			CHECK(images.empty());
			CHECK(handles.empty());
		}
	}
}
