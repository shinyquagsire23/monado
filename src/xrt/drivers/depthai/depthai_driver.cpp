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

#include "xrt/xrt_tracking.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_frame.h"
#include "util/u_format.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"

#include "tracking/t_tracking.h"

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

#define DEPTHAI_TRACE(d, ...) U_LOG_IFL_T(d->log_level, __VA_ARGS__)
#define DEPTHAI_DEBUG(d, ...) U_LOG_IFL_D(d->log_level, __VA_ARGS__)
#define DEPTHAI_INFO(d, ...) U_LOG_IFL_I(d->log_level, __VA_ARGS__)
#define DEPTHAI_WARN(d, ...) U_LOG_IFL_W(d->log_level, __VA_ARGS__)
#define DEPTHAI_ERROR(d, ...) U_LOG_IFL_E(d->log_level, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(depthai_log, "DEPTHAI_LOG", U_LOGGING_INFO)


/*
 *
 * Helper frame wrapper code.
 *
 */

extern "C" void
depthai_frame_wrapper_destroy(struct xrt_frame *xf);

/*!
 * Manage dai::ImgFrame life-time.
 */
class DepthAIFrameWrapper
{
public:
	struct xrt_frame frame = {};

	std::shared_ptr<dai::ImgFrame> depthai_frame = {};


public:
	DepthAIFrameWrapper(std::shared_ptr<dai::ImgFrame> depthai_frame)
	{
		this->frame.reference.count = 1;
		this->frame.destroy = depthai_frame_wrapper_destroy;
		this->depthai_frame = depthai_frame;
	}
};

extern "C" void
depthai_frame_wrapper_destroy(struct xrt_frame *xf)
{
	DepthAIFrameWrapper *dfw = (DepthAIFrameWrapper *)xf;
	delete dfw;
}


/*
 *
 * DepthAI frameserver.
 *
 */

enum depthai_camera_type
{
	RGB_IMX_378,
	RGB_OV_9782,
	GRAY_OV_9282_L,
	GRAY_OV_9282_R,
};

/*!
 * DepthAI frameserver support the Luxonis Oak devices.
 *
 * @ingroup drv_depthai
 */
struct depthai_fs
{
	struct xrt_fs base;
	struct xrt_frame_node node;
	struct os_thread_helper play_thread;

	u_logging_level log_level;

	uint32_t width;
	uint32_t height;
	xrt_format format;

	// Sink:, RGB, Left, Right, CamC.
	xrt_frame_sink *sink[4];

	dai::Device *device;
	dai::DataOutputQueue *queue;

	dai::ColorCameraProperties::SensorResolution color_sensor_resoultion;
	dai::ColorCameraProperties::ColorOrder color_order;

	dai::MonoCameraProperties::SensorResolution grayscale_sensor_resolution;
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

static bool
depthai_get_gray_cameras_calibration(struct depthai_fs *depthai, struct t_stereo_camera_calibration **c_ptr)
{
	/*
	 * Read out values.
	 */

	std::vector<std::vector<float>> extrinsics = {};
	struct
	{
		std::vector<std::vector<float>> intrinsics = {};
		std::vector<float> distortion = {};
		int width = -1, height = -1;
	} left, right = {};


	/*
	 * Get data.
	 */

	// Try to create a device and see if that fail first.
	dai::CalibrationHandler calibData;
	try {
		calibData = depthai->device->readCalibration();
		std::tie(left.intrinsics, left.width, left.height) =
		    calibData.getDefaultIntrinsics(dai::CameraBoardSocket::LEFT);
		std::tie(right.intrinsics, right.width, right.height) =
		    calibData.getDefaultIntrinsics(dai::CameraBoardSocket::RIGHT);
		left.distortion = calibData.getDistortionCoefficients(dai::CameraBoardSocket::LEFT);
		right.distortion = calibData.getDistortionCoefficients(dai::CameraBoardSocket::RIGHT);
		extrinsics = calibData.getCameraExtrinsics(dai::CameraBoardSocket::LEFT, dai::CameraBoardSocket::RIGHT);
	} catch (std::exception &e) {
		std::string what = e.what();
		U_LOG_E("DepthAI error: %s", what.c_str());
		return false;
	}


	/*
	 * Copy to the Monado calibration struct.
	 */

	const uint32_t num_dist = 14;
	struct t_stereo_camera_calibration *c = NULL;
	t_stereo_camera_calibration_alloc(&c, num_dist);

	// Copy intrinsics
	c->view[0].image_size_pixels.w = left.width;
	c->view[0].image_size_pixels.h = left.height;
	c->view[1].image_size_pixels.w = right.width;
	c->view[1].image_size_pixels.h = right.height;
	for (uint32_t row = 0; row < 3; row++) {
		for (uint32_t col = 0; col < 3; col++) {
			c->view[0].intrinsics[row][col] = left.intrinsics[row][col];
			c->view[1].intrinsics[row][col] = right.intrinsics[row][col];
		}
	}

	// Copy distortion
	c->view[0].use_fisheye = false;
	c->view[1].use_fisheye = false;
	for (uint32_t i = 0; i < num_dist; i++) {
		c->view[0].distortion[i] = left.distortion[i];
		c->view[1].distortion[i] = right.distortion[i];
	}

	// Copy translation
	for (uint32_t i = 0; i < 3; i++) {
		c->camera_translation[i] = extrinsics[i][3];
	}

	// Copy rotation
	for (uint32_t row = 0; row < 3; row++) {
		for (uint32_t col = 0; col < 3; col++) {
			c->camera_rotation[row][col] = extrinsics[row][col];
		}
	}

	// To properly handle ref counting.
	t_stereo_camera_calibration_reference(c_ptr, c);
	t_stereo_camera_calibration_reference(&c, NULL);

	return true;
}

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
depthai_print_calib(struct depthai_fs *depthai)
{
	if (depthai->log_level > U_LOGGING_DEBUG) {
		return;
	}

	struct t_stereo_camera_calibration *c = NULL;

	if (!depthai_get_gray_cameras_calibration(depthai, &c)) {
		return;
	}

	t_stereo_camera_calibration_dump(c);
	t_stereo_camera_calibration_reference(&c, NULL);
}

static void
depthai_do_one_frame(struct depthai_fs *depthai)
{
	std::shared_ptr<dai::ImgFrame> imgFrame = depthai->queue->get<dai::ImgFrame>();
	if (!imgFrame) {
		std::cout << "Not ImgFrame" << std::endl;
		return; // Nothing to do.
	}

	// Trace-marker here for timing after we have gotten a frame.
	SINK_TRACE_IDENT(depthai_frame);


	// Get the timestamp.
	auto duration = imgFrame->getTimestamp().time_since_epoch();
	uint32_t num = imgFrame->getInstanceNum();
	auto nano = std::chrono::duration_cast<std::chrono::duration<int64_t, std::nano>>(duration);
	uint64_t timestamp_ns = nano.count();

	// Sanity check.
	if (num >= ARRAY_SIZE(depthai->sink)) {
		DEPTHAI_ERROR(depthai, "Instance number too large! (%u)", num);
		return;
	}

	if (depthai->sink[num] == nullptr) {
		DEPTHAI_ERROR(depthai, "No sink waiting for frame! (%u)", num);
		return;
	}

	// Create a wrapper that will keep the frame alive as long as the frame was alive.
	DepthAIFrameWrapper *dfw = new DepthAIFrameWrapper(imgFrame);

	// Fill in all of the data.
	struct xrt_frame *xf = &dfw->frame;
	xf->width = depthai->width;
	xf->height = depthai->height;
	xf->format = depthai->format;
	xf->timestamp = timestamp_ns;
	xf->data = imgFrame->getData().data();

	// Calculate stride and size, assuming tightly packed rows.
	u_format_size_for_dimensions(xf->format, xf->width, xf->height, &xf->stride, &xf->size);

	// Push the frame to the sink.
	xrt_sink_push_frame(depthai->sink[num], xf);

	// If downstream wants to keep the frame they would have referenced it.
	xrt_frame_reference(&xf, NULL);
}

static void *
depthai_mainloop(void *ptr)
{
	SINK_TRACE_MARKER();

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

	depthai->queue->close();
	delete depthai->device;

	free(depthai);

	return true;
}

static void
depthai_setup_monocular_pipeline(struct depthai_fs *depthai, enum depthai_camera_type camera_type)
{
	switch (camera_type) {
#if 0
	case (RGB_OV_9782):
		depthai->width = 1280;
		depthai->height = 800;
		depthai->format = XRT_FORMAT_R8G8B8;
		depthai->color_sensor_resoultion = dai::ColorCameraProperties::SensorResolution::THE_800_P;
		depthai->image_orientation = dai::CameraImageOrientation::ROTATE_180_DEG;
		depthai->fps = 60; // Currently only supports 60.
		depthai->interleaved = true;
		depthai->color_order = dai::ColorCameraProperties::ColorOrder::RGB;
		break;
#endif
	case (RGB_IMX_378):
		depthai->width = 1920;
		depthai->height = 1080;
		depthai->format = XRT_FORMAT_R8G8B8;
		depthai->color_sensor_resoultion = dai::ColorCameraProperties::SensorResolution::THE_1080_P;
		depthai->image_orientation = dai::CameraImageOrientation::AUTO;
		depthai->fps = 118; // Actual max.
		depthai->interleaved = true;
		depthai->color_order = dai::ColorCameraProperties::ColorOrder::RGB;
		break;
	case (GRAY_OV_9282_L):
		depthai->width = 1280;
		depthai->height = 800;
		depthai->format = XRT_FORMAT_L8;
		depthai->camera_board_socket = dai::CameraBoardSocket::LEFT;
		depthai->grayscale_sensor_resolution = dai::MonoCameraProperties::SensorResolution::THE_800_P;
		depthai->image_orientation = dai::CameraImageOrientation::AUTO;
		depthai->fps = 60; // Currently only supports 60.
		break;
	case (GRAY_OV_9282_R):
		depthai->width = 1280;
		depthai->height = 800;
		depthai->format = XRT_FORMAT_L8;
		depthai->camera_board_socket = dai::CameraBoardSocket::RIGHT;
		depthai->grayscale_sensor_resolution = dai::MonoCameraProperties::SensorResolution::THE_800_P;
		depthai->image_orientation = dai::CameraImageOrientation::AUTO;
		depthai->fps = 60; // Currently only supports 60.
		break;
	default: assert(false);
	}

	dai::Pipeline p = {};

	auto xlinkOut = p.create<dai::node::XLinkOut>();
	xlinkOut->setStreamName("preview");

	std::shared_ptr<dai::node::ColorCamera> colorCam = nullptr;
	std::shared_ptr<dai::node::MonoCamera> grayCam = nullptr;

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
		grayCam = p.create<dai::node::MonoCamera>();
		grayCam->setBoardSocket(depthai->camera_board_socket);
		grayCam->setResolution(depthai->grayscale_sensor_resolution);
		grayCam->setImageOrientation(depthai->image_orientation);
		grayCam->setFps(depthai->fps);

		// Link plugins CAM -> XLINK
		grayCam->out.link(xlinkOut->input);
	}


	// Start the pipeline
	depthai->device->startPipeline(p);
	depthai->queue = depthai->device->getOutputQueue("preview", 1, false).get(); // out of shared pointer
}

static void
depthai_setup_stereo_pipeline(struct depthai_fs *depthai)
{
	// Hardcoded to OV_9282 L/R
	depthai->width = 1280;
	depthai->height = 800;
	depthai->format = XRT_FORMAT_L8;
	depthai->camera_board_socket = dai::CameraBoardSocket::LEFT;
	depthai->grayscale_sensor_resolution = dai::MonoCameraProperties::SensorResolution::THE_800_P;
	depthai->image_orientation = dai::CameraImageOrientation::AUTO;
	depthai->fps = 60; // Currently only supports 60.

	dai::Pipeline p = {};

	const char *name = "frames";
	std::shared_ptr<dai::node::XLinkOut> xlinkOut = p.create<dai::node::XLinkOut>();
	xlinkOut->setStreamName(name);

	dai::CameraBoardSocket sockets[2] = {
	    dai::CameraBoardSocket::LEFT,
	    dai::CameraBoardSocket::RIGHT,
	};

	for (int i = 0; i < 2; i++) {
		std::shared_ptr<dai::node::MonoCamera> grayCam = nullptr;

		grayCam = p.create<dai::node::MonoCamera>();
		grayCam->setBoardSocket(sockets[i]);
		grayCam->setResolution(depthai->grayscale_sensor_resolution);
		grayCam->setImageOrientation(depthai->image_orientation);
		grayCam->setFps(depthai->fps);

		// Link plugins CAM -> XLINK
		grayCam->out.link(xlinkOut->input);
	}

	// Start the pipeline
	depthai->device->startPipeline(p);
	depthai->queue = depthai->device->getOutputQueue(name, 4, false).get(); // out of shared pointer
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

	depthai->sink[0] = xs; // 0 == CamA-4L / RGB
	depthai->sink[1] = xs; // 1 == CamB-2L / Left Gray
	depthai->sink[2] = xs; // 2 == CamC-2L / Right Gray
	depthai->sink[3] = xs; // 3 == CamD-4L

	os_thread_helper_start(&depthai->play_thread, depthai_mainloop, depthai);

	return true;
}

static bool
depthai_fs_slam_stream_start(struct xrt_fs *xfs, struct xrt_slam_sinks *sinks)
{
	struct depthai_fs *depthai = depthai_fs(xfs);
	DEPTHAI_DEBUG(depthai, "DepthAI: SLAM stream start called");

	depthai->sink[0] = nullptr;      // 0 == CamA-4L / RGB
	depthai->sink[1] = sinks->left;  // 1 == CamB-2L / Left Gray
	depthai->sink[2] = sinks->right; // 2 == CamC-2L / Right Gray
	depthai->sink[3] = nullptr;      // 3 == CamD-4L

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
 * Create function, needs to be last.
 *
 */

static struct depthai_fs *
depthai_create_and_do_minimal_setup(void)
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
	depthai->base.slam_stream_start = depthai_fs_slam_stream_start;
	depthai->base.stream_stop = depthai_fs_stream_stop;
	depthai->base.is_running = depthai_fs_is_running;
	depthai->node.break_apart = depthai_fs_node_break_apart;
	depthai->node.destroy = depthai_fs_node_destroy;
	depthai->log_level = debug_get_log_option_depthai_log();
	depthai->device = d;

	// Some debug printing.
	depthai_print_connected_cameras(depthai);
	depthai_print_calib(depthai);

	// Make sure that the thread helper is initialised.
	os_thread_helper_init(&depthai->play_thread);

	return depthai;
}


/*
 *
 * 'Exported' functions.
 *
 */

extern "C" struct xrt_fs *
depthai_fs_monocular_rgb(struct xrt_frame_context *xfctx)
{
	struct depthai_fs *depthai = depthai_create_and_do_minimal_setup();
	if (depthai == nullptr) {
		return nullptr;
	}

	// Currently hardcoded to the default Oak-D camera.
	enum depthai_camera_type camera_type = RGB_IMX_378;

	// Last bit is to setup the pipeline.
	depthai_setup_monocular_pipeline(depthai, camera_type);

	// And finally add us to the context when we are done.
	xrt_frame_context_add(xfctx, &depthai->node);

	DEPTHAI_DEBUG(depthai, "DepthAI: Created");

	return &depthai->base;
}

extern "C" struct xrt_fs *
depthai_fs_stereo_grayscale(struct xrt_frame_context *xfctx)
{
	struct depthai_fs *depthai = depthai_create_and_do_minimal_setup();
	if (depthai == nullptr) {
		return nullptr;
	}

	// Last bit is to setup the pipeline.
	depthai_setup_stereo_pipeline(depthai);

	// And finally add us to the context when we are done.
	xrt_frame_context_add(xfctx, &depthai->node);

	DEPTHAI_DEBUG(depthai, "DepthAI: Created");

	return &depthai->base;
}

extern "C" bool
depthai_fs_get_stereo_calibration(struct xrt_fs *xfs, struct t_stereo_camera_calibration **c_ptr)
{
	struct depthai_fs *depthai = depthai_fs(xfs);

	return depthai_get_gray_cameras_calibration(depthai, c_ptr);
}
