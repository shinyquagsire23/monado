// Copyright 2021-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  DepthAI frameserver implementation.
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_depthai
 */

#include "xrt/xrt_tracking.h"

#include "os/os_time.h"
#include "os/os_threading.h"

#include "math/m_api.h"
#include "math/m_vec3.h"

#include "util/u_sink.h"
#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_frame.h"
#include "util/u_format.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"

#ifdef XRT_OS_LINUX
#include "util/u_linux.h"
#endif

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
DEBUG_GET_ONCE_NUM_OPTION(depthai_floodlight_brightness, "DEPTHAI_FLOODLIGHT_BRIGHTNESS", 1000)
DEBUG_GET_ONCE_NUM_OPTION(depthai_startup_wait_frames, "DEPTHAI_STARTUP_WAIT_FRAMES", 0)
DEBUG_GET_ONCE_NUM_OPTION(depthai_imu_hz, "DEPTHAI_IMU_HZ", 500)
DEBUG_GET_ONCE_NUM_OPTION(depthai_imu_batch_size, "DEPTHAI_IMU_BATCH_SIZE", 2)
DEBUG_GET_ONCE_NUM_OPTION(depthai_imu_max_batch_size, "DEPTHAI_IMU_MAX_BATCH_SIZE", 2)



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

	struct
	{
		bool has;
		bool manual_control;

		u_var_draggable_f32 mA;
		float last_mA;
	} floodlights;

	struct
	{
		bool active;
		// Remember, these hold a pointer to a value!
		u_var_draggable_u16 exposure_time_ui;
		u_var_draggable_u16 iso_ui;

		uint16_t exposure_time;
		uint16_t iso;

		uint16_t last_exposure_time;
		uint16_t last_iso;
	} manual_exposure;


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


	// Good enough assumption that they're using the same distortion model
	enum t_camera_distortion_model type = T_DISTORTION_OPENCV_RADTAN_14;
	if (calibData.getDistortionModel(dai::CameraBoardSocket::LEFT) == dai::CameraModel::Fisheye) {
		type = T_DISTORTION_FISHEYE_KB4;
	}

	uint32_t num_dist = t_num_params_from_distortion_model(type);

	struct t_stereo_camera_calibration *c = NULL;
	t_stereo_camera_calibration_alloc(&c, type);

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

	c->view[0].distortion_model = type;
	c->view[1].distortion_model = type;
	for (uint32_t i = 0; i < num_dist; i++) {
		c->view[0].distortion_parameters_as_array[i] = left.distortion[i];
		c->view[1].distortion_parameters_as_array[i] = right.distortion[i];
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
	depthai->floodlights.has = false;

	for (std::tuple<std::string, int, int> elem : list_of_drivers) {
		if (std::get<0>(elem) == "LM3644") {
			DEPTHAI_DEBUG(depthai, "DepthAI: Found an IR floodlight");
			depthai->floodlights.has = true;
		}
	}

	if (!depthai->floodlights.has) {
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

static void
depthai_maybe_send_exposure_command(struct depthai_fs *depthai)
{
	if (!depthai->manual_exposure.active) {
		return;
	}

	// If the user hasn't changed the exposure values since last we sent a command, we don't need to send a new one.
	if (depthai->manual_exposure.last_exposure_time == depthai->manual_exposure.exposure_time && //
	    depthai->manual_exposure.last_iso == depthai->manual_exposure.iso) {
		return;
	}

	dai::CameraControl ctrl;
	ctrl.setManualExposure(depthai->manual_exposure.exposure_time, depthai->manual_exposure.iso);
	depthai->control_queue->send(ctrl);

	depthai->manual_exposure.last_exposure_time = depthai->manual_exposure.exposure_time;
	depthai->manual_exposure.last_iso = depthai->manual_exposure.iso;
}

static void
depthai_maybe_send_floodlight_command(struct depthai_fs *depthai)
{
	if (!(depthai->floodlights.has && depthai->floodlights.manual_control)) {
		return;
	}

	// If the user hasn't changed the exposure values since last we sent a command, we don't need to send a new one.
	if (depthai->floodlights.last_mA == depthai->floodlights.mA.val) {
		return;
	}

	depthai->device->setIrFloodLightBrightness(depthai->floodlights.mA.val);
}


static void *
depthai_mainloop(void *ptr)
{
	struct depthai_fs *depthai = (struct depthai_fs *)ptr;

	U_TRACE_SET_THREAD_NAME("DepthAI: Image");
	os_thread_helper_name(&depthai->image_thread, "DepthAI: Image");

	DEPTHAI_DEBUG(depthai, "DepthAI: Image thread called");

	os_thread_helper_lock(&depthai->image_thread);
	while (os_thread_helper_is_running_locked(&depthai->image_thread)) {
		os_thread_helper_unlock(&depthai->image_thread);

		depthai_do_one_frame(depthai);

		depthai_maybe_send_exposure_command(depthai);
		depthai_maybe_send_floodlight_command(depthai);

		// Need to lock the thread when we go back to the while condition.
		os_thread_helper_lock(&depthai->image_thread);
	}
	os_thread_helper_unlock(&depthai->image_thread);

	DEPTHAI_DEBUG(depthai, "DepthAI: Image thread exiting");

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
	uint32_t num_packets = (uint32_t)imuPackets.size();

	/*
	 * We used to check num_packets here, but don't since they are now
	 * configurable. Tho we probably should test them, or warn when the
	 * number of packets is larger then batch size for too long.
	 */

	struct xrt_vec3 a = {0, 0, 0};
	struct xrt_vec3 g = {0, 0, 0};

	int64_t ts = 0;

	for (dai::IMUPacket imuPacket : imuPackets) {

		dai::IMUReportAccelerometer &accel = imuPacket.acceleroMeter;
		dai::IMUReportGyroscope &gyro = imuPacket.gyroscope;


		int64_t ts_accel = dai_ts_to_monado_ts(accel.timestamp);
		int64_t ts_gyro = dai_ts_to_monado_ts(gyro.timestamp);
		int64_t diff = (ts_gyro - ts_accel);

		ts += ts_accel / (2 * num_packets);
		ts += ts_gyro / (2 * num_packets);

		float diff_in_ms = (float)(abs((double)diff) / (double)U_TIME_1MS_IN_NS);
		if (diff_in_ms > 2.5) {
			DEPTHAI_WARN(depthai, "Accel and gyro samples are too far apart - %f ms!", diff_in_ms);
		}

		struct xrt_vec3 this_a = {accel.x, accel.y, accel.z};
		struct xrt_vec3 this_g = {gyro.x, gyro.y, gyro.z};


		math_vec3_accum(&this_a, &a);
		math_vec3_accum(&this_g, &g);
	}

	if (num_packets > 1) {
		float scalar = 1.0f / (float)num_packets;
		math_vec3_scalar_mul(scalar, &a);
		math_vec3_scalar_mul(scalar, &g);
	}


	// Prepare sample
	xrt_imu_sample sample;
	sample.timestamp_ns = ts;
	sample.accel_m_s2.x = a.x;
	sample.accel_m_s2.y = a.y;
	sample.accel_m_s2.z = a.z;

	sample.gyro_rad_secs.x = g.x;
	sample.gyro_rad_secs.y = g.y;
	sample.gyro_rad_secs.z = g.z;

	// Sample prepared, now push it out.
	xrt_sink_push_imu(depthai->imu_sink, &sample);

	// Only do this if we are really debugging stuff.
#ifdef XRT_FEATURE_TRACING
	static timepoint_ns last_ns = 0;
	if (last_ns == 0) {
		last_ns = ts - U_TIME_1MS_IN_NS; // Just so it isn't zero.
	}

	timepoint_ns now_ns = (timepoint_ns)os_monotonic_get_ns();
	timepoint_ns now_diff_ns = ts - now_ns;
	timepoint_ns last_diff_ns = ts - last_ns;
	last_ns = ts;

	double now_diff_ms = time_ns_to_ms_f(now_diff_ns);
	double last_diff_ms = time_ns_to_ms_f(last_diff_ns);

	float gyro_length = m_vec3_len(g);
	float weighted_gyro_length = gyro_length * time_ns_to_s(last_diff_ns);

#ifdef U_TRACE_TRACY
	TracyCPlot("DepthAI IMU to now(ms)", now_diff_ms);
	TracyCPlot("DepthAI IMU to last(ms)", last_diff_ms);
	TracyCPlot("DepthAI IMU num packets", num_packets);
	TracyCPlot("DepthAI IMU gyro length", gyro_length);
	TracyCPlot("DepthAI IMU gyro weighted length", weighted_gyro_length);
#endif
#endif
}

static void *
depthai_imu_mainloop(void *ptr)
{
	struct depthai_fs *depthai = (struct depthai_fs *)ptr;

	U_TRACE_SET_THREAD_NAME("DepthAI: IMU");
	os_thread_helper_name(&depthai->imu_thread, "DepthAI: IMU");

#ifdef XRT_OS_LINUX
	// Try to raise priority of this thread.
	u_linux_try_to_set_realtime_priority_on_thread(depthai->log_level, "DepthAI: IMU");
#endif

	DEPTHAI_DEBUG(depthai, "DepthAI: IMU thread called");

	os_thread_helper_lock(&depthai->imu_thread);
	while (os_thread_helper_is_running_locked(&depthai->imu_thread)) {
		os_thread_helper_unlock(&depthai->imu_thread);

		depthai_do_one_imu_frame(depthai);

		// Need to lock the thread when we go back to the while condition.
		os_thread_helper_lock(&depthai->imu_thread);
	}
	os_thread_helper_unlock(&depthai->imu_thread);

	DEPTHAI_DEBUG(depthai, "DepthAI: IMU thread exiting");

	return nullptr;
}

static bool
depthai_destroy(struct depthai_fs *depthai)
{
	DEPTHAI_DEBUG(depthai, "DepthAI: Frameserver destroy called");
	os_thread_helper_destroy(&depthai->image_thread);
	os_thread_helper_destroy(&depthai->imu_thread);
	u_var_remove_root(depthai);
	for (int i = 0; i < 4; i++) {
		u_sink_debug_destroy(&depthai->debug_sinks[i]);
	}

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
	case (RGB_OV_9782):
		depthai->width = 1280;
		depthai->height = 800;
		depthai->format = XRT_FORMAT_R8G8B8;
		depthai->color_sensor_resolution = dai::ColorCameraProperties::SensorResolution::THE_800_P;
		depthai->image_orientation = dai::CameraImageOrientation::ROTATE_180_DEG;
		depthai->fps = 60; // Currently only supports 60.
		depthai->interleaved = true;
		depthai->color_order = dai::ColorCameraProperties::ColorOrder::RGB;
		break;
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
		uint32_t imu_hz = (uint32_t)debug_get_num_option_depthai_imu_hz();
		uint32_t batch_size = (uint32_t)debug_get_num_option_depthai_imu_batch_size();
		uint32_t max_batch_size = (uint32_t)debug_get_num_option_depthai_imu_max_batch_size();

		/*
		 * Limitations from:
		 * https://docs.luxonis.com/projects/api/en/latest/components/nodes/imu/#limitations
		 */
		switch (imu_hz) {
		case 400: // Supposed to be okay
			DEPTHAI_DEBUG(depthai, "%uHz IMU sample rate is supposed to be ok.", imu_hz);
			break;
		case 500: // Maybe ok?
			DEPTHAI_INFO(depthai, "%uHz IMU sample rate maybe produce jitters.", imu_hz);
			break;
		default: // Not known to be good on any (or both IMU and both Gyra/Accel at the same time).
			DEPTHAI_WARN(depthai, "%uHz IMU sample rate not a known good rate.", imu_hz);
			break;
		}

		switch (batch_size) {
		case 1:
		case 2: // Seems okay
			DEPTHAI_DEBUG(depthai, "%u IMU batch size is supposed to be ok.", batch_size);
			break;
		default: // Not known to be good on any
			DEPTHAI_WARN(depthai, "%iHz IMU batch size is not tested!", batch_size);
			break;
		}

		if (max_batch_size < 2) {
			DEPTHAI_WARN(depthai, "Max batch size(%u) smaller then 2, setting two.", max_batch_size);
			max_batch_size = 2;
		}

		if (max_batch_size < batch_size) {
			DEPTHAI_WARN(depthai, "Max batch size(%u) smaller then batch size(%u), setting to batch size.",
			             max_batch_size, batch_size);
			max_batch_size = batch_size;
		}

		std::shared_ptr<dai::node::XLinkOut> xlinkOut_imu = p.create<dai::node::XLinkOut>();
		xlinkOut_imu->setStreamName(name_imu);

		auto imu = p.create<dai::node::IMU>();
		imu->enableIMUSensor({dai::IMUSensor::ACCELEROMETER_RAW, dai::IMUSensor::GYROSCOPE_RAW}, imu_hz);
		imu->setBatchReportThreshold(batch_size);
		imu->setMaxBatchReports(max_batch_size);
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

	if (depthai->floodlights.has) {
		float mA = depthai->floodlights.mA.val;

		if (mA > 1500.0f) {
			DEPTHAI_ERROR(depthai, "Can not set brightness to more then 1500mA, clamping!");
			mA = 1500.0f;
		}

		if (mA > 0.0f) {
			depthai->device->setIrFloodLightBrightness(mA);
		}
	}

	//!@todo This code will turn the exposure time down, but you may not want it. Or we may want to rework Monado's
	//! AEG code to control the IR floodlight brightness in concert with the exposure itme. For now, disable.
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

	depthai->sink[0] = nullptr;        // 0 == CamA-4L / RGB
	depthai->sink[1] = sinks->cams[0]; // 1 == CamB-2L / Left Gray
	depthai->sink[2] = sinks->cams[1]; // 2 == CamC-2L / Right Gray
	depthai->sink[3] = nullptr;        // 3 == CamD-4L
	if (depthai->want_cameras && sinks->cams[0] != NULL && sinks->cams[1] != NULL) {
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
	depthai->device = d;

	depthai->manual_exposure.active = false;
	// Low values, useful for marker calibration on a monitor.
	depthai->manual_exposure.iso = 270;
	depthai->manual_exposure.exposure_time = 320;

	depthai->manual_exposure.iso_ui.val = &depthai->manual_exposure.iso;
	depthai->manual_exposure.iso_ui.min = 0;
	depthai->manual_exposure.iso_ui.max = 1600;
	depthai->manual_exposure.iso_ui.step = 1;

	depthai->manual_exposure.exposure_time_ui.val = &depthai->manual_exposure.exposure_time;
	depthai->manual_exposure.exposure_time_ui.min = 0;
	// 160,000 us = 0.1s
	depthai->manual_exposure.exposure_time_ui.max = 65535;
	depthai->manual_exposure.exposure_time_ui.step = 1;

	depthai->floodlights.mA.val = debug_get_num_option_depthai_floodlight_brightness();
	depthai->floodlights.mA.min = 0.0f;
	depthai->floodlights.mA.max = 1500.0f;
	depthai->floodlights.mA.step = 1.0f;



	u_var_add_root(depthai, "DepthAI Source", 0);
	for (int i = 0; i < 4; i++) {
		u_sink_debug_init(&depthai->debug_sinks[i]);
	}
	u_var_add_sink_debug(depthai, &depthai->debug_sinks[0], "RGB");
	u_var_add_sink_debug(depthai, &depthai->debug_sinks[1], "Left");
	u_var_add_sink_debug(depthai, &depthai->debug_sinks[2], "Right");
	u_var_add_sink_debug(depthai, &depthai->debug_sinks[3], "CamD");

	u_var_add_bool(depthai, &depthai->manual_exposure.active, "Manual exposure");

	u_var_add_draggable_u16(depthai, &depthai->manual_exposure.exposure_time_ui, "Exposure time");
	u_var_add_draggable_u16(depthai, &depthai->manual_exposure.iso_ui, "ISO");

	depthai_guess_ir_drivers(depthai);
	if (depthai->floodlights.has) {
		u_var_add_bool(depthai, &depthai->floodlights.manual_control, "Manual floodlight control");
		u_var_add_draggable_f32(depthai, &depthai->floodlights.mA, "Floodlight brightness (mA)");
	}


	// Some debug printing.
	depthai_guess_camera_type(depthai);

	depthai_print_calib(depthai);

	// Make sure that the thread helper is initialised.
	os_thread_helper_init(&depthai->image_thread);
	os_thread_helper_init(&depthai->imu_thread);

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

	// Set after checking for null.
	depthai->want_imu = false;
	depthai->want_cameras = true;

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

	// Set after checking for null.
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
	if (depthai == nullptr) {
		return nullptr;
	}

	// Set after checking for null.
	depthai->want_cameras = true;
	depthai->want_imu = true;

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
	if (depthai == nullptr) {
		return nullptr;
	}

	// Set after checking for null.
	depthai->want_cameras = false;
	depthai->want_imu = true;

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
