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

#include <xrt/xrt_config_have.h>
#include <d3d/d3d_dxgi_helpers.hpp>
#include <d3d/d3d_d3d11_helpers.hpp>
#include <d3d/d3d_d3d11_allocator.hpp>
#include <util/u_win32_com_guard.hpp>

#ifdef XRT_HAVE_VULKAN
#include "vktest_init_bundle.hpp"
#include <vk/vk_image_allocator.h>
#include <util/u_handles.h>
#include <d3d/d3d_dxgi_formats.h>
#endif

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

#ifdef XRT_HAVE_VULKAN

static inline bool
tryImport(struct vk_bundle *vk, std::vector<HANDLE> const &handles, const struct xrt_swapchain_create_info &xsci)
{

	INFO("Testing import into Vulkan");

	static constexpr bool use_dedicated_allocation = false;
	xrt_swapchain_create_info vk_info = xsci;
	vk_info.format = d3d_dxgi_format_to_vk((DXGI_FORMAT)xsci.format);
	const auto free_vk_ic = [&](struct vk_image_collection *vkic) {
		vk_ic_destroy(vk, vkic);
		delete vkic;
	};

	std::shared_ptr<vk_image_collection> vkic{new vk_image_collection, free_vk_ic};

	uint32_t image_count = static_cast<uint32_t>(handles.size());
	// Populate for import
	std::vector<xrt_image_native> xins;
	xins.reserve(image_count);

	// Keep this around until after successful import, then detach all.
	std::vector<wil::unique_handle> handlesForImport;
	handlesForImport.reserve(image_count);

	for (HANDLE handle : handles) {
		wil::unique_handle duped{u_graphics_buffer_ref(handle)};
		xrt_image_native xin;
		xin.handle = duped.get();
		xin.size = 0;
		xin.use_dedicated_allocation = use_dedicated_allocation;

		handlesForImport.emplace_back(std::move(duped));
		xins.emplace_back(xin);
	}

	// Import into a vulkan image collection
	bool result = VK_SUCCESS == vk_ic_from_natives(vk, &vk_info, xins.data(), (uint32_t)xins.size(), vkic.get());

	if (result) {
		// The imported swapchain took ownership of them now, release them from ownership here.
		for (auto &h : handlesForImport) {
			h.release();
		}
	}
	return result;
}
#else

static inline bool
tryImport(struct vk_bundle * /* vk */,
          std::vector<wil::unique_handle> const & /* handles */,
          const struct xrt_swapchain_create_info & /* xsci */)
{
	return true;
}

#endif

TEST_CASE("d3d11_allocate", "[.][needgpu]")
{
	unique_vk_bundle vk = makeVkBundle();

#ifdef XRT_HAVE_VULKAN
	REQUIRE(vktest_init_bundle(vk.get()));
#endif

	ComGuard comGuard;
	wil::com_ptr<ID3D11Device> device;
	wil::com_ptr<ID3D11DeviceContext> context;
	std::tie(device, context) = createDevice();
	auto device5 = device.query<ID3D11Device5>();
	std::vector<wil::com_ptr<ID3D11Texture2D1>> images;
	std::vector<HANDLE> handles;

	static constexpr bool kKeyedMutex = true;
	size_t imageCount = 3;

	xrt_swapchain_create_info xsci{};
	CAPTURE(xsci.sample_count = 1);
	CAPTURE(xsci.width = 800);
	CAPTURE(xsci.height = 600);

	CAPTURE(xsci.mip_count = 1);
	xsci.face_count = 1;
	xsci.array_size = 1;
	SECTION("create images")
	{
		auto nameAndFormat = GENERATE(values(namesAndFormats));
		DYNAMIC_SECTION("Texture format " << nameAndFormat.first)
		{
			auto format = nameAndFormat.second;
			CAPTURE(isDepthStencilFormat(format));
			xsci.format = format;
			if (isDepthStencilFormat(format)) {
				xsci.bits = XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL;
			} else {
				xsci.bits = XRT_SWAPCHAIN_USAGE_COLOR;
			}
			xsci.bits = (xrt_swapchain_usage_bits)(xsci.bits | XRT_SWAPCHAIN_USAGE_SAMPLED);
			images.clear();
			handles.clear();
			SECTION("invalid array size 0")
			{
				CAPTURE(xsci.array_size = 0);
				REQUIRE(XRT_SUCCESS !=
				        allocateSharedImages(*device5, xsci, imageCount, kKeyedMutex, images, handles));
				CHECK(images.empty());
				CHECK(handles.empty());
			}
			SECTION("not array")
			{
				CAPTURE(xsci.array_size);
				REQUIRE(XRT_SUCCESS ==
				        allocateSharedImages(*device5, xsci, imageCount, kKeyedMutex, images, handles));
				CHECK(images.size() == imageCount);
				CHECK(handles.size() == imageCount);
				CHECK(tryImport(vk.get(), handles, xsci));
			}
			SECTION("array of 2")
			{
				CAPTURE(xsci.array_size = 2);
				REQUIRE(XRT_SUCCESS ==
				        allocateSharedImages(*device5, xsci, imageCount, kKeyedMutex, images, handles));
				CHECK(images.size() == imageCount);
				CHECK(handles.size() == imageCount);
				CHECK(tryImport(vk.get(), handles, xsci));
			}
			SECTION("cubemaps not implemented")
			{
				CAPTURE(xsci.array_size);
				CAPTURE(xsci.face_count = 6);
				REQUIRE(XRT_ERROR_ALLOCATION ==
				        allocateSharedImages(*device5, xsci, imageCount, kKeyedMutex, images, handles));
				CHECK(images.empty());
				CHECK(handles.empty());
			}
			SECTION("protected content not implemented")
			{
				CAPTURE(xsci.array_size);
				CAPTURE(xsci.create = XRT_SWAPCHAIN_CREATE_PROTECTED_CONTENT);
				REQUIRE(XRT_ERROR_SWAPCHAIN_FLAG_VALID_BUT_UNSUPPORTED ==
				        allocateSharedImages(*device5, xsci, imageCount, kKeyedMutex, images, handles));
				CHECK(images.empty());
				CHECK(handles.empty());
			}
		}
	}
}
