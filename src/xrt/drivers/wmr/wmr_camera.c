// Copyright 2021, Jan Schmidt
// Copyright 2021, Philipp Zabel
// Copyright 2021, Jakob Bornecrantz
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  WMR camera interface
 * @author Jan Schmidt <jan@centricular.com>
 * @author Philipp Zabel <philipp.zabel@gmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_wmr
 */
#include <asm/byteorder.h>
#include <libusb.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>

#include "math/m_api.h"
#include "os/os_threading.h"
#include "util/u_autoexpgain.h"
#include "util/u_debug.h"
#include "util/u_var.h"
#include "util/u_sink.h"
#include "util/u_frame.h"
#include "util/u_trace_marker.h"

#include "wmr_config.h"
#include "wmr_protocol.h"
#include "wmr_camera.h"

//! Specifies whether the user wants to enable autoexposure from the start.
DEBUG_GET_ONCE_BOOL_OPTION(wmr_autoexposure, "WMR_AUTOEXPOSURE", true)

//! Specifies whether the user wants to use the same exp/gain values for all cameras
DEBUG_GET_ONCE_BOOL_OPTION(wmr_unify_expgain, "WMR_UNIFY_EXPGAIN", false)

static int
update_expgain(struct wmr_camera *cam, struct xrt_frame **frames);

/*
 *
 * Defines and structs.
 *
 */

#define WMR_CAM_TRACE(c, ...) U_LOG_IFL_T((c)->log_level, __VA_ARGS__)
#define WMR_CAM_DEBUG(c, ...) U_LOG_IFL_D((c)->log_level, __VA_ARGS__)
#define WMR_CAM_INFO(c, ...) U_LOG_IFL_I((c)->log_level, __VA_ARGS__)
#define WMR_CAM_WARN(c, ...) U_LOG_IFL_W((c)->log_level, __VA_ARGS__)
#define WMR_CAM_ERROR(c, ...) U_LOG_IFL_E((c)->log_level, __VA_ARGS__)

#define CAM_ENDPOINT 0x05

#define NUM_XFERS 4

#define WMR_CAMERA_CMD_GAIN 0x80
#define WMR_CAMERA_CMD_ON 0x81
#define WMR_CAMERA_CMD_OFF 0x82

#define DEFAULT_EXPOSURE 6000
#define DEFAULT_GAIN 127

#define WMR_FRAMETYPE_SLAM 0x0
#define WMR_FRAMETYPE_CONTROLLER 0x2

#define WMR_DEBUG_SINK_SLAM 0
#define WMR_DEBUG_SINK_CONTROLLER 1

struct wmr_camera_active_cmd
{
	__le32 magic;
	__le32 len;
	__le32 cmd;
} __attribute__((packed));

struct wmr_camera_gain_cmd
{
	__le32 magic;
	__le32 len;
	__le16 cmd;
	__le16 camera_id;
	__le16 exposure;   //!< observed 60 to 6000 (but supports up to ~9000)
	__le16 gain;       //!< observed 16 to 255
	__le16 camera_id2; //!< same as camera_id
} __attribute__((packed));

struct wmr_camera
{
	libusb_context *ctx;
	libusb_device_handle *dev;

	bool running;

	struct os_thread_helper usb_thread;
	int usb_complete;

	struct wmr_camera_config tcam_confs[WMR_MAX_CAMERAS]; //!< Configs for tracking cameras
	int tcam_count;                                       //!< Number of tracking cameras
	int slam_cam_count;                                   //!< Number of tracking cameras used for SLAM

	size_t xfer_size;
	uint32_t frame_width, frame_height;
	uint8_t last_seq;
	uint64_t last_frame_ts;

	/* Unwrapped frame sequence number */
	uint64_t frame_sequence;

	struct libusb_transfer *xfers[NUM_XFERS];

	struct wmr_camera_expgain
	{
		bool manual_control; //!< Whether to control exp/gain manually or with aeg
		uint16_t last_exposure, exposure;
		uint8_t last_gain, gain;
		struct u_var_draggable_u16 exposure_ui; //! Widget to control `exposure` value
		struct u_autoexpgain *aeg;
	} ceg[WMR_MAX_CAMERAS]; //!< Camera exposure-gain control
	bool unify_expgains;    //!< Whether to use the same exposure/gain values for all cameras

	struct u_sink_debug debug_sinks[2];

	struct xrt_frame_sink *cam_sinks[WMR_MAX_CAMERAS]; //!< Downstream sinks to push tracking frames to

	enum u_logging_level log_level;
};


/*
 *
 * Helper functions.
 *
 */

/* Some WMR headsets use 616538 byte transfers. HP G2 needs 1233018 (4 cameras)
 * As a general formula, it seems we have:
 *   0x6000 byte packets. Each has a 32 byte header.
 *     packet contains frame data for each camera in turn.
 *     Each frame has an extra (first) line with metadata
 *   Then, there's an extra 26 bytes on the end.
 *
 *   F = camera frames X * (Y+1) + 26
 *   n_packets = F/(0x6000-32)
 *   leftover = F - n_packets*(0x6000-32)
 *   size = n_packets * 0x6000 + 32 + leftover,
 *
 *   so for 2 x 640x480 cameras:
 *			F = 2 * 640 * 481 + 26 = 615706
 *      n_packets = 615706 / 24544 = 25
 *      leftover = 615706 - 25 * 24544 = 2106
 *      size = 25 * 0x6000 + 32 + 2106 = 616538
 *
 *  For HP G2 = 4 x 640 * 480 cameras:
 *			F = 4 * 640 * 481 + 26 = 1231386
 *      n_packets = 1231386 / 24544 = 50
 *      leftover = 1231386 - 50 * 24544 = 4186
 *      size = 50 * 0x6000 + 32 + 4186 = 1233018
 *
 *  It would be good to test these calculations on other headsets with
 *  different camera setups.
 */
static bool
compute_frame_size(struct wmr_camera *cam)
{
	int i;
	int cams_found = 0;
	int width;
	int height;
	size_t F;
	size_t n_packets;
	size_t leftover;

	F = 26;

	for (i = 0; i < cam->tcam_count; i++) {
		const struct wmr_camera_config *config = &cam->tcam_confs[i];

		WMR_CAM_DEBUG(cam, "Found head tracking camera index %d width %d height %d", i, config->roi.extent.w,
		              config->roi.extent.h);

		if (cams_found == 0) {
			width = config->roi.extent.w;
			height = config->roi.extent.h;
		} else if (height != config->roi.extent.h) {
			WMR_CAM_ERROR(cam, "Head tracking sensors have mismatched heights - %u != %u. Please report",
			              height, config->roi.extent.h);
			return false;
		} else {
			width += config->roi.extent.w;
		}

		cams_found++;
		F += config->roi.extent.w * (config->roi.extent.h + 1);
	}

	if (cams_found == 0) {
		return false;
	}

	if (width < 1280 || height < 480) {
		return false;
	}

	n_packets = F / (0x6000 - 32);
	leftover = F - n_packets * (0x6000 - 32);

	cam->xfer_size = n_packets * 0x6000 + 32 + leftover;

	cam->frame_width = width;
	cam->frame_height = height;

	WMR_CAM_INFO(cam, "WMR camera framebuffer %u x %u - %zu transfer size", cam->frame_width, cam->frame_height,
	             cam->xfer_size);

	return true;
}

static void *
wmr_cam_usb_thread(void *ptr)
{
	U_TRACE_SET_THREAD_NAME("WMR: USB-Camera");

	struct wmr_camera *cam = ptr;

	os_thread_helper_lock(&cam->usb_thread);
	while (os_thread_helper_is_running_locked(&cam->usb_thread) && !cam->usb_complete) {
		os_thread_helper_unlock(&cam->usb_thread);

		libusb_handle_events_completed(cam->ctx, &cam->usb_complete);

		os_thread_helper_lock(&cam->usb_thread);
	}

	//! @todo Think this is not needed? what condition are we waiting for?
	os_thread_helper_wait_locked(&cam->usb_thread);
	os_thread_helper_unlock(&cam->usb_thread);

	return NULL;
}

static int
send_buffer_to_device(struct wmr_camera *cam, uint8_t *buf, uint8_t len)
{
	struct libusb_transfer *xfer;
	uint8_t *data;

	xfer = libusb_alloc_transfer(0);
	if (xfer == NULL) {
		return LIBUSB_ERROR_NO_MEM;
	}

	data = malloc(len);
	if (data == NULL) {
		libusb_free_transfer(xfer);
		return LIBUSB_ERROR_NO_MEM;
	}

	memcpy(data, buf, len);

	libusb_fill_bulk_transfer(xfer, cam->dev, CAM_ENDPOINT | LIBUSB_ENDPOINT_OUT, data, len, NULL, NULL, 0);
	xfer->flags |= LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;

	return libusb_submit_transfer(xfer);
}

static int
set_active(struct wmr_camera *cam, bool active)
{
	struct wmr_camera_active_cmd cmd = {
	    .magic = __cpu_to_le32(WMR_MAGIC),
	    .len = __cpu_to_le32(sizeof(struct wmr_camera_active_cmd)),
	    .cmd = __cpu_to_le32(active ? WMR_CAMERA_CMD_ON : WMR_CAMERA_CMD_OFF),
	};

	return send_buffer_to_device(cam, (uint8_t *)&cmd, sizeof(cmd));
}

static void LIBUSB_CALL
img_xfer_cb(struct libusb_transfer *xfer)
{
	DRV_TRACE_MARKER();

	struct wmr_camera *cam = xfer->user_data;

	if (xfer->status != LIBUSB_TRANSFER_COMPLETED) {
		WMR_CAM_DEBUG(cam, "Camera transfer completed with status: %s (%u)", libusb_error_name(xfer->status),
		              xfer->status);
		goto out;
	}

	if (xfer->actual_length < xfer->length) {
		WMR_CAM_DEBUG(cam, "Camera transfer only delivered %d bytes", xfer->actual_length);
		goto out;
	}

	WMR_CAM_TRACE(cam, "Camera transfer complete - %d bytes of %d", xfer->actual_length, xfer->length);

	/* Convert the output into frames and send them off to debug / tracking */
	struct xrt_frame *xf = NULL;

	/* There's always one extra line of pixels with exposure info */
	u_frame_create_one_off(XRT_FORMAT_L8, cam->frame_width, cam->frame_height + 1, &xf);

	const uint8_t *src = xfer->buffer;

	uint8_t *dst = xf->data;
	size_t dst_remain = xf->size;
	const size_t chunk_size = 0x6000 - 32;

	DRV_TRACE_BEGIN(copy_to_frame);
	while (dst_remain > 0) {
		const size_t to_copy = dst_remain > chunk_size ? chunk_size : dst_remain;

		/* 32 byte header seems to contain:
		 *   __be32 magic = "Dlo+"
		 *   __le32 frame_ctr;
		 *   __le32 slice_ctr;
		 *   __u8 unknown[20]; - binary block where all bytes are different each slice,
		 *                       but repeat every 8 slices. They're different each boot
		 *                       of the headset. Might just be uninitialised memory?
		 */
		src += 0x20;

		memcpy(dst, src, to_copy);
		src += to_copy;
		dst += to_copy;
		dst_remain -= to_copy;
	}
	DRV_TRACE_END(copy_to_frame);

	/* There should be exactly a 26 byte footer left over */
	assert(xfer->buffer + xfer->length - src == 26);

	/* Footer contains:
	 * __le64 start_ts; - 100ns unit timestamp, from same clock as video_timestamps on the IMU feed
	 * __le64 end_ts;   - 100ns unit timestamp, always about 111000 * 100ns later than start_ts ~= 90Hz
	 * __le16 ctr1;     - Counter that increments by 88, but sometimes by 96, and wraps at 16384
	 * __le16 unknown0  - Unknown value, has only ever been 0
	 * __be32 magic     - "Dlo+"
	 * __le16 frametype?- either 0x00 or 0x02. Every 3rd frame is 0x0, others are 0x2. Might be SLAM vs controllers?
	 */
	uint64_t frame_start_ts = read64(&src) * WMR_MS_HOLOLENS_NS_PER_TICK;
	uint64_t frame_end_ts = read64(&src) * WMR_MS_HOLOLENS_NS_PER_TICK;
	int64_t delta = frame_end_ts - frame_start_ts;

	uint16_t unknown16 = read16(&src);
	uint16_t unknown16_2 = read16(&src);
	src += 4; // Skip "Dlo+" magic bytes
	uint16_t frametype = read16(&src);
	/* frametype 0 is SLAM, frametype 2 is controller tracking */
	bool slam_tracking_frame = (frametype == WMR_FRAMETYPE_SLAM);

	WMR_CAM_TRACE(cam,
	              "Frame start TS %" PRIu64 " (%" PRIi64 " since last) end %" PRIu64 " dt %" PRIi64
	              " unknown %u %u frame type %u",
	              frame_start_ts, frame_start_ts - cam->last_frame_ts, frame_end_ts, delta, unknown16, unknown16_2,
	              frametype);

	/* Read values from the pixel header */
	uint16_t exposure = xf->data[6] << 8 | xf->data[7];
	uint8_t seq = xf->data[89];
	uint8_t seq_delta = seq - cam->last_seq;

	/* Extend the sequence number to 64-bits */
	cam->frame_sequence += seq_delta;

	WMR_CAM_TRACE(cam, "Camera frame seq %u (prev %u) -> frame %" PRIu64 " - exposure %u", seq, cam->last_seq,
	              cam->frame_sequence, exposure);

	xf->source_sequence = cam->frame_sequence;
	xf->timestamp = frame_start_ts + delta / 2;
	xf->source_timestamp = frame_start_ts;

	cam->last_frame_ts = frame_start_ts;
	cam->last_seq = seq;

	/* Push to the appropriate debug output based on frame type */
	int sink_index = slam_tracking_frame ? WMR_DEBUG_SINK_SLAM : WMR_DEBUG_SINK_CONTROLLER;
	if (u_sink_debug_is_active(&cam->debug_sinks[sink_index])) {
		u_sink_debug_push_frame(&cam->debug_sinks[sink_index], xf);
	}

	// Push to sinks
	if (slam_tracking_frame) {
		DRV_TRACE_IDENT(push_to_sinks);

		// Tracking frames usually come at ~30fps
		struct xrt_frame *frames[WMR_MAX_CAMERAS] = {NULL};
		for (int i = 0; i < cam->slam_cam_count; i++) {
			u_frame_create_roi(xf, cam->tcam_confs[i].roi, &frames[i]);
		}

		update_expgain(cam, frames);

		for (int i = 0; i < cam->slam_cam_count; i++) {
			xrt_sink_push_frame(cam->cam_sinks[i], frames[i]);
		}

		for (int i = 0; i < cam->slam_cam_count; i++) {
			xrt_frame_reference(&frames[i], NULL);
		}
	}

	xrt_frame_reference(&xf, NULL);

out:
	libusb_submit_transfer(xfer);
}


/*
 *
 * 'Exported' functions.
 *
 */

struct wmr_camera *
wmr_camera_open(struct wmr_camera_open_config *config)
{
	DRV_TRACE_MARKER();

	struct wmr_camera *cam = calloc(1, sizeof(struct wmr_camera));
	int res;
	int i;

	cam->tcam_count = config->tcam_count;
	cam->slam_cam_count = config->slam_cam_count;
	cam->log_level = config->log_level;

	for (int i = 0; i < cam->tcam_count; i++) {
		cam->tcam_confs[i] = *config->tcam_confs[i];
		cam->cam_sinks[i] = config->tcam_sinks[i];
	}

	if (os_thread_helper_init(&cam->usb_thread) != 0) {
		WMR_CAM_ERROR(cam, "Failed to initialise threading");
		wmr_camera_free(cam);
		return NULL;
	}

	res = libusb_init(&cam->ctx);
	if (res < 0) {
		goto fail;
	}

	struct xrt_prober_device *dev_holo = config->dev_holo;
	cam->dev = libusb_open_device_with_vid_pid(cam->ctx, dev_holo->vendor_id, dev_holo->product_id);
	if (cam->dev == NULL) {
		goto fail;
	}

	res = libusb_claim_interface(cam->dev, 3);
	if (res < 0) {
		goto fail;
	}

	cam->usb_complete = 0;
	if (os_thread_helper_start(&cam->usb_thread, wmr_cam_usb_thread, cam) != 0) {
		WMR_CAM_ERROR(cam, "Failed to start camera USB thread");
		goto fail;
	}

	for (i = 0; i < NUM_XFERS; i++) {
		cam->xfers[i] = libusb_alloc_transfer(0);
		if (cam->xfers[i] == NULL) {
			res = LIBUSB_ERROR_NO_MEM;
			goto fail;
		}
	}

	bool enable_aeg = debug_get_bool_option_wmr_autoexposure();
	int frame_delay = 3; // WMR takes about three frames until the cmd changes the image
	cam->unify_expgains = debug_get_bool_option_wmr_unify_expgain();

	for (int i = 0; i < cam->tcam_count; i++) {
		struct wmr_camera_expgain *ceg = &cam->ceg[i];
		ceg->manual_control = false;
		ceg->last_exposure = DEFAULT_EXPOSURE;
		ceg->exposure = DEFAULT_EXPOSURE;
		ceg->last_gain = DEFAULT_GAIN;
		ceg->gain = DEFAULT_GAIN;
		ceg->exposure_ui.val = &ceg->exposure;
		ceg->exposure_ui.max = WMR_MAX_EXPOSURE;
		ceg->exposure_ui.min = WMR_MIN_EXPOSURE;
		ceg->exposure_ui.step = 25;
		ceg->aeg = u_autoexpgain_create(U_AEG_STRATEGY_TRACKING, enable_aeg, frame_delay);
	}

	u_sink_debug_init(&cam->debug_sinks[WMR_DEBUG_SINK_SLAM]);
	u_sink_debug_init(&cam->debug_sinks[WMR_DEBUG_SINK_CONTROLLER]);
	u_var_add_root(cam, "WMR Camera", true);
	u_var_add_log_level(cam, &cam->log_level, "Log level");

	u_var_add_gui_header_begin(cam, NULL, "Camera Streams");
	u_var_add_sink_debug(cam, &cam->debug_sinks[WMR_DEBUG_SINK_SLAM], "SLAM Tracking Streams");
	u_var_add_sink_debug(cam, &cam->debug_sinks[WMR_DEBUG_SINK_CONTROLLER], "Controller Tracking Streams");
	u_var_add_gui_header_end(cam, NULL, NULL);

	u_var_add_gui_header_begin(cam, NULL, "Exposure and gain control");
	u_var_add_bool(cam, &cam->unify_expgains, "Use same values");

	for (int i = 0; i < cam->tcam_count; i++) {
		struct wmr_camera_expgain *ceg = &cam->ceg[i];
		char label[256] = {0};

		(void)snprintf(label, sizeof(label), "Control for camera %d", i);
		u_var_add_gui_header_begin(cam, NULL, label);

		(void)snprintf(label, sizeof(label), "[%d] Manual exposure and gain control", i);
		u_var_add_bool(cam, &ceg->manual_control, label);

		(void)snprintf(label, sizeof(label), "[%d] Exposure (usec)", i);
		u_var_add_draggable_u16(cam, &ceg->exposure_ui, label);

		(void)snprintf(label, sizeof(label), "[%d] Gain", i);
		u_var_add_u8(cam, &ceg->gain, label);

		(void)snprintf(label, sizeof(label), "[%d] ", i);
		u_autoexpgain_add_vars(ceg->aeg, cam, label);

		u_var_add_gui_header_end(cam, NULL, NULL);
	}

	u_var_add_gui_header_end(cam, NULL, "Auto exposure and gain control END");

	return cam;

fail:
	WMR_CAM_ERROR(cam, "Failed to open camera: %s", libusb_error_name(res));
	wmr_camera_free(cam);
	return NULL;
}

void
wmr_camera_free(struct wmr_camera *cam)
{
	DRV_TRACE_MARKER();

	// Stop the camera.
	wmr_camera_stop(cam);

	if (cam->ctx != NULL) {
		int i;

		os_thread_helper_lock(&cam->usb_thread);
		cam->usb_complete = 1;
		os_thread_helper_unlock(&cam->usb_thread);

		if (cam->dev != NULL) {
			libusb_close(cam->dev);
		}

		os_thread_helper_destroy(&cam->usb_thread);

		for (i = 0; i < NUM_XFERS; i++) {
			if (cam->xfers[i] == NULL) {
				continue;
			}

			libusb_free_transfer(cam->xfers[i]);
			cam->xfers[i] = NULL;
		}

		libusb_exit(cam->ctx);
		cam->ctx = NULL;
	}

	// Tidy the variable tracking.
	u_var_remove_root(cam);
	u_sink_debug_destroy(&cam->debug_sinks[WMR_DEBUG_SINK_SLAM]);
	u_sink_debug_destroy(&cam->debug_sinks[WMR_DEBUG_SINK_CONTROLLER]);

	free(cam);
}

bool
wmr_camera_start(struct wmr_camera *cam)
{
	DRV_TRACE_MARKER();

	int res = 0;

	if (!compute_frame_size(cam)) {
		WMR_CAM_WARN(cam, "Invalid config or no head tracking cameras found");
		goto fail;
	}

	res = set_active(cam, false);
	if (res < 0) {
		goto fail;
	}

	res = set_active(cam, true);
	if (res < 0) {
		goto fail;
	}

	res = update_expgain(cam, NULL);
	if (res < 0) {
		goto fail;
	}

	for (int i = 0; i < NUM_XFERS; i++) {
		uint8_t *recv_buf = malloc(cam->xfer_size);

		libusb_fill_bulk_transfer(cam->xfers[i], cam->dev, LIBUSB_ENDPOINT_IN | 5, recv_buf, cam->xfer_size,
		                          img_xfer_cb, cam, 0);
		cam->xfers[i]->flags |= LIBUSB_TRANSFER_FREE_BUFFER;

		res = libusb_submit_transfer(cam->xfers[i]);
		if (res < 0) {
			goto fail;
		}
	}

	WMR_CAM_INFO(cam, "WMR camera started");

	return true;


fail:
	if (res < 0) {
		WMR_CAM_ERROR(cam, "Error starting camera input: %s", libusb_error_name(res));
	}

	wmr_camera_stop(cam);

	return false;
}

bool
wmr_camera_stop(struct wmr_camera *cam)
{
	DRV_TRACE_MARKER();

	int res;
	int i;

	if (!cam->running) {
		return true;
	}
	cam->running = false;

	for (i = 0; i < NUM_XFERS; i++) {
		if (cam->xfers[i] != NULL) {
			libusb_cancel_transfer(cam->xfers[i]);
		}
	}

	res = set_active(cam, false);
	if (res < 0) {
		goto fail;
	}

	WMR_CAM_INFO(cam, "WMR camera stopped");

	return true;


fail:
	if (res < 0) {
		WMR_CAM_ERROR(cam, "Error stopping camera input: %s", libusb_error_name(res));
	}

	return false;
}

static int
update_expgain(struct wmr_camera *cam, struct xrt_frame **frames)
{
	int res = 0;
	for (int i = 0; i < cam->tcam_count; i++) {
		const struct wmr_camera_config *config = &cam->tcam_confs[i];

		struct wmr_camera_expgain *ceg = &cam->ceg[i];

		if (!ceg->manual_control && frames != NULL && frames[i] != NULL) {
			if (!cam->unify_expgains || i == 0) {
				u_autoexpgain_update(ceg->aeg, frames[i]);
				ceg->exposure = (uint16_t)u_autoexpgain_get_exposure(ceg->aeg);
				ceg->gain = (uint8_t)u_autoexpgain_get_gain(ceg->aeg);
			} else {
				ceg->exposure = cam->ceg[0].exposure;
				ceg->gain = cam->ceg[0].gain;
			}
		}

		if (ceg->last_exposure == ceg->exposure && ceg->last_gain == ceg->gain) {
			continue;
		}
		ceg->last_exposure = ceg->exposure;
		ceg->last_gain = ceg->gain;

		bool status = wmr_camera_set_exposure_gain(cam, config->location, ceg->exposure, ceg->gain);
		if (status != 0) {
			WMR_CAM_ERROR(cam, "Failed to set exposure and gain for camera %d", i);
		}
		res |= status;
	}
	return res;
}

int
wmr_camera_set_exposure_gain(struct wmr_camera *cam, uint8_t camera_id, uint16_t exposure, uint8_t gain)
{
	DRV_TRACE_MARKER();

	WMR_CAM_TRACE(cam, "Setting camera %d exposure %u gain %u", camera_id, exposure, gain);
	struct wmr_camera_gain_cmd cmd = {
	    .magic = __cpu_to_le32(WMR_MAGIC),
	    .len = __cpu_to_le32(sizeof(struct wmr_camera_gain_cmd)),
	    .cmd = __cpu_to_le16(WMR_CAMERA_CMD_GAIN),
	    .camera_id = __cpu_to_le16(camera_id),
	    .exposure = __cpu_to_le16(exposure),
	    .gain = __cpu_to_le16(gain),
	    .camera_id2 = __cpu_to_le16(camera_id),
	};

	return send_buffer_to_device(cam, (uint8_t *)&cmd, sizeof(cmd));
}
