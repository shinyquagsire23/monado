// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A very small scene that lets the user select a video device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "util/u_misc.h"
#include "util/u_format.h"

#include "xrt/xrt_prober.h"
#include "xrt/xrt_settings.h"
#include "xrt/xrt_frameserver.h"

#include "gui_common.h"
#include "gui_imgui.h"


/*!
 * A GUI scene that lets the user select a user device.
 * @implements gui_scene
 */
struct video_select
{
	struct gui_scene base;

	struct xrt_frame_context *xfctx;
	struct xrt_fs *xfs;

	struct xrt_settings_tracking *settings;

	struct xrt_fs_mode *modes;
	uint32_t num_modes;
};

static ImVec2 button_dims = {256 + 64, 0};


/*
 *
 * Internal functions.
 *
 */

static void
on_video_device(struct xrt_prober *xp,
                struct xrt_prober_device *pdev,
                const char *product,
                const char *manufacturer,
                const char *serial,
                void *ptr)
{
	struct video_select *vs = (struct video_select *)ptr;

	if (product == NULL) {
		return;
	}

	char buf[256] = {0};
	snprintf(buf, sizeof(buf), "%04x:%04x '%s' '%s'\n", pdev->vendor_id, pdev->product_id, product, serial);
	if (!igButton(buf, button_dims)) {
		return;
	}

	snprintf(vs->settings->camera_name, sizeof(vs->settings->camera_name), "%s", product);

	vs->xfctx = U_TYPED_CALLOC(struct xrt_frame_context);
	xrt_prober_open_video_device(xp, pdev, vs->xfctx, &vs->xfs);
	xrt_fs_enumerate_modes(vs->xfs, &vs->modes, &vs->num_modes);
}

static bool
render_mode(struct xrt_fs_mode *mode)
{
	char tmp[512];

	snprintf(tmp, 512, "%ux%u: %s", mode->width, mode->height, u_format_str(mode->format));

	return igButton(tmp, button_dims);
}

static void
scene_render(struct gui_scene *scene, struct gui_program *p)
{
	struct video_select *vs = (struct video_select *)scene;

	igBegin("Select video device/mode", NULL, 0);

	// If we have not found any modes keep showing the devices.
	if (vs->xfs == NULL) {
		xrt_prober_list_video_devices(p->xp, on_video_device, vs);
	} else if (vs->num_modes == 0) {
		// No modes on it :(
		igText("No modes found on '%s'!", vs->xfs->name);
	}

	// We have selected a stream device and it has modes.
	for (size_t i = 0; i < vs->num_modes; i++) {
		if (!render_mode(&vs->modes[i])) {
			continue;
		}

		vs->settings->camera_mode = i;

		// User selected this mode, create the next scene.
		gui_scene_calibrate(p, vs->xfctx, vs->xfs, vs->settings);

		// We should not clean these up, zero them out.
		vs->settings = NULL;
		vs->xfctx = NULL;
		vs->xfs = NULL;

		// Schedule us to be deleted when it's safe.
		gui_scene_delete_me(p, scene);
	}

	igSeparator();

	if (igButton("Exit", button_dims)) {
		gui_scene_delete_me(p, scene);
	}

	igEnd();
}

static void
scene_destroy(struct gui_scene *scene, struct gui_program *p)
{
	struct video_select *vs = (struct video_select *)scene;

	if (vs->xfctx) {
		xrt_frame_context_destroy_nodes(vs->xfctx);
		free(vs->xfctx);
		vs->xfctx = NULL;
	}

	if (vs->modes != NULL) {
		free(vs->modes);
		vs->modes = NULL;
	}

	if (vs->settings != NULL) {
		free(vs->settings);
		vs->settings = NULL;
	}

	free(scene);
}

static struct video_select *
create(void)
{
	struct video_select *vs = U_TYPED_CALLOC(struct video_select);

	vs->base.render = scene_render;
	vs->base.destroy = scene_destroy;
	vs->settings = U_TYPED_CALLOC(struct xrt_settings_tracking);

	return vs;
}


/*
 *
 * 'Exported' functions.
 *
 */

void
gui_scene_select_video_calibrate(struct gui_program *p)
{
	if (p->xp == NULL) {
		// No prober, nothing to create.
		return;
	}
	struct video_select *vs = create();

	gui_scene_push_front(p, &vs->base);
}
