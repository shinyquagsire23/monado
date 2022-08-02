// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functionality common to D3D11 and D3D12 for client side compositor implementation.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */
#pragma once

#include "xrt/xrt_compositor.h"
#include "xrt/xrt_deleters.hpp"
#include "xrt/xrt_results.h"

#include "util/u_handles.h"
#include "util/u_logging.h"

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result_macros.h>

#include <d3d11_3.h>

#include <memory>
#include <vector>
#include <stdint.h>


namespace xrt::compositor::client {

using unique_swapchain_ref =
    std::unique_ptr<struct xrt_swapchain,
                    xrt::deleters::reference_deleter<struct xrt_swapchain, xrt_swapchain_reference>>;

/**
 * Import the provided handles into a native compositor, without consuming them.
 *
 * @param c The native compositor
 * @param handles A vector of uniquely-owned handles. These will be duplicated, not consumed, by this import.
 * @param vkinfo The swapchain create info, with format as a Vulkan constant
 * @param use_dedicated_allocation Passed through to @ref xrt_image_native
 * @param[out] out_xsc The swapchain to populate
 * @return XRT_SUCCESS if everything went well, otherwise whatever error a call internally returned.
 */
static inline xrt_result_t
importFromHandleDuplicates(xrt_compositor_native &xcn,
                           std::vector<wil::unique_handle> const &handles,
                           const struct xrt_swapchain_create_info &vkinfo,
                           bool use_dedicated_allocation,
                           unique_swapchain_ref &out_xsc)
{
	uint32_t image_count = static_cast<uint32_t>(handles.size());
	// Populate for import
	std::vector<xrt_image_native> xins;
	xins.reserve(image_count);

	// Keep this around until after successful import, then detach all.
	std::vector<wil::unique_handle> handlesForImport;
	handlesForImport.reserve(image_count);

	for (const wil::unique_handle &handle : handles) {
		wil::unique_handle duped{u_graphics_buffer_ref(handle.get())};
		xrt_image_native xin;
		xin.handle = duped.get();
		xin.size = 0;
		xin.use_dedicated_allocation = use_dedicated_allocation;

		handlesForImport.emplace_back(std::move(duped));
		xins.emplace_back(xin);
	}

	// Import into the native compositor, to create the corresponding swapchain which we wrap.
	xrt_swapchain *xsc = nullptr;
	xrt_result_t xret = xrt_comp_import_swapchain(&(xcn.base), &vkinfo, xins.data(), image_count, &xsc);
	if (xret != XRT_SUCCESS) {
		return xret;
	}
	// Let unique_ptr manage the lifetime of xsc now
	out_xsc.reset(xsc);

	// The imported swapchain took ownership of them now, release them from ownership here.
	for (auto &h : handlesForImport) {
		h.release();
	}
	return XRT_SUCCESS;
}

/**
 * A collection of DXGIKeyedMutex objects, one for each swapchain image in a swapchain.
 *
 */
class KeyedMutexCollection
{
public:
	// 0 is special
	static constexpr uint64_t kKeyedMutexKey = 0;

	/**
	 * @brief Construct a new Keyed Mutex Collection object
	 *
	 * @param log_level The compositor log level to use
	 */
	explicit KeyedMutexCollection(u_logging_level log_level) noexcept;

	/**
	 * Make the keyed mutex vector before starting to use the images.
	 *
	 * @param images Your vector of textures to acquire keyed mutexes from.
	 */
	xrt_result_t
	init(const std::vector<wil::com_ptr<ID3D11Texture2D1>> &images) noexcept;

	/**
	 * Wait for acquisition of the keyed mutex.
	 *
	 * @param index Swapchain image index
	 * @param timeout_ns Timeout in nanoseconds or XRT_INFINITE_DURATION
	 * @return xrt_result_t: XRT_SUCCESS, XRT_TIMEOUT, or some error
	 */
	xrt_result_t
	waitKeyedMutex(uint32_t index, uint64_t timeout_ns) noexcept;

	/**
	 * Release the keyed mutex
	 *
	 * @param index Swapchain image index
	 * @return xrt_result_t
	 */
	xrt_result_t
	releaseKeyedMutex(uint32_t index) noexcept;

private:
	//! Keyed mutex per image associated with client_d3d11_compositor::app_device
	std::vector<wil::com_ptr<IDXGIKeyedMutex>> keyed_mutex_collection;

	std::vector<bool> keyed_mutex_acquired;

	//! Logging level.
	u_logging_level log_level;
};


} // namespace xrt::compositor::client
