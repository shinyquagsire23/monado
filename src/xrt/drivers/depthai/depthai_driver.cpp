// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  DepthAI frameserver implementation.
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_depthai
 */

#include "os/os_time.h"
#include "os/os_threading.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_frame.h"
#include "util/u_format.h"
#include "util/u_logging.h"

#include "depthai_interface.h"

#include "depthai/depthai.hpp"

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>

#include <memory>
#include <sstream>


/*
 *
 * Printing functions.
 *
 */

#define DEPTHAI_TRACE(d, ...) U_LOG_IFL_T(d->ll, __VA_ARGS__)
#define DEPTHAI_DEBUG(d, ...) U_LOG_IFL_D(d->ll, __VA_ARGS__)
#define DEPTHAI_INFO(d, ...) U_LOG_IFL_I(d->ll, __VA_ARGS__)
#define DEPTHAI_WARN(d, ...) U_LOG_IFL_W(d->ll, __VA_ARGS__)
#define DEPTHAI_ERROR(d, ...) U_LOG_IFL_E(d->ll, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(depthai_log, "DEPTHAI_LOG", U_LOGGING_WARN)


/*
 *
 * Helper frame wrapper code.
 *
 */

class depthairameWrapper
{
public:
	struct xrt_frame frame = {};

	std::shared_ptr<dai::ImgFrame> depthai_frame = {};
};

extern "C" void
depthai_frame_wrapper_destroy(struct xrt_frame *xf)
{
	depthairameWrapper *dfw = (depthairameWrapper *)xf;
	delete dfw;
}


/*
 *
 * DepthAI frameserver.
 *
 */

struct depthai_fs
{
	struct xrt_fs base;
	struct xrt_frame_node node;
	struct os_thread_helper play_thread;

	u_logging_level ll;

	uint32_t width;
	uint32_t height;
	xrt_format format;

	xrt_frame_sink *sink;

	dai::Device *device;
	dai::DataOutputQueue *queue;

	dai::ColorCameraProperties::SensorResolution color_sensor_resoultion;
	dai::ColorCameraProperties::ColorOrder color_order;

	dai::MonoCameraProperties::SensorResolution mono_sensor_resoultion;
	dai::CameraBoardSocket camera_board_socket;

	dai::CameraImageOrientation image_orientation;
	uint32_t fps;
	bool interleaved;
};


/*
 *
 * Internal functions.
 *
 */

static void
depthai_print_connected_cameras(struct depthai_fs *depthai)
{
	std::ostringstream oss = {};
	for (const auto &cam : depthai->device->getConnectedCameras()) {
		oss << "'" << static_cast<int>(cam) << "' ";
	}
	std::string str = oss.str();

	DEPTHAI_DEBUG(depthai, "DepthAI: Connected cameras: %s", str.c_str());
}

static void
depthai_do_one_frame(struct depthai_fs *depthai)
{
	std::shared_ptr<dai::ImgFrame> imgFrame = depthai->queue->get<dai::ImgFrame>();
	if (!imgFrame) {
		std::cout << "Not ImgFrame" << std::endl;
		return; // Nothing to do.
	}

	// Get the timestamp.
	auto duration = imgFrame->getTimestamp().time_since_epoch();
	auto nano = std::chrono::duration_cast<std::chrono::duration<int64_t, std::nano>>(duration);
	uint64_t timestamp_ns = nano.count();

	// Create a wrapper that will keep the frame alive as long as the frame was alive.
	depthairameWrapper *dfw = new depthairameWrapper();
	dfw->depthai_frame = imgFrame;

	// Fill in all of the data.
	struct xrt_frame *xf = &dfw->frame;
	xf->reference.count = 1;
	xf->destroy = depthai_frame_wrapper_destroy;
	xf->width = depthai->width;
	xf->height = depthai->height;
	xf->format = depthai->format;
	xf->timestamp = timestamp_ns;
	xf->data = imgFrame->getData().data();

	// Calculate stride and size, assuming tightly packed rows.
	u_format_size_for_dimensions(xf->format, xf->width, xf->height, &xf->stride, &xf->size);

	// Push the frame to the sink.
	xrt_sink_push_frame(depthai->sink, xf);

	// If downstream wants to keep the frame they would have referenced it.
	xrt_frame_reference(&xf, NULL);
}

static void *
depthai_mainloop(void *ptr)
{
	struct depthai_fs *depthai = (struct depthai_fs *)ptr;
	DEPTHAI_DEBUG(depthai, "DepthAI: Mainloop called");

	os_thread_helper_lock(&depthai->play_thread);
	while (os_thread_helper_is_running_locked(&depthai->play_thread)) {
		os_thread_helper_unlock(&depthai->play_thread);

		depthai_do_one_frame(depthai);

		// Need to lock the thread when we go back to the while condition.
		os_thread_helper_lock(&depthai->play_thread);
	}
	os_thread_helper_unlock(&depthai->play_thread);

	DEPTHAI_DEBUG(depthai, "DepthAI: Mainloop exiting");
	return nullptr;
}

static bool
depthai_destroy(struct depthai_fs *depthai)
{
	DEPTHAI_DEBUG(depthai, "DepthAI: Frameserver destroy called");

	os_thread_helper_destroy(&depthai->play_thread);

	delete depthai->device;

	free(depthai);

	return true;
}


/*
 *
 * Frame server functions.
 *
 */

/*!
 * Cast to derived type.
 */
static inline struct depthai_fs *
depthai_fs(struct xrt_fs *xfs)
{
	return (struct depthai_fs *)xfs;
}

static bool
depthai_fs_enumerate_modes(struct xrt_fs *xfs, struct xrt_fs_mode **out_modes, uint32_t *out_count)
{
	struct depthai_fs *depthai = depthai_fs(xfs);
	DEPTHAI_DEBUG(depthai, "DepthAI: Enumerate modes called");

	struct xrt_fs_mode *modes = U_TYPED_ARRAY_CALLOC(struct xrt_fs_mode, 1);
	if (modes == NULL) {
		return false;
	}

	modes[0].width = depthai->width;
	modes[0].height = depthai->height;
	modes[0].format = depthai->format;
	modes[0].stereo_format = XRT_STEREO_FORMAT_NONE;

	*out_modes = modes;
	*out_count = 1;

	return true;
}

static bool
depthai_fs_configure_capture(struct xrt_fs *xfs, struct xrt_fs_capture_parameters *cp)
{
	struct depthai_fs *depthai = depthai_fs(xfs);
	DEPTHAI_DEBUG(depthai, "DepthAI: Configure capture called");

	// Noop
	return false;
}

static bool
depthai_fs_stream_start(struct xrt_fs *xfs,
                        struct xrt_frame_sink *xs,
                        enum xrt_fs_capture_type capture_type,
                        uint32_t descriptor_index)
{
	struct depthai_fs *depthai = depthai_fs(xfs);
	DEPTHAI_DEBUG(depthai, "DepthAI: Stream start called");

	assert(descriptor_index == 0);
	(void)capture_type; // Don't care about this one just yet.

	depthai->sink = xs;

	os_thread_helper_start(&depthai->play_thread, depthai_mainloop, depthai);

	return true;
}

static bool
depthai_fs_stream_stop(struct xrt_fs *xfs)
{
	struct depthai_fs *depthai = depthai_fs(xfs);
	DEPTHAI_DEBUG(depthai, "DepthAI: Stream stop called");

	// This call fully stops the thread.
	os_thread_helper_stop(&depthai->play_thread);

	return true;
}

static bool
depthai_fs_is_running(struct xrt_fs *xfs)
{
	struct depthai_fs *depthai = depthai_fs(xfs);

	os_thread_helper_lock(&depthai->play_thread);
	bool running = os_thread_helper_is_running_locked(&depthai->play_thread);
	os_thread_helper_unlock(&depthai->play_thread);

	return running;
}


/*
 *
 * Node functions.
 *
 */

static void
depthai_fs_node_break_apart(struct xrt_frame_node *node)
{
	struct depthai_fs *depthai = container_of(node, struct depthai_fs, node);
	DEPTHAI_DEBUG(depthai, "DepthAI: Node break apart called");

	depthai_fs_stream_stop(&depthai->base);
}

static void
depthai_fs_node_destroy(struct xrt_frame_node *node)
{
	struct depthai_fs *depthai = container_of(node, struct depthai_fs, node);
	DEPTHAI_DEBUG(depthai, "DepthAI: Node destroy called");

	// Safe to call, break apart have already stopped the stream.
	depthai_destroy(depthai);
}


/*
 *
 * 'Exported' functions.
 *
 */

extern "C" struct xrt_fs *
depthai_fs_single_rgb(struct xrt_frame_context *xfctx)
{
	// Try to create a device and see if that fail first.
	dai::Device *d;
	try {
		d = new dai::Device();
	} catch (std::exception &e) {
		std::string what = e.what();
		U_LOG_E("DepthAI error: %s", what.c_str());
		return nullptr;
	}

	struct depthai_fs *depthai = U_TYPED_CALLOC(struct depthai_fs);
	depthai->base.enumerate_modes = depthai_fs_enumerate_modes;
	depthai->base.configure_capture = depthai_fs_configure_capture;
	depthai->base.stream_start = depthai_fs_stream_start;
	depthai->base.stream_stop = depthai_fs_stream_stop;
	depthai->base.is_running = depthai_fs_is_running;
	depthai->node.break_apart = depthai_fs_node_break_apart;
	depthai->node.destroy = depthai_fs_node_destroy;
	depthai->ll = debug_get_log_option_depthai_log();
	depthai->device = d;

	if (true) {
		depthai->width = 1280;
		depthai->height = 800;
		depthai->format = XRT_FORMAT_R8G8B8;
		depthai->color_sensor_resoultion = dai::ColorCameraProperties::SensorResolution::THE_800_P;
		depthai->image_orientation = dai::CameraImageOrientation::ROTATE_180_DEG;
		depthai->fps = 60; // 120?
		depthai->interleaved = true;
		depthai->color_order = dai::ColorCameraProperties::ColorOrder::RGB;
	} else if (false) {
		depthai->width = 1920;
		depthai->height = 1080;
		depthai->format = XRT_FORMAT_R8G8B8;
		depthai->color_sensor_resoultion = dai::ColorCameraProperties::SensorResolution::THE_1080_P;
		depthai->image_orientation = dai::CameraImageOrientation::AUTO;
		depthai->fps = 118; // Actual max.
		depthai->interleaved = true;
		depthai->color_order = dai::ColorCameraProperties::ColorOrder::RGB;
	} else if (false) {
		depthai->width = 1280;
		depthai->height = 800;
		depthai->format = XRT_FORMAT_L8;
		depthai->camera_board_socket = dai::CameraBoardSocket::RIGHT;
		depthai->mono_sensor_resoultion = dai::MonoCameraProperties::SensorResolution::THE_800_P;
		depthai->image_orientation = dai::CameraImageOrientation::AUTO;
		depthai->fps = 60; // 120?
	} else {
		depthai->width = 1280;
		depthai->height = 800;
		depthai->format = XRT_FORMAT_L8;
		depthai->camera_board_socket = dai::CameraBoardSocket::LEFT;
		depthai->mono_sensor_resoultion = dai::MonoCameraProperties::SensorResolution::THE_800_P;
		depthai->image_orientation = dai::CameraImageOrientation::AUTO;
		depthai->fps = 60; // 120?
	}

	// Some debug printing.
	depthai_print_connected_cameras(depthai);

	// Make sure that the thread helper is initialised.
	os_thread_helper_init(&depthai->play_thread);

	dai::Pipeline p = {};

	auto xlinkOut = p.create<dai::node::XLinkOut>();
	xlinkOut->setStreamName("preview");

	std::shared_ptr<dai::node::ColorCamera> colorCam = nullptr;
	std::shared_ptr<dai::node::MonoCamera> monoCam = nullptr;

	if (depthai->format == XRT_FORMAT_R8G8B8) {
		colorCam = p.create<dai::node::ColorCamera>();
		colorCam->setPreviewSize(depthai->width, depthai->height);
		colorCam->setResolution(depthai->color_sensor_resoultion);
		colorCam->setImageOrientation(depthai->image_orientation);
		colorCam->setInterleaved(depthai->interleaved);
		colorCam->setFps(depthai->fps);
		colorCam->setColorOrder(depthai->color_order);

		// Link plugins CAM -> XLINK
		colorCam->preview.link(xlinkOut->input);
	}

	if (depthai->format == XRT_FORMAT_L8) {
		monoCam = p.create<dai::node::MonoCamera>();
		monoCam->setBoardSocket(depthai->camera_board_socket);
		monoCam->setResolution(depthai->mono_sensor_resoultion);
		monoCam->setImageOrientation(depthai->image_orientation);
		monoCam->setFps(depthai->fps);

		// Link plugins CAM -> XLINK
		monoCam->out.link(xlinkOut->input);
	}


	// Start the pipeline
	d->startPipeline(p);
	depthai->queue = d->getOutputQueue("preview", 1, false).get(); // out of shared pointer

	xrt_frame_context_add(xfctx, &depthai->node);

	DEPTHAI_DEBUG(depthai, "DepthAI: Created");

	return &depthai->base;
}
