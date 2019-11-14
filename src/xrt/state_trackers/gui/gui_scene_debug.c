// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A debugging scene.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_sink.h"

#ifdef XRT_HAVE_OPENCV
#include "tracking/t_tracking.h"
#endif

#include "xrt/xrt_frame.h"
#include "xrt/xrt_prober.h"
#include "xrt/xrt_tracking.h"
#include "xrt/xrt_frameserver.h"

#include "gui_common.h"
#include "gui_imgui.h"


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
conv_rgb_f32_to_u8(struct xrt_colour_rgb_f32 *from,
                   struct xrt_colour_rgb_u8 *to)
{
	to->r = (uint8_t)(from->r * 255.0f);
	to->g = (uint8_t)(from->g * 255.0f);
	to->b = (uint8_t)(from->b * 255.0f);
}

static void
conv_rgb_u8_to_f32(struct xrt_colour_rgb_u8 *from,
                   struct xrt_colour_rgb_f32 *to)
{
	to->r = from->r / 255.0f;
	to->g = from->g / 255.0f;
	to->b = from->b / 255.0f;
}

struct draw_state
{
	struct gui_program *p;
	bool hidden;
};

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

		if (!igCollapsingHeader(name, 0)) {
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

static void
on_elem(const char *name, enum u_var_kind kind, void *ptr, void *priv)
{
	struct draw_state *state = (struct draw_state *)priv;
	if (state->hidden && kind != U_VAR_KIND_GUI_HEADER) {
		return;
	}

	const float drag_speed = 0.2f;
	const float power = 1.0f;
	const ImVec2 dummy = {0, 0};
	ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs |
	                            ImGuiColorEditFlags_NoLabel |
	                            ImGuiColorEditFlags_PickerHueWheel;
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
	case U_VAR_KIND_RGB_U8:;
		struct xrt_colour_rgb_f32 tmp;
		conv_rgb_u8_to_f32((struct xrt_colour_rgb_u8 *)ptr, &tmp);
		on_elem(name, U_VAR_KIND_RGB_F32, &tmp, priv);
		conv_rgb_f32_to_u8(&tmp, (struct xrt_colour_rgb_u8 *)ptr);
		break;
	case U_VAR_KIND_U8:
		igDragScalar(name, ImGuiDataType_U8, ptr, drag_speed, NULL,
		             NULL, NULL, power);
		break;
	case U_VAR_KIND_I32:
		igInputInt(name, (int *)ptr, 1, 10, i_flags);
		break;
	case U_VAR_KIND_VEC3_I32: igInputInt3(name, (int *)ptr, i_flags); break;
	case U_VAR_KIND_F32:
		igInputFloat(name, (float *)ptr, 1, 10, "%+f", i_flags);
		break;
	case U_VAR_KIND_VEC3_F32:
		igInputFloat3(name, (float *)ptr, "%+f", i_flags);
		break;
	case U_VAR_KIND_RO_TEXT: igText("%s: '%s'", name, (char *)ptr); break;
	case U_VAR_KIND_RO_I32:
		igInputInt(name, (int *)ptr, 1, 10, ro_i_flags);
		break;
	case U_VAR_KIND_RO_VEC3_I32:
		igInputInt3(name, (int *)ptr, ro_i_flags);
		break;
	case U_VAR_KIND_RO_F32:
		igInputFloat(name, (float *)ptr, 1, 10, "%+f", ro_i_flags);
		break;
	case U_VAR_KIND_RO_VEC3_F32:
		igInputFloat3(name, (float *)ptr, "%+f", ro_i_flags);
		break;
	case U_VAR_KIND_RO_QUAT_F32:
		igInputFloat4(name, (float *)ptr, "%+f", ro_i_flags);
		break;
	case U_VAR_KIND_GUI_HEADER:
		state->hidden = !igCollapsingHeader(name, 0);
		break;
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
on_elem_sink(const char *name, enum u_var_kind kind, void *ptr, void *priv)
{
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
gui_scene_debug_video(struct gui_program *p,
                      struct xrt_frame_context *xfctx,
                      struct xrt_fs *xfs,
                      size_t mode)
{
	struct debug_scene *ds = U_TYPED_CALLOC(struct debug_scene);
	uint32_t num_texs = 0;

	ds->base.render = scene_render;
	ds->base.destroy = scene_destroy;
	ds->xfctx = xfctx;

	gui_scene_push_front(p, &ds->base);

	struct xrt_frame_sink *xsink = NULL;

	p->texs[num_texs++] = gui_ogl_sink_create("Stream", xfctx, &xsink);
	u_sink_create_format_converter(xfctx, XRT_FORMAT_R8G8B8, xsink, &xsink);
	u_sink_queue_create(xfctx, xsink, &xsink);

#ifdef XRT_HAVE_OPENCV
	struct xrt_frame_sink *split = xsink;
	xsink = NULL;
	struct xrt_frame_sink *xsinks[4] = {NULL, NULL, NULL, NULL};

	struct t_hsv_filter_params params = T_HSV_DEFAULT_PARAMS();
	t_hsv_filter_create(xfctx, &params, xsinks, &xsink);
	u_sink_create_to_yuv_or_yuyv(xfctx, xsink, &xsink);
	u_sink_queue_create(xfctx, xsink, &xsink);

	u_sink_split_create(xfctx, split, xsink, &xsink);
#endif

	// Create the sink interceptors.
	u_var_visit(on_root_enter_sink, on_root_exit_sink, on_elem_sink, p);

	// Now that we have setup a node graph, start it.
	xrt_fs_stream_start(xfs, xsink, mode);
}

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
