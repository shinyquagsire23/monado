// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Direct3D 11 tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */


#include "mock/mock_compositor.h"
#include "client/comp_d3d11_client.h"

#include "catch/catch.hpp"
#include "util/u_handles.h"

#include <d3d/d3d_dxgi_helpers.hpp>
#include <d3d/d3d_d3d11_helpers.hpp>
#include <stdint.h>
#include <util/u_win32_com_guard.hpp>

#include <d3d11_4.h>


using namespace xrt::auxiliary::d3d;
using namespace xrt::auxiliary::d3d::d3d11;
using namespace xrt::auxiliary::util;

TEST_CASE("client_compositor", "[.][needgpu]")
{
	xrt_compositor_native *xcn = mock_create_native_compositor();
	struct mock_compositor *mc = mock_compositor(&(xcn->base));

	ComGuard comGuard;

	wil::com_ptr<ID3D11Device> device;
	wil::com_ptr<ID3D11DeviceContext> context;
	std::tie(device, context) = createDevice();
	struct xrt_compositor_d3d11 *xcd3d = client_d3d11_compositor_create(xcn, device.get());
	struct xrt_compositor *xc = &xcd3d->base;

	SECTION("Swapchain create and import")
	{
		struct Data
		{
			bool nativeCreateCalled = false;

			bool nativeImportCalled = false;
		} data;
		mc->userdata = &data;
		mc->compositor_hooks.create_swapchain =
		    [](struct mock_compositor *mc, struct mock_compositor_swapchain *mcsc,
		       const struct xrt_swapchain_create_info *info, struct xrt_swapchain **out_xsc) {
			    auto *data = static_cast<Data *>(mc->userdata);
			    data->nativeCreateCalled = true;
			    return XRT_SUCCESS;
		    };
		mc->compositor_hooks.import_swapchain =
		    [](struct mock_compositor *mc, struct mock_compositor_swapchain *mcsc,
		       const struct xrt_swapchain_create_info *info, struct xrt_image_native *native_images,
		       uint32_t image_count, struct xrt_swapchain **out_xscc) {
			    auto *data = static_cast<Data *>(mc->userdata);
			    data->nativeImportCalled = true;
			    // need to release the native handles to avoid leaks
			    for (uint32_t i = 0; i < image_count; ++i) {
				    u_graphics_buffer_unref(&native_images[i].handle);
			    }
			    return XRT_SUCCESS;
		    };
		xrt_swapchain_create_info xsci{};
		xsci.format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		xsci.bits = (xrt_swapchain_usage_bits)(XRT_SWAPCHAIN_USAGE_COLOR | XRT_SWAPCHAIN_USAGE_SAMPLED);
		xsci.sample_count = 1;
		xsci.width = 800;
		xsci.height = 600;
		xsci.face_count = 1;
		xsci.array_size = 1;
		xsci.mip_count = 1;
		SECTION("Swapchain Create")
		{
			struct xrt_swapchain *xsc = nullptr;
			// This will fail because the mock compositor doesn't actually import, but it will get far
			// enough to trigger our hook and update the flag.
			xrt_comp_create_swapchain(xc, &xsci, &xsc);
			// D3D always imports into the native compositor
			CHECK(data.nativeImportCalled);
			CHECK_FALSE(data.nativeCreateCalled);
			xrt_swapchain_reference(&xsc, nullptr);
		}
	}

	xrt_comp_destroy(&xc);
	xrt_comp_native_destroy(&xcn);
}
