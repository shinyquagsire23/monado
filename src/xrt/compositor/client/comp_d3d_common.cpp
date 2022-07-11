// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  D3D12 client side glue to compositor implementation.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include "comp_d3d_common.hpp"

#include "util/u_logging.h"
#include "util/u_time.h"

#include <inttypes.h>


#define D3D_COMMON_SPEW(log_level, ...) U_LOG_IFL_T(log_level, __VA_ARGS__);

#define D3D_COMMON_DEBUG(log_level, ...) U_LOG_IFL_D(log_level, __VA_ARGS__);

#define D3D_COMMON_INFO(log_level, ...) U_LOG_IFL_I(log_level, __VA_ARGS__);

#define D3D_COMMON_WARN(log_level, ...) U_LOG_IFL_W(log_level, __VA_ARGS__);

#define D3D_COMMON_ERROR(log_level, ...) U_LOG_IFL_E(log_level, __VA_ARGS__);

namespace xrt::compositor::client {

// xrt_result_t
// importFromHandleDuplicates(xrt_compositor_native &xcn,
//                            std::vector<wil::unique_handle> const &handles,
//                            const xrt_swapchain_create_info &vkinfo,
//                            bool use_dedicated_allocation,
//                            unique_swapchain_ref &out_xsc)


static inline DWORD
convertTimeoutToWindowsMilliseconds(uint64_t timeout_ns)
{
	return (timeout_ns == XRT_INFINITE_DURATION) ? INFINITE : (DWORD)(timeout_ns / (uint64_t)U_TIME_1MS_IN_NS);
}

KeyedMutexCollection::KeyedMutexCollection(u_logging_level log_level) noexcept : log_level(log_level) {}

xrt_result_t
KeyedMutexCollection::init(const std::vector<wil::com_ptr<ID3D11Texture2D1>> &images) noexcept
try {
	keyed_mutex_collection.clear();
	keyed_mutex_collection.reserve(images.size());
	for (const auto &image : images) {
		keyed_mutex_collection.emplace_back(image.query<IDXGIKeyedMutex>());
	}

	keyed_mutex_acquired.clear();
	keyed_mutex_acquired.resize(keyed_mutex_collection.size());
	return XRT_SUCCESS;
} catch (wil::ResultException const &e) {
	U_LOG_E("Error getting keyed mutex collection for swapchain: %s", e.what());
	return XRT_ERROR_D3D;
} catch (std::exception const &e) {
	U_LOG_E("Error getting keyed mutex collection for swapchain: %s", e.what());
	return XRT_ERROR_D3D;
} catch (...) {
	U_LOG_E("Error getting keyed mutex collection for swapchain");
	return XRT_ERROR_D3D;
}

xrt_result_t
KeyedMutexCollection::waitKeyedMutex(uint32_t index, uint64_t timeout_ns) noexcept

try {

	if (keyed_mutex_acquired[index]) {

		D3D_COMMON_WARN(log_level,
		                "Will not acquire the keyed mutex for image %" PRId32 " - it was already acquired!",
		                index);
		return XRT_ERROR_NO_IMAGE_AVAILABLE;
	}

	auto hr =
	    keyed_mutex_collection[index]->AcquireSync(kKeyedMutexKey, convertTimeoutToWindowsMilliseconds(timeout_ns));
	if (hr == WAIT_ABANDONED) {
		D3D_COMMON_ERROR(log_level,
		                 "Could not acquire the keyed mutex for image %" PRId32
		                 " due to it being in an inconsistent state",
		                 index);
		return XRT_ERROR_D3D;
	}
	if (hr == WAIT_TIMEOUT) {
		return XRT_TIMEOUT;
	}
	if (FAILED(hr)) {
		D3D_COMMON_ERROR(log_level, "Could not acquire the keyed mutex for image %" PRId32, index);
		return XRT_ERROR_D3D;
	}
	keyed_mutex_acquired[index] = true;
	return XRT_SUCCESS;
} catch (wil::ResultException const &e) {
	U_LOG_E("Error acquiring keyed mutex for image %" PRId32 ": %s", index, e.what());
	return XRT_ERROR_D3D;
} catch (std::exception const &e) {
	U_LOG_E("Error acquiring keyed mutex for image %" PRId32 ": %s", index, e.what());
	return XRT_ERROR_D3D;
} catch (...) {
	U_LOG_E("Error acquiring keyed mutex for image %" PRId32, index);
	return XRT_ERROR_D3D;
}


xrt_result_t
KeyedMutexCollection::releaseKeyedMutex(uint32_t index) noexcept

try {

	if (!keyed_mutex_acquired[index]) {

		D3D_COMMON_WARN(log_level,
		                "Will not release the keyed mutex for image %" PRId32 " - it was not acquired!", index);
		return XRT_ERROR_D3D;
	}
	auto hr = LOG_IF_FAILED(keyed_mutex_collection[index]->ReleaseSync(kKeyedMutexKey));
	if (FAILED(hr)) {
		D3D_COMMON_ERROR(log_level, "Could not release the keyed mutex for image %" PRId32, index);
		return XRT_ERROR_D3D;
	}
	keyed_mutex_acquired[index] = false;
	return XRT_SUCCESS;

} catch (wil::ResultException const &e) {
	U_LOG_E("Error releasing keyed mutex %" PRId32 ": %s", index, e.what());
	return XRT_ERROR_D3D;
} catch (std::exception const &e) {
	U_LOG_E("Error releasing keyed mutex %" PRId32 ": %s", index, e.what());
	return XRT_ERROR_D3D;
} catch (...) {
	U_LOG_E("Error releasing keyed mutex %" PRId32, index);
	return XRT_ERROR_D3D;
}


} // namespace xrt::compositor::client
