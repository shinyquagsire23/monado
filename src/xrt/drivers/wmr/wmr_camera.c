// Copyright 2021, Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  WMR camera interface
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_wmr
 */
#include <asm/byteorder.h>
#include <libusb.h>
#include <stdlib.h>

#include "os/os_threading.h"

#include "wmr_protocol.h"
#include "wmr_camera.h"

#define WMR_CAM_TRACE(c, ...) U_LOG_IFL_T((c)->log_level, __VA_ARGS__)
#define WMR_CAM_DEBUG(c, ...) U_LOG_IFL_D((c)->log_level, __VA_ARGS__)
#define WMR_CAM_INFO(c, ...) U_LOG_IFL_I((c)->log_level, __VA_ARGS__)
#define WMR_CAM_WARN(c, ...) U_LOG_IFL_W((c)->log_level, __VA_ARGS__)
#define WMR_CAM_ERROR(c, ...) U_LOG_IFL_E((c)->log_level, __VA_ARGS__)

#define CAM_ENDPOINT 0x05

#define NUM_XFERS 2

struct wmr_camera_cmd
{
	__le32 magic;
	__le32 len;
	__le32 cmd;
} __attribute__((packed));

struct wmr_camera
{
	libusb_context *ctx;
	libusb_device_handle *dev;

	bool running;

	struct os_thread_helper usb_thread;
	int usb_complete;

	struct wmr_camera_config *configs;
	int n_configs;

	size_t xfer_size;
	struct libusb_transfer *xfers[NUM_XFERS];

	enum u_logging_level log_level;
};

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
static size_t
compute_transfer_size(struct wmr_camera *cam)
{
	int i, cams_found = 0;
	size_t F, n_packets, leftover, xfer_size;

	F = 26;

	for (i = 0; i < cam->n_configs; i++) {
		struct wmr_camera_config *config = cam->configs + i;
		if (config->purpose != WMR_CAMERA_PURPOSE_HEAD_TRACKING)
			continue;

		WMR_CAM_DEBUG(cam, "Found head tracking camera index %d", i);
		cams_found++;
		F += config->sensor_width * (config->sensor_height + 1);
	}

	if (cams_found == 0)
		return 0;

	n_packets = F / (0x6000 - 32);
	leftover = F - n_packets * (0x6000 - 32);

	xfer_size = n_packets * 0x6000 + 32 + leftover;

	return xfer_size;
}

static void *
wmr_cam_usb_thread(void *ptr)
{
	struct wmr_camera *cam = ptr;

	os_thread_helper_lock(&cam->usb_thread);
	while (os_thread_helper_is_running_locked(&cam->usb_thread) && !cam->usb_complete) {
		os_thread_helper_unlock(&cam->usb_thread);

		libusb_handle_events_completed(cam->ctx, &cam->usb_complete);

		os_thread_helper_lock(&cam->usb_thread);
	}

	os_thread_helper_wait_locked(&cam->usb_thread);
	os_thread_helper_unlock(&cam->usb_thread);
	return NULL;
}

static int
wmr_camera_send(struct wmr_camera *cam, uint8_t *buf, uint8_t len)
{
	struct libusb_transfer *xfer;
	uint8_t *data;

	xfer = libusb_alloc_transfer(0);
	if (xfer == NULL)
		return LIBUSB_ERROR_NO_MEM;

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
wmr_set_active(struct wmr_camera *cam, bool active)
{
	struct wmr_camera_cmd cmd = {.magic = __cpu_to_le32(WMR_MAGIC),
	                             .len = __cpu_to_le32(sizeof(struct wmr_camera_cmd)),
	                             .cmd = __cpu_to_le32(active ? 0x81 : 0x82)};

	return wmr_camera_send(cam, (uint8_t *)&cmd, sizeof(cmd));
}

struct wmr_camera *
wmr_camera_open(struct xrt_prober_device *dev_holo, enum u_logging_level ll)
{
	struct wmr_camera *cam = calloc(1, sizeof(struct wmr_camera));
	int res, i;

	cam->log_level = ll;

	if (os_thread_helper_init(&cam->usb_thread) != 0) {
		WMR_CAM_ERROR(cam, "Failed to initialise threading");
		wmr_camera_free(cam);
		return NULL;
	}

	res = libusb_init(&cam->ctx);
	if (res < 0)
		goto fail;

	cam->dev = libusb_open_device_with_vid_pid(cam->ctx, dev_holo->vendor_id, dev_holo->product_id);
	if (cam->dev == NULL)
		goto fail;

	res = libusb_claim_interface(cam->dev, 3);
	if (res < 0)
		goto fail;

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

	return cam;

fail:
	WMR_CAM_ERROR(cam, "Failed to open camera: %s", libusb_error_name(res));
	wmr_camera_free(cam);
	return NULL;
}

void
wmr_camera_free(struct wmr_camera *cam)
{
	wmr_camera_stop(cam);

	if (cam->ctx != NULL) {
		int i;

		os_thread_helper_lock(&cam->usb_thread);
		cam->usb_complete = 1;
		os_thread_helper_unlock(&cam->usb_thread);

		if (cam->dev != NULL)
			libusb_close(cam->dev);

		os_thread_helper_destroy(&cam->usb_thread);

		for (i = 0; i < NUM_XFERS; i++) {
			if (cam->xfers[i] != NULL)
				libusb_free_transfer(cam->xfers[i]);
		}

		libusb_exit(cam->ctx);
	}

	free(cam);
}

static void LIBUSB_CALL
img_xfer_cb(struct libusb_transfer *xfer)
{
	struct wmr_camera *cam = xfer->user_data;
	size_t offset, buf_len;

	if (xfer->status != LIBUSB_TRANSFER_COMPLETED) {
		WMR_CAM_TRACE(cam, "Camera transfer completed with status %u", xfer->status);
		goto out;
	}

	WMR_CAM_TRACE(cam, "Camera transfer complete - %d bytes of %d", xfer->actual_length, xfer->length);

	/* TODO: Convert the output into frames and emit them */
	buf_len = (size_t)xfer->actual_length;
	for (offset = 0; offset < buf_len; offset += 0x6000) {
		size_t avail = buf_len - offset;
		if (avail > 0x6000)
			avail = 0x6000;

		if (avail < 0x20)
			break;
	}

out:
	libusb_submit_transfer(xfer);
}

bool
wmr_camera_start(struct wmr_camera *cam, struct wmr_camera_config *cam_configs, int n_configs)
{
	int res, i;

	cam->configs = cam_configs;
	cam->n_configs = n_configs;
	cam->xfer_size = compute_transfer_size(cam);

	if (cam->xfer_size == 0) {
		WMR_CAM_WARN(cam, "No head tracking cameras found");
		goto fail;
	}

	res = wmr_set_active(cam, false);
	if (res < 0)
		goto fail;

	res = wmr_set_active(cam, true);
	if (res < 0)
		goto fail;

	for (i = 0; i < NUM_XFERS; i++) {
		uint8_t *recv_buf = malloc(cam->xfer_size);

		libusb_fill_bulk_transfer(cam->xfers[i], cam->dev, 0x85, recv_buf, cam->xfer_size, img_xfer_cb, cam, 0);
		cam->xfers[i]->flags |= LIBUSB_TRANSFER_FREE_BUFFER;

		res = libusb_submit_transfer(cam->xfers[i]);
		if (res < 0)
			goto fail;
	}

	WMR_CAM_INFO(cam, "WMR camera started");

	return true;

fail:
	if (res < 0)
		WMR_CAM_ERROR(cam, "Error starting camera input: %s", libusb_error_name(res));
	wmr_camera_stop(cam);
	return false;
}

bool
wmr_camera_stop(struct wmr_camera *cam)
{
	int res, i;

	if (!cam->running)
		return true;
	cam->running = false;

	for (i = 0; i < NUM_XFERS; i++) {
		if (cam->xfers[i] != NULL)
			libusb_cancel_transfer(cam->xfers[i]);
	}

	res = wmr_set_active(cam, false);
	if (res < 0)
		goto fail;

	WMR_CAM_INFO(cam, "WMR camera stopped");

	return true;

fail:
	if (res < 0)
		WMR_CAM_ERROR(cam, "Error stopping camera input: %s", libusb_error_name(res));
	return false;
}
