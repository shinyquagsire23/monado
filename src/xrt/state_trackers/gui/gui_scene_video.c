// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A very small scene that lets the user select a video device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "xrt/xrt_prober.h"
#include "xrt/xrt_settings.h"
#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_config_os.h"

#include "util/u_misc.h"
#include "util/u_format.h"
#include "util/u_logging.h"

#include "gui_common.h"
#include "gui_imgui.h"

#ifdef XRT_BUILD_DRIVER_DEPTHAI
#include "depthai/depthai_interface.h"
#endif


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

#ifdef XRT_BUILD_DRIVER_DEPTHAI
static void
create_depthai_monocular(struct video_select *vs)
{
	vs->xfctx = U_TYPED_CALLOC(struct xrt_frame_context);

	vs->xfs = depthai_fs_monocular_rgb(vs->xfctx);
	if (vs->xfs == NULL) {
		U_LOG_E("Failed to open DepthAI camera!");
		free(vs->xfctx);
		vs->xfctx = NULL;
		return;
	}

	xrt_fs_enumerate_modes(vs->xfs, &vs->modes, &vs->num_modes);
}

static void
create_depthai_stereo(struct video_select *vs)
{
	vs->xfctx = U_TYPED_CALLOC(struct xrt_frame_context);


	struct depthai_slam_startup_settings settings = {0};
	settings.frames_per_second = 60;
	settings.half_size_ov9282 = false;
	settings.want_cameras = true;
	settings.want_imu = false;


	vs->xfs = depthai_fs_slam(vs->xfctx, &settings);
	if (vs->xfs == NULL) {
		U_LOG_E("Failed to open DepthAI camera!");
		free(vs->xfctx);
		vs->xfctx = NULL;
		return;
	}
	vs->settings->camera_type = XRT_SETTINGS_CAMERA_TYPE_SLAM;


	xrt_fs_enumerate_modes(vs->xfs, &vs->modes, &vs->num_modes);
}
#endif /* XRT_BUILD_DRIVER_DEPTHAI */

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
	uint16_t vendor_id = pdev ? pdev->vendor_id : -1;
	uint16_t product_id = pdev ? pdev->product_id : -1;
	snprintf(buf, sizeof(buf), "%04x:%04x '%s' '%s'\n", vendor_id, product_id, product, serial);
	if (!igButton(buf, button_dims)) {
		return;
	}

	snprintf(vs->settings->camera_name, sizeof(vs->settings->camera_name), "%s", product);

	vs->xfctx = U_TYPED_CALLOC(struct xrt_frame_context);

	xrt_prober_open_video_device(xp, pdev, vs->xfctx, &vs->xfs);
	if (vs->xfs == NULL) {
		U_LOG_E("Failed to open camera!");
#if defined(XRT_OS_LINUX) && !defined(XRT_HAVE_V4L2)
		U_LOG_E("Monado was built with the v4l driver disabled. Most video devices require this driver!");
#endif
		free(vs->xfctx);
		vs->xfctx = NULL;
		return;
	}

	xrt_fs_enumerate_modes(vs->xfs, &vs->modes, &vs->num_modes);
}

static bool
render_mode(struct xrt_fs_mode *mode)
{
	char tmp[512];

	snprintf(tmp, 512, "%ux%u: %s", mode->width, mode->height, u_format_str(mode->format));

	return igButton(tmp, button_dims);
}

void
mode_selected_so_continue(struct gui_scene *scene, struct gui_program *p)
{
	struct video_select *vs = (struct video_select *)scene;
	gui_scene_calibrate(p, vs->xfctx, vs->xfs, vs->settings);

	// We should not clean these up, zero them out.
	vs->settings = NULL;
	vs->xfctx = NULL;
	vs->xfs = NULL;

	// Schedule us to be deleted when it's safe.
	gui_scene_delete_me(p, scene);
}

static void
scene_render(struct gui_scene *scene, struct gui_program *p)
{
	struct video_select *vs = (struct video_select *)scene;

	igBegin("Select video device/mode", NULL, 0);

	// If we have not found any modes keep showing the devices.
	if (vs->xfs == NULL) {
		xrt_prober_list_video_devices(p->xp, on_video_device, vs);

#ifdef XRT_BUILD_DRIVER_DEPTHAI
		igSeparator();
		if (igButton("DepthAI (Monocular)", button_dims)) {
			create_depthai_monocular(vs);
		}
		if (igButton("DepthAI (Stereo)", button_dims)) {
			create_depthai_stereo(vs);
		}
#endif
	} else if (vs->num_modes == 0) {
		// No modes on it :(
		igText("No modes found on '%s'!", vs->xfs->name);
	}

	// We have selected a stream device and it has only one mode - user doesn't need to care what that is; proceed
	// immediately
	if (vs->num_modes == 1) {
		vs->settings->camera_mode = 0;
		mode_selected_so_continue(scene, p);
	} else {
		// We have selected a stream device and it has multiple modes - let user decide which to use
		for (uint32_t i = 0; i < vs->num_modes; i++) {
			if (!render_mode(&vs->modes[i])) {
				continue;
			}

			vs->settings->camera_mode = (int)i;
			mode_selected_so_continue(scene, p);
		}
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
