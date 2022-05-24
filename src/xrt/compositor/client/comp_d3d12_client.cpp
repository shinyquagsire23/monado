// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  D3D12 client side glue to compositor implementation.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include "comp_d3d12_client.h"

#include "comp_d3d_common.hpp"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_config_os.h"
#include "xrt/xrt_handles.h"
#include "xrt/xrt_deleters.hpp"
#include "xrt/xrt_results.h"
#include "xrt/xrt_vulkan_includes.h"
#include "d3d/d3d_dxgi_formats.h"
#include "d3d/d3d_d3d12_helpers.hpp"
#include "d3d/d3d_d3d12_fence.hpp"
#include "d3d/d3d_d3d12_bits.h"
#include "d3d/d3d_d3d11_allocator.hpp"
#include "d3d/d3d_d3d11_helpers.hpp"
#include "d3d/d3d_dxgi_helpers.hpp"
#include "util/u_misc.h"
#include "util/u_pretty_print.h"
#include "util/u_time.h"
#include "util/u_logging.h"
#include "util/u_debug.h"
#include "util/u_handles.h"
#include "util/u_win32_com_guard.hpp"

#include <d3d12.h>
#include <wil/resource.h>
#include <wil/com.h>
#include <wil/result_macros.h>

#include <assert.h>
#include <inttypes.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <array>

using namespace std::chrono_literals;
using namespace std::chrono;

DEBUG_GET_ONCE_LOG_OPTION(log, "D3D_COMPOSITOR_LOG", U_LOGGING_INFO)
DEBUG_GET_ONCE_BOOL_OPTION(allow_depth, "D3D_COMPOSITOR_ALLOW_DEPTH", false);

DEBUG_GET_ONCE_BOOL_OPTION(barriers, "D3D12_COMPOSITOR_BARRIERS", false);
DEBUG_GET_ONCE_BOOL_OPTION(initial_transition, "D3D12_COMPOSITOR_INITIAL_TRANSITION", true);

/*!
 * Spew level logging.
 *
 * @relates client_d3d12_compositor
 */
#define D3D_SPEW(c, ...) U_LOG_IFL_T(c->log_level, __VA_ARGS__);

/*!
 * Debug level logging.
 *
 * @relates client_d3d12_compositor
 */
#define D3D_DEBUG(c, ...) U_LOG_IFL_D(c->log_level, __VA_ARGS__);

/*!
 * Info level logging.
 *
 * @relates client_d3d12_compositor
 */
#define D3D_INFO(c, ...) U_LOG_IFL_I(c->log_level, __VA_ARGS__);

/*!
 * Warn level logging.
 *
 * @relates client_d3d12_compositor
 */
#define D3D_WARN(c, ...) U_LOG_IFL_W(c->log_level, __VA_ARGS__);

/*!
 * Error level logging.
 *
 * @relates client_d3d12_compositor
 */
#define D3D_ERROR(c, ...) U_LOG_IFL_E(c->log_level, __VA_ARGS__);

using unique_compositor_semaphore_ref = std::unique_ptr<
    struct xrt_compositor_semaphore,
    xrt::deleters::reference_deleter<struct xrt_compositor_semaphore, xrt_compositor_semaphore_reference>>;

using unique_swapchain_ref =
    std::unique_ptr<struct xrt_swapchain,
                    xrt::deleters::reference_deleter<struct xrt_swapchain, xrt_swapchain_reference>>;

// 0 is special
static constexpr uint64_t kKeyedMutexKey = 0;

// Timeout to wait for completion
static constexpr auto kFenceTimeout = 500ms;

/*!
 * @class client_d3d12_compositor
 *
 * Wraps the real compositor providing a D3D12 based interface.
 *
 * @ingroup comp_client
 * @implements xrt_compositor_d3d12
 */
struct client_d3d12_compositor
{
	struct xrt_compositor_d3d12 base = {};

	//! Owning reference to the backing native compositor
	struct xrt_compositor_native *xcn{nullptr};

	//! Just keeps COM alive while we keep references to COM things.
	xrt::auxiliary::util::ComGuard com_guard;

	//! Logging level.
	enum u_logging_level log_level;

	//! Device we got from the app
	wil::com_ptr<ID3D12Device> device;

	//! Command queue for @ref device
	wil::com_ptr<ID3D12CommandQueue> app_queue;

	//! Command list allocator for the compositor
	wil::com_ptr<ID3D12CommandAllocator> command_allocator;

	//! D3D11 device used for allocating images
	wil::com_ptr<ID3D11Device5> d3d11_device;

	//! D3D11 context used for allocating images
	wil::com_ptr<ID3D11DeviceContext4> d3d11_context;

	/*!
	 * A timeline semaphore made by the native compositor and imported by us.
	 *
	 * When this is valid, we should use xrt_compositor::layer_commit_with_sync:
	 * it means the native compositor knows about timeline semaphores, and we can import its semaphores, so we can
	 * pass @ref timeline_semaphore instead of blocking locally.
	 */
	unique_compositor_semaphore_ref timeline_semaphore;

	/*!
	 * A fence (timeline semaphore) object, owned by @ref fence_device.
	 *
	 * Signal using @ref fence_context if this is not null.
	 *
	 * Wait on it in `layer_commit` if @ref timeline_semaphore *is* null/invalid.
	 */
	wil::com_ptr<ID3D12Fence> fence;

	/*!
	 * Event used for blocking in `layer_commit` if required (if @ref timeline_semaphore *is* null/invalid)
	 */
	wil::unique_event_nothrow local_wait_event;

	/*!
	 * The value most recently signaled on the timeline semaphore
	 */
	uint64_t timeline_semaphore_value = 0;
};

static_assert(std::is_standard_layout<client_d3d12_compositor>::value);

struct client_d3d12_swapchain;

static inline DWORD
convertTimeoutToWindowsMilliseconds(uint64_t timeout_ns)
{
	return (timeout_ns == XRT_INFINITE_DURATION) ? INFINITE : (DWORD)(timeout_ns / (uint64_t)U_TIME_1MS_IN_NS);
}

/*!
 * Split out from @ref client_d3d12_swapchain to ensure that it is standard
 * layout, std::vector for instance is not standard layout.
 */
struct client_d3d12_swapchain_data
{
	explicit client_d3d12_swapchain_data(enum u_logging_level log_level) : keyed_mutex_collection(log_level) {}

	xrt::compositor::client::KeyedMutexCollection keyed_mutex_collection;

	//! The shared handles for all our images
	std::vector<wil::unique_handle> handles;

	//! D3D11 Images
	std::vector<wil::com_ptr<ID3D11Texture2D1>> d3d11_images;

	//! Images
	std::vector<wil::com_ptr<ID3D12Resource>> images;

	//! Command list per-image to put the resource in a state for acquire (@ref appResourceState) from @ref
	//! compositorResourceState
	std::vector<wil::com_ptr<ID3D12CommandList>> commandsToApp;

	//! Command list per-image to put the resource in a state for composition (@ref compositorResourceState) from
	//! @ref appResourceState
	std::vector<wil::com_ptr<ID3D12CommandList>> commandsToCompositor;

	//! State we hand over the image in, and expect it back in.
	D3D12_RESOURCE_STATES appResourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	//! State the compositor wants the image in before use.
	D3D12_RESOURCE_STATES compositorResourceState = D3D12_RESOURCE_STATE_COMMON;

	std::vector<D3D12_RESOURCE_STATES> state;
};

/*!
 * Wraps the real compositor swapchain providing a D3D12 based interface.
 *
 * @ingroup comp_client
 * @implements xrt_swapchain_d3d12
 */
struct client_d3d12_swapchain
{
	struct xrt_swapchain_d3d12 base;

	//! Owning reference to the imported swapchain.
	unique_swapchain_ref xsc;

	//! Non-owning reference to our parent compositor.
	struct client_d3d12_compositor *c{nullptr};

	//! implementation struct with things that aren't standard_layout
	std::unique_ptr<client_d3d12_swapchain_data> data;
};

static_assert(std::is_standard_layout<client_d3d12_swapchain>::value);

/*!
 * Down-cast helper.
 * @private @memberof client_d3d12_swapchain
 */
static inline struct client_d3d12_swapchain *
as_client_d3d12_swapchain(struct xrt_swapchain *xsc)
{
	return reinterpret_cast<client_d3d12_swapchain *>(xsc);
}

/*!
 * Down-cast helper.
 * @private @memberof client_d3d12_compositor
 */
static inline struct client_d3d12_compositor *
as_client_d3d12_compositor(struct xrt_compositor *xc)
{
	return (struct client_d3d12_compositor *)xc;
}


/*
 *
 * Logging helper.
 *
 */
static constexpr size_t kErrorBufSize = 256;

template <size_t N>
static inline bool
formatMessage(DWORD err, char (&buf)[N])
{
	if (0 != FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err,
	                        LANG_SYSTEM_DEFAULT, buf, N - 1, NULL)) {
		return true;
	}
	memset(buf, 0, N);
	return false;
}


/*
 *
 * Helpers for Swapchain
 *
 */

static xrt_result_t
client_d3d12_swapchain_barrier_to_app(client_d3d12_swapchain *sc, uint32_t index)
{
	auto *data = sc->data.get();
	if (data->commandsToApp.empty()) {
		// We have decided not to use barriers here
		return XRT_SUCCESS;
	}
	if (data->state[index] == data->appResourceState) {
		D3D_INFO(sc->c, "Image %" PRId32 " is already in the right state", index);
		return XRT_SUCCESS;
	}
	if (data->state[index] == data->compositorResourceState) {
		D3D_INFO(sc->c, "Acquiring image %" PRId32, index);
		std::array<ID3D12CommandList *, 1> commandLists{{data->commandsToApp[index].get()}};
		sc->c->app_queue->ExecuteCommandLists(1, commandLists.data());
		data->state[index] = data->appResourceState;
		return XRT_SUCCESS;
	}
	D3D_WARN(sc->c, "Image %" PRId32 " is in an unknown state", index);
	return XRT_ERROR_D3D12;
}

static xrt_result_t
client_d3d12_swapchain_barrier_to_compositor(client_d3d12_swapchain *sc, uint32_t index)
{
	auto *data = sc->data.get();

	if (data->commandsToCompositor.empty()) {
		// We have decided not to use barriers here
		return XRT_SUCCESS;
	}

	std::array<ID3D12CommandList *, 1> commandLists{{data->commandsToCompositor[index].get()}};
	sc->c->app_queue->ExecuteCommandLists(1, commandLists.data());
	data->state[index] = data->compositorResourceState;
	return XRT_SUCCESS;
}

/*
 *
 * Swapchain functions.
 *
 */

static xrt_result_t
client_d3d12_swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *out_index)
{
	struct client_d3d12_swapchain *sc = as_client_d3d12_swapchain(xsc);

	uint32_t index = 0;
	// Pipe down call into imported swapchain in native compositor.
	xrt_result_t xret = xrt_swapchain_acquire_image(sc->xsc.get(), &index);

	if (xret == XRT_SUCCESS) {
		// Set output variable
		*out_index = index;
	}
	return xret;
}

static xrt_result_t
client_d3d12_swapchain_wait_image(struct xrt_swapchain *xsc, uint64_t timeout_ns, uint32_t index)
{
	struct client_d3d12_swapchain *sc = as_client_d3d12_swapchain(xsc);

	// Pipe down call into imported swapchain in native compositor.
	xrt_result_t xret = xrt_swapchain_wait_image(sc->xsc.get(), timeout_ns, index);

	if (xret == XRT_SUCCESS) {
		// OK, we got the image in the native compositor, now need the keyed mutex in d3d11.
		xret = sc->data->keyed_mutex_collection.waitKeyedMutex(index, timeout_ns);
	}
	if (xret == XRT_SUCCESS) {
		// OK, we got the image in the native compositor, now need the transition in d3d12.
		xret = client_d3d12_swapchain_barrier_to_app(sc, index);
	}

	//! @todo discard old contents?
	return xret;
}

static xrt_result_t
client_d3d12_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	struct client_d3d12_swapchain *sc = as_client_d3d12_swapchain(xsc);

	// Pipe down call into imported swapchain in native compositor.
	xrt_result_t xret = xrt_swapchain_release_image(sc->xsc.get(), index);

	if (xret == XRT_SUCCESS) {
		// Release the keyed mutex
		xret = sc->data->keyed_mutex_collection.releaseKeyedMutex(index);
	}

	if (xret == XRT_SUCCESS) {
		xret = client_d3d12_swapchain_barrier_to_compositor(sc, index);
	}
	return xret;
}

static void
client_d3d12_swapchain_destroy(struct xrt_swapchain *xsc)
{
	// letting destruction do it all
	std::unique_ptr<client_d3d12_swapchain> sc(as_client_d3d12_swapchain(xsc));
}


xrt_result_t
client_d3d12_create_swapchain(struct xrt_compositor *xc,
                              const struct xrt_swapchain_create_info *info,
                              struct xrt_swapchain **out_xsc)
try {
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);
	xrt_result_t xret = XRT_SUCCESS;
	xrt_swapchain_create_properties xsccp{};
	xret = xrt_comp_get_swapchain_create_properties(xc, info, &xsccp);

	if (xret != XRT_SUCCESS) {
		D3D_ERROR(c, "Could not get properties for creating swapchain");
		return xret;
	}
	uint32_t image_count = xsccp.image_count;


	if ((info->create & XRT_SWAPCHAIN_CREATE_PROTECTED_CONTENT) != 0) {
		D3D_WARN(c,
		         "Swapchain info is valid but this compositor doesn't support creating protected content "
		         "swapchains!");
		return XRT_ERROR_SWAPCHAIN_FLAG_VALID_BUT_UNSUPPORTED;
	}

	int64_t vk_format = d3d_dxgi_format_to_vk((DXGI_FORMAT)info->format);
	if (vk_format == 0) {
		D3D_ERROR(c, "Invalid format!");
		return XRT_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	}

	struct xrt_swapchain_create_info xinfo = *info;
	struct xrt_swapchain_create_info vkinfo = *info;
	vkinfo.format = vk_format;

	std::unique_ptr<struct client_d3d12_swapchain> sc = std::make_unique<struct client_d3d12_swapchain>();
	sc->data = std::make_unique<client_d3d12_swapchain_data>(c->log_level);
	auto &data = sc->data;

	// Make images with D3D11
	xret = xrt::auxiliary::d3d::d3d11::allocateSharedImages(*(c->d3d11_device), xinfo, image_count, true,
	                                                        data->d3d11_images, data->handles);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	data->images.reserve(image_count);

	// Import to D3D12 from the handle.
	for (uint32_t i = 0; i < image_count; ++i) {
		const auto &handle = data->handles[i];
		wil::unique_handle dupedForD3D12{u_graphics_buffer_ref(handle.get())};
		auto d3d12Image = xrt::auxiliary::d3d::d3d12::importImage(*(c->device), dupedForD3D12.get());
		// Put the image where the OpenXR state tracker can get it
		sc->base.images[i] = d3d12Image.get();

		// Store the owning pointer for lifetime management
		data->images.emplace_back(std::move(d3d12Image));
	}

	D3D12_RESOURCE_STATES appResourceState = d3d_convert_usage_bits_to_d3d12_app_resource_state(xinfo.bits);

	// Transition all images from _COMMON to the correct state
	if (debug_get_bool_option_initial_transition()) {
		D3D_INFO(c, "Executing initial barriers");
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; // state at creation in d3d11
		barrier.Transition.StateAfter = appResourceState;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		data->state.resize(image_count, barrier.Transition.StateAfter);

		std::vector<D3D12_RESOURCE_BARRIER> barriers;
		for (const auto &image : data->images) {
			barrier.Transition.pResource = image.get();
			barriers.emplace_back(barrier);
		}
		wil::com_ptr<ID3D12GraphicsCommandList> commandList;
		THROW_IF_FAILED(c->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		                                             c->command_allocator.get(), nullptr,
		                                             IID_PPV_ARGS(commandList.put())));
		commandList->ResourceBarrier((UINT)barriers.size(), barriers.data());
		commandList->Close();
		std::array<ID3D12CommandList *, 1> commandLists{commandList.get()};

		c->app_queue->ExecuteCommandLists((UINT)commandLists.size(), commandLists.data());
	}

	/// @todo No idea if this is right, might depend on whether it's the compute or graphics compositor!
	D3D12_RESOURCE_STATES compositorResourceState = D3D12_RESOURCE_STATE_COMMON;

	data->appResourceState = appResourceState;
	data->compositorResourceState = compositorResourceState;

	data->state.resize(image_count, appResourceState);

	if (debug_get_bool_option_barriers()) {
		D3D_INFO(c, "Will use barriers at runtime");
		data->commandsToApp.reserve(image_count);
		data->commandsToCompositor.reserve(image_count);

		// Make the command lists to transition images
		for (uint32_t i = 0; i < image_count; ++i) {
			wil::com_ptr<ID3D12CommandList> commandsToApp;
			wil::com_ptr<ID3D12CommandList> commandsToCompositor;

			D3D_INFO(c, "Creating command lists for image %" PRId32, i);
			HRESULT hr = xrt::auxiliary::d3d::d3d12::createCommandLists(
			    *(c->device), *(c->command_allocator), *(data->images[i]), xinfo.bits, commandsToApp,
			    commandsToCompositor);
			if (!SUCCEEDED(hr)) {
				char buf[kErrorBufSize];
				formatMessage(hr, buf);
				D3D_ERROR(c, "Error creating command list: %s", buf);
				return XRT_ERROR_D3D12;
			}

			data->commandsToApp.emplace_back(std::move(commandsToApp));
			data->commandsToCompositor.emplace_back(std::move(commandsToCompositor));
		}
	}

	// Cache the keyed mutex interface
	xret = data->keyed_mutex_collection.init(data->d3d11_images);
	if (xret != XRT_SUCCESS) {
		D3D_ERROR(c, "Error retrieving keyex mutex interfaces");
		return xret;
	}

	// Import into the native compositor, to create the corresponding swapchain which we wrap.
	xret = xrt::compositor::client::importFromHandleDuplicates(
	    *(c->xcn), data->handles, vkinfo, false /** @todo not sure - dedicated allocation */, sc->xsc);
	if (xret != XRT_SUCCESS) {
		D3D_ERROR(c, "Error importing D3D swapchain into native compositor");
		return xret;
	}

	sc->base.base.destroy = client_d3d12_swapchain_destroy;
	sc->base.base.acquire_image = client_d3d12_swapchain_acquire_image;
	sc->base.base.wait_image = client_d3d12_swapchain_wait_image;
	sc->base.base.release_image = client_d3d12_swapchain_release_image;
	sc->c = c;
	sc->base.base.image_count = image_count;

	xrt_swapchain_reference(out_xsc, &sc->base.base);
	(void)sc.release();
	return XRT_SUCCESS;
} catch (wil::ResultException const &e) {
	U_LOG_E("Error creating D3D12 swapchain: %s", e.what());
	return XRT_ERROR_ALLOCATION;
} catch (std::exception const &e) {
	U_LOG_E("Error creating D3D12 swapchain: %s", e.what());
	return XRT_ERROR_ALLOCATION;
} catch (...) {
	U_LOG_E("Error creating D3D12 swapchain");
	return XRT_ERROR_ALLOCATION;
}

/*
 *
 * Compositor functions.
 *
 */


static xrt_result_t
client_d3d12_compositor_begin_session(struct xrt_compositor *xc, enum xrt_view_type type)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_begin_session(&c->xcn->base, type);
}

static xrt_result_t
client_d3d12_compositor_end_session(struct xrt_compositor *xc)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_end_session(&c->xcn->base);
}

static xrt_result_t
client_d3d12_compositor_wait_frame(struct xrt_compositor *xc,
                                   int64_t *out_frame_id,
                                   uint64_t *predicted_display_time,
                                   uint64_t *predicted_display_period)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_wait_frame(&c->xcn->base, out_frame_id, predicted_display_time, predicted_display_period);
}

static xrt_result_t
client_d3d12_compositor_begin_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_begin_frame(&c->xcn->base, frame_id);
}

static xrt_result_t
client_d3d12_compositor_discard_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_discard_frame(&c->xcn->base, frame_id);
}

static xrt_result_t
client_d3d12_compositor_layer_begin(struct xrt_compositor *xc,
                                    int64_t frame_id,
                                    uint64_t display_time_ns,
                                    enum xrt_blend_mode env_blend_mode)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_layer_begin(&c->xcn->base, frame_id, display_time_ns, env_blend_mode);
}

static xrt_result_t
client_d3d12_compositor_layer_stereo_projection(struct xrt_compositor *xc,
                                                struct xrt_device *xdev,
                                                struct xrt_swapchain *l_xsc,
                                                struct xrt_swapchain *r_xsc,
                                                const struct xrt_layer_data *data)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	assert(data->type == XRT_LAYER_STEREO_PROJECTION);

	struct xrt_swapchain *l_xscn = as_client_d3d12_swapchain(l_xsc)->xsc.get();
	struct xrt_swapchain *r_xscn = as_client_d3d12_swapchain(r_xsc)->xsc.get();

	// No flip required: D3D12 swapchain image convention matches Vulkan.
	return xrt_comp_layer_stereo_projection(&c->xcn->base, xdev, l_xscn, r_xscn, data);
}

static xrt_result_t
client_d3d12_compositor_layer_stereo_projection_depth(struct xrt_compositor *xc,
                                                      struct xrt_device *xdev,
                                                      struct xrt_swapchain *l_xsc,
                                                      struct xrt_swapchain *r_xsc,
                                                      struct xrt_swapchain *l_d_xsc,
                                                      struct xrt_swapchain *r_d_xsc,
                                                      const struct xrt_layer_data *data)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	assert(data->type == XRT_LAYER_STEREO_PROJECTION_DEPTH);

	struct xrt_swapchain *l_xscn = as_client_d3d12_swapchain(l_xsc)->xsc.get();
	struct xrt_swapchain *r_xscn = as_client_d3d12_swapchain(r_xsc)->xsc.get();
	struct xrt_swapchain *l_d_xscn = as_client_d3d12_swapchain(l_d_xsc)->xsc.get();
	struct xrt_swapchain *r_d_xscn = as_client_d3d12_swapchain(r_d_xsc)->xsc.get();

	// No flip required: D3D12 swapchain image convention matches Vulkan.
	return xrt_comp_layer_stereo_projection_depth(&c->xcn->base, xdev, l_xscn, r_xscn, l_d_xscn, r_d_xscn, data);
}

static xrt_result_t
client_d3d12_compositor_layer_quad(struct xrt_compositor *xc,
                                   struct xrt_device *xdev,
                                   struct xrt_swapchain *xsc,
                                   const struct xrt_layer_data *data)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	assert(data->type == XRT_LAYER_QUAD);

	struct xrt_swapchain *xscfb = as_client_d3d12_swapchain(xsc)->xsc.get();

	// No flip required: D3D12 swapchain image convention matches Vulkan.
	return xrt_comp_layer_quad(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_d3d12_compositor_layer_cube(struct xrt_compositor *xc,
                                   struct xrt_device *xdev,
                                   struct xrt_swapchain *xsc,
                                   const struct xrt_layer_data *data)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	assert(data->type == XRT_LAYER_CUBE);

	struct xrt_swapchain *xscfb = as_client_d3d12_swapchain(xsc)->xsc.get();

	// No flip required: D3D12 swapchain image convention matches Vulkan.
	return xrt_comp_layer_cube(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_d3d12_compositor_layer_cylinder(struct xrt_compositor *xc,
                                       struct xrt_device *xdev,
                                       struct xrt_swapchain *xsc,
                                       const struct xrt_layer_data *data)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	assert(data->type == XRT_LAYER_CYLINDER);

	struct xrt_swapchain *xscfb = as_client_d3d12_swapchain(xsc)->xsc.get();

	// No flip required: D3D12 swapchain image convention matches Vulkan.
	return xrt_comp_layer_cylinder(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_d3d12_compositor_layer_equirect1(struct xrt_compositor *xc,
                                        struct xrt_device *xdev,
                                        struct xrt_swapchain *xsc,
                                        const struct xrt_layer_data *data)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	assert(data->type == XRT_LAYER_EQUIRECT1);

	struct xrt_swapchain *xscfb = as_client_d3d12_swapchain(xsc)->xsc.get();

	// No flip required: D3D12 swapchain image convention matches Vulkan.
	return xrt_comp_layer_equirect1(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_d3d12_compositor_layer_equirect2(struct xrt_compositor *xc,
                                        struct xrt_device *xdev,
                                        struct xrt_swapchain *xsc,
                                        const struct xrt_layer_data *data)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	assert(data->type == XRT_LAYER_EQUIRECT2);

	struct xrt_swapchain *xscfb = as_client_d3d12_swapchain(xsc)->xsc.get();

	// No flip required: D3D12 swapchain image convention matches Vulkan.
	return xrt_comp_layer_equirect2(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_d3d12_compositor_layer_commit(struct xrt_compositor *xc,
                                     int64_t frame_id,
                                     xrt_graphics_sync_handle_t sync_handle)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	// We make the sync object, not st/oxr which is our user.
	assert(!xrt_graphics_sync_handle_is_valid(sync_handle));

	xrt_result_t xret = XRT_SUCCESS;
	if (c->fence) {
		c->timeline_semaphore_value++;
		HRESULT hr = c->fence->Signal(c->timeline_semaphore_value);
		if (!SUCCEEDED(hr)) {
			char buf[kErrorBufSize];
			formatMessage(hr, buf);
			D3D_ERROR(c, "Error signaling fence: %s", buf);
			return xrt_comp_layer_commit(&c->xcn->base, frame_id, XRT_GRAPHICS_SYNC_HANDLE_INVALID);
		}
	}
	if (c->timeline_semaphore) {
		// We got this from the native compositor, so we can pass it back
		return xrt_comp_layer_commit_with_semaphore(&c->xcn->base, frame_id, c->timeline_semaphore.get(),
		                                            c->timeline_semaphore_value);
	}

	if (c->fence) {
		// Wait on it ourselves, if we have it and didn't tell the native compositor to wait on it.
		xret = xrt::auxiliary::d3d::d3d12::waitOnFenceWithTimeout(c->fence, c->local_wait_event,
		                                                          c->timeline_semaphore_value, kFenceTimeout);
		if (xret != XRT_SUCCESS) {
			struct u_pp_sink_stack_only sink; // Not inited, very large.
			u_pp_delegate_t dg = u_pp_sink_stack_only_init(&sink);
			u_pp(dg, "Problem waiting on fence: ");
			u_pp_xrt_result(dg, xret);
			D3D_ERROR(c, "%s", sink.buffer);

			return xret;
		}
	}

	return xrt_comp_layer_commit(&c->xcn->base, frame_id, XRT_GRAPHICS_SYNC_HANDLE_INVALID);
}


static xrt_result_t
client_d3d12_compositor_get_swapchain_create_properties(struct xrt_compositor *xc,
                                                        const struct xrt_swapchain_create_info *info,
                                                        struct xrt_swapchain_create_properties *xsccp)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	return xrt_comp_get_swapchain_create_properties(&c->xcn->base, info, xsccp);
}

static xrt_result_t
client_d3d12_compositor_poll_events(struct xrt_compositor *xc, union xrt_compositor_event *out_xce)
{
	struct client_d3d12_compositor *c = as_client_d3d12_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_poll_events(&c->xcn->base, out_xce);
}

static void
client_d3d12_compositor_destroy(struct xrt_compositor *xc)
{
	std::unique_ptr<struct client_d3d12_compositor> c{as_client_d3d12_compositor(xc)};
}

static void
client_d3d12_compositor_init_try_timeline_semaphores(struct client_d3d12_compositor *c)
{
	c->timeline_semaphore_value = 1;
	// See if we can make a "timeline semaphore", also known as ID3D12Fence
	if (!c->xcn->base.create_semaphore || !c->xcn->base.layer_commit_with_semaphore) {
		return;
	}
	struct xrt_compositor_semaphore *xcsem = nullptr;
	wil::unique_handle timeline_semaphore_handle;
	if (XRT_SUCCESS != xrt_comp_create_semaphore(&(c->xcn->base), timeline_semaphore_handle.put(), &xcsem)) {
		D3D_WARN(c, "Native compositor tried but failed to created a timeline semaphore for us.");
		return;
	}
	D3D_INFO(c, "Native compositor created a timeline semaphore for us.");

	unique_compositor_semaphore_ref timeline_semaphore{xcsem};

	// try to import and signal
	wil::com_ptr<ID3D12Fence1> fence =
	    xrt::auxiliary::d3d::d3d12::importFence(*(c->device), timeline_semaphore_handle.get());
	D3D12_FENCE_FLAGS flags = fence->GetCreationFlags();
	if (flags & D3D12_FENCE_FLAG_NON_MONITORED) {
		D3D_WARN(c,
		         "Your graphics driver creates the native compositor's semaphores as 'non-monitored' making "
		         "them unusable in D3D12, falling back to local blocking.");
		return;
	}
	HRESULT hr = fence->Signal(c->timeline_semaphore_value);
	if (!SUCCEEDED(hr)) {
		D3D_WARN(c,
		         "Your graphics driver does not support importing the native compositor's "
		         "semaphores into D3D12, falling back to local blocking.");
		return;
	}

	D3D_INFO(c, "We imported a timeline semaphore and can signal it.");
	// OK, keep these resources around.
	c->fence = std::move(fence);
	c->timeline_semaphore = std::move(timeline_semaphore);
}

static void
client_d3d12_compositor_init_try_internal_blocking(struct client_d3d12_compositor *c)
{
	wil::com_ptr<ID3D12Fence> fence;
	HRESULT hr = c->device->CreateFence( //
	    0,                               // InitialValue
	    D3D12_FENCE_FLAG_NONE,           // Flags
	    __uuidof(ID3D12Fence),           // ReturnedInterface
	    fence.put_void());               // ppFence

	if (!SUCCEEDED(hr)) {
		char buf[kErrorBufSize];
		formatMessage(hr, buf);
		D3D_WARN(c, "Cannot even create an ID3D12Fence for internal use: %s", buf);
		return;
	}

	hr = c->local_wait_event.create();
	if (!SUCCEEDED(hr)) {
		char buf[kErrorBufSize];
		formatMessage(hr, buf);
		D3D_ERROR(c, "Error creating event for synchronization usage: %s", buf);
		return;
	}

	D3D_INFO(c, "We created our own ID3D12Fence and will wait on it ourselves.");
	c->fence = std::move(fence);
}

struct xrt_compositor_d3d12 *
client_d3d12_compositor_create(struct xrt_compositor_native *xcn, ID3D12Device *device, ID3D12CommandQueue *queue)
try {
	std::unique_ptr<struct client_d3d12_compositor> c = std::make_unique<struct client_d3d12_compositor>();
	c->log_level = debug_get_log_option_log();
	c->xcn = xcn;

	c->device = device;
	c->app_queue = queue;

	HRESULT hr =
	    c->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(c->command_allocator.put()));
	if (!SUCCEEDED(hr)) {
		char buf[kErrorBufSize];
		formatMessage(hr, buf);
		D3D_ERROR(c, "Error creating command allocator: %s", buf);
		return nullptr;
	}


	// Get D3D11 device/context for the same underlying adapter
	{
		LUID adapterLuid = device->GetAdapterLuid();
		auto adapter = xrt::auxiliary::d3d::getAdapterByLUID(
		    *reinterpret_cast<const xrt_luid_t *>(&adapterLuid), c->log_level);
		if (!adapter) {
			D3D_ERROR(c, "Error getting DXGI adapter");
			return nullptr;
		}

		// Now, try to get an equivalent device of our own
		wil::com_ptr<ID3D11Device> our_dev;
		wil::com_ptr<ID3D11DeviceContext> our_context;
		std::tie(our_dev, our_context) = xrt::auxiliary::d3d::d3d11::createDevice(adapter, c->log_level);
		our_dev.query_to(c->d3d11_device.put());
		our_context.query_to(c->d3d11_context.put());
	}


	// See if we can make a "timeline semaphore", also known as ID3D12Fence
	client_d3d12_compositor_init_try_timeline_semaphores(c.get());
	if (!c->timeline_semaphore) {
		// OK native compositor doesn't know how to handle timeline semaphores, or we can't import them, but we
		// can still use them entirely internally.
		client_d3d12_compositor_init_try_internal_blocking(c.get());
	}
	if (!c->fence) {
		D3D_WARN(c, "No sync mechanism for D3D12 was successful!");
	}
	c->base.base.get_swapchain_create_properties = client_d3d12_compositor_get_swapchain_create_properties;
	c->base.base.create_swapchain = client_d3d12_create_swapchain;
	c->base.base.begin_session = client_d3d12_compositor_begin_session;
	c->base.base.end_session = client_d3d12_compositor_end_session;
	c->base.base.wait_frame = client_d3d12_compositor_wait_frame;
	c->base.base.begin_frame = client_d3d12_compositor_begin_frame;
	c->base.base.discard_frame = client_d3d12_compositor_discard_frame;
	c->base.base.layer_begin = client_d3d12_compositor_layer_begin;
	c->base.base.layer_stereo_projection = client_d3d12_compositor_layer_stereo_projection;
	c->base.base.layer_stereo_projection_depth = client_d3d12_compositor_layer_stereo_projection_depth;
	c->base.base.layer_quad = client_d3d12_compositor_layer_quad;
	c->base.base.layer_cube = client_d3d12_compositor_layer_cube;
	c->base.base.layer_cylinder = client_d3d12_compositor_layer_cylinder;
	c->base.base.layer_equirect1 = client_d3d12_compositor_layer_equirect1;
	c->base.base.layer_equirect2 = client_d3d12_compositor_layer_equirect2;
	c->base.base.layer_commit = client_d3d12_compositor_layer_commit;
	c->base.base.destroy = client_d3d12_compositor_destroy;
	c->base.base.poll_events = client_d3d12_compositor_poll_events;


	// Passthrough our formats from the native compositor to the client.
	uint32_t count = 0;
	for (uint32_t i = 0; i < xcn->base.info.format_count; i++) {
		// Can we turn this format into DXGI?
		DXGI_FORMAT f = d3d_vk_format_to_dxgi(xcn->base.info.formats[i]);
		if (f == 0) {
			continue;
		}
		// And back to Vulkan?
		auto v = d3d_dxgi_format_to_vk(f);
		if (v == 0) {
			continue;
		}
		// Do we have a typeless version of it?
		DXGI_FORMAT typeless = d3d_dxgi_format_to_typeless_dxgi(f);
		if (typeless == f) {
			continue;
		}
		// Sometimes we have to forbid depth formats to avoid errors in Vulkan.
		if (!debug_get_bool_option_allow_depth() &&
		    (f == DXGI_FORMAT_D32_FLOAT || f == DXGI_FORMAT_D16_UNORM || f == DXGI_FORMAT_D24_UNORM_S8_UINT)) {
			continue;
		}

		c->base.base.info.formats[count++] = f;
	}
	c->base.base.info.format_count = count;

	return &(c.release()->base);
} catch (wil::ResultException const &e) {
	U_LOG_E("Error creating D3D12 client compositor: %s", e.what());
	return nullptr;
} catch (std::exception const &e) {
	U_LOG_E("Error creating D3D12 client compositor: %s", e.what());
	return nullptr;
} catch (...) {
	U_LOG_E("Error creating D3D12 client compositor");
	return nullptr;
}
