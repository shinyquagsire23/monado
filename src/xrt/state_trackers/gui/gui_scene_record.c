// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Recording scene gui.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */


#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_drivers.h"

#include "os/os_threading.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_sink.h"
#include "util/u_file.h"
#include "util/u_json.h"
#include "util/u_frame.h"
#include "util/u_format.h"

#include "xrt/xrt_frame.h"
#include "xrt/xrt_prober.h"
#include "xrt/xrt_tracking.h"
#include "xrt/xrt_frameserver.h"

#ifdef XRT_BUILD_DRIVER_VF
#include "vf/vf_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_DEPTHAI
#include "depthai/depthai_interface.h"
#endif

#include "gui_imgui.h"
#include "gui_common.h"
#include "gui_window_record.h"

#include "stb_image_write.h"

#include <assert.h>
#include <inttypes.h>


struct camera_window
{
	struct gui_record_window base;

	struct
	{
		// Use DepthAI camera, single.
		bool depthai_monocular;

		// Use DepthAI camera, stereo.
		bool depthai_stereo;

		// Use leap_motion.
		bool leap_motion;

		// Use index.
		bool index;

		// Use ELP.
		bool elp;
	} use;

	struct
	{
		struct xrt_frame_context xfctx;

		struct xrt_fs *xfs;

		struct xrt_fs_mode mode;

		char name[256];
	} camera;
};

struct record_scene
{
	struct gui_scene base;

	struct camera_window *window;
};



/*
 *
 * Camera window functions.
 *
 */

static void
window_set_camera_source(struct camera_window *cw, uint32_t width, uint32_t height, enum xrt_format format)
{
	cw->base.source.width = width;
	cw->base.source.height = height;
	cw->base.source.format = format;

	// Touch up.
	if (cw->use.leap_motion) {
		cw->base.source.width = cw->base.source.width * 2;
		cw->base.source.format = XRT_FORMAT_L8;
	}

	// If it's a large source, scale to 50%
	if (cw->base.source.width > 640) {
		cw->base.texture.scale = 2;
	}
}

static void
window_destroy(struct camera_window *cw)
{
	// Stop the camera if we have one.
	xrt_frame_context_destroy_nodes(&cw->camera.xfctx);
	cw->camera.xfs = NULL;

	// Now it's safe to close the window.
	gui_window_record_close(&cw->base);

	// And free.
	free(cw);
}

static bool
window_has_source(struct camera_window *cw)
{
	return cw->camera.xfs != NULL;
}

static struct camera_window *
window_create(struct gui_program *p, const char *camera)
{
	struct camera_window *cw = U_TYPED_CALLOC(struct camera_window);

	// First init recording window.
	if (!gui_window_record_init(&cw->base)) {
		free(cw);
		return NULL;
	}

	cw->use.index = camera == NULL ? false : strcmp(camera, "index") == 0;
	cw->use.leap_motion = camera == NULL ? false : strcmp(camera, "leap_motion") == 0;
	cw->use.depthai_monocular = camera == NULL ? false : strcmp(camera, "depthai") == 0;
	cw->use.depthai_monocular = camera == NULL ? false : strcmp(camera, "depthai_monocular") == 0;
	cw->use.depthai_monocular = camera == NULL ? false : strcmp(camera, "depthai-monocular") == 0;
	cw->use.depthai_stereo = camera == NULL ? false : strcmp(camera, "depthai_stereo") == 0;
	cw->use.depthai_stereo = camera == NULL ? false : strcmp(camera, "depthai-stereo") == 0;
	cw->use.elp = camera == NULL ? false : strcmp(camera, "elp") == 0;

	if (!cw->use.index &&             //
	    !cw->use.leap_motion &&       //
	    !cw->use.depthai_monocular && //
	    !cw->use.depthai_stereo &&    //
	    !cw->use.elp) {
		U_LOG_W(
		    "Can't recongnize camera name '%s', options are 'elp', 'depthai-[monocular|stereo]', index' & "
		    "'leap_motion'.\n\tFalling back to 'index'.",
		    camera);
		cw->use.index = true;
	}

	return cw;
}


/*
 *
 * DepthAI functions
 *
 */

#ifdef XRT_BUILD_DRIVER_DEPTHAI
static void
create_depthai_monocular(struct camera_window *cw)
{
	// Should we be using a DepthAI camera?
	if (!cw->use.depthai_monocular) {
		return;
	}

	cw->camera.xfs = depthai_fs_monocular_rgb(&cw->camera.xfctx);
	if (cw->camera.xfs == NULL) {
		U_LOG_W("Could not create depthai camera!");
		return;
	}

	// No special pipeline needed.
	struct xrt_frame_sink *tmp = &cw->base.sink;

	// Hardcoded.
	uint32_t mode_index = 0;

	// Now that we have setup a node graph, start it.
	xrt_fs_stream_start(cw->camera.xfs, tmp, XRT_FS_CAPTURE_TYPE_CALIBRATION, mode_index);
}

static void
create_depthai_stereo(struct camera_window *cw)
{
	// Should we be using a DepthAI camera?
	if (!cw->use.depthai_stereo) {
		return;
	}

	struct depthai_slam_startup_settings settings = {0};
	settings.frames_per_second = 60;
	settings.half_size_ov9282 = false;
	settings.want_cameras = true;
	settings.want_imu = false;


	cw->camera.xfs = depthai_fs_slam(&cw->camera.xfctx, &settings);
	if (cw->camera.xfs == NULL) {
		U_LOG_W("Could not create depthai camera!");
		return;
	}

	// First grab the window sink.
	struct xrt_frame_sink *tmp = &cw->base.sink;

	struct xrt_slam_sinks sinks;
	u_sink_combiner_create(&cw->camera.xfctx, tmp, &sinks.cams[0], &sinks.cams[1]);

	// Now that we have setup a node graph, start it.
	xrt_fs_slam_stream_start(cw->camera.xfs, &sinks);
}
#endif /* XRT_BUILD_DRIVER_DEPTHAI */


/*
 *
 * Video frame functions
 *
 */

#ifdef XRT_BUILD_DRIVER_VF
static void
create_videotestsrc(struct camera_window *cw)
{
	uint32_t width = 1920;
	uint32_t height = 960;
	cw->camera.xfs = vf_fs_videotestsource(&cw->camera.xfctx, width, height);

	// Just after the camera create a quirk stream.
	struct u_sink_quirk_params qp;
	U_ZERO(&qp);
	qp.stereo_sbs = false;
	qp.ps4_cam = false;
	qp.leap_motion = false;

	struct xrt_frame_sink *tmp = NULL;
	u_sink_quirk_create(&cw->camera.xfctx, &cw->base.sink, &qp, &tmp);

	window_set_camera_source( //
	    cw,                   //
	    width,                //
	    height,               //
	    XRT_FORMAT_R8G8B8);   //

	// Now that we have setup a node graph, start it (mode index is hardcoded to 0).
	xrt_fs_stream_start(cw->camera.xfs, tmp, XRT_FS_CAPTURE_TYPE_CALIBRATION, 0);
}
#endif /* XRT_BUILD_DRIVER_VF */


/*
 *
 * Prober functions.
 *
 */


static bool
is_camera_elp(const char *product, const char *manufacturer)
{
	return strcmp(product, "3D USB Camera") == 0 && strcmp(manufacturer, "3D USB Camera") == 0;
}

static bool
is_camera_index(const char *product, const char *manufacturer)
{
	return strcmp(product, "3D Camera") == 0 && strcmp(manufacturer, "Etron Technology, Inc.") == 0;
}

static bool
is_camera_leap_motion(const char *product, const char *manufacturer)
{
	return strcmp(product, "Leap Motion Controller") == 0 && strcmp(manufacturer, "Leap Motion") == 0;
}

static void
on_video_device(struct xrt_prober *xp,
                struct xrt_prober_device *pdev,
                const char *product,
                const char *manufacturer,
                const char *serial,
                void *ptr)
{
	struct camera_window *rw = (struct camera_window *)ptr;

	if (rw->camera.xfs != NULL) {
		return;
	}


	// Hardcoded for the Index.
	if (rw->use.elp && !is_camera_elp(product, manufacturer)) {
		return;
	}

	// Hardcoded for the Index.
	if (rw->use.index && !is_camera_index(product, manufacturer)) {
		return;
	}

	// Hardcoded for the Leap Motion.
	if (rw->use.leap_motion && !is_camera_leap_motion(product, manufacturer)) {
		return;
	}

	snprintf(rw->camera.name, sizeof(rw->camera.name), "%s-%s", product, serial);

	xrt_prober_open_video_device(xp, pdev, &rw->camera.xfctx, &rw->camera.xfs);

	struct xrt_frame_sink *tmp = &rw->base.sink;

	if (rw->use.leap_motion) {
		// De-interleaving.
		u_sink_deinterleaver_create(&rw->camera.xfctx, tmp, &tmp);
	}

	// Just use the first one.
	uint32_t mode_index = 0;

	// Just after the camera create a quirk stream.
	struct u_sink_quirk_params qp;
	U_ZERO(&qp);
	qp.stereo_sbs = false;
	qp.ps4_cam = false;
	qp.leap_motion = rw->use.leap_motion;

	// Tweaks for the ELP camera.
	if (rw->use.elp) {
		qp.stereo_sbs = true;
		mode_index = 2;
	}

	u_sink_quirk_create(&rw->camera.xfctx, tmp, &qp, &tmp);

	struct xrt_fs_mode *modes = NULL;
	uint32_t mode_count = 0;
	xrt_fs_enumerate_modes(rw->camera.xfs, &modes, &mode_count);
	assert(mode_count > 0);

	window_set_camera_source(      //
	    rw,                        //
	    modes[mode_index].width,   //
	    modes[mode_index].height,  //
	    modes[mode_index].format); //

	free(modes);
	modes = NULL;

	// Now that we have setup a node graph, start it.
	xrt_fs_stream_start(rw->camera.xfs, tmp, XRT_FS_CAPTURE_TYPE_CALIBRATION, mode_index);
}


/*
 *
 * Scene functions.
 *
 */

static void
scene_render(struct gui_scene *scene, struct gui_program *p)
{
	static ImVec2 button_dims = {0, 0};
	struct record_scene *rs = (struct record_scene *)scene;


	igBegin("Record-a-tron!", NULL, 0);

	gui_window_record_render(&rs->window->base, p);

	igSeparator();

	if (igButton("Exit", button_dims)) {
		gui_scene_delete_me(p, &rs->base);
	}

	igEnd();
}

static void
scene_destroy(struct gui_scene *scene, struct gui_program *p)
{
	struct record_scene *rs = (struct record_scene *)scene;

	if (rs->window != NULL) {
		window_destroy(rs->window);
		rs->window = NULL;
	}

	free(rs);
}

void
gui_scene_record(struct gui_program *p, const char *camera)
{
	struct record_scene *rs = U_TYPED_CALLOC(struct record_scene);

	rs->base.render = scene_render;
	rs->base.destroy = scene_destroy;

	rs->window = window_create(p, camera);

#ifdef XRT_BUILD_DRIVER_DEPTHAI
	if (!window_has_source(rs->window)) {
		create_depthai_monocular(rs->window);
	}

	if (!window_has_source(rs->window)) {
		create_depthai_stereo(rs->window);
	}
#endif

	if (!window_has_source(rs->window)) {
		xrt_prober_list_video_devices(p->xp, on_video_device, rs->window);
	}

#ifdef XRT_BUILD_DRIVER_VF
	if (!window_has_source(rs->window)) {
		create_videotestsrc(rs->window);
	}
#endif

	gui_scene_push_front(p, &rs->base);
}
