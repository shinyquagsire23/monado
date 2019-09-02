// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  V4L2 frameserver implementation
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_v4l2
 */

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_format.h"

#include "v4l2_interface.h"

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>

#include <linux/videodev2.h>
#include <linux/v4l2-common.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>


/*
 *
 * Defines.
 *
 */

#define V_SPEW(p, ...)                                                         \
	do {                                                                   \
		if (p->print_spew) {                                           \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define V_DEBUG(p, ...)                                                        \
	do {                                                                   \
		if (p->print_debug) {                                          \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define V_ERROR(p, ...)                                                        \
	do {                                                                   \
		fprintf(stderr, "%s - ", __func__);                            \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, "\n");                                         \
	} while (false)

#define V_CONTROL_GET(VID, CONTROL)                                            \
	do {                                                                   \
		int _value = 0;                                                \
		if (v4l2_control_get(VID, V4L2_CID_##CONTROL, &_value) != 0) { \
			V_ERROR(VID, "failed to get V4L2_CID_" #CONTROL);      \
		} else {                                                       \
			V_DEBUG(VID, "V4L2_CID_" #CONTROL " = %i", _value);    \
		}                                                              \
	} while (false);

#define V_CONTROL_SET(VID, CONTROL, VALUE)                                     \
	do {                                                                   \
		if (v4l2_control_set(VID, V4L2_CID_##CONTROL, VALUE) != 0) {   \
			V_ERROR(VID, "failed to set V4L2_CID_" #CONTROL);      \
		}                                                              \
	} while (false);

DEBUG_GET_ONCE_BOOL_OPTION(v4l2_options, "V4L2_PRINT_OPTIONS", false)
DEBUG_GET_ONCE_BOOL_OPTION(v4l2_spew, "V4L2_PRINT_SPEW", false)
DEBUG_GET_ONCE_BOOL_OPTION(v4l2_debug, "V4L2_PRINT_DEBUG", false)
DEBUG_GET_ONCE_NUM_OPTION(v4l2_exposure_absolute, "V4L2_EXPOSURE_ABSOLUTE", 15)

#define NUM_V4L2_BUFFERS 3


/*
 *
 * Structs
 *
 */

struct v4l2_frame
{
	struct xrt_frame base;

	void *mem; //!< Data might be at an offset, so we need base memory.

	struct v4l2_buffer v_buf;
};

/*!
 * A single open v4l2 capture device, starts it's own thread and waits on it.
 */
struct v4l2_fs
{
	struct xrt_fs base;

	struct xrt_frame_node node;

	int fd;

	struct
	{
		bool extended_format;
		bool timeperframe;
	} has;

	struct
	{
		bool ps4_cam;
		bool set_auto_exposure;
		bool set_exposure_absolute;
		int value_exposure_absolute;
		int value_auto_exposure;
	} quirks;

	struct v4l2_frame frames[NUM_V4L2_BUFFERS];

	struct
	{
		bool mmap;
		bool userptr;
	} capture;

	struct xrt_frame_sink *sink;

	pthread_t stream_thread;

	struct v4l2_source_descriptor *descriptors;
	uint32_t num_descriptors;
	uint32_t selected;

	struct xrt_fs_capture_parameters capture_params;

	bool is_configured;
	bool is_running;
	bool print_spew;
	bool print_debug;

	char card[521];
};

/*!
 * Streaming thread entrypoint
 */
static void *
v4l2_fs_stream_run(void *ptr);

/*!
 * Cast to derived type.
 */
static inline struct v4l2_fs *
v4l2_fs(struct xrt_fs *xfs)
{
	return (struct v4l2_fs *)xfs;
}

static void
dump_controls(struct v4l2_fs *vid);


/*
 *
 * Misc helper functions
 *
 */

static void
v4l2_free_frame(struct xrt_frame *xf)
{
	struct v4l2_frame *vf = (struct v4l2_frame *)xf;
	struct v4l2_fs *vid = (struct v4l2_fs *)xf->owner;

	if (!vid->is_running) {
		return;
	}

	if (ioctl(vid->fd, VIDIOC_QBUF, &vf->v_buf) < 0) {
		V_ERROR(vid, "error: Requeue failed!");
		vid->is_running = false;
	}
}

XRT_MAYBE_UNUSED static int
v4l2_control_get(struct v4l2_fs *vid, uint32_t id, int *out_value)
{
	struct v4l2_control control = {0};
	int ret;

	control.id = id;
	ret = ioctl(vid->fd, VIDIOC_G_CTRL, &control);
	if (ret != 0) {
		return ret;
	}

	*out_value = control.value;
	return 0;
}

static int
v4l2_control_set(struct v4l2_fs *vid, uint32_t id, int value)
{
	struct v4l2_control control = {0};
	int ret;

	control.id = id;
	control.value = value;
	ret = ioctl(vid->fd, VIDIOC_S_CTRL, &control);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

static int
v4l2_query_cap_and_validate(struct v4l2_fs *vid)
{
	int ret;

	/*
	 * Regular caps.
	 */
	struct v4l2_capability cap;
	ret = ioctl(vid->fd, VIDIOC_QUERYCAP, &cap);
	if (ret != 0) {
		V_ERROR(vid, "error: Failed to get v4l2 cap.");
		return ret;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		// not a video device
		V_ERROR(vid, "error: Is not a capture device.");
		return -1;
	}
	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		// cannot stream
		V_ERROR(vid, "error: Can not stream!");
		return -1;
	}
	if (cap.capabilities & V4L2_CAP_EXT_PIX_FORMAT) {
		// need to query for extended format info
		vid->has.extended_format = true;
	}

	/*
	 * Stream capture caps.
	 */
	struct v4l2_streamparm stream = {0};
	stream.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = ioctl(vid->fd, VIDIOC_G_PARM, &stream);
	if (ret != 0) {
		V_ERROR(vid, "error: Failed to get v4l2 stream param.");
		return ret;
	}

	if (stream.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
		// Does this device support setting the timeperframe interval.
		vid->has.timeperframe = true;
	} else {
		V_DEBUG(vid, "warning: No V4L2_CAP_TIMEPERFRAME");
	}

	// Dump controls.
	if (debug_get_bool_option_v4l2_options()) {
		dump_controls(vid);
	}

	/*
	 * Find quirks
	 */
	char *card = (char *)cap.card;
	snprintf(vid->card, sizeof(vid->card), "%s", card);

	vid->quirks.ps4_cam =
	    strcmp(card, "USB Camera-OV580: USB Camera-OV") == 0;

	if (vid->quirks.ps4_cam) {
		// The experimented best controls to best track things.
		vid->quirks.set_auto_exposure = true;
		vid->quirks.value_auto_exposure = 2;
		vid->quirks.set_exposure_absolute = true;
		vid->quirks.value_exposure_absolute =
		    debug_get_num_option_v4l2_exposure_absolute();
	}

	// Done
	return 0;
}

static int
v4l2_try_userptr(struct v4l2_fs *vid, struct v4l2_requestbuffers *v_bufrequest)
{
	v_bufrequest->memory = V4L2_MEMORY_USERPTR;
	if (ioctl(vid->fd, VIDIOC_REQBUFS, v_bufrequest) == 0) {
		vid->capture.userptr = true;
		return 0;
	}

	V_DEBUG(vid, "info: Driver does not handle userptr buffers.");
	return -1;
}

static int
v4l2_try_mmap(struct v4l2_fs *vid, struct v4l2_requestbuffers *v_bufrequest)
{
	v_bufrequest->memory = V4L2_MEMORY_MMAP;
	if (ioctl(vid->fd, VIDIOC_REQBUFS, v_bufrequest) == 0) {
		vid->capture.mmap = true;
		return 0;
	}

	V_DEBUG(vid, "info: Driver does not mmap userptr buffers.");
	return -1;
}

static int
v4l2_setup_mmap_buffer(struct v4l2_fs *vid,
                       struct v4l2_frame *vf,
                       struct v4l2_buffer *v_buf)
{
	void *ptr = mmap(0, v_buf->length, PROT_READ, MAP_SHARED, vid->fd,
	                 v_buf->m.offset);
	if (ptr == MAP_FAILED) {
		V_ERROR(vid, "error: Call to mmap failed!");
		return -1;
	}

	vf->mem = ptr;

	return 0;
}

static int
v4l2_setup_userptr_buffer(struct v4l2_fs *vid,
                          struct v4l2_frame *vf,
                          struct v4l2_buffer *v_buf)
{
	// align this to a memory page, v4l2 likes it that way
	void *ptr = aligned_alloc(getpagesize(), v_buf->length);
	if (ptr == NULL) {
		V_ERROR(vid, "error: Could not alloc page-aligned memory!");
		return -1;
	}

	vf->mem = ptr;
	v_buf->m.userptr = (intptr_t)ptr;

	return 0;
}


/*
 *
 * Mode adding functions.
 *
 */

static void
v4l2_quirk_apply_ps4(struct v4l2_fs *vid, struct v4l2_source_descriptor *desc)
{
	desc->offset = 32 + 64;
	desc->base.stereo_format = XRT_STEREO_FORMAT_SBS;

	switch (desc->stream.width) {
	case 3448:
		desc->base.width = 1280 * 2;
		desc->base.height = 800;
		break;
	case 1748:
		desc->base.width = 640 * 2;
		desc->base.height = 400;
		break;
	case 898:
		desc->base.width = 320 * 2;
		desc->base.height = 192;
		break;
	default: break;
	}
}

static struct v4l2_source_descriptor *
v4l2_add_descriptor(struct v4l2_fs *vid)
{
	uint32_t index = vid->num_descriptors++;
	size_t new_size =
	    vid->num_descriptors * sizeof(struct v4l2_source_descriptor);
	vid->descriptors = realloc(vid->descriptors, new_size);

	struct v4l2_source_descriptor *desc = &vid->descriptors[index];
	memset(desc, 0, sizeof(*desc));

	return desc;
}

static void
v4l2_list_modes_interval(struct v4l2_fs *vid,
                         const struct v4l2_fmtdesc *fmt,
                         const struct v4l2_frmsizeenum *size,
                         const struct v4l2_frmivalenum *interval)
{
	if (interval->discrete.denominator % interval->discrete.numerator ==
	    0) {
		int fps = interval->discrete.denominator /
		          interval->discrete.numerator;

		V_DEBUG(vid, "#%i %dx%d@%i", vid->num_descriptors,
		        interval->width, interval->height, fps);
	} else {
		double fps = (double)interval->discrete.denominator /
		             (double)interval->discrete.numerator;

		V_DEBUG(vid, "#%i %dx%d@%f", vid->num_descriptors,
		        interval->width, interval->height, fps);
	}
}

static void
v4l2_list_modes_size(struct v4l2_fs *vid,
                     const struct v4l2_fmtdesc *fmt,
                     const struct v4l2_frmsizeenum *size)
{
	if (size->type != V4L2_FRMSIZE_TYPE_DISCRETE) {
		V_DEBUG(vid, "warning: Skipping non discrete frame size.");
		return;
	}

	struct v4l2_frmivalenum interval;
	U_ZERO(&interval);
	interval.pixel_format = size->pixel_format;
	interval.width = size->discrete.width;
	interval.height = size->discrete.height;

	// Since we don't keep track of the interval
	// we only make sure there is at least one.
	while (ioctl(vid->fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == 0) {
		v4l2_list_modes_interval(vid, fmt, size, &interval);
		interval.index++;
	}

	// We didn't find any frame intervals.
	if (interval.index == 0) {
		return;
	}

	enum xrt_format format = 0;
	switch (interval.pixel_format) {
	case V4L2_PIX_FMT_YUYV: format = XRT_FORMAT_YUV422; break;
	case V4L2_PIX_FMT_MJPEG: format = XRT_FORMAT_MJPEG; break;
	default: V_ERROR(vid, "error: Format not supported."); return;
	}

	// Allocate new descriptor.
	struct v4l2_source_descriptor *desc = v4l2_add_descriptor(vid);

	// Fill out the stream variables.
	desc->stream.width = interval.width;
	desc->stream.height = interval.height;
	desc->stream.format = interval.pixel_format;
	snprintf(desc->format_name, sizeof(desc->format_name), "%s",
	         fmt->description);

	if (u_format_is_blocks(format)) {
		u_format_size_for_dimensions(
		    format, interval.width, interval.height,
		    &desc->stream.stride, &desc->stream.size);
	}

	// Fill out the out sink variables.
	desc->base.stereo_format = XRT_STEREO_FORMAT_NONE;
	desc->base.format = format;
	desc->base.width = desc->stream.width;
	desc->base.height = desc->stream.height;

	/*
	 * Apply any quirks to the modes.
	 */

	if (vid->quirks.ps4_cam) {
		v4l2_quirk_apply_ps4(vid, desc);
	}
}

static void
v4l2_list_modes_fmt(struct v4l2_fs *vid, const struct v4l2_fmtdesc *fmt)
{
	V_DEBUG(vid, "format: %s %08x %d", fmt->description, fmt->pixelformat,
	        fmt->type);

	switch (fmt->pixelformat) {
	case V4L2_PIX_FMT_YUYV: break;
	case V4L2_PIX_FMT_MJPEG: break;
	default:
		V_ERROR(vid, "error: Unknown pixelformat '%s'",
		        fmt->description);
		return;
	}

	struct v4l2_frmsizeenum size = {0};
	size.pixel_format = fmt->pixelformat;

	while (ioctl(vid->fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0) {
		v4l2_list_modes_size(vid, fmt, &size);
		size.index++;
	}
}

static void
v4l2_list_modes(struct v4l2_fs *vid)
{
	struct v4l2_fmtdesc desc;
	U_ZERO(&desc);
	desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	while (ioctl(vid->fd, VIDIOC_ENUM_FMT, &desc) == 0) {
		v4l2_list_modes_fmt(vid, &desc);
		desc.index++;
	}
}


/*
 *
 * Exported functions.
 *
 */

static bool
v4l2_fs_enumerate_modes(struct xrt_fs *xfs,
                        struct xrt_fs_mode **out_modes,
                        uint32_t *out_count)
{
	struct v4l2_fs *vid = v4l2_fs(xfs);
	if (vid->num_descriptors == 0) {
		return false;
	}

	struct xrt_fs_mode *modes =
	    U_TYPED_ARRAY_CALLOC(struct xrt_fs_mode, vid->num_descriptors);
	if (modes == NULL) {
		return false;
	}

	for (uint32_t i = 0; i < vid->num_descriptors; i++) {
		modes[i] = vid->descriptors[i].base;
	}

	*out_modes = modes;
	*out_count = vid->num_descriptors;

	return true;
}

static bool
v4l2_fs_configure_capture(struct xrt_fs *xfs,
                          struct xrt_fs_capture_parameters *cp)
{
	// struct v4l2_fs *vid = v4l2_fs(xfs);
	//! @todo
	return false;
}

static bool
v4l2_fs_stream_start(struct xrt_fs *xfs,
                     struct xrt_frame_sink *xs,
                     uint32_t descriptor_index)
{
	struct v4l2_fs *vid = v4l2_fs(xfs);

	if (descriptor_index >= vid->num_descriptors) {
		V_ERROR(vid, "error Invalid descriptor_index (%i >= %i)",
		        descriptor_index, vid->num_descriptors);
		return false;
	}
	vid->selected = descriptor_index;

	vid->sink = xs;
	vid->is_running = true;
	if (pthread_create(&vid->stream_thread, NULL, v4l2_fs_stream_run,
	                   xfs)) {
		vid->is_running = false;
		V_ERROR(vid, "error: Could not create thread");
		return false;
	}

	V_SPEW(vid, "info: Started!");

	// we're off to the races!
	return true;
}

static bool
v4l2_fs_stream_stop(struct xrt_fs *xfs)
{
	struct v4l2_fs *vid = v4l2_fs(xfs);

	if (!vid->is_running) {
		return true;
	}

	vid->is_running = false;
	pthread_join(vid->stream_thread, NULL);

	return true;
}

static bool
v4l2_fs_is_running(struct xrt_fs *xfs)
{
	struct v4l2_fs *vid = v4l2_fs(xfs);

	return vid->is_running;
}

static void
v4l2_fs_destroy(struct v4l2_fs *vid)
{
	// Make sure that the stream is stopped.
	v4l2_fs_stream_stop(&vid->base);

	// Stop the variable tracking.
	u_var_remove_root(vid);

	if (vid->descriptors != NULL) {
		free(vid->descriptors);
		vid->descriptors = NULL;
		vid->num_descriptors = 0;
	}

	vid->capture.mmap = false;
	if (vid->capture.userptr) {
		vid->capture.userptr = false;
		for (uint32_t i = 0; i < NUM_V4L2_BUFFERS; i++) {
			free(vid->frames[i].mem);
			vid->frames[i].mem = NULL;
		}
	}

	if (vid->fd >= 0) {
		close(vid->fd);
		vid->fd = -1;
	}

	free(vid);
}

static void
v4l2_fs_node_break_apart(struct xrt_frame_node *node)
{
	struct v4l2_fs *vid = container_of(node, struct v4l2_fs, node);
	v4l2_fs_stream_stop(&vid->base);
}

static void
v4l2_fs_node_destroy(struct xrt_frame_node *node)
{
	struct v4l2_fs *vid = container_of(node, struct v4l2_fs, node);
	v4l2_fs_destroy(vid);
}

struct xrt_fs *
v4l2_fs_create(struct xrt_frame_context *xfctx, const char *path)
{
	struct v4l2_fs *vid = U_TYPED_CALLOC(struct v4l2_fs);
	vid->base.enumerate_modes = v4l2_fs_enumerate_modes;
	vid->base.configure_capture = v4l2_fs_configure_capture;
	vid->base.stream_start = v4l2_fs_stream_start;
	vid->base.stream_stop = v4l2_fs_stream_stop;
	vid->base.is_running = v4l2_fs_is_running;
	vid->node.break_apart = v4l2_fs_node_break_apart;
	vid->node.destroy = v4l2_fs_node_destroy;
	vid->print_spew = debug_get_bool_option_v4l2_spew();
	vid->print_debug = debug_get_bool_option_v4l2_debug();
	vid->fd = -1;

	int fd = open(path, O_RDWR, 0);
	if (fd < 0) {
		V_ERROR(vid, "Can not open '%s'", path);
		free(vid);
		return NULL;
	}

	vid->fd = fd;

	int ret = v4l2_query_cap_and_validate(vid);
	if (ret != 0) {
		v4l2_fs_destroy(vid);
		vid = NULL;
		return NULL;
	}

	// It's now safe to add it to the context.
	xrt_frame_context_add(xfctx, &vid->node);

	// Start the variable tracking after we know what device we have.
	u_var_add_root(vid, "V4L2 Frameserver", true);
	u_var_add_ro_text(vid, vid->card, "Card");
	u_var_add_bool(vid, &vid->print_debug, "Debug");
	u_var_add_bool(vid, &vid->print_spew, "Spew");

	v4l2_list_modes(vid);

	return &(vid->base);
}

void *
v4l2_fs_stream_run(void *ptr)
{
	struct xrt_fs *xfs = (struct xrt_fs *)ptr;
	struct v4l2_fs *vid = v4l2_fs(xfs);

	V_DEBUG(vid, "info: Thread enter!");

	if (vid->fd == -1) {
		V_ERROR(vid, "error: Device not opened!");
		return NULL;
	}

	// set up our capture format

	struct v4l2_source_descriptor *desc = &vid->descriptors[vid->selected];

	struct v4l2_format v_format;
	U_ZERO(&v_format);
	v_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v_format.fmt.pix.width = desc->stream.width;
	v_format.fmt.pix.height = desc->stream.height;
	v_format.fmt.pix.pixelformat = desc->stream.format;
	v_format.fmt.pix.field = V4L2_FIELD_ANY;
	if (vid->has.extended_format) {
		v_format.fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
	}

	if (ioctl(vid->fd, VIDIOC_S_FMT, &v_format) < 0) {
		V_ERROR(vid, "could not set up format!");
		return NULL;
	}

	// set up our buffers - prefer userptr (client alloc) vs mmap (kernel
	// alloc)
	// TODO: using buffer caps may be better than 'fallthrough to mmap'
	struct v4l2_requestbuffers v_bufrequest;
	U_ZERO(&v_bufrequest);
	v_bufrequest.count = NUM_V4L2_BUFFERS;
	v_bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (v4l2_try_userptr(vid, &v_bufrequest) != 0 &&
	    v4l2_try_mmap(vid, &v_bufrequest) != 0) {
		V_ERROR(vid, "error: Driver does not support mmap or userptr.");
		return NULL;
	}


	for (uint32_t i = 0; i < NUM_V4L2_BUFFERS; i++) {
		struct v4l2_frame *vf = &vid->frames[i];
		struct v4l2_buffer *v_buf = &vf->v_buf;

		vf->base.owner = vid;
		vf->base.destroy = v4l2_free_frame;

		v_buf->index = i;
		v_buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v_buf->memory = v_bufrequest.memory;

		if (ioctl(vid->fd, VIDIOC_QUERYBUF, v_buf) < 0) {
			V_ERROR(vid, "error: Could not query buffers!");
			return NULL;
		}

		if (vid->capture.userptr &&
		    v4l2_setup_userptr_buffer(vid, vf, v_buf) != 0) {
			return NULL;
		}
		if (vid->capture.mmap &&
		    v4l2_setup_mmap_buffer(vid, vf, v_buf) != 0) {
			return NULL;
		}

		// Silence valgrind.
		memset(vf->mem, 0, v_buf->length);

		// Queue this buffer
		if (ioctl(vid->fd, VIDIOC_QBUF, v_buf) < 0) {
			V_ERROR(vid, "error: queueing buffer failed!");
			return NULL;
		}
	}

	int start_capture = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(vid->fd, VIDIOC_STREAMON, &start_capture) < 0) {
		V_ERROR(vid, "error: Could not start capture!");
		return NULL;
	}

	/*
	 * Need to set these after we have started the stream.
	 */
	if (vid->quirks.set_auto_exposure) {
		V_CONTROL_SET(vid, EXPOSURE_AUTO,
		              vid->quirks.value_auto_exposure);
	}
	if (vid->quirks.set_exposure_absolute) {
		V_CONTROL_SET(vid, EXPOSURE_ABSOLUTE,
		              vid->quirks.value_exposure_absolute);
	}

	struct v4l2_buffer v_buf = {0};
	v_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v_buf.memory = v_bufrequest.memory;

	while (vid->is_running) {
		if (ioctl(vid->fd, VIDIOC_DQBUF, &v_buf) < 0) {
			V_ERROR(vid, "error: Dequeue failed!");
			vid->is_running = false;
			break;
		}
		V_SPEW(vid, "Got frame #%u, index %i", v_buf.sequence,
		       v_buf.index);

		struct v4l2_frame *vf = &vid->frames[v_buf.index];
		struct xrt_frame *xf = NULL;

		xrt_frame_reference(&xf, &vf->base);
		uint8_t *data = vf->mem;

		//! @todo Sequense number and timestamp.
		xf->width = desc->base.width;
		xf->height = desc->base.height;
		xf->format = desc->base.format;
		xf->stereo_format = desc->base.stereo_format;

		xf->data = data + desc->offset;
		xf->stride = desc->stream.stride;
		xf->size = v_buf.bytesused - desc->offset;
		xf->source_id = vid->base.source_id;
		xf->source_sequence = v_buf.sequence;

		vid->sink->push_frame(vid->sink, xf);

		// The frame is requeued as soon as the refcount reaches zero,
		// this can be done safely from another thread.
		xrt_frame_reference(&xf, NULL);
	}

	V_DEBUG(vid, "info: Thread leave!");

	return NULL;
}


/*
 *
 * Helper debug functions.
 *
 */

static void
dump_menu(struct v4l2_fs *vid, uint32_t id, uint32_t min, uint32_t max)
{
	fprintf(stderr, "  Menu items:\n");

	struct v4l2_querymenu querymenu = {0};
	querymenu.id = id;

	for (querymenu.index = min; querymenu.index <= max; querymenu.index++) {
		if (0 != ioctl(vid->fd, VIDIOC_QUERYMENU, &querymenu)) {
			fprintf(stderr, "  %i\n", querymenu.index);
			continue;
		}
		fprintf(stderr, "  %i: %s\n", querymenu.index, querymenu.name);
	}
}

static void
dump_contron_name(uint32_t id)
{
	const char *str = "ERROR";
	switch (id) {
#define CASE(CONTROL)                                                          \
	case V4L2_CID_##CONTROL: str = "V4L2_CID_" #CONTROL; break
		CASE(BRIGHTNESS);
		CASE(CONTRAST);
		CASE(SATURATION);
		CASE(HUE);
		CASE(AUDIO_VOLUME);
		CASE(AUDIO_BALANCE);
		CASE(AUDIO_BASS);
		CASE(AUDIO_TREBLE);
		CASE(AUDIO_MUTE);
		CASE(AUDIO_LOUDNESS);
		CASE(BLACK_LEVEL);
		CASE(AUTO_WHITE_BALANCE);
		CASE(DO_WHITE_BALANCE);
		CASE(RED_BALANCE);
		CASE(BLUE_BALANCE);
		CASE(GAMMA);

		CASE(EXPOSURE);
		CASE(AUTOGAIN);
		CASE(GAIN);
		CASE(DIGITAL_GAIN);
		CASE(ANALOGUE_GAIN);
		CASE(HFLIP);
		CASE(VFLIP);
		CASE(POWER_LINE_FREQUENCY);
		CASE(POWER_LINE_FREQUENCY_DISABLED);
		CASE(POWER_LINE_FREQUENCY_50HZ);
		CASE(POWER_LINE_FREQUENCY_60HZ);
		CASE(POWER_LINE_FREQUENCY_AUTO);
		CASE(HUE_AUTO);
		CASE(WHITE_BALANCE_TEMPERATURE);
		CASE(SHARPNESS);
		CASE(BACKLIGHT_COMPENSATION);
		CASE(CHROMA_AGC);
		CASE(CHROMA_GAIN);
		CASE(COLOR_KILLER);
		CASE(COLORFX);
		CASE(COLORFX_CBCR);
		CASE(AUTOBRIGHTNESS);
		CASE(ROTATE);
		CASE(BG_COLOR);
		CASE(ILLUMINATORS_1);
		CASE(ILLUMINATORS_2);
		CASE(MIN_BUFFERS_FOR_CAPTURE);
		CASE(MIN_BUFFERS_FOR_OUTPUT);
		CASE(ALPHA_COMPONENT);

		// Camera controls
		CASE(EXPOSURE_AUTO);
		CASE(EXPOSURE_ABSOLUTE);
		CASE(EXPOSURE_AUTO_PRIORITY);
		CASE(AUTO_EXPOSURE_BIAS);
		CASE(PAN_RELATIVE);
		CASE(TILT_RELATIVE);
		CASE(PAN_RESET);
		CASE(TILT_RESET);
		CASE(PAN_ABSOLUTE);
		CASE(TILT_ABSOLUTE);
		CASE(FOCUS_ABSOLUTE);
		CASE(FOCUS_RELATIVE);
		CASE(FOCUS_AUTO);
		CASE(ZOOM_ABSOLUTE);
		CASE(ZOOM_RELATIVE);
		CASE(ZOOM_CONTINUOUS);
		CASE(PRIVACY);
		CASE(IRIS_ABSOLUTE);
		CASE(IRIS_RELATIVE);
#undef CASE
	default: fprintf(stderr, "0x%08x", id); return;
	}
	fprintf(stderr, "%s", str);
}

static void
dump_controls(struct v4l2_fs *vid)
{
	struct v4l2_queryctrl queryctrl = {0};

	queryctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
	while (0 == ioctl(vid->fd, VIDIOC_QUERYCTRL, &queryctrl)) {
		fprintf(stderr, "Control ");
		dump_contron_name(queryctrl.id);
		fprintf(stderr, " '%s'", queryctrl.name);

#define V_CHECK(FLAG)                                                          \
	do {                                                                   \
		if (queryctrl.flags & V4L2_CTRL_FLAG_##FLAG) {                 \
			fprintf(stderr, ", " #FLAG);                           \
		}                                                              \
	} while (false)

		V_CHECK(DISABLED);
		V_CHECK(GRABBED);
		V_CHECK(READ_ONLY);
		V_CHECK(UPDATE);
		V_CHECK(INACTIVE);
		V_CHECK(SLIDER);
		V_CHECK(WRITE_ONLY);
		V_CHECK(VOLATILE);
		V_CHECK(HAS_PAYLOAD);
		V_CHECK(EXECUTE_ON_WRITE);
		V_CHECK(MODIFY_LAYOUT);
#undef V_CHECK

		fprintf(stderr, "\n");

		if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
			continue;
		}

		if (queryctrl.type == V4L2_CTRL_TYPE_MENU) {
			dump_menu(vid, queryctrl.id, queryctrl.minimum,
			          queryctrl.maximum);
		}

		queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
	}
}
