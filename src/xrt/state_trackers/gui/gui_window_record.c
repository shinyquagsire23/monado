// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Recording window gui.
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

#include "gui_window_record.h"

#ifdef XRT_HAVE_GST
#include "gstreamer/gst_sink.h"
#include "gstreamer/gst_pipeline.h"
#endif

#include "gui_imgui.h"
#include "gui_common.h"
#include "gui_window_record.h"

#include "stb_image_write.h"

#include <assert.h>
#include <inttypes.h>


/*
 *
 * GStreamer functions.
 *
 */

#ifdef XRT_HAVE_GST
static void
create_pipeline(struct gui_record_window *rw)
{
	const char *source_name = "source_name";
	const char *bitrate = NULL;
	const char *speed_preset = NULL;

	char pipeline_string[2048];

	switch (rw->gst.bitrate) {
	default:
	case GUI_RECORD_BITRATE_4096: bitrate = "4096"; break;
	case GUI_RECORD_BITRATE_2048: bitrate = "2048"; break;
	case GUI_RECORD_BITRATE_1024: bitrate = "1024"; break;
	}

	switch (rw->gst.pipeline) {
	case GUI_RECORD_PIPELINE_SOFTWARE_FAST: speed_preset = "fast"; break;
	case GUI_RECORD_PIPELINE_SOFTWARE_MEDIUM: speed_preset = "medium"; break;
	case GUI_RECORD_PIPELINE_SOFTWARE_SLOW: speed_preset = "slow"; break;
	case GUI_RECORD_PIPELINE_SOFTWARE_VERYSLOW: speed_preset = "veryslow"; break;
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

	uint32_t width = rw->source.width;
	uint32_t height = rw->source.height;
	enum xrt_format format = rw->source.format;

	bool do_convert = false;
	if (format == XRT_FORMAT_MJPEG) {
		format = XRT_FORMAT_R8G8B8;
		do_convert = true;
	}

	struct gstreamer_sink *gs = NULL;
	gstreamer_sink_create_with_pipeline(gp, width, height, format, source_name, &gs, &tmp);
	if (do_convert) {
		u_sink_create_to_r8g8b8_or_l8(&rw->gst.xfctx, tmp, &tmp);
	}
	u_sink_queue_create(&rw->gst.xfctx, tmp, &tmp);

	os_mutex_lock(&rw->gst.mutex);
	rw->gst.gs = gs;
	rw->gst.sink = tmp;
	rw->gst.gp = gp;
	gstreamer_pipeline_play(rw->gst.gp);
	os_mutex_unlock(&rw->gst.mutex);
}

static void
destroy_pipeline(struct gui_record_window *rw)
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
draw_gst(struct gui_record_window *rw)
{
	static ImVec2 button_dims = {0, 0};

	if (!igCollapsingHeaderBoolPtr("Record", NULL, ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}


	os_mutex_lock(&rw->gst.mutex);
	bool recording = rw->gst.gp != NULL;
	os_mutex_unlock(&rw->gst.mutex);

	igComboStr("Pipeline", (int *)&rw->gst.pipeline, "SW Fast\0SW Medium\0SW Slow\0SW Veryslow\0VAAPI H264\0\0", 5);
	igComboStr("Bitrate", (int *)&rw->gst.bitrate, "4096bps\0002048bps\0001024bps\0\0", 3);

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
 * Misc helpers and interface functions.
 *
 */

static void
window_draw_misc(struct gui_record_window *rw)
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
window_frame(struct xrt_frame_sink *xfs, struct xrt_frame *xf)
{
	struct gui_record_window *rw = container_of(xfs, struct gui_record_window, sink);

	if (rw->source.width != xf->width || rw->source.height != xf->height || rw->source.format != xf->format) {
		if (rw->source.width != 0 || rw->source.height != 0) {
			U_LOG_E("Changing properties! Old: %ux%u:%s(%u), new %ux%u:%s(%u)", rw->source.width,
			        rw->source.height, u_format_str(rw->source.format), rw->source.format, xf->width,
			        xf->height, u_format_str(xf->format), xf->format);
		}
		assert(rw->source.width == 0 && rw->source.height == 0);

		rw->source.width = xf->width;
		rw->source.height = xf->height;
		rw->source.format = xf->format;
	}

#ifdef XRT_HAVE_GST
	os_mutex_lock(&rw->gst.mutex);
	if (rw->gst.sink != NULL) {
		xrt_sink_push_frame(rw->gst.sink, xf);
	}
	os_mutex_unlock(&rw->gst.mutex);
#endif

	xrt_sink_push_frame(rw->texture.sink, xf);
}


/*
 *
 * 'Exported' functions.
 *
 */

bool
gui_window_record_init(struct gui_record_window *rw)
{
	// Basic init.
	rw->sink.push_frame = window_frame;

	// Mutex first.
#ifdef XRT_HAVE_GST
	int ret = os_mutex_init(&rw->gst.mutex);
	if (ret < 0) {
		return false;
	}

	snprintf(rw->gst.filename, sizeof(rw->gst.filename), "/tmp/capture.mp4");
#endif

	// Setup the preview texture.
	rw->texture.scale = 1;
	struct xrt_frame_sink *tmp = NULL;
	rw->texture.ogl = gui_ogl_sink_create("View", &rw->texture.xfctx, &tmp);
	u_sink_create_to_r8g8b8_or_l8(&rw->texture.xfctx, tmp, &tmp);
	u_sink_queue_create(&rw->texture.xfctx, tmp, &rw->texture.sink);

	return true;
}

void
gui_window_record_render(struct gui_record_window *rw, struct gui_program *p)
{
	// Make all IDs unique.
	igPushIDPtr(rw);

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

	// Pop the ID making everything unique.
	igPopID();
}

void
gui_window_record_close(struct gui_record_window *rw)
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

	xrt_frame_context_destroy_nodes(&rw->texture.xfctx);

	/*
	 * This is safe to do, because we require that our sink 'window_frame'
	 * function is not called when close is called.
	 */
	rw->texture.sink = NULL;
	rw->texture.ogl = NULL;

#ifdef XRT_HAVE_GST
	os_mutex_destroy(&rw->gst.mutex);
#endif
}
