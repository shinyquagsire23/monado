// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Calibration gui scene.
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


struct calibration_scene
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
draw_texture(struct gui_ogl_texture *tex, bool header)
{
	if (tex == NULL) {
		return;
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
	if (header && !igCollapsingHeader(tex->name, flags)) {
		return;
	}

	gui_ogl_sink_update(tex);

	int w = tex->w / (tex->half ? 2 : 1);
	int h = tex->h / (tex->half ? 2 : 1);

	ImVec2 size = {w, h};
	ImVec2 uv0 = {0, 0};
	ImVec2 uv1 = {1, 1};
	ImVec4 white = {1, 1, 1, 1};
	ImTextureID id = (ImTextureID)(intptr_t)tex->id;
	igImage(id, size, uv0, uv1, white, white);
	igText("Sequence %u", (uint32_t)tex->seq);

	char temp[512];
	snprintf(temp, 512, "Half (%s)", tex->name);
	igCheckbox(temp, &tex->half);
}

static void
scene_render(struct gui_scene *scene, struct program *p)
{
	struct calibration_scene *cs = (struct calibration_scene *)scene;

	igBegin("Calibration", NULL, 0);

	draw_texture(p->texs[0], false);
	draw_texture(p->texs[1], true);

	for (size_t i = 2; i < ARRAY_SIZE(p->texs); i++) {
		draw_texture(p->texs[i], true);
	}

	igSeparator();

	static ImVec2 button_dims = {0, 0};
	if (igButton("Exit", button_dims)) {
		gui_scene_delete_me(p, &cs->base);
	}

	igEnd();
}

static void
scene_destroy(struct gui_scene *scene, struct program *p)
{
	struct calibration_scene *cs = (struct calibration_scene *)scene;

	if (cs->xfctx != NULL) {
		xrt_frame_context_destroy_nodes(cs->xfctx);
		cs->xfctx = NULL;
	}

	free(cs);
}


/*
 *
 * 'Exported' functions.
 *
 */

void
gui_scene_calibrate(struct program *p,
                    struct xrt_frame_context *xfctx,
                    struct xrt_fs *xfs,
                    size_t mode)
{
	struct calibration_scene *cs = U_TYPED_CALLOC(struct calibration_scene);

	cs->base.render = scene_render;
	cs->base.destroy = scene_destroy;
	cs->xfctx = xfctx;

	gui_scene_push_front(p, &cs->base);

#ifdef XRT_HAVE_OPENCV
	struct xrt_frame_sink *rgb = NULL;
	struct xrt_frame_sink *raw = NULL;
	struct xrt_frame_sink *cali = NULL;

	p->texs[p->num_texs++] =
	    gui_ogl_sink_create("Calibration", xfctx, &rgb);
	u_sink_create_format_converter(xfctx, XRT_FORMAT_R8G8B8, rgb, &rgb);
	u_sink_queue_create(xfctx, rgb, &rgb);

	p->texs[p->num_texs++] = gui_ogl_sink_create("Raw", xfctx, &raw);
	u_sink_create_format_converter(xfctx, XRT_FORMAT_R8G8B8, raw, &raw);
	u_sink_queue_create(xfctx, raw, &raw);

	t_calibration_create(xfctx, rgb, &cali);
	u_sink_create_to_yuv_or_yuyv(xfctx, cali, &cali);
	u_sink_queue_create(xfctx, cali, &cali);
	u_sink_split_create(xfctx, raw, cali, &cali);

	// Now that we have setup a node graph, start it.
	xrt_fs_stream_start(xfs, cali, mode);
#else
	gui_scene_delete_me(p, &cs->base);
#endif
}
