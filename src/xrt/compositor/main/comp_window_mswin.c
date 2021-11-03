// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Microsoft Windows window code.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include <stdlib.h>
#include <string.h>
#include "xrt/xrt_compiler.h"
#include "main/comp_window.h"
#include "util/u_misc.h"
#include "os/os_threading.h"

#include <processthreadsapi.h>


#undef ALLOW_CLOSING_WINDOW

/*
 *
 * Private structs.
 *
 */

/*!
 * A Microsoft Windows window.
 *
 * @implements comp_target_swapchain
 */
struct comp_window_mswin
{
	struct comp_target_swapchain base;
	struct os_thread_helper oth;

	HINSTANCE instance;
	HWND window;


	bool fullscreen_requested;
	bool should_exit;
};

static WCHAR szWindowClass[] = L"Monado";
static WCHAR szWindowData[] = L"MonadoWindow";

/*
 *
 * Functions.
 *
 */

static void
draw_window(HWND hWnd, struct comp_window_mswin *cwm)
{
	ValidateRect(hWnd, NULL);
}

static LRESULT CALLBACK
WndProc(HWND hWnd, unsigned int message, WPARAM wParam, LPARAM lParam)
{
	struct comp_window_mswin *cwm = GetPropW(hWnd, szWindowData);
	if (!cwm) {
		// This is before we've set up our window, or for some other helper window...
		// We might want to handle messages differently in here.
		return DefWindowProcW(hWnd, message, wParam, lParam);
	}
	switch (message) {
	case WM_PAINT: draw_window(hWnd, cwm); return 0;
	case WM_CLOSE: cwm->should_exit = true; return 0;
	case WM_DESTROY:
		// Post a quit message and return.
		cwm->should_exit = true;
		PostQuitMessage(0);
		return 0;
	default: return DefWindowProcW(hWnd, message, wParam, lParam);
	}
	return 0;
}


static inline struct vk_bundle *
get_vk(struct comp_window_mswin *cwm)
{
	return &cwm->base.base.c->base.vk;
}

static void
comp_window_mswin_destroy(struct comp_target *ct)
{
	struct comp_window_mswin *cwm = (struct comp_window_mswin *)ct;
	// Stop the Windows thread first.
	os_thread_helper_stop(&cwm->oth);

	comp_target_swapchain_cleanup(&cwm->base);

	//! @todo

	free(ct);
}

static void
comp_window_mswin_update_window_title(struct comp_target *ct, const char *title)
{
	struct comp_window_mswin *cwm = (struct comp_window_mswin *)ct;
	//! @todo
}

static void
comp_window_mswin_fullscreen(struct comp_window_mswin *w)
{
	//! @todo
}

static VkResult
comp_window_mswin_create_surface(struct comp_window_mswin *w, VkSurfaceKHR *vk_surface)
{
	struct vk_bundle *vk = get_vk(w);
	VkResult ret;
	VkWin32SurfaceCreateInfoKHR surface_info = {
	    .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
	    .hinstance = w->instance,
	    .hwnd = w->window,
	};

	ret = vk->vkCreateWin32SurfaceKHR(vk->instance, &surface_info, NULL, vk_surface);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.base.c, "vkCreateWin32SurfaceKHR: %s", vk_result_string(ret));
		return ret;
	}

	return VK_SUCCESS;
}

static bool
comp_window_mswin_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height)
{
	struct comp_window_mswin *cwm = (struct comp_window_mswin *)ct;
	VkResult ret;

	ret = comp_window_mswin_create_surface(cwm, &cwm->base.surface.handle);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(ct->c, "Failed to create surface '%s'!", vk_result_string(ret));
		return false;
	}

	//! @todo

	return true;
}


static void
comp_window_mswin_flush(struct comp_target *ct)
{
	struct comp_window_mswin *cwm = (struct comp_window_mswin *)ct;
}

static void
comp_window_mswin_thread(struct comp_window_mswin *cwm)
{
	// OK if this fails
	(void)SetThreadDescription(GetCurrentThread(), L"Message Handler");

	struct comp_target *ct = &cwm->base.base;

	RECT rc = {0, 0, (LONG)(ct->width), (LONG)ct->height};

	WNDCLASSEXW wcex;
	U_ZERO(&wcex);
	wcex.cbSize = sizeof(WNDCLASSEXW);
	wcex.style = CS_HREDRAW | CS_VREDRAW;

	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = cwm->instance;
	wcex.lpszClassName = szWindowClass;
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
//! @todo icon
#if 0
	wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SAMPLEGUI));
	wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_SAMPLEGUI);
	wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
#endif
	if (!RegisterClassExW(&wcex)) {
		COMP_ERROR(ct->c, "Failed to register window class");
		// parent thread will be notified (by caller) that we have exited.
		return;
	}

	cwm->window =
	    CreateWindowExW(0, szWindowClass, L"Monado (Windowed)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
	                    rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, cwm->instance, NULL);
	if (cwm->window == NULL) {
		COMP_ERROR(ct->c, "Failed to create window");
		// parent thread will be notified (by caller) that we have exited.
		return;
	}

	SetPropW(cwm->window, szWindowData, cwm);
	SetWindowLongPtr(cwm->window, GWLP_USERDATA, (LONG_PTR)(cwm));
	ShowWindow(cwm->window, SW_SHOWDEFAULT);
	UpdateWindow(cwm->window);

	// Unblock the parent thread now that we're successfully running.
	{
		os_thread_helper_lock(&cwm->oth);
		os_thread_helper_signal_locked(&cwm->oth);
		os_thread_helper_unlock(&cwm->oth);
	}
	COMP_WARN(cwm->base.base.c, "Starting the Windows window message loop");

	while (os_thread_helper_is_running(&cwm->oth)) {
		// force handling messages.
		MSG msg;
		while (PeekMessageW(&msg, cwm->window, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
#ifdef ALLOW_CLOSING_WINDOW
			/// @todo We need to bubble this up to multi-compositor
			/// and the state tracker (as "instance lost")
			if (msg.message == WM_QUIT) {
				COMP_INFO(cwm->base.base.c, "Got WM_QUIT message");
				return;
			}
			if (msg.message == WM_DESTROY) {
				COMP_INFO(cwm->base.base.c, "Got WM_DESTROY message");
				return;
			}
			if (cwm->should_exit) {
				COMP_INFO(cwm->base.base.c, "Got 'should_exit' flag.");
				return;
			}
#endif
		}
	}
}

static void *
comp_window_mswin_thread_func(void *ptr)
{

	struct comp_window_mswin *cwm = (struct comp_window_mswin *)ptr;
	comp_window_mswin_thread(cwm);
	os_thread_helper_signal_stop(&cwm->oth);
	COMP_WARN(cwm->base.base.c, "Windows window message thread now exiting.");
	return NULL;
}

static bool
comp_window_mswin_init(struct comp_target *ct)
{
	struct comp_window_mswin *cwm = (struct comp_window_mswin *)ct;
	cwm->instance = GetModuleHandle(NULL);

	ct->width = 1280;
	ct->height = 720;

	if (os_thread_helper_start(&cwm->oth, comp_window_mswin_thread_func, cwm) != 0) {
		COMP_ERROR(ct->c, "Failed to start Windows window message thread");
		return false;
	}

	// Wait for thread to start, create window, etc.
	os_thread_helper_lock(&cwm->oth);
	os_thread_helper_wait_locked(&cwm->oth);
	// if it fails it will exit immediately.
	bool ret = os_thread_helper_is_running_locked(&cwm->oth);
	os_thread_helper_unlock(&cwm->oth);
	return ret;
}

static void
comp_window_mswin_configure(struct comp_window_mswin *w, int32_t width, int32_t height)
{
	if (w->base.base.c->settings.fullscreen && !w->fullscreen_requested) {
		COMP_DEBUG(w->base.base.c, "Setting full screen");
		comp_window_mswin_fullscreen(w);
		w->fullscreen_requested = true;
	}
}

#ifdef ALLOW_CLOSING_WINDOW
/// @todo This is somehow triggering crashes in the multi-compositor, which is trying to run without things it needs,
/// even though it didn't do this when we called the parent impl instead of inlining it.
static bool
comp_window_mswin_configure_check_ready(struct comp_target *ct)
{
	struct comp_window_mswin *cwm = (struct comp_window_mswin *)ct;
	return os_thread_helper_is_running(&cwm->oth) && cwm->base.swapchain.handle != VK_NULL_HANDLE;
}
#endif

struct comp_target *
comp_window_mswin_create(struct comp_compositor *c)
{
	struct comp_window_mswin *w = U_TYPED_CALLOC(struct comp_window_mswin);
	if (os_thread_helper_init(&w->oth) != 0) {
		COMP_ERROR(c, "Failed to init Windows window message thread");
		free(w);
		return NULL;
	}

	// The display timing code hasn't been tested on Windows and may be broken.
	comp_target_swapchain_init_and_set_fnptrs(&w->base, COMP_TARGET_FORCE_FAKE_DISPLAY_TIMING);

	w->base.base.name = "MS Windows";
	w->base.base.destroy = comp_window_mswin_destroy;
	w->base.base.flush = comp_window_mswin_flush;
	w->base.base.init_pre_vulkan = comp_window_mswin_init;
	w->base.base.init_post_vulkan = comp_window_mswin_init_swapchain;
	w->base.base.set_title = comp_window_mswin_update_window_title;
#ifdef ALLOW_CLOSING_WINDOW
	w->base.base.check_ready = comp_window_mswin_configure_check_ready;
#endif
	w->base.base.c = c;

	return &w->base.base;
}
