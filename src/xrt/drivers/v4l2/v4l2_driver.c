// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  V4L2 frameserver implementation
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_v4l2
 */

#include "os/os_time.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_format.h"
#include "util/u_logging.h"

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

/*
 *
 * Printing functions.
 *
 */

#define V4L2_TRACE(d, ...) U_LOG_IFL_T(d->ll, __VA_ARGS__)
#define V4L2_DEBUG(d, ...) U_LOG_IFL_D(d->ll, __VA_ARGS__)
#define V4L2_INFO(d, ...) U_LOG_IFL_I(d->ll, __VA_ARGS__)
#define V4L2_WARN(d, ...) U_LOG_IFL_W(d->ll, __VA_ARGS__)
#define V4L2_ERROR(d, ...) U_LOG_IFL_E(d->ll, __VA_ARGS__)

#define V_CONTROL_GET(VID, CONTROL)                                                                                    \
	do {                                                                                                           \
		int _value = 0;                                                                                        \
		if (v4l2_control_get(VID, V4L2_CID_##CONTROL, &_value) != 0) {                                         \
			V4L2_ERROR(VID, "failed to get V4L2_CID_" #CONTROL);                                           \
		} else {                                                                                               \
			V4L2_DEBUG(VID, "V4L2_CID_" #CONTROL " = %i", _value);                                         \
		}                                                                                                      \
	} while (false);

#define V_CONTROL_SET(VID, CONTROL, VALUE)                                                                             \
	do {                                                                                                           \
		if (v4l2_control_set(VID, V4L2_CID_##CONTROL, VALUE) != 0) {                                           \
			V4L2_ERROR(VID, "failed to set V4L2_CID_" #CONTROL);                                           \
		}                                                                                                      \
	} while (false);

DEBUG_GET_ONCE_LOG_OPTION(v4l2_log, "V4L2_LOG", U_LOGGING_WARN)
DEBUG_GET_ONCE_NUM_OPTION(v4l2_exposure_absolute, "V4L2_EXPOSURE_ABSOLUTE", 10)

#define NUM_V4L2_BUFFERS 5


/*
 *
 * Structs
 *
 */

/*!
 * @extends xrt_frame
 * @ingroup drv_v4l2
 */
struct v4l2_frame
{
	struct xrt_frame base;

	void *mem; //!< Data might be at an offset, so we need base memory.

	struct v4l2_buffer v_buf;
};

struct v4l2_state_want
{
	bool active;
	int value;
};

/*!
 * @ingroup drv_v4l2
 */
struct v4l2_control_state
{
	int id;
	int force;

	struct v4l2_state_want want[2];

	int value;

	const char *name;
};

/*!
 * A single open v4l2 capture device, starts its own thread and waits on it.
 *
 * @implements xrt_frame_node
 * @implements xrt_fs
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

	enum xrt_fs_capture_type capture_type;
	struct v4l2_control_state states[256];
	size_t num_states;

	struct
	{
		bool ps4_cam;
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
	enum u_logging_level ll;
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

static void
dump_contron_name(uint32_t id);


/*
 *
 * Misc helper functions
 *
 */

static size_t
align_up(size_t size, size_t align)
{
	if ((size % align) == 0) {
		return size;
	}

	return size + (align - (size % align));
}

static void
v4l2_free_frame(struct xrt_frame *xf)
{
	struct v4l2_frame *vf = (struct v4l2_frame *)xf;
	struct v4l2_fs *vid = (struct v4l2_fs *)xf->owner;

	if (!vid->is_running) {
		return;
	}

	if (ioctl(vid->fd, VIDIOC_QBUF, &vf->v_buf) < 0) {
		V4L2_ERROR(vid, "error: Requeue failed!");
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

static void
v4l2_add_control_state(struct v4l2_fs *vid, int control, struct v4l2_state_want want[2], int force, const char *name)
{
	struct v4l2_control_state *state = &vid->states[vid->num_states++];

	state->id = control;
	state->name = name;
	state->want[0] = want[0];
	state->want[1] = want[1];
	state->force = force;
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
		V4L2_ERROR(vid, "error: Failed to get v4l2 cap.");
		return ret;
	}

	char *card = (char *)cap.card;
	snprintf(vid->base.name, sizeof(vid->base.name), "%s", card);

	V4L2_DEBUG(vid, "V4L2 device: '%s'", vid->base.name);

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		// not a video device
		V4L2_ERROR(vid, "error: Is not a capture device.");
		return -1;
	}
	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		// cannot stream
		V4L2_ERROR(vid, "error: Can not stream!");
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
		V4L2_ERROR(vid, "error: Failed to get v4l2 stream param.");
		return ret;
	}

	if (stream.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
		// Does this device support setting the timeperframe interval.
		vid->has.timeperframe = true;
	} else {
		V4L2_DEBUG(vid, "warning: No V4L2_CAP_TIMEPERFRAME");
	}

	// Log controls.
	if (vid->ll <= U_LOGGING_DEBUG) {
		dump_controls(vid);
	}

	/*
	 * Find quirks
	 */
	vid->quirks.ps4_cam = strcmp(card, "USB Camera-OV580: USB Camera-OV") == 0;

#define ADD(CONTROL, WANT1, WANT2, WANT3, WANT4, NAME)                                                                 \
	do {                                                                                                           \
		struct v4l2_state_want want[2] = {{WANT1, WANT2}, {WANT3, WANT4}};                                     \
		v4l2_add_control_state(vid, V4L2_CID_##CONTROL, want, 2, NAME);                                        \
	} while (false)

	if (vid->quirks.ps4_cam) {
		// The experimented best controls to best track things.
		ADD(GAIN,              //
		    true, 0, false, 0, //
		    "gain");
		ADD(AUTO_WHITE_BALANCE, //
		    true, 0, true, 1,   //
		    "auto_white_balance");
		ADD(WHITE_BALANCE_TEMPERATURE, //
		    true, 3900, false, 0,      //
		    "white_balance_temperature");
		ADD(EXPOSURE_AUTO,    //
		    true, 2, true, 0, //
		    "exposure_auto");
		long num = debug_get_num_option_v4l2_exposure_absolute();
		ADD(EXPOSURE_ABSOLUTE,   //
		    true, num, false, 0, //
		    "exposure_absolute");
	}

	if (strcmp(card, "3D USB Camera: 3D USB Camera") == 0) {
		// The experimented best controls to best track things.
		ADD(AUTO_WHITE_BALANCE, //
		    true, 0, true, 1,   //
		    "auto_white_balance");
		ADD(WHITE_BALANCE_TEMPERATURE, //
		    true, 6500, false, 0,      //
		    "white_balance_temperature");
		ADD(EXPOSURE_AUTO,    //
		    true, 1, true, 3, //
		    "exposure_auto");
		ADD(EXPOSURE_ABSOLUTE,  //
		    true, 10, false, 0, //
		    "exposure_absolute");
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

	V4L2_DEBUG(vid, "info: Driver does not handle userptr buffers.");
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

	V4L2_DEBUG(vid, "info: Driver does not mmap userptr buffers.");
	return -1;
}

static int
v4l2_setup_mmap_buffer(struct v4l2_fs *vid, struct v4l2_frame *vf, struct v4l2_buffer *v_buf)
{
	void *ptr = mmap(0, v_buf->length, PROT_READ, MAP_SHARED, vid->fd, v_buf->m.offset);
	if (ptr == MAP_FAILED) {
		V4L2_ERROR(vid, "error: Call to mmap failed!");
		return -1;
	}

	vf->mem = ptr;

	return 0;
}

static int
v4l2_setup_userptr_buffer(struct v4l2_fs *vid, struct v4l2_frame *vf, struct v4l2_buffer *v_buf)
{
	// align this to a memory page, v4l2 likes it that way
	long sz = sysconf(_SC_PAGESIZE);
	size_t size = align_up(v_buf->length, (size_t)sz);

	void *ptr = aligned_alloc(sz, size);
	if (ptr == NULL) {
		V4L2_ERROR(vid, "error: Could not alloc page-aligned memory!");
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

static struct v4l2_source_descriptor *
v4l2_add_descriptor(struct v4l2_fs *vid)
{
	uint32_t index = vid->num_descriptors++;
	U_ARRAY_REALLOC_OR_FREE(vid->descriptors, struct v4l2_source_descriptor, vid->num_descriptors);

	struct v4l2_source_descriptor *desc = &vid->descriptors[index];
	U_ZERO(desc);

	return desc;
}

static void
v4l2_list_modes_interval(struct v4l2_fs *vid,
                         const struct v4l2_fmtdesc *fmt,
                         const struct v4l2_frmsizeenum *size,
                         const struct v4l2_frmivalenum *interval)
{
	if (interval->discrete.denominator % interval->discrete.numerator == 0) {
		int fps = interval->discrete.denominator / interval->discrete.numerator;

		V4L2_DEBUG(vid, "#%i %dx%d@%i", vid->num_descriptors, interval->width, interval->height, fps);
	} else {
		double fps = (double)interval->discrete.denominator / (double)interval->discrete.numerator;

		V4L2_DEBUG(vid, "#%i %dx%d@%f", vid->num_descriptors, interval->width, interval->height, fps);
	}
}

static void
v4l2_list_modes_size(struct v4l2_fs *vid, const struct v4l2_fmtdesc *fmt, const struct v4l2_frmsizeenum *size)
{
	if (size->type != V4L2_FRMSIZE_TYPE_DISCRETE) {
		V4L2_DEBUG(vid, "warning: Skipping non discrete frame size.");
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

	enum xrt_format format = (enum xrt_format)0;
	switch (interval.pixel_format) {
	case V4L2_PIX_FMT_YUYV: format = XRT_FORMAT_YUYV422; break;
	case V4L2_PIX_FMT_UYVY: format = XRT_FORMAT_UYVY422; break;
	case V4L2_PIX_FMT_MJPEG: format = XRT_FORMAT_MJPEG; break;
	default: V4L2_ERROR(vid, "error: Format not supported."); return;
	}

	// Allocate new descriptor.
	struct v4l2_source_descriptor *desc = v4l2_add_descriptor(vid);

	// Fill out the stream variables.
	desc->stream.width = interval.width;
	desc->stream.height = interval.height;
	desc->stream.format = interval.pixel_format;
	snprintf(desc->format_name, sizeof(desc->format_name), "%s", fmt->description);

	if (u_format_is_blocks(format)) {
		u_format_size_for_dimensions(format, interval.width, interval.height, &desc->stream.stride,
		                             &desc->stream.size);
	}

	// Fill out the out sink variables.
	desc->base.stereo_format = XRT_STEREO_FORMAT_NONE;
	desc->base.format = format;
	desc->base.width = desc->stream.width;
	desc->base.height = desc->stream.height;
}

static void
v4l2_list_modes_fmt(struct v4l2_fs *vid, const struct v4l2_fmtdesc *fmt)
{
	V4L2_DEBUG(vid, "format: %s %08x %d", fmt->description, fmt->pixelformat, fmt->type);

	switch (fmt->pixelformat) {
	case V4L2_PIX_FMT_YUYV: break;
	case V4L2_PIX_FMT_UYVY: break;
	case V4L2_PIX_FMT_MJPEG: break;
	default: V4L2_ERROR(vid, "error: Unknown pixelformat '%s' '%08x'", fmt->description, fmt->pixelformat); return;
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

static void
v4l2_set_control_if_diff(struct v4l2_fs *vid, struct v4l2_control_state *state)
{
	int value = 0;
	int ret = 0;

	struct v4l2_state_want *want = &state->want[vid->capture_type];
	if (!want->active) {
		return;
	}

	ret = v4l2_control_get(vid, state->id, &value);
	if (ret != 0) {
		return;
	}

	if (value == want->value && state->force <= 0) {
		return;
	}

#if 0
	dump_contron_name(state->id);

	U_LOG_E(" ret: %i, want: %i, was: %i, force: %i", ret,
	        state->want, value, state->force);
#endif

	ret = v4l2_control_set(vid, state->id, want->value);
	if (ret != 0) {
		fprintf(stderr, "Failed to set ");
		dump_contron_name(state->id);
		fprintf(stderr, "\n");
		return;
	}

	if (state->force > 0) {
		state->force--;
	}
}

static void
v4l2_update_controls(struct v4l2_fs *vid)
{
	for (size_t i = 0; i < vid->num_states; i++) {
		v4l2_set_control_if_diff(vid, &vid->states[i]);
	}
}


/*
 *
 * Exported functions.
 *
 */

static bool
v4l2_fs_enumerate_modes(struct xrt_fs *xfs, struct xrt_fs_mode **out_modes, uint32_t *out_count)
{
	struct v4l2_fs *vid = v4l2_fs(xfs);
	if (vid->num_descriptors == 0) {
		return false;
	}

	struct xrt_fs_mode *modes = U_TYPED_ARRAY_CALLOC(struct xrt_fs_mode, vid->num_descriptors);
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
v4l2_fs_configure_capture(struct xrt_fs *xfs, struct xrt_fs_capture_parameters *cp)
{
	// struct v4l2_fs *vid = v4l2_fs(xfs);
	//! @todo
	return false;
}

static bool
v4l2_fs_stream_start(struct xrt_fs *xfs,
                     struct xrt_frame_sink *xs,
                     enum xrt_fs_capture_type capture_type,
                     uint32_t descriptor_index)
{
	struct v4l2_fs *vid = v4l2_fs(xfs);

	if (descriptor_index >= vid->num_descriptors) {
		V4L2_ERROR(vid, "error Invalid descriptor_index (%i >= %i)", descriptor_index, vid->num_descriptors);
		return false;
	}
	vid->selected = descriptor_index;

	vid->sink = xs;
	vid->is_running = true;
	vid->capture_type = capture_type;
	if (pthread_create(&vid->stream_thread, NULL, v4l2_fs_stream_run, xfs)) {
		vid->is_running = false;
		V4L2_ERROR(vid, "error: Could not create thread");
		return false;
	}

	V4L2_TRACE(vid, "info: Started!");

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
v4l2_fs_create(struct xrt_frame_context *xfctx,
               const char *path,
               const char *product,
               const char *manufacturer,
               const char *serial)
{
	struct v4l2_fs *vid = U_TYPED_CALLOC(struct v4l2_fs);
	vid->base.enumerate_modes = v4l2_fs_enumerate_modes;
	vid->base.configure_capture = v4l2_fs_configure_capture;
	vid->base.stream_start = v4l2_fs_stream_start;
	vid->base.stream_stop = v4l2_fs_stream_stop;
	vid->base.is_running = v4l2_fs_is_running;
	vid->node.break_apart = v4l2_fs_node_break_apart;
	vid->node.destroy = v4l2_fs_node_destroy;
	vid->ll = debug_get_log_option_v4l2_log();
	vid->fd = -1;

	snprintf(vid->base.product, sizeof(vid->base.product), "%s", product);
	snprintf(vid->base.manufacturer, sizeof(vid->base.manufacturer), "%s", manufacturer);
	snprintf(vid->base.serial, sizeof(vid->base.serial), "%s", serial);

	int fd = open(path, O_RDWR, 0);
	if (fd < 0) {
		V4L2_ERROR(vid, "Can not open '%s'", path);
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
	// clang-format off
	u_var_add_root(vid, "V4L2 Frameserver", true);
	u_var_add_ro_text(vid, vid->base.name, "Card");
	u_var_add_ro_u32(vid, &vid->ll, "Log Level");
	for (size_t i = 0; i < vid->num_states; i++) {
		u_var_add_i32(vid, &vid->states[i].want[0].value, vid->states[i].name);
	}
	// clang-format on

	v4l2_list_modes(vid);

	return &(vid->base);
}

void *
v4l2_fs_stream_run(void *ptr)
{
	struct xrt_fs *xfs = (struct xrt_fs *)ptr;
	struct v4l2_fs *vid = v4l2_fs(xfs);

	V4L2_DEBUG(vid, "info: Thread enter!");

	if (vid->fd == -1) {
		V4L2_ERROR(vid, "error: Device not opened!");
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
		V4L2_ERROR(vid, "could not set up format!");
		return NULL;
	}

	// set up our buffers - prefer userptr (client alloc) vs mmap (kernel
	// alloc)
	// TODO: using buffer caps may be better than 'fallthrough to mmap'
	struct v4l2_requestbuffers v_bufrequest;
	U_ZERO(&v_bufrequest);
	v_bufrequest.count = NUM_V4L2_BUFFERS;
	v_bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (v4l2_try_userptr(vid, &v_bufrequest) != 0 && v4l2_try_mmap(vid, &v_bufrequest) != 0) {
		V4L2_ERROR(vid, "error: Driver does not support mmap or userptr.");
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
			V4L2_ERROR(vid, "error: Could not query buffers!");
			return NULL;
		}

		if (vid->capture.userptr && v4l2_setup_userptr_buffer(vid, vf, v_buf) != 0) {
			return NULL;
		}
		if (vid->capture.mmap && v4l2_setup_mmap_buffer(vid, vf, v_buf) != 0) {
			return NULL;
		}

		// Silence valgrind.
		memset(vf->mem, 0, v_buf->length);

		// Queue this buffer
		if (ioctl(vid->fd, VIDIOC_QBUF, v_buf) < 0) {
			V4L2_ERROR(vid, "error: queueing buffer failed!");
			return NULL;
		}
	}

	int start_capture = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(vid->fd, VIDIOC_STREAMON, &start_capture) < 0) {
		V4L2_ERROR(vid, "error: Could not start capture!");
		return NULL;
	}

	/*
	 * Need to set these after we have started the stream.
	 */
	v4l2_update_controls(vid);

	struct v4l2_buffer v_buf = {0};
	v_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v_buf.memory = v_bufrequest.memory;

	while (vid->is_running) {
		if (ioctl(vid->fd, VIDIOC_DQBUF, &v_buf) < 0) {
			V4L2_ERROR(vid, "error: Dequeue failed!");
			vid->is_running = false;
			break;
		}

		v4l2_update_controls(vid);

		V4L2_TRACE(vid, "Got frame #%u, index %i", v_buf.sequence, v_buf.index);

		struct v4l2_frame *vf = &vid->frames[v_buf.index];
		struct xrt_frame *xf = NULL;

		xrt_frame_reference(&xf, &vf->base);
		uint8_t *data = (uint8_t *)vf->mem;

		//! @todo Sequence number and timestamp.
		xf->width = desc->base.width;
		xf->height = desc->base.height;
		xf->format = desc->base.format;
		xf->stereo_format = desc->base.stereo_format;

		xf->data = data + desc->offset;
		xf->stride = desc->stream.stride;
		xf->size = v_buf.bytesused - desc->offset;
		xf->source_id = vid->base.source_id;
		xf->source_sequence = v_buf.sequence;

		if ((v_buf.flags & V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC) != 0) {
			xf->timestamp = os_timeval_to_ns(&v_buf.timestamp);
		}

		vid->sink->push_frame(vid->sink, xf);

		// The frame is requeued as soon as the refcount reaches zero,
		// this can be done safely from another thread.
		xrt_frame_reference(&xf, NULL);
	}

	V4L2_DEBUG(vid, "info: Thread leave!");

	return NULL;
}


/*
 *
 * Helper debug functions.
 *
 */

static void
dump_integer(struct v4l2_fs *vid, struct v4l2_queryctrl *queryctrl)
{
	U_LOG_D("  Type: Integer");
	U_LOG_D("    min: %i, max: %i, step: %i.", queryctrl->minimum, queryctrl->maximum, queryctrl->step);
}

static void
dump_menu(struct v4l2_fs *vid, uint32_t id, uint32_t min, uint32_t max)
{
	U_LOG_D("  Menu items:");

	struct v4l2_querymenu querymenu = {0};
	querymenu.id = id;

	for (querymenu.index = min; querymenu.index <= max; querymenu.index++) {
		if (0 != ioctl(vid->fd, VIDIOC_QUERYMENU, &querymenu)) {
			U_LOG_D("    %i", querymenu.index);
			continue;
		}
		U_LOG_D("    %i: %s", querymenu.index, querymenu.name);
	}
}

static void
dump_contron_name(uint32_t id)
{
	const char *str = "ERROR";
	switch (id) {
#define CASE(CONTROL)                                                                                                  \
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

#define V_CHECK(FLAG)                                                                                                  \
	do {                                                                                                           \
		if (queryctrl.flags & V4L2_CTRL_FLAG_##FLAG) {                                                         \
			fprintf(stderr, ", " #FLAG);                                                                   \
		}                                                                                                      \
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

		U_LOG_E(" ");

		switch (queryctrl.type) {
		case V4L2_CTRL_TYPE_BOOLEAN: U_LOG_D("  Type: Boolean"); break;
		case V4L2_CTRL_TYPE_INTEGER: dump_integer(vid, &queryctrl); break;
		case V4L2_CTRL_TYPE_INTEGER64: U_LOG_D("  Type: Integer64"); break;
		case V4L2_CTRL_TYPE_BUTTON: U_LOG_D("  Type: Buttons"); break;
		case V4L2_CTRL_TYPE_MENU: dump_menu(vid, queryctrl.id, queryctrl.minimum, queryctrl.maximum); break;
		case V4L2_CTRL_TYPE_STRING: U_LOG_D("  Type: String"); break;
		default: U_LOG_D(" Type: Unknown"); break;
		}

		if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
			continue;
		}

		queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
	}
}
