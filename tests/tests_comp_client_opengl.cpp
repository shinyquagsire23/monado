// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenGL tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */


#include "mock/mock_compositor.h"
#include "client/comp_gl_client.h"

#include "catch/catch.hpp"
#include "util/u_handles.h"
#include "ogl/ogl_api.h"

#include "xrt/xrt_config_os.h"
#include "xrt/xrt_config_vulkan.h"
#include "xrt/xrt_deleters.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#if defined(XRT_OS_WINDOWS)
#include "client/comp_gl_win32_client.h"
#elif defined(VK_USE_PLATFORM_XLIB_XRANDR_EXT)
#include "client/comp_gl_xlib_client.h"
#else
#error "not implemented"
#endif

#include <stdint.h>

TEST_CASE("opengl_client_compositor", "[.][needgpu]")
{
	xrt_compositor_native *xcn = mock_create_native_compositor();
	struct mock_compositor *mc = mock_compositor(&(xcn->base));

	REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	auto *window = SDL_CreateWindow("Tests", 100, 100, 320, 240, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	REQUIRE(window);
	SDL_SysWMinfo info{};
	SDL_VERSION(&info.version);
	REQUIRE(SDL_GetWindowWMInfo(window, &info));

	auto context = SDL_GL_CreateContext(window);

	SDL_GL_MakeCurrent(window, context);

#if defined(XRT_OS_WINDOWS)
	HDC hDC = info.info.win.hdc;
	HGLRC hGLRC = wglGetCurrentContext();
	struct client_gl_win32_compositor *c = client_gl_win32_compositor_create(xcn, hDC, hGLRC);
#elif defined(VK_USE_PLATFORM_XLIB_XRANDR_EXT)
	Display *display = info.info.x11.display;
	struct client_gl_xlib_compositor *c =
	    client_gl_xlib_compositor_create(xcn, display, 0, nullptr, nullptr, (GLXContext)context);
#else
#error "not implemented"
#endif
	REQUIRE(c);
	struct xrt_compositor *xc = &c->base.base.base;

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
		xsci.format = GL_SRGB8_ALPHA8;
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
			// OpenGL should always create using the native compositor
			CHECK_FALSE(data.nativeImportCalled);
			CHECK(data.nativeCreateCalled);
			xrt_swapchain_reference(&xsc, nullptr);
		}
	}

	xrt_comp_destroy(&xc);
	xrt_comp_native_destroy(&xcn);

	SDL_GL_DeleteContext(context);
	SDL_DestroyWindow(window);
	SDL_Quit();
}
