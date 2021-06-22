// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Calibration gui scene.
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

#ifdef XRT_HAVE_GST
#include "gstreamer/gst_sink.h"
#include "gstreamer/gst_pipeline.h"
#endif

#ifdef XRT_BUILD_DRIVER_VF
#include "vf/vf_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_DEPTHAI
#include "depthai/depthai_interface.h"
#endif

#include "gui_common.h"
#include "gui_imgui.h"

#include "stb_image_write.h"

#include <assert.h>
#include <inttypes.h>

enum bitrate
{
	BITRATE_4096,
	BITRATE_2048,
	BITRATE_1024,
};

enum pipeline
{
	PIPELINE_SOFTWARE_FAST,
	PIPELINE_SOFTWARE_MEDIUM,
	PIPELINE_SOFTWARE_SLOW,
	PIPELINE_SOFTWARE_VERYSLOW,
	PIPELINE_VAAPI_H246,
};

struct record_window
{
	struct xrt_frame_sink sink;

	struct
	{
		// Use DepthAI camera.
		bool depthai;

		// Use leap_motion.
		bool leap_motion;

		// Use index.
		bool index;
	} use;

	struct
	{
		struct xrt_frame_context xfctx;

		struct xrt_fs *xfs;

		struct xrt_fs_mode mode;

		char name[256];
	} camera;

	struct
	{
		int scale;

		struct xrt_frame_sink *sink;
		struct gui_ogl_texture *ogl;
	} texture;

#ifdef XRT_HAVE_GST
	struct
	{
		enum bitrate bitrate;

		enum pipeline pipeline;

		struct xrt_frame_context xfctx;

		//! When not null we are recording.
		struct xrt_frame_sink *sink;

		//! Protects sink
		struct os_mutex mutex;

		//! App sink we are pushing frames into.
		struct gstreamer_sink *gs;

		//! Recording pipeline.
		struct gstreamer_pipeline *gp;

		char filename[512];
	} gst;
#endif
};

struct record_scene
{
	struct gui_scene base;

	struct record_window *window;
};


/*
 *
 * GStreamer functions.
 *
 */

#ifdef XRT_HAVE_GST
static void
create_pipeline(struct record_window *rw)
{
	const char *source_name = "source_name";
	const char *bitrate = NULL;
	const char *speed_preset = NULL;

	char pipeline_string[2048];

	switch (rw->gst.bitrate) {
	default:
	case BITRATE_4096: bitrate = "4096"; break;
	case BITRATE_2048: bitrate = "2048"; break;
	case BITRATE_1024: bitrate = "1024"; break;
	}

	switch (rw->gst.pipeline) {
	case PIPELINE_SOFTWARE_FAST: speed_preset = "fast"; break;
	case PIPELINE_SOFTWARE_MEDIUM: speed_preset = "medium"; break;
	case PIPELINE_SOFTWARE_SLOW: speed_preset = "slow"; break;
	case PIPELINE_SOFTWARE_VERYSLOW: speed_preset = "veryslow"; break;
	default: break;
	}

	if (speed_preset != NULL) {
		snprintf(pipeline_string,         //
		         sizeof(pipeline_string), //
		         "appsrc name=\"%s\" ! "
		         "queue ! "
		         "videoconvert ! "
		         "queue ! "
		         "x264enc bitrate=\"%s\" speed-preset=\"%s\" ! "
		         "h264parse ! "
		         "queue ! "
		         "mp4mux ! "
		         "filesink location=\"%s\"",
		         source_name, bitrate, speed_preset, rw->gst.filename);
	} else {
		snprintf(pipeline_string,         //
		         sizeof(pipeline_string), //
		         "appsrc name=\"%s\" ! "
		         "queue ! "
		         "videoconvert ! "
		         "video/x-raw,format=NV12 ! "
		         "queue ! "
		         "vaapih264enc rate-control=cbr bitrate=\"%s\" tune=high-compression ! "
		         "video/x-h264,profile=main ! "
		         "h264parse ! "
		         "queue ! "
		         "mp4mux ! "
		         "filesink location=\"%s\"",
		         source_name, bitrate, rw->gst.filename);
	}

	struct xrt_frame_sink *tmp = NULL;
	struct gstreamer_pipeline *gp = NULL;

	gstreamer_pipeline_create_from_string(&rw->gst.xfctx, pipeline_string, &gp);

	uint32_t width = rw->camera.mode.width;
	uint32_t height = rw->camera.mode.height;
	enum xrt_format format = rw->camera.mode.format;

	struct gstreamer_sink *gs = NULL;

	gstreamer_sink_create_with_pipeline(gp, width, height, format, source_name, &gs, &tmp);
	u_sink_queue_create(&rw->gst.xfctx, tmp, &tmp);

	os_mutex_lock(&rw->gst.mutex);
	rw->gst.gs = gs;
	rw->gst.sink = tmp;
	rw->gst.gp = gp;
	gstreamer_pipeline_play(rw->gst.gp);
	os_mutex_unlock(&rw->gst.mutex);
}

static void
destroy_pipeline(struct record_window *rw)
{
	U_LOG_D("Called");

	// Make sure we are not streaming any more frames into the pipeline.
	os_mutex_lock(&rw->gst.mutex);
	rw->gst.gs = NULL;
	rw->gst.sink = NULL;
	os_mutex_unlock(&rw->gst.mutex);

	// Stop the pipeline.
	gstreamer_pipeline_stop(rw->gst.gp);
	rw->gst.gp = NULL;

	xrt_frame_context_destroy_nodes(&rw->gst.xfctx);
}

static void
draw_gst(struct record_window *rw)
{
	static ImVec2 button_dims = {0, 0};

	if (!igCollapsingHeaderBoolPtr("Record", NULL, ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}


	os_mutex_lock(&rw->gst.mutex);
	bool recording = rw->gst.gp != NULL;
	os_mutex_unlock(&rw->gst.mutex);

	igComboStr("Pipeline", (int *)&rw->gst.pipeline, "SW Fast\0SW Medium\0SW Slow\0SW Veryslow\0VAAPI H264\0\0", 5);

	igInputText("Filename", rw->gst.filename, sizeof(rw->gst.filename), 0, NULL, NULL);

	if (!recording && igButton("Start", button_dims)) {
		create_pipeline(rw);
	}

	if (recording && igButton("Stop", button_dims)) {
		destroy_pipeline(rw);
	}
}
#endif


/*
 *
 * Record window functions.
 *
 */

static void
window_destroy(struct record_window *rw)
{
	// Stop and remove the recording pipeline first.
#ifdef XRT_HAVE_GST
	if (rw->gst.gp != NULL) {
		os_mutex_lock(&rw->gst.mutex);
		rw->gst.gs = NULL;
		rw->gst.sink = NULL;
		os_mutex_unlock(&rw->gst.mutex);

		gstreamer_pipeline_stop(rw->gst.gp);
		rw->gst.gp = NULL;
		xrt_frame_context_destroy_nodes(&rw->gst.xfctx);
	}
#endif

	// Stop the camera if we have one.
	xrt_frame_context_destroy_nodes(&rw->camera.xfctx);
	rw->camera.xfs = NULL;
	rw->texture.ogl = NULL;
	rw->texture.sink = NULL;

	free(rw);
}

static bool
window_has_source(struct record_window *rw)
{
	return rw->camera.xfs != NULL;
}

static void
window_draw_misc(struct record_window *rw)
{
	if (!igCollapsingHeaderBoolPtr("Misc", NULL, ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	static ImVec2 button_dims = {0, 0};
	bool plus = igButton("+", button_dims);
	igSameLine(0.0f, 4.0f);
	bool minus = igButton("-", button_dims);
	igSameLine(0.0f, 4.0f);

	if (rw->texture.scale == 1) {
		igText("Scale 100%%");
	} else {
		igText("Scale 1/%i", rw->texture.scale);
	}

	if (plus && rw->texture.scale > 1) {
		rw->texture.scale--;
	}
	if (minus && rw->texture.scale < 6) {
		rw->texture.scale++;
	}

	igText("Sequence %u", (uint32_t)rw->texture.ogl->seq);
}

static void
window_render(struct record_window *rw, struct gui_program *p)
{
	igBegin("Preview and Control", NULL, 0);

	gui_ogl_sink_update(rw->texture.ogl);

	struct gui_ogl_texture *tex = rw->texture.ogl;

	int w = tex->w / rw->texture.scale;
	int h = tex->h / rw->texture.scale;

	ImVec2 size = {(float)w, (float)h};
	ImVec2 uv0 = {0, 0};
	ImVec2 uv1 = {1, 1};
	ImVec4 white = {1, 1, 1, 1};
	ImTextureID id = (ImTextureID)(intptr_t)tex->id;
	igImage(id, size, uv0, uv1, white, white);

#ifdef XRT_HAVE_GST
	draw_gst(rw);
#endif

	window_draw_misc(rw);

	igEnd();
}

static void
window_frame(struct xrt_frame_sink *xfs, struct xrt_frame *xf)
{
	struct record_window *rw = container_of(xfs, struct record_window, sink);

#ifdef XRT_HAVE_GST
	os_mutex_lock(&rw->gst.mutex);
	if (rw->gst.sink != NULL) {
		xrt_sink_push_frame(rw->gst.sink, xf);
	}
	os_mutex_unlock(&rw->gst.mutex);
#endif

	xrt_sink_push_frame(rw->texture.sink, xf);
}

static struct record_window *
window_create(struct gui_program *p, const char *camera)
{
	struct record_window *rw = U_TYPED_CALLOC(struct record_window);
	rw->sink.push_frame = window_frame;
	rw->use.index = camera == NULL ? false : strcmp(camera, "index") == 0;
	rw->use.leap_motion = camera == NULL ? false : strcmp(camera, "leap_motion") == 0;
	rw->use.depthai = camera == NULL ? false : strcmp(camera, "depthai") == 0;

	if (!rw->use.index && !rw->use.leap_motion && !rw->use.depthai) {
		U_LOG_W(
		    "Can't recongnize camera name '%s', options are 'depthai', index' & 'leap_motion'."
		    "\n\tFalling back to 'index'.",
		    camera);
		rw->use.index = true;
	}

	// Setup the preview texture.
	rw->texture.scale = 1;
	struct xrt_frame_sink *tmp = NULL;
	rw->texture.ogl = gui_ogl_sink_create("View", &rw->camera.xfctx, &tmp);
	u_sink_create_to_r8g8b8_or_l8(&rw->camera.xfctx, tmp, &tmp);
	u_sink_queue_create(&rw->camera.xfctx, tmp, &rw->texture.sink);

#ifdef XRT_HAVE_GST
	int ret = os_mutex_init(&rw->gst.mutex);
	if (ret < 0) {
		free(rw);
		return NULL;
	}

	snprintf(rw->gst.filename, sizeof(rw->gst.filename), "/tmp/capture.mp4");
#endif

	return rw;
}


/*
 *
 * DepthAI functions
 *
 */

#ifdef XRT_BUILD_DRIVER_DEPTHAI
static void
create_depthai(struct record_window *rw)
{
	// Should we be using a DepthAI camera?
	if (!rw->use.depthai) {
		return;
	}

	rw->camera.xfs = depthai_fs_single_rgb(&rw->camera.xfctx);

	// Just after the camera create a quirk stream.
	struct u_sink_quirk_params qp;
	U_ZERO(&qp);
	qp.stereo_sbs = false;
	qp.ps4_cam = false;
	qp.leap_motion = false;

	struct xrt_frame_sink *tmp = &rw->sink;
	u_sink_quirk_create(&rw->camera.xfctx, tmp, &qp, &tmp);

	struct xrt_fs_mode *modes = NULL;
	uint32_t mode_count = 0;
	xrt_fs_enumerate_modes(rw->camera.xfs, &modes, &mode_count);
	assert(mode_count > 0);

	// Just use the first one.
	uint32_t mode_index = 0;

	rw->camera.mode = modes[mode_index];
	free(modes);
	modes = NULL;

	// Now that we have setup a node graph, start it.
	xrt_fs_stream_start(rw->camera.xfs, tmp, XRT_FS_CAPTURE_TYPE_CALIBRATION, mode_index);

	// If it's a large mode, scale to 50%
	if (rw->camera.mode.width > 640) {
		rw->texture.scale = 2;
	}
}
#endif /* XRT_BUILD_DRIVER_DEPTHAI */


/*
 *
 * Video frame functions
 *
 */

#ifdef XRT_BUILD_DRIVER_VF
static void
create_videotestsrc(struct record_window *rw)
{
	uint32_t width = 1920;
	uint32_t height = 960;
	rw->camera.xfs = vf_fs_videotestsource(&rw->camera.xfctx, width, height);

	// Just after the camera create a quirk stream.
	struct u_sink_quirk_params qp;
	U_ZERO(&qp);
	qp.stereo_sbs = false;
	qp.ps4_cam = false;
	qp.leap_motion = false;

	struct xrt_frame_sink *tmp = NULL;
	u_sink_quirk_create(&rw->camera.xfctx, &rw->sink, &qp, &tmp);

	// Now that we have setup a node graph, start it (mode index is hardcoded to 0).
	xrt_fs_stream_start(rw->camera.xfs, tmp, XRT_FS_CAPTURE_TYPE_CALIBRATION, 0);

	rw->camera.mode.width = width;
	rw->camera.mode.height = height;
	rw->camera.mode.format = XRT_FORMAT_R8G8B8;

	// If it's a large mode, scale to 50%
	if (rw->camera.mode.width > 640) {
		rw->texture.scale = 2;
	}
}
#endif /* XRT_BUILD_DRIVER_VF */


/*
 *
 * Prober functions.
 *
 */

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
	struct record_window *rw = (struct record_window *)ptr;

	if (rw->camera.xfs != NULL) {
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

	struct xrt_frame_sink *tmp = &rw->sink;

	if (rw->use.leap_motion) {
		// De-interleaving.
		u_sink_deinterleaver_create(&rw->camera.xfctx, tmp, &tmp);
	}

	// Just after the camera create a quirk stream.
	struct u_sink_quirk_params qp;
	U_ZERO(&qp);
	qp.stereo_sbs = false;
	qp.ps4_cam = false;
	qp.leap_motion = rw->use.leap_motion;

	u_sink_quirk_create(&rw->camera.xfctx, tmp, &qp, &tmp);

	struct xrt_fs_mode *modes = NULL;
	uint32_t mode_count = 0;
	xrt_fs_enumerate_modes(rw->camera.xfs, &modes, &mode_count);
	assert(mode_count > 0);

	// Just use the first one.
	uint32_t mode_index = 0;

	rw->camera.mode = modes[mode_index];
	free(modes);
	modes = NULL;

	// Touch up.
	if (rw->use.leap_motion) {
		rw->camera.mode.width = rw->camera.mode.width * 2;
		rw->camera.mode.format = XRT_FORMAT_L8;
	}

	// If it's a large mode, scale to 50%
	if (rw->camera.mode.width > 640) {
		rw->texture.scale = 2;
	}

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

	window_render(rs->window, p);

	igBegin("Record-a-tron!", NULL, 0);
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
		create_depthai(rs->window);
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
