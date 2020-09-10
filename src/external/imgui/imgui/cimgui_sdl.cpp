#include "imgui.h"
#include "imgui_internal.h"
#include "cimgui.h"
#include "imgui_impl_sdl.h"


CIMGUI_API bool igImGui_ImplSDL2_InitForOpenGL(SDL_Window* window, void* sdl_gl_context)
{
	return ImGui_ImplSDL2_InitForOpenGL(window, sdl_gl_context);
}
#if 0
CIMGUI_API bool igImGui_ImplSDL2_InitForVulkan(SDL_Window* window)
{
	return ImGui_ImplSDL2_InitForVulkan(window);
}
CIMGUI_API bool igImGui_ImplSDL2_InitForD3D(SDL_Window* window)
{
	return ImGui_ImplSDL2_InitForD3D(window);
}
#endif
CIMGUI_API void igImGui_ImplSDL2_Shutdown()
{
	ImGui_ImplSDL2_Shutdown();
}
CIMGUI_API void igImGui_ImplSDL2_NewFrame(SDL_Window* window)
{
	ImGui_ImplSDL2_NewFrame(window);
}
CIMGUI_API bool igImGui_ImplSDL2_ProcessEvent(const SDL_Event* event)
{
	return ImGui_ImplSDL2_ProcessEvent(event);
}
