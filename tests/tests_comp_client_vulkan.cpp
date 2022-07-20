// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Basic Vulkan compositor tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */


#include "mock/mock_compositor.h"
#include "client/comp_vk_client.h"
#include "xrt/xrt_results.h"
#include <vulkan/vulkan_core.h>

#undef Always
#undef None

#include "catch/catch.hpp"
#include "util/comp_vulkan.h"
#include "util/u_logging.h"
#include "util/u_string_list.h"
#include "vk/vk_helpers.h"
#include "xrt/xrt_compositor.h"
#include <xrt/xrt_deleters.hpp>

#include <memory>
#include <iostream>

using unique_native_compositor =
    std::unique_ptr<xrt_compositor_native,
                    xrt::deleters::ptr_ptr_deleter<xrt_compositor_native, &xrt_comp_native_destroy>>;

// clang-format off
#define COMP_INSTANCE_EXTENSIONS_COMMON                         \
	VK_EXT_DEBUG_REPORT_EXTENSION_NAME,                     \
	VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,      \
	VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,     \
	VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,  \
	VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, \
	VK_KHR_SURFACE_EXTENSION_NAME
// clang-format on

static const char *instance_extensions_common[] = {
    COMP_INSTANCE_EXTENSIONS_COMMON,
};
static const char *required_device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,                 //
    VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,      //
    VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,            //
    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,           //
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,        //
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, //

// Platform version of "external_memory"
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
    VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_WIN32_HANDLE)
    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,

#else
#error "Need port!"
#endif

// Platform version of "external_fence" and "external_semaphore"
#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD) // Optional

#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
    VK_KHR_EXTERNAL_FENCE_WIN32_EXTENSION_NAME,

#else
#error "Need port!"
#endif
};


using unique_string_list =
    std::unique_ptr<u_string_list, xrt::deleters::ptr_ptr_deleter<u_string_list, &u_string_list_destroy>>;

static void
xrt_comp_vk_destroy(struct xrt_compositor_vk **ptr_xcvk)
{
	if (!ptr_xcvk) {
		return;
	}
	xrt_compositor *xc = &(*ptr_xcvk)->base;
	xrt_comp_destroy(&xc);
}

using unique_compositor_vk =
    std::unique_ptr<struct xrt_compositor_vk,
                    xrt::deleters::ptr_ptr_deleter<struct xrt_compositor_vk, &xrt_comp_vk_destroy>>;

TEST_CASE("client_compositor", "[.][needgpu]")
{
	xrt_compositor_native *xcn = mock_create_native_compositor();
	struct mock_compositor *mc = mock_compositor(&(xcn->base));

	// every backend needs at least the common extensions
	unique_string_list required_instance_ext_list{
	    u_string_list_create_from_array(instance_extensions_common, ARRAY_SIZE(instance_extensions_common))};

	unique_string_list optional_instance_ext_list{u_string_list_create()};

	unique_string_list required_device_extension_list{
	    u_string_list_create_from_array(required_device_extensions, ARRAY_SIZE(required_device_extensions))};

	unique_string_list optional_device_extension_list{u_string_list_create()};

	comp_vulkan_arguments args{VK_MAKE_VERSION(1, 0, 0),
	                           vkGetInstanceProcAddr,
	                           required_instance_ext_list.get(),
	                           optional_instance_ext_list.get(),
	                           required_device_extension_list.get(),
	                           optional_device_extension_list.get(),
	                           U_LOGGING_TRACE,
	                           false /* only_compute_queue */,
	                           true /*timeline_semaphore*/,
	                           -1,
	                           -1};
	vk_bundle vk_bundle_storage{};
	vk_bundle *vk = &vk_bundle_storage;
	comp_vulkan_results results{};
	REQUIRE(comp_vulkan_init_bundle(vk, &args, &results));
	struct xrt_compositor_vk *xcvk = xrt_gfx_vk_provider_create( //
	    xcn,                                                     //
	    vk->instance,                                            //
	    vkGetInstanceProcAddr,                                   //
	    vk->physical_device,                                     //
	    vk->device,
#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)          //
	    vk->external.fence_sync_fd,              //
	    vk->external.binary_semaphore_sync_fd,   //
	    vk->external.timeline_semaphore_sync_fd, //
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	    vk->external.fence_win32_handle,              //
	    vk->external.binary_semaphore_win32_handle,   //
	    vk->external.timeline_semaphore_win32_handle, //
#else
#error "Need port for fence sync handles checkers"
#endif
	    vk->queue_family_index, //
	    vk->queue_index);
	struct xrt_compositor *xc = &xcvk->base;

	SECTION("CreateSwapchain calls native create")
	{
		bool nativeCreateCalled = false;
		mc->userdata = &nativeCreateCalled;
		mc->compositor_hooks.create_swapchain =
		    [](struct mock_compositor *mc, struct mock_compositor_swapchain *mcsc,
		       const struct xrt_swapchain_create_info *info, struct xrt_swapchain **out_xsc) {
			    *static_cast<bool *>(mc->userdata) = true;
			    return XRT_SUCCESS;
		    };
		xrt_swapchain_create_info xsci{};
		xsci.format = VK_FORMAT_B8G8R8A8_SRGB;
		xsci.bits = (xrt_swapchain_usage_bits)(XRT_SWAPCHAIN_USAGE_COLOR | XRT_SWAPCHAIN_USAGE_SAMPLED);
		xsci.sample_count = 1;
		xsci.width = 800;
		xsci.height = 600;
		xsci.face_count = 1;
		xsci.array_size = 1;
		xsci.mip_count = 1;

		struct xrt_swapchain *xsc = nullptr;
		// This will fail because the mock compositor doesn't actually create images for Vulkan to import, but
		// it will get far enough to trigger our hook and update the flag.
		xrt_comp_create_swapchain(xc, &xsci, &xsc);
		CHECK(nativeCreateCalled);
		xrt_swapchain_reference(&xsc, nullptr);
	}

	xrt_comp_destroy(&xc);

	if (vk->cmd_pool != VK_NULL_HANDLE) {
		vk->vkDeviceWaitIdle(vk->device);
		vk->vkDestroyCommandPool(vk->device, vk->cmd_pool, NULL);
		vk->cmd_pool = VK_NULL_HANDLE;
	}
	vk_deinit_mutex(vk);

	xrt_comp_native_destroy(&xcn);
}
