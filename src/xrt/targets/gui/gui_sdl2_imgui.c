// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main entrypoint for the Monado GUI program.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "util/u_var.h"
#include "util/u_time.h"
#include "util/u_misc.h"
#include "util/u_sink.h"
#include "util/u_format.h"

#include "ogl/ogl_api.h"
#include "gui/gui_imgui.h"

#include "gui_sdl2.h"


/*
 *
 * Structs and defines.
 *
 */

/*!
 * Internal gui state.
 *
 * @ingroup gui
 */
struct gui_imgui
{
	bool show_demo_window;
	struct xrt_colour_rgb_f32 clear;
};


/*
 *
 * 'Exported' functions.
 *
 */

void
gui_sdl2_imgui_loop(struct sdl2_program *p)
{
	// Need to call this before any other Imgui call.
	igCreateContext(NULL);


	// Local state
	ImGuiIO *io = igGetIO();

	// Setup Platform/Renderer bindings
	igImGui_ImplSDL2_InitForOpenGL(p->win, p->ctx);
	igImGui_ImplOpenGL3_Init(NULL);

	// Setup Dear ImGui style
	igStyleColorsDark(NULL);

	// Setup the plot context.
	ImPlotContext *plot_ctx = ImPlot_CreateContext();
	ImPlot_SetCurrentContext(plot_ctx);

	// Main loop
	struct gui_imgui gui = {0};
	gui.clear.r = 0.45f;
	gui.clear.g = 0.55f;
	gui.clear.b = 0.60f;
	u_var_add_root(&gui, "GUI Control", false);
	u_var_add_rgb_f32(&gui, &gui.clear, "Clear Colour");
	u_var_add_bool(&gui, &gui.show_demo_window, "Demo Window");
	u_var_add_bool(&gui, &p->base.stopped, "Exit");

	while (!p->base.stopped) {
		SDL_Event event;

		while (SDL_PollEvent(&event)) {
			igImGui_ImplSDL2_ProcessEvent(&event);

			if (event.type == SDL_QUIT) {
				p->base.stopped = true;
			}

			if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
			    event.window.windowID == SDL_GetWindowID(p->win)) {
				p->base.stopped = true;
			}
		}

		// Start the Dear ImGui frame
		igImGui_ImplOpenGL3_NewFrame();
		igImGui_ImplSDL2_NewFrame(p->win);

		// Start new frame.
		igNewFrame();

		// Render the scene into it.
		gui_scene_manager_render(&p->base);

		// Handle this here.
		if (gui.show_demo_window) {
			igShowDemoWindow(&gui.show_demo_window);
		}

		// Build the DrawData (EndFrame).
		igRender();

		// Clear the background.
		glViewport(0, 0, (int)io->DisplaySize.x, (int)io->DisplaySize.y);
		glClearColor(gui.clear.r, gui.clear.g, gui.clear.b, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		igImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());

		SDL_GL_SwapWindow(p->win);

		gui_prober_update(&p->base);
	}

	// Cleanup
	u_var_remove_root(&gui);
	ImPlot_DestroyContext(plot_ctx);
	igImGui_ImplOpenGL3_Shutdown();
	igImGui_ImplSDL2_Shutdown();
	igDestroyContext(NULL);
}
