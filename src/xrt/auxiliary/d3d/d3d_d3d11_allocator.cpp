// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief D3D11 backed image buffer allocator.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_d3d
 */

#include "d3d_d3d11_allocator.h"
#include "d3d_d3d11_allocator.hpp"

#include "d3d_d3d11_bits.h"
#include "d3d_dxgi_formats.h"

#include "util/u_misc.h"
#include "util/u_logging.h"
#include "util/u_debug.h"
#include "util/u_handles.h"

#include "xrt/xrt_windows.h"

#include <Unknwn.h>
#include <d3d11_3.h>
#include <wil/com.h>
#include <wil/result.h>

#include <inttypes.h>
#include <memory>

#define DEFAULT_CATCH(...)                                                                                             \
	catch (wil::ResultException const &e)                                                                          \
	{                                                                                                              \
		U_LOG_E("Caught exception: %s", e.what());                                                             \
		return __VA_ARGS__;                                                                                    \
	}                                                                                                              \
	catch (std::exception const &e)                                                                                \
	{                                                                                                              \
		U_LOG_E("Caught exception: %s", e.what());                                                             \
		return __VA_ARGS__;                                                                                    \
	}                                                                                                              \
	catch (...)                                                                                                    \
	{                                                                                                              \
		U_LOG_E("Caught exception");                                                                           \
		return __VA_ARGS__;                                                                                    \
	}


DEBUG_GET_ONCE_LOG_OPTION(d3d11_log, "DXGI_LOG", U_LOGGING_WARN)
#define D3DA_TRACE(...) U_LOG_IFL_T(debug_get_log_option_d3d11_log(), __VA_ARGS__)
#define D3DA_DEBUG(...) U_LOG_IFL_D(debug_get_log_option_d3d11_log(), __VA_ARGS__)
#define D3DA_INFO(...) U_LOG_IFL_I(debug_get_log_option_d3d11_log(), __VA_ARGS__)
#define D3DA_WARN(...) U_LOG_IFL_W(debug_get_log_option_d3d11_log(), __VA_ARGS__)
#define D3DA_ERROR(...) U_LOG_IFL_E(debug_get_log_option_d3d11_log(), __VA_ARGS__)

namespace xrt::auxiliary::d3d::d3d11 {

wil::unique_handle
createSharedHandle(const wil::com_ptr<ID3D11Texture2D1> &image)
{

	wil::com_ptr<IDXGIResource1> dxgiRes;
	image.query_to(dxgiRes.put());
	wil::unique_handle h;
	THROW_IF_FAILED(dxgiRes->CreateSharedHandle(                //
	    nullptr,                                                // pAttributes
	    DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, // dwAccess
	    nullptr,                                                // lpName
	    h.put()));                                              // pHandle
	return h;
}

xrt_result_t
allocateSharedImages(ID3D11Device5 &device,
                     const xrt_swapchain_create_info &xsci,
                     size_t image_count,
                     bool keyed_mutex,
                     std::vector<wil::com_ptr<ID3D11Texture2D1>> &out_images,
                     std::vector<wil::unique_handle> &out_handles)
try {
	if (0 != (xsci.create & XRT_SWAPCHAIN_CREATE_PROTECTED_CONTENT)) {
		return XRT_ERROR_SWAPCHAIN_FLAG_VALID_BUT_UNSUPPORTED;
	}

	if (0 != (xsci.create & XRT_SWAPCHAIN_CREATE_STATIC_IMAGE) && image_count > 1) {
		D3DA_ERROR("Got XRT_SWAPCHAIN_CREATE_STATIC_IMAGE but an image count greater than 1!");
		return XRT_ERROR_ALLOCATION;
	}
	CD3D11_TEXTURE2D_DESC1 desc{d3d_dxgi_format_to_typeless_dxgi((DXGI_FORMAT)xsci.format),
	                            xsci.width,
	                            xsci.height,
	                            xsci.array_size,
	                            xsci.mip_count,
	                            d3d_convert_usage_bits_to_d3d11_bind_flags(xsci.bits)};
	desc.SampleDesc.Count = xsci.sample_count;
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	if (keyed_mutex) {
		desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
	} else {
		desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
	}
	if (desc.Format == 0) {
		D3DA_ERROR("Invalid format %04" PRIx64 "!", (uint64_t)xsci.format);
		return XRT_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	}
	if (xsci.face_count == 6) {
		//! @todo implement instead of erroring out
		D3DA_ERROR("No idea how to do cube maps in d3d11!");
		return XRT_ERROR_ALLOCATION;
	}
	// Create textures
	std::vector<wil::com_ptr<ID3D11Texture2D1>> images;
	for (size_t i = 0; i < image_count; ++i) {
		wil::com_ptr<ID3D11Texture2D1> tex;
		THROW_IF_FAILED(device.CreateTexture2D1(&desc, nullptr, tex.put()));
		images.emplace_back(std::move(tex));
	}
	std::vector<wil::unique_handle> handles;
	handles.reserve(image_count);
	for (const auto &tex : images) {
		handles.emplace_back(createSharedHandle(tex));
	}
	out_images = std::move(images);
	out_handles = std::move(handles);
	return XRT_SUCCESS;
}
DEFAULT_CATCH(XRT_ERROR_ALLOCATION)

} // namespace xrt::auxiliary::d3d::d3d11

struct d3d11_allocator
{
	struct xrt_image_native_allocator base;
	wil::com_ptr<ID3D11Device5> device;
};


static xrt_result_t
d3d11_images_allocate(struct xrt_image_native_allocator *xina,
                      const struct xrt_swapchain_create_info *xsci,
                      size_t image_count,
                      struct xrt_image_native *out_images)
{
	try {
		d3d11_allocator *d3da = reinterpret_cast<d3d11_allocator *>(xina);

		std::vector<wil::com_ptr<ID3D11Texture2D1>> images;
		std::vector<wil::unique_handle> handles;
		auto result = xrt::auxiliary::d3d::d3d11::allocateSharedImages(*(d3da->device), *xsci, image_count,
		                                                               false, images, handles);
		if (XRT_SUCCESS != result) {
			return result;
		}
		// if we made it this far without exceptions, we can safely report back the output.
		for (size_t i = 0; i < image_count; ++i) {
			out_images[i].handle = handles[i].release();
		}
		return XRT_SUCCESS;
	}
	DEFAULT_CATCH(XRT_ERROR_ALLOCATION)
}

static xrt_result_t
d3d11_images_free(struct xrt_image_native_allocator *xina, size_t image_count, struct xrt_image_native *images)
{
	for (size_t i = 0; i < image_count; ++i) {
		u_graphics_buffer_unref(&(images[i].handle));
	}
	return XRT_SUCCESS;
}

static void
d3d11_destroy(struct xrt_image_native_allocator *xina)
{
	d3d11_allocator *d3da = reinterpret_cast<d3d11_allocator *>(xina);
	delete d3da;
}

struct xrt_image_native_allocator *
d3d11_allocator_create(ID3D11Device *device)
try {
	auto ret = std::make_unique<d3d11_allocator>();
	U_ZERO(&(ret->base));
	ret->base.images_allocate = d3d11_images_allocate;
	ret->base.images_free = d3d11_images_free;
	ret->base.destroy = d3d11_destroy;
	return &(ret.release()->base);
}
DEFAULT_CATCH(nullptr)
