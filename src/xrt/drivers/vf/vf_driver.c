// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Video file frameserver implementation
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_vf
 */

#include "os/os_time.h"
#include "os/os_threading.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_format.h"
#include "util/u_frame.h"
#include "util/u_logging.h"


#include <stdio.h>
#include <assert.h>

#include "vf_interface.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <glib.h>

/*
 *
 * Defines.
 *
 */

/*
 *
 * Printing functions.
 *
 */

#define VF_TRACE(d, ...) U_LOG_IFL_T(d->ll, __VA_ARGS__)
#define VF_DEBUG(d, ...) U_LOG_IFL_D(d->ll, __VA_ARGS__)
#define VF_INFO(d, ...) U_LOG_IFL_I(d->ll, __VA_ARGS__)
#define VF_WARN(d, ...) U_LOG_IFL_W(d->ll, __VA_ARGS__)
#define VF_ERROR(d, ...) U_LOG_IFL_E(d->ll, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(vf_log, "VF_LOG", U_LOGGING_WARN)

/*!
 * A frame server operating on a video file.
 *
 * @implements xrt_frame_node
 * @implements xrt_fs
 */
struct vf_fs
{
	struct xrt_fs base;

	struct os_thread_helper play_thread;

	const char *path;
	GMainLoop *loop;
	GstElement *source;
	GstElement *testsink;
	bool got_sample;
	int width;
	int height;
	enum xrt_format format;
	enum xrt_stereo_format stereo_format;

	struct xrt_frame_node node;

	struct
	{
		bool extended_format;
		bool timeperframe;
	} has;

	enum xrt_fs_capture_type capture_type;
	struct xrt_frame_sink *sink;

	uint32_t selected;

	struct xrt_fs_capture_parameters capture_params;

	bool is_configured;
	bool is_running;
	enum u_logging_level ll;
};

/*!
 * Cast to derived type.
 */
static inline struct vf_fs *
vf_fs(struct xrt_fs *xfs)
{
	return (struct vf_fs *)xfs;
}

/*
 *
 * Misc helper functions
 *
 */


/*
 *
 * Exported functions.
 *
 */

static bool
vf_fs_enumerate_modes(struct xrt_fs *xfs, struct xrt_fs_mode **out_modes, uint32_t *out_count)
{
	struct vf_fs *vid = vf_fs(xfs);

	struct xrt_fs_mode *modes = U_TYPED_ARRAY_CALLOC(struct xrt_fs_mode, 1);
	if (modes == NULL) {
		return false;
	}

	modes[0].width = vid->width;
	modes[0].height = vid->height;
	modes[0].format = vid->format;
	modes[0].stereo_format = vid->stereo_format;

	*out_modes = modes;
	*out_count = 1;

	return true;
}

static bool
vf_fs_configure_capture(struct xrt_fs *xfs, struct xrt_fs_capture_parameters *cp)
{
	// struct vf_fs *vid = vf_fs(xfs);
	//! @todo
	return false;
}

static bool
vf_fs_stream_start(struct xrt_fs *xfs,
                   struct xrt_frame_sink *xs,
                   enum xrt_fs_capture_type capture_type,
                   uint32_t descriptor_index)
{
	struct vf_fs *vid = vf_fs(xfs);

	vid->sink = xs;
	vid->is_running = true;
	vid->capture_type = capture_type;
	vid->selected = descriptor_index;

	gst_element_set_state(vid->source, GST_STATE_PLAYING);

	VF_TRACE(vid, "info: Started!");

	// we're off to the races!
	return true;
}

static bool
vf_fs_stream_stop(struct xrt_fs *xfs)
{
	struct vf_fs *vid = vf_fs(xfs);

	if (!vid->is_running) {
		return true;
	}

	vid->is_running = false;
	gst_element_set_state(vid->source, GST_STATE_PAUSED);

	return true;
}

static bool
vf_fs_is_running(struct xrt_fs *xfs)
{
	struct vf_fs *vid = vf_fs(xfs);

	GstState current = GST_STATE_NULL;
	GstState pending;
	gst_element_get_state(vid->source, &current, &pending, 0);

	return current == GST_STATE_PLAYING;
}

static void
vf_fs_destroy(struct vf_fs *vid)
{
	g_main_loop_quit(vid->loop);

	os_thread_helper_stop(&vid->play_thread);
	os_thread_helper_destroy(&vid->play_thread);

	free(vid);
}

static void
vf_fs_node_break_apart(struct xrt_frame_node *node)
{
	struct vf_fs *vid = container_of(node, struct vf_fs, node);
	vf_fs_stream_stop(&vid->base);
}

static void
vf_fs_node_destroy(struct xrt_frame_node *node)
{
	struct vf_fs *vid = container_of(node, struct vf_fs, node);
	vf_fs_destroy(vid);
}

#include <gst/video/video-frame.h>

void
vf_fs_frame(struct vf_fs *vid, GstSample *sample)
{
	GstBuffer *buffer;
	buffer = gst_sample_get_buffer(sample);
	GstCaps *caps = gst_sample_get_caps(sample);

	static int seq = 0;

	GstVideoFrame frame;
	GstVideoInfo info;
	gst_video_info_init(&info);
	gst_video_info_from_caps(&info, caps);
	if (gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ)) {

		int plane = 0;

		struct xrt_frame *xf = NULL;

		u_frame_create_one_off(vid->format, vid->width, vid->height, &xf);

		//! @todo Sequence number and timestamp.
		xf->width = vid->width;
		xf->height = vid->height;
		xf->format = vid->format;
		xf->stereo_format = vid->stereo_format;

		xf->data = frame.data[plane];
		xf->stride = info.stride[plane];
		xf->size = info.size;
		xf->source_id = vid->base.source_id;
		xf->source_sequence = seq;
		xf->timestamp = os_monotonic_get_ns();
		if (vid->sink) {
			vid->sink->push_frame(vid->sink, xf);
			// The frame is requeued as soon as the refcount reaches
			// zero, this can be done safely from another thread.
			// xrt_frame_reference(&xf, NULL);
		}
		gst_video_frame_unmap(&frame);
	} else {
		VF_ERROR(vid, "Failed to map frame %d", seq);
	}

	seq++;
}

static GstFlowReturn
on_new_sample_from_sink(GstElement *elt, struct vf_fs *vid)
{
	GstSample *sample;
	sample = gst_app_sink_pull_sample(GST_APP_SINK(elt));

	if (!vid->got_sample) {
		gint width;
		gint height;

		GstCaps *caps = gst_sample_get_caps(sample);
		GstStructure *structure = gst_caps_get_structure(caps, 0);

		gst_structure_get_int(structure, "width", &width);
		gst_structure_get_int(structure, "height", &height);

		VF_DEBUG(vid, "video size is %dx%d\n", width, height);
		vid->got_sample = true;
		vid->width = width;
		vid->height = height;

		// first sample is only used for getting metadata
		return GST_FLOW_OK;
	}

	vf_fs_frame(vid, sample);

	gst_sample_unref(sample);

	return GST_FLOW_OK;
}

static void
print_gst_error(GstMessage *message)
{
	GError *err = NULL;
	gchar *dbg_info = NULL;

	gst_message_parse_error(message, &err, &dbg_info);
	U_LOG_E("ERROR from element %s: %s\n", GST_OBJECT_NAME(message->src), err->message);
	U_LOG_E("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
	g_error_free(err);
	g_free(dbg_info);
}

static gboolean
on_source_message(GstBus *bus, GstMessage *message, struct vf_fs *vid)
{
	/* nil */
	switch (GST_MESSAGE_TYPE(message)) {
	case GST_MESSAGE_EOS:
		VF_DEBUG(vid, "Finished playback\n");
		g_main_loop_quit(vid->loop);
		break;
	case GST_MESSAGE_ERROR:
		VF_ERROR(vid, "Received error\n");
		print_gst_error(message);
		g_main_loop_quit(vid->loop);
		break;
	default: break;
	}
	return TRUE;
}

static void *
run_play_thread(void *ptr)
{
	struct vf_fs *vid = (struct vf_fs *)ptr;

	VF_DEBUG(vid, "Let's run!\n");
	g_main_loop_run(vid->loop);
	VF_DEBUG(vid, "Going out\n");

	gst_object_unref(vid->testsink);
	gst_element_set_state(vid->source, GST_STATE_NULL);


	gst_object_unref(vid->source);
	g_main_loop_unref(vid->loop);

	return NULL;
}

struct xrt_fs *
vf_fs_create(struct xrt_frame_context *xfctx, const char *path)
{
	if (path == NULL) {
		U_LOG_E("No path given");
		return NULL;
	}


	struct vf_fs *vid = U_TYPED_CALLOC(struct vf_fs);
	vid->path = path;
	vid->got_sample = false;

	gchar *loop = "false";

	gchar *string = NULL;
	GstBus *bus = NULL;


	gst_init(0, NULL);

	if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
		VF_ERROR(vid, "File %s does not exist\n", path);
		return NULL;
	}

	vid->loop = g_main_loop_new(NULL, FALSE);

#if 0
	const gchar *caps = "video/x-raw,format=RGB";
	vid->format = XRT_FORMAT_R8G8B8;
	vid->stereo_format = XRT_STEREO_FORMAT_SBS;
#endif

#if 1
	const gchar *caps = "video/x-raw,format=YUY2";
	vid->format = XRT_FORMAT_YUYV422;
	vid->stereo_format = XRT_STEREO_FORMAT_SBS;
#endif

	string = g_strdup_printf(
	    "multifilesrc location=\"%s\" loop=%s ! decodebin ! videoconvert ! "
	    "appsink caps=\"%s\" name=testsink",
	    path, loop, caps);
	VF_DEBUG(vid, "Pipeline: %s\n", string);
	vid->source = gst_parse_launch(string, NULL);
	g_free(string);

	if (vid->source == NULL) {
		VF_ERROR(vid, "Bad source\n");
		g_main_loop_unref(vid->loop);
		free(vid);
		return NULL;
	}

	vid->testsink = gst_bin_get_by_name(GST_BIN(vid->source), "testsink");
	g_object_set(G_OBJECT(vid->testsink), "emit-signals", TRUE, "sync", TRUE, NULL);
	g_signal_connect(vid->testsink, "new-sample", G_CALLBACK(on_new_sample_from_sink), vid);

	bus = gst_element_get_bus(vid->source);
	gst_bus_add_watch(bus, (GstBusFunc)on_source_message, vid);
	gst_object_unref(bus);

	int ret = os_thread_helper_start(&vid->play_thread, run_play_thread, vid);
	if (!ret) {
		VF_ERROR(vid, "Failed to start thread");
	}

	// we need one sample to determine frame size
	gst_element_set_state(vid->source, GST_STATE_PLAYING);
	while (!vid->got_sample) {
		os_nanosleep(100 * 1000 * 1000);
	}
	gst_element_set_state(vid->source, GST_STATE_PAUSED);

	vid->base.enumerate_modes = vf_fs_enumerate_modes;
	vid->base.configure_capture = vf_fs_configure_capture;
	vid->base.stream_start = vf_fs_stream_start;
	vid->base.stream_stop = vf_fs_stream_stop;
	vid->base.is_running = vf_fs_is_running;
	vid->node.break_apart = vf_fs_node_break_apart;
	vid->node.destroy = vf_fs_node_destroy;
	vid->ll = debug_get_log_option_vf_log();

	// It's now safe to add it to the context.
	xrt_frame_context_add(xfctx, &vid->node);

	// Start the variable tracking after we know what device we have.
	// clang-format off
	u_var_add_root(vid, "Video File Frameserver", true);
	u_var_add_ro_text(vid, vid->base.name, "Card");
	u_var_add_ro_u32(vid, &vid->ll, "Log Level");
	// clang-format on

	return &(vid->base);
}
