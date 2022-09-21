// Copyright 2021-2022, Collabora, Ltd.
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

#include "util/u_sink.h"
#include "xrt/xrt_tracking.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_frame.h"
#include "util/u_format.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"
#include "math/m_api.h"

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
DEBUG_GET_ONCE_BOOL_OPTION(depthai_want_floodlight, "DEPTHAI_WANT_FLOODLIGHT", true)
DEBUG_GET_ONCE_NUM_OPTION(depthai_startup_wait_frames, "DEPTHAI_STARTUP_WAIT_FRAMES", 0)



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
	GRAY_OV_7251_L,
	GRAY_OV_7251_R,
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
	struct os_thread_helper image_thread;
	struct os_thread_helper imu_thread;

	u_logging_level log_level;

	uint32_t width;
	uint32_t height;
	xrt_format format;

	// Sink:, RGB, Left, Right, CamC.
	xrt_frame_sink *sink[4];
	xrt_imu_sink *imu_sink;

	struct u_sink_debug debug_sinks[4];

	dai::Device *device;
	dai::DataOutputQueue *image_queue;
	dai::DataOutputQueue *imu_queue;

	dai::DataInputQueue *control_queue;

	dai::ColorCameraProperties::SensorResolution color_sensor_resolution;
	dai::ColorCameraProperties::ColorOrder color_order;

	dai::MonoCameraProperties::SensorResolution grayscale_sensor_resolution;
	dai::CameraBoardSocket camera_board_socket;

	dai::CameraImageOrientation image_orientation;


	uint32_t fps;
	bool interleaved;
	bool oak_d_lite;

	bool has_floodlight;
	bool want_floodlight;

	bool want_cameras;
	bool want_imu;
	bool half_size_ov9282;

	uint32_t first_frames_idx;
	uint32_t first_frames_camera_to_watch;
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

	uint32_t num_dist = 14;

	// Good enough assumption that they're using the same distortion model
	bool use_fisheye = calibData.getDistortionModel(dai::CameraBoardSocket::LEFT) == dai::CameraModel::Fisheye;
	if (use_fisheye) {
		num_dist = 4;
	}

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
	c->view[0].use_fisheye = use_fisheye;
	c->view[1].use_fisheye = use_fisheye;
	for (uint32_t i = 0; i < num_dist; i++) {
		if (use_fisheye) {
			c->view[0].distortion_fisheye[i] = left.distortion[i];
			c->view[1].distortion_fisheye[i] = right.distortion[i];
		} else {
			c->view[0].distortion[i] = left.distortion[i];
			c->view[1].distortion[i] = right.distortion[i];
		}
	}

	// Copy translation
	for (uint32_t i = 0; i < 3; i++) {
		// Is in centimeters, odd. Monado uses meters.
		c->camera_translation[i] = extrinsics[i][3] / 100.0f;
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

//!@todo this function will look slightly different for an OAK-D Pro with dot projectors - mine only has floodlights
void
depthai_guess_ir_drivers(struct depthai_fs *depthai)
{
	std::vector<std::tuple<std::string, int, int>> list_of_drivers = depthai->device->getIrDrivers();
	depthai->has_floodlight = false;

	for (std::tuple<std::string, int, int> elem : list_of_drivers) {
		if (std::get<0>(elem) == "LM3644") {
			DEPTHAI_DEBUG(depthai, "DepthAI: Found an IR floodlight");
			depthai->has_floodlight = true;
		}
	}
	if (!depthai->has_floodlight) {
		DEPTHAI_DEBUG(depthai, "DepthAI: Didn't find any IR illuminators");
	}
}

static void
depthai_guess_camera_type(struct depthai_fs *depthai)
{
	// We could be a lot more pedantic here, but let's just not.
	// For now, ov7251 == oak-d lite, and ov9282 == oak-d/oak-d S2/oak-d pro
	std::ostringstream oss = {};
	std::vector<dai::CameraBoardSocket> sockets = depthai->device->getConnectedCameras();
	std::unordered_map<dai::CameraBoardSocket, std::string> sensornames = depthai->device->getCameraSensorNames();

	bool ov9282 = false;

	bool ov7251 = false;



	for (size_t i = 0; i < sockets.size(); i++) {
		dai::CameraBoardSocket sock = sockets[i];
		std::string sensorname = sensornames.at(sock);
		if (sensorname == "OV9282" || sensorname == "OV9*82") {
			ov9282 = true;
		} else if (sensorname == "OV7251") {
			ov7251 = true;
		}
		oss << "'" << static_cast<int>(sock) << "': " << sensorname << ", ";
	}


	std::string str = oss.str();

	DEPTHAI_DEBUG(depthai, "DepthAI: Connected cameras: %s", str.c_str());

	if (ov9282 && !ov7251) {
		// OAK-D
		DEPTHAI_DEBUG(depthai, "DepthAI: Found an OAK-D!");
		depthai->oak_d_lite = false;
	} else if (ov7251 && !ov9282) {
		// OAK-D Lite
		DEPTHAI_DEBUG(depthai, "DepthAI: Found and OAK-D Lite!");
		depthai->oak_d_lite = true;
	} else {
		DEPTHAI_WARN(depthai,
		             "DepthAI: Not sure what kind of device this is - going to pretend this is an OAK-D.");
		depthai->oak_d_lite = false;
	}
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
	std::shared_ptr<dai::ImgFrame> imgFrame = depthai->image_queue->get<dai::ImgFrame>();
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

	if (num >= ARRAY_SIZE(depthai->sink)) {
		DEPTHAI_ERROR(depthai, "Instance number too large! (%u)", num);
		return;
	}

	if (depthai->sink[num] == nullptr) {
		DEPTHAI_ERROR(depthai, "No sink waiting for frame! (%u)", num);
		return;
	}

	if (depthai->first_frames_idx < debug_get_num_option_depthai_startup_wait_frames()) {
		if (depthai->first_frames_idx == 0) {
			depthai->first_frames_camera_to_watch = num;
		}
		if (num != depthai->first_frames_camera_to_watch) {
			return;
		}
		depthai->first_frames_idx++;
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
	u_sink_debug_push_frame(&depthai->debug_sinks[num], xf);

	// If downstream wants to keep the frame they would have referenced it.
	xrt_frame_reference(&xf, NULL);
}

static void *
depthai_mainloop(void *ptr)
{
	SINK_TRACE_MARKER();

	struct depthai_fs *depthai = (struct depthai_fs *)ptr;
	DEPTHAI_DEBUG(depthai, "DepthAI: Mainloop called");

	os_thread_helper_lock(&depthai->image_thread);
	while (os_thread_helper_is_running_locked(&depthai->image_thread)) {
		os_thread_helper_unlock(&depthai->image_thread);

		depthai_do_one_frame(depthai);

		// Need to lock the thread when we go back to the while condition.
		os_thread_helper_lock(&depthai->image_thread);
	}
	os_thread_helper_unlock(&depthai->image_thread);

	DEPTHAI_DEBUG(depthai, "DepthAI: Mainloop exiting");
	return nullptr;
}

int64_t
dai_ts_to_monado_ts(dai::Timestamp &in)
{
	return std::chrono::time_point<std::chrono::steady_clock, std::chrono::steady_clock::duration>{
	    std::chrono::seconds(in.sec) + std::chrono::nanoseconds(in.nsec)}
	    .time_since_epoch()
	    .count();
}

// Look at the WMR driver - that's where these averaging shenanigans come from ;)
static void
depthai_do_one_imu_frame(struct depthai_fs *depthai)
{
	std::shared_ptr<dai::IMUData> imuData = depthai->imu_queue->get<dai::IMUData>();

	if (depthai->first_frames_idx < debug_get_num_option_depthai_startup_wait_frames()) {
		return;
	}


	std::vector<dai::IMUPacket> imuPackets = imuData->packets;

	if (imuPackets.size() != 2) {
		DEPTHAI_ERROR(depthai, "Wrong number of IMU reports!");
		// Yeah we're not dealing with this. Shouldn't ever happen
		return;
	}

	assert(imuPackets.size() == 2);

	struct xrt_vec3 a = {0, 0, 0};
	struct xrt_vec3 g = {0, 0, 0};

	int64_t ts = 0;

	for (dai::IMUPacket imuPacket : imuPackets) {

		dai::IMUReportAccelerometer &accel = imuPacket.acceleroMeter;
		dai::IMUReportGyroscope &gyro = imuPacket.gyroscope;


		int64_t ts_accel = dai_ts_to_monado_ts(accel.timestamp);
		int64_t ts_gyro = dai_ts_to_monado_ts(gyro.timestamp);
		int64_t diff = (ts_gyro - ts_accel);

		ts += ts_accel / 4;
		ts += ts_gyro / 4;

		float diff_in_ms = fabs(diff) / (double)U_TIME_1MS_IN_NS;
		if (diff_in_ms > 2.5) {
			DEPTHAI_WARN(depthai, "Accel and gyro samples are too far apart - %f ms!", diff_in_ms);
		}

		struct xrt_vec3 this_a = {accel.x, accel.y, accel.z};
		struct xrt_vec3 this_g = {gyro.x, gyro.y, gyro.z};


		math_vec3_accum(&this_a, &a);
		math_vec3_accum(&this_g, &g);
	}

	math_vec3_scalar_mul(0.5, &a);
	math_vec3_scalar_mul(0.5, &g);


	xrt_imu_sample sample;
	sample.timestamp_ns = ts;
	sample.accel_m_s2.x = a.x;
	sample.accel_m_s2.y = a.y;
	sample.accel_m_s2.z = a.z;

	sample.gyro_rad_secs.x = g.x;
	sample.gyro_rad_secs.y = g.y;
	sample.gyro_rad_secs.z = g.z;
	xrt_sink_push_imu(depthai->imu_sink, &sample);
}

static void *
depthai_imu_mainloop(void *ptr)
{
	SINK_TRACE_MARKER();

	struct depthai_fs *depthai = (struct depthai_fs *)ptr;
	DEPTHAI_DEBUG(depthai, "DepthAI: Mainloop called");

	os_thread_helper_lock(&depthai->imu_thread);
	while (os_thread_helper_is_running_locked(&depthai->imu_thread)) {
		os_thread_helper_unlock(&depthai->imu_thread);

		depthai_do_one_imu_frame(depthai);

		// Need to lock the thread when we go back to the while condition.
		os_thread_helper_lock(&depthai->imu_thread);
	}
	os_thread_helper_unlock(&depthai->imu_thread);

	DEPTHAI_DEBUG(depthai, "DepthAI: Mainloop exiting");
	return nullptr;
}

static bool
depthai_destroy(struct depthai_fs *depthai)
{
	DEPTHAI_DEBUG(depthai, "DepthAI: Frameserver destroy called");
	os_thread_helper_destroy(&depthai->image_thread);
	os_thread_helper_destroy(&depthai->imu_thread);

	// To work around use after free issue detected by ASan, v2.13.3 has this bug.
	if (depthai->image_queue) {
		depthai->image_queue->close();
	}
	if (depthai->imu_queue) {
		depthai->imu_queue->close();
	}
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
		depthai->color_sensor_resolution = dai::ColorCameraProperties::SensorResolution::THE_1080_P;
		depthai->image_orientation = dai::CameraImageOrientation::AUTO;
		depthai->fps = 60; // API says max is 118, anything over 60 seems broken with the v2.13.3 release.
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
	case (GRAY_OV_7251_L):
		depthai->width = 640;
		depthai->height = 480;
		depthai->format = XRT_FORMAT_L8;
		depthai->camera_board_socket = dai::CameraBoardSocket::LEFT;
		depthai->grayscale_sensor_resolution = dai::MonoCameraProperties::SensorResolution::THE_480_P;
		depthai->image_orientation = dai::CameraImageOrientation::AUTO;
		depthai->fps = 60; // Currently only supports 60.
		break;
	case (GRAY_OV_7251_R):
		depthai->width = 640;
		depthai->height = 480;
		depthai->format = XRT_FORMAT_L8;
		depthai->camera_board_socket = dai::CameraBoardSocket::RIGHT;
		depthai->grayscale_sensor_resolution = dai::MonoCameraProperties::SensorResolution::THE_480_P;
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
		colorCam->setResolution(depthai->color_sensor_resolution);
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

	p.setXLinkChunkSize(0);

	// Start the pipeline
	depthai->device->startPipeline(p);
	depthai->image_queue = depthai->device->getOutputQueue("preview", 1, false).get(); // out of shared pointer
}

static void
depthai_setup_stereo_grayscale_pipeline(struct depthai_fs *depthai)
{
	// Hardcoded to OV_9282 L/R
	if (!depthai->oak_d_lite) {
		// OV_9282 L/R
		depthai->width = 1280;
		depthai->height = 800;
		if (depthai->half_size_ov9282) {
			depthai->width /= 2;
			depthai->height /= 2;
			depthai->grayscale_sensor_resolution = dai::MonoCameraProperties::SensorResolution::THE_400_P;
		} else {
			depthai->grayscale_sensor_resolution = dai::MonoCameraProperties::SensorResolution::THE_800_P;
		}
		depthai->format = XRT_FORMAT_L8;
		depthai->camera_board_socket = dai::CameraBoardSocket::LEFT;
		depthai->image_orientation = dai::CameraImageOrientation::AUTO;
	} else {
		// OV_7251 L/R
		depthai->width = 640;
		depthai->height = 480;
		depthai->format = XRT_FORMAT_L8;
		depthai->camera_board_socket = dai::CameraBoardSocket::LEFT;
		depthai->grayscale_sensor_resolution = dai::MonoCameraProperties::SensorResolution::THE_480_P;
		depthai->image_orientation = dai::CameraImageOrientation::AUTO;
	}

	dai::Pipeline p = {};

	const char *name_images = "image_frames";
	const char *name_imu = "imu_samples";

	auto controlIn = p.create<dai::node::XLinkIn>();
	controlIn->setStreamName("control");

	if (depthai->want_cameras) {

		std::shared_ptr<dai::node::XLinkOut> xlinkOut = p.create<dai::node::XLinkOut>();
		xlinkOut->setStreamName(name_images);

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
			// Link control to camera
			controlIn->out.link(grayCam->inputControl);
		}
	}

	if (depthai->want_imu) {
		std::shared_ptr<dai::node::XLinkOut> xlinkOut_imu = p.create<dai::node::XLinkOut>();
		xlinkOut_imu->setStreamName(name_imu);

		auto imu = p.create<dai::node::IMU>();
		imu->enableIMUSensor({dai::IMUSensor::ACCELEROMETER_RAW, dai::IMUSensor::GYROSCOPE_RAW}, 500);
		imu->setBatchReportThreshold(2);
		imu->setMaxBatchReports(2);
		imu->out.link(xlinkOut_imu->input);
	}

	p.setXLinkChunkSize(0);

	// Start the pipeline
	depthai->device->startPipeline(p);
	if (depthai->want_cameras) {
		depthai->image_queue =
		    depthai->device->getOutputQueue(name_images, 4, false).get(); // out of shared pointer
	}
	if (depthai->want_imu) {
		depthai->imu_queue = depthai->device->getOutputQueue(name_imu, 4, false).get(); // out of shared pointer
	}


	depthai->control_queue = depthai->device->getInputQueue("control").get();

	if (depthai->has_floodlight && depthai->want_floodlight) {
		depthai->device->setIrFloodLightBrightness(1500);
	}

	//!@todo This code will turn the exposure time down, but you may not want it. Or we may want to rework Monado's
	//! AEG code to control the IR floodlight brightness in concert with the exposure itme. For now, disable.
#if 0
	dai::CameraControl ctrl;
	ctrl.setManualExposure(500, 700);
	depthai->control_queue->send(ctrl);
#endif
}

#ifdef DEPTHAI_HAS_MULTICAM_SUPPORT
static void
depthai_setup_stereo_rgb_pipeline(struct depthai_fs *depthai)
{
	// Hardcoded to OV_9782 L/R
	depthai->width = 1280;
	depthai->height = 800;
	depthai->format = XRT_FORMAT_R8G8B8;
	depthai->camera_board_socket = dai::CameraBoardSocket::LEFT;
	depthai->color_sensor_resolution = dai::ColorCameraProperties::SensorResolution::THE_800_P;
	depthai->image_orientation = dai::CameraImageOrientation::AUTO;
	depthai->fps = 30; // Supports up to 60, but pushing 60fps over USB is typically hard

	dai::Pipeline p = {};

	const char *name = "frames";
	std::shared_ptr<dai::node::XLinkOut> xlinkOut = p.create<dai::node::XLinkOut>();
	xlinkOut->setStreamName(name);

	dai::CameraBoardSocket sockets[2] = {
	    dai::CameraBoardSocket::CAM_B,
	    dai::CameraBoardSocket::CAM_C,
	};

	for (int i = 0; i < 2; i++) {
		std::shared_ptr<dai::node::ColorCamera> grayCam = nullptr;

		grayCam = p.create<dai::node::ColorCamera>();
		grayCam->setPreviewSize(1280, 800);
		grayCam->setBoardSocket(sockets[i]);
		grayCam->setResolution(depthai->color_sensor_resolution);
		grayCam->setImageOrientation(depthai->image_orientation);
		grayCam->setInterleaved(true);
		grayCam->setFps(depthai->fps);
		grayCam->setColorOrder(dai::ColorCameraProperties::ColorOrder::RGB);

		// Link plugins CAM -> XLINK
		grayCam->preview.link(xlinkOut->input);
	}

	p.setXLinkChunkSize(0);

	// Start the pipeline
	depthai->device->startPipeline(p);
	depthai->queue = depthai->device->getOutputQueue(name, 4, false).get(); // out of shared pointer
}
#endif

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

	os_thread_helper_start(&depthai->image_thread, depthai_mainloop, depthai);

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
	if (depthai->want_cameras && sinks->left != NULL && sinks->right != NULL) {
		os_thread_helper_start(&depthai->image_thread, depthai_mainloop, depthai);
	}
	if (depthai->want_imu && sinks->imu != NULL) {
		os_thread_helper_start(&depthai->imu_thread, depthai_imu_mainloop, depthai);
		depthai->imu_sink = sinks->imu;
	}
	return true;
}

static bool
depthai_fs_stream_stop(struct xrt_fs *xfs)
{
	struct depthai_fs *depthai = depthai_fs(xfs);
	DEPTHAI_DEBUG(depthai, "DepthAI: Stream stop called");

	// This call fully stops the thread.
	os_thread_helper_stop_and_wait(&depthai->image_thread);
	os_thread_helper_stop_and_wait(&depthai->imu_thread);

	return true;
}

static bool
depthai_fs_is_running(struct xrt_fs *xfs)
{
	struct depthai_fs *depthai = depthai_fs(xfs);

	os_thread_helper_lock(&depthai->image_thread);
	bool running = os_thread_helper_is_running_locked(&depthai->image_thread);
	os_thread_helper_unlock(&depthai->image_thread);

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
	depthai->want_floodlight = debug_get_bool_option_depthai_want_floodlight();
	depthai->device = d;

	u_var_add_root(depthai, "DepthAI Source", 0);
	u_var_add_sink_debug(depthai, &depthai->debug_sinks[0], "RGB");
	u_var_add_sink_debug(depthai, &depthai->debug_sinks[1], "Left");
	u_var_add_sink_debug(depthai, &depthai->debug_sinks[2], "Right");
	u_var_add_sink_debug(depthai, &depthai->debug_sinks[3], "CamD");

	// Some debug printing.
	depthai_guess_camera_type(depthai);
	depthai_guess_ir_drivers(depthai);
	depthai_print_calib(depthai);

	// Make sure that the thread helper is initialised.
	os_thread_helper_init(&depthai->image_thread);

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
	depthai->want_cameras = true;
	depthai->want_imu = false;
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
depthai_fs_slam(struct xrt_frame_context *xfctx, struct depthai_slam_startup_settings *settings)
{
	struct depthai_fs *depthai = depthai_create_and_do_minimal_setup();
	if (depthai == nullptr) {
		return nullptr;
	}

	depthai->fps = settings->frames_per_second;
	depthai->want_cameras = settings->want_cameras;
	depthai->want_imu = settings->want_imu;
	depthai->half_size_ov9282 = settings->half_size_ov9282;


	// Last bit is to setup the pipeline.
	depthai_setup_stereo_grayscale_pipeline(depthai);

	// And finally add us to the context when we are done.
	xrt_frame_context_add(xfctx, &depthai->node);

	DEPTHAI_DEBUG(depthai, "DepthAI: Created");

	return &depthai->base;
}

extern "C" struct xrt_fs *
depthai_fs_stereo_grayscale_and_imu(struct xrt_frame_context *xfctx)
{
	struct depthai_fs *depthai = depthai_create_and_do_minimal_setup();
	depthai->want_cameras = true;
	depthai->want_imu = true;
	if (depthai == nullptr) {
		return nullptr;
	}

	// Last bit is to setup the pipeline.
	depthai_setup_stereo_grayscale_pipeline(depthai);

	// And finally add us to the context when we are done.
	xrt_frame_context_add(xfctx, &depthai->node);

	DEPTHAI_DEBUG(depthai, "DepthAI: Created");

	return &depthai->base;
}


extern "C" struct xrt_fs *
depthai_fs_just_imu(struct xrt_frame_context *xfctx)
{
	struct depthai_fs *depthai = depthai_create_and_do_minimal_setup();
	depthai->want_cameras = false;
	depthai->want_imu = true;
	if (depthai == nullptr) {
		return nullptr;
	}

	// Last bit is to setup the pipeline.
	depthai_setup_stereo_grayscale_pipeline(depthai);

	// And finally add us to the context when we are done.
	xrt_frame_context_add(xfctx, &depthai->node);

	DEPTHAI_DEBUG(depthai, "DepthAI: Created");

	return &depthai->base;
}

#ifdef DEPTHAI_HAS_MULTICAM_SUPPORT
extern "C" struct xrt_fs *
depthai_fs_stereo_rgb(struct xrt_frame_context *xfctx)
{
	struct depthai_fs *depthai = depthai_create_and_do_minimal_setup();
	if (depthai == nullptr) {
		return nullptr;
	}

	// Last bit is to setup the pipeline.
	depthai_setup_stereo_rgb_pipeline(depthai);

	// And finally add us to the context when we are done.

	xrt_frame_context_add(xfctx, &depthai->node);
	DEPTHAI_DEBUG(depthai, "DepthAI: Created");
	return &depthai->base;
}
#endif

extern "C" bool
depthai_fs_get_stereo_calibration(struct xrt_fs *xfs, struct t_stereo_camera_calibration **c_ptr)
{
	struct depthai_fs *depthai = depthai_fs(xfs);

	return depthai_get_gray_cameras_calibration(depthai, c_ptr);
}
