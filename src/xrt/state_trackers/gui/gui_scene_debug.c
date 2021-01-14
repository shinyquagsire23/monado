// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A debugging scene.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "xrt/xrt_config_have.h"

#include "os/os_time.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_sink.h"

#ifdef XRT_HAVE_OPENCV
#include "tracking/t_tracking.h"
#endif

#include "xrt/xrt_frame.h"
#include "xrt/xrt_prober.h"
#include "xrt/xrt_tracking.h"
#include "xrt/xrt_settings.h"
#include "xrt/xrt_frameserver.h"

#include "math/m_api.h"
#include "math/m_filter_fifo.h"

#include "gui_common.h"
#include "gui_imgui.h"

#include "imgui_monado/cimgui_monado.h"

#include <float.h>

/*!
 * A GUI scene showing the variable tracking provided by @ref util/u_var.h
 * @implements gui_scene
 */
struct debug_scene
{
	struct gui_scene base;
	struct xrt_frame_context *xfctx;
};


/*
 *
 * Internal functions.
 *
 */

static void
conv_rgb_f32_to_u8(struct xrt_colour_rgb_f32 *from, struct xrt_colour_rgb_u8 *to)
{
	to->r = (uint8_t)(from->r * 255.0f);
	to->g = (uint8_t)(from->g * 255.0f);
	to->b = (uint8_t)(from->b * 255.0f);
}

static void
conv_rgb_u8_to_f32(struct xrt_colour_rgb_u8 *from, struct xrt_colour_rgb_f32 *to)
{
	to->r = from->r / 255.0f;
	to->g = from->g / 255.0f;
	to->b = from->b / 255.0f;
}

static void
handle_draggable_vec3_f32(const char *name, struct xrt_vec3 *v)
{
	float min = -256.0f;
	float max = 256.0f;

	igDragFloat3(name, (float *)v, 0.005f, min, max, "%+f", 1.0f);
}

static void
handle_draggable_quat(const char *name, struct xrt_quat *q)
{
	float min = -1.0f;
	float max = 1.0f;

	igDragFloat4(name, (float *)q, 0.005f, min, max, "%+f", 1.0f);

	// Avoid invalid
	if (q->x == 0.0f && q->y == 0.0f && q->z == 0.0f && q->w == 0.0f) {
		q->w = 1.0f;
	}

	// And make sure it's a unit rotation.
	math_quat_normalize(q);
}

struct draw_state
{
	struct gui_program *p;
	bool hidden;
};

struct plot_state
{
	struct m_ff_vec3_f32 *ff;
	uint64_t now;
};

#define PLOT_HELPER(elm)                                                                                               \
	ImPlotPoint plot_##elm(void *ptr, int index)                                                                   \
	{                                                                                                              \
		struct plot_state *state = (struct plot_state *)ptr;                                                   \
		struct xrt_vec3 value;                                                                                 \
		uint64_t timestamp;                                                                                    \
		m_ff_vec3_f32_get(state->ff, index, &value, &timestamp);                                               \
		ImPlotPoint point = {time_ns_to_s(state->now - timestamp), value.elm};                                 \
		return point;                                                                                          \
	}

PLOT_HELPER(x)
PLOT_HELPER(y)
PLOT_HELPER(z)

static void
on_ff_vec3_var(struct u_var_info *info, struct gui_program *p)
{
	char tmp[512];
	const char *name = info->name;
	struct m_ff_vec3_f32 *ff = (struct m_ff_vec3_f32 *)info->ptr;


	struct xrt_vec3 value = {0};
	uint64_t timestamp;

	m_ff_vec3_f32_get(ff, 0, &value, &timestamp);

	snprintf(tmp, sizeof(tmp), "%s.toggle", name);
	igToggleButton(tmp, &info->gui.graphed);
	igSameLine(0, 0);
	igInputFloat3(name, &value.x, "%+f", ImGuiInputTextFlags_ReadOnly);

	if (!info->gui.graphed) {
		return;
	}


	/*
	 * Showing the plot
	 */

	struct plot_state state = {ff, os_monotonic_get_ns()};
	ImPlotFlags flags = 0;
	ImPlotAxisFlags x_flags = 0;
	ImPlotAxisFlags y_flags = 0;
	ImPlotAxisFlags y2_flags = 0;
	ImPlotAxisFlags y3_flags = 0;

	ImVec2 size = {1024, 256};
	bool shown = ImPlot_BeginPlot(name, "time", "value", size, flags, x_flags, y_flags, y2_flags, y3_flags);
	if (!shown) {
		return;
	}

	size_t num = m_ff_vec3_f32_get_num(ff);
	ImPlot_PlotLineG("x", plot_x, &state, num, 0);
	ImPlot_PlotLineG("y", plot_y, &state, num, 0);
	ImPlot_PlotLineG("z", plot_z, &state, num, 0);

	ImPlot_EndPlot();
}

static void
on_sink_var(const char *name, void *ptr, struct gui_program *p)
{
	for (size_t i = 0; i < ARRAY_SIZE(p->texs); i++) {
		struct gui_ogl_texture *tex = p->texs[i];

		if (tex == NULL) {
			continue;
		}

		if ((ptrdiff_t)tex->ptr != (ptrdiff_t)ptr) {
			continue;
		}

		if (!igCollapsingHeaderBoolPtr(name, NULL, 0)) {
			continue;
		}

		gui_ogl_sink_update(tex);

		igText("Sequence %u", (uint32_t)tex->seq);
		char temp[512];
		snprintf(temp, 512, "Half (%s)", tex->name);
		igCheckbox(temp, &tex->half);
		int w = tex->w / (tex->half ? 2 : 1);
		int h = tex->h / (tex->half ? 2 : 1);

		ImVec2 size = {(float)w, (float)h};
		ImVec2 uv0 = {0, 0};
		ImVec2 uv1 = {1, 1};
		ImVec4 white = {1, 1, 1, 1};
		ImTextureID id = (ImTextureID)(intptr_t)tex->id;
		igImage(id, size, uv0, uv1, white, white);
	}
}

static void
on_root_enter(const char *name, void *priv)
{
	struct draw_state *state = (struct draw_state *)priv;
	state->hidden = false;

	igBegin(name, NULL, 0);
}

static float
get_float_arr_val(void *_data, int _idx)
{
	float *arr = _data;
	return arr[_idx];
}

static void
on_elem(struct u_var_info *info, void *priv)
{
	const char *name = info->name;
	void *ptr = info->ptr;
	enum u_var_kind kind = info->kind;

	struct draw_state *state = (struct draw_state *)priv;
	if (state->hidden && kind != U_VAR_KIND_GUI_HEADER) {
		return;
	}

	const float drag_speed = 0.2f;
	const float power = 1.0f;
	const ImVec2 dummy = {0, 0};
	ImGuiColorEditFlags flags =
	    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_PickerHueWheel;
	(void)dummy;
	ImGuiInputTextFlags i_flags = ImGuiInputTextFlags_None;
	ImGuiInputTextFlags ro_i_flags = ImGuiInputTextFlags_ReadOnly;

	switch (kind) {
	case U_VAR_KIND_BOOL: igCheckbox(name, (bool *)ptr); break;
	case U_VAR_KIND_RGB_F32:
		igColorEdit3(name, (float *)ptr, flags);
		igSameLine(0.0f, 4.0f);
		igText("%s", name);
		break;
	case U_VAR_KIND_RGB_U8: {
		struct xrt_colour_rgb_f32 tmp;
		conv_rgb_u8_to_f32((struct xrt_colour_rgb_u8 *)ptr, &tmp);
		igColorEdit3(name, (float *)&tmp, flags);
		igSameLine(0.0f, 4.0f);
		igText("%s", name);
		conv_rgb_f32_to_u8(&tmp, (struct xrt_colour_rgb_u8 *)ptr);
		break;
	}
	case U_VAR_KIND_U8: igDragScalar(name, ImGuiDataType_U8, ptr, drag_speed, NULL, NULL, NULL, power); break;
	case U_VAR_KIND_I32: igInputInt(name, (int *)ptr, 1, 10, i_flags); break;
	case U_VAR_KIND_VEC3_I32: igInputInt3(name, (int *)ptr, i_flags); break;
	case U_VAR_KIND_F32: igInputFloat(name, (float *)ptr, 1, 10, "%+f", i_flags); break;
	case U_VAR_KIND_F32_ARR: {
		struct u_var_f32_arr *f32_arr = ptr;
		int index = *f32_arr->index_ptr;
		int length = f32_arr->length;
		float *arr = (float *)f32_arr->data;

		float w = igGetWindowContentRegionWidth();
		ImVec2 graph_size = {w, 200};

		float stats_min = FLT_MAX;
		float stats_max = FLT_MAX;

		igPlotLinesFnFloatPtr(name, get_float_arr_val, arr, length, index, NULL, stats_min, stats_max,
		                      graph_size);
		break;
	}
	case U_VAR_KIND_TIMING: {
		struct u_var_timing *frametime_arr = ptr;
		struct u_var_f32_arr *f32_arr = &frametime_arr->values;
		int index = *f32_arr->index_ptr;
		int length = f32_arr->length;
		float *arr = (float *)f32_arr->data;

		float w = igGetWindowContentRegionWidth();
		ImVec2 graph_size = {w, 200};


		float stats_min = FLT_MAX;
		float stats_max = 0;

		for (int f = 0; f < length; f++) {
			if (arr[f] < stats_min)
				stats_min = arr[f];
			if (arr[f] > stats_max)
				stats_max = arr[f];
		}

		igPlotTimings(name, get_float_arr_val, arr, length, index, NULL, 0, stats_max, graph_size,
		              frametime_arr->reference_timing, frametime_arr->center_reference_timing,
		              frametime_arr->range, frametime_arr->unit, frametime_arr->dynamic_rescale);
		break;
	}
	case U_VAR_KIND_VEC3_F32: igInputFloat3(name, (float *)ptr, "%+f", i_flags); break;
	case U_VAR_KIND_POSE: {
		struct xrt_pose *pose = (struct xrt_pose *)ptr;
		char text[512];
		snprintf(text, 512, "%s.position", name);
		handle_draggable_vec3_f32(text, &pose->position);
		snprintf(text, 512, "%s.orientation", name);
		handle_draggable_quat(text, &pose->orientation);
		break;
	}
	case U_VAR_KIND_LOG_LEVEL: igComboStr(name, (int *)ptr, "Trace\0Debug\0Info\0Warn\0Error\0\0", 5); break;
	case U_VAR_KIND_RO_TEXT: igText("%s: '%s'", name, (char *)ptr); break;
	case U_VAR_KIND_RO_I32: igInputScalar(name, ImGuiDataType_S32, ptr, NULL, NULL, NULL, ro_i_flags); break;
	case U_VAR_KIND_RO_U32: igInputScalar(name, ImGuiDataType_U32, ptr, NULL, NULL, NULL, ro_i_flags); break;
	case U_VAR_KIND_RO_F32: igInputScalar(name, ImGuiDataType_Float, ptr, NULL, NULL, "%+f", ro_i_flags); break;
	case U_VAR_KIND_RO_I64: igInputScalar(name, ImGuiDataType_S64, ptr, NULL, NULL, NULL, ro_i_flags); break;
	case U_VAR_KIND_RO_U64: igInputScalar(name, ImGuiDataType_S64, ptr, NULL, NULL, NULL, ro_i_flags); break;
	case U_VAR_KIND_RO_F64: igInputScalar(name, ImGuiDataType_Double, ptr, NULL, NULL, "%+f", ro_i_flags); break;
	case U_VAR_KIND_RO_VEC3_I32: igInputInt3(name, (int *)ptr, ro_i_flags); break;
	case U_VAR_KIND_RO_VEC3_F32: igInputFloat3(name, (float *)ptr, "%+f", ro_i_flags); break;
	case U_VAR_KIND_RO_QUAT_F32: igInputFloat4(name, (float *)ptr, "%+f", ro_i_flags); break;
	case U_VAR_KIND_RO_FF_VEC3_F32: on_ff_vec3_var(info, state->p); break;
	case U_VAR_KIND_GUI_HEADER: {
		state->hidden = !igCollapsingHeaderBoolPtr(name, NULL, 0);
		break;
	}
	case U_VAR_KIND_SINK: on_sink_var(name, ptr, state->p); break;
	default: igLabelText(name, "Unknown tag '%i'", kind); break;
	}
}

static void
on_root_exit(const char *name, void *priv)
{
	struct draw_state *state = (struct draw_state *)priv;
	state->hidden = false;

	igEnd();
}

static void
scene_render(struct gui_scene *scene, struct gui_program *p)
{
	struct debug_scene *ds = (struct debug_scene *)scene;
	(void)ds;
	struct draw_state state = {p, false};

	u_var_visit(on_root_enter, on_root_exit, on_elem, &state);
}

static void
scene_destroy(struct gui_scene *scene, struct gui_program *p)
{
	struct debug_scene *ds = (struct debug_scene *)scene;

	if (ds->xfctx != NULL) {
		xrt_frame_context_destroy_nodes(ds->xfctx);
		ds->xfctx = NULL;
	}

	free(ds);
}


/*
 *
 * Sink interception.
 *
 */

static void
on_root_enter_sink(const char *name, void *priv)
{}

static void
on_elem_sink(struct u_var_info *info, void *priv)
{
	const char *name = info->name;
	void *ptr = info->ptr;
	enum u_var_kind kind = info->kind;
	struct gui_program *p = (struct gui_program *)priv;

	if (kind != U_VAR_KIND_SINK) {
		return;
	}

	if (p->xp == NULL || p->xp->tracking == NULL) {
		return;
	}

	struct xrt_frame_context *xfctx = p->xp->tracking->xfctx;
	struct xrt_frame_sink **xsink_ptr = (struct xrt_frame_sink **)ptr;
	struct xrt_frame_sink *split = NULL;

	p->texs[p->num_texs] = gui_ogl_sink_create(name, xfctx, &split);
	p->texs[p->num_texs++]->ptr = ptr;

	u_sink_create_to_r8g8b8_or_l8(xfctx, split, &split);

	if (*xsink_ptr != NULL) {
		u_sink_split_create(xfctx, split, *xsink_ptr, xsink_ptr);
	} else {
		*xsink_ptr = split;
	}
}

static void
on_root_exit_sink(const char *name, void *priv)
{}


/*
 *
 * 'Exported' functions.
 *
 */

void
gui_scene_debug(struct gui_program *p)
{
	struct debug_scene *ds = U_TYPED_CALLOC(struct debug_scene);

	ds->base.render = scene_render;
	ds->base.destroy = scene_destroy;

	gui_scene_push_front(p, &ds->base);

	// Create the sink interceptors.
	u_var_visit(on_root_enter_sink, on_root_exit_sink, on_elem_sink, p);
}
