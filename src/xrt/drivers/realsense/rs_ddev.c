// Copyright 2020, Collabora, Ltd.
// Copyright 2020, Nova King.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  RealSense helper driver for in-device SLAM 6DOF tracking.
 * @author Moses Turner <mosesturner@protonmail.com>
 * @author Nova King <technobaboo@gmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_realsense
 */

#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "math/m_api.h"
#include "math/m_space.h"
#include "math/m_predict.h"
#include "math/m_relation_history.h"

#include "os/os_time.h"
#include "os/os_threading.h"

#include "util/u_time.h"
#include "util/u_device.h"
#include "util/u_logging.h"

#include "util/u_json.h"
#include "util/u_config_json.h"

#include "rs_driver.h"

#include <librealsense2/rs.h>
#include <librealsense2/h/rs_pipeline.h>
#include <librealsense2/h/rs_option.h>
#include <librealsense2/h/rs_frame.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>


/*!
 * Convenience macro to print out a pose, only used for debugging
 */
#define print_pose(msg, pose)                                                                                          \
	U_LOG_E(msg " %f %f %f  %f %f %f %f", pose.position.x, pose.position.y, pose.position.z, pose.orientation.x,   \
	        pose.orientation.y, pose.orientation.z, pose.orientation.w)

/*!
 * Device-SLAM tracked RealSense device (T26X series).
 *
 * @implements xrt_device
 */
struct rs_ddev
{
	struct xrt_device base;

	struct m_relation_history *relation_hist;

	struct os_thread_helper oth;

	bool enable_mapping;
	bool enable_pose_jumping;
	bool enable_relocalization;
	bool enable_pose_prediction;
	bool enable_pose_filtering; //!< Forward compatibility for when that 1-euro filter is working

	struct rs_container rsc; //!< Container of realsense API related objects
};


/*!
 * Helper to convert a xdev to a @ref rs_ddev.
 */
static inline struct rs_ddev *
rs_ddev(struct xrt_device *xdev)
{
	return (struct rs_ddev *)xdev;
}

/*!
 * Simple helper to check and print error messages.
 */
static int
check_error(struct rs_ddev *rs, rs2_error *e)
{
	if (e == NULL) {
		return 0;
	}

	U_LOG_E("rs_error was raised when calling %s(%s):", rs2_get_failed_function(e), rs2_get_failed_args(e));
	U_LOG_E("%s", rs2_get_error_message(e));

	return 1;
}

/*!
 * Frees all RealSense resources.
 */
static void
close_ddev(struct rs_ddev *rs)
{
	struct rs_container *rsc = &rs->rsc;
	rs2_pipeline_stop(rsc->pipeline, NULL);
	rs_container_cleanup(&rs->rsc);
}

#define CHECK_RS2()                                                                                                    \
	do {                                                                                                           \
		if (check_error(rs, e) != 0) {                                                                         \
			close_ddev(rs);                                                                                \
			return 1;                                                                                      \
		}                                                                                                      \
	} while (0)


/*!
 * Create all RealSense resources needed for 6DOF tracking.
 */
static int
create_ddev(struct rs_ddev *rs, int device_idx)
{
	assert(rs != NULL);
	rs2_error *e = NULL;

	struct rs_container *rsc = &rs->rsc;

	rsc->context = rs2_create_context(RS2_API_VERSION, &e);
	CHECK_RS2();

	rsc->device_list = rs2_query_devices(rsc->context, &e);
	CHECK_RS2();

	rsc->pipeline = rs2_create_pipeline(rsc->context, &e);
	CHECK_RS2();

	rsc->config = rs2_create_config(&e);
	CHECK_RS2();

	// Set the pipeline to start specifically on the realsense device the prober selected
	rsc->device_idx = device_idx;
	rsc->device = rs2_create_device(rsc->device_list, rsc->device_idx, &e);
	CHECK_RS2();

	bool ddev_has_serial = rs2_supports_device_info(rsc->device, RS2_CAMERA_INFO_SERIAL_NUMBER, &e);
	CHECK_RS2();

	if (ddev_has_serial) {

		const char *ddev_serial = rs2_get_device_info(rsc->device, RS2_CAMERA_INFO_SERIAL_NUMBER, &e);
		CHECK_RS2();

		rs2_config_enable_device(rsc->config, ddev_serial, &e);
		CHECK_RS2();

	} else {
		U_LOG_W("Unexpected, the realsense device in use does not provide a serial number.");
	}

	rs2_delete_device(rsc->device);

	rs2_config_enable_stream(rsc->config,     //
	                         RS2_STREAM_POSE, // Type
	                         0,               // Index
	                         0,               // Width
	                         0,               // Height
	                         RS2_FORMAT_6DOF, // Format
	                         200,             // FPS
	                         &e);
	CHECK_RS2();

	rsc->profile = rs2_config_resolve(rsc->config, rsc->pipeline, &e);
	CHECK_RS2();

	rsc->device = rs2_pipeline_profile_get_device(rsc->profile, &e);
	CHECK_RS2();

	rs2_sensor_list *sensors = rs2_query_sensors(rsc->device, &e);
	CHECK_RS2();

	//! @todo 0 index hardcoded, check device with RS2_EXTENSION_POSE_SENSOR or similar instead
	rs2_sensor *sensor = rs2_create_sensor(sensors, 0, &e);
	CHECK_RS2();

	rs2_set_option((rs2_options *)sensor, RS2_OPTION_ENABLE_MAPPING, rs->enable_mapping ? 1.0f : 0.0f, &e);
	CHECK_RS2();

	if (rs->enable_mapping) {
		// Neither of these options mean anything if mapping is off; in fact it
		// errors out if we mess with these with mapping off
		rs2_set_option((rs2_options *)sensor, RS2_OPTION_ENABLE_RELOCALIZATION,
		               rs->enable_relocalization ? 1.0f : 0.0f, &e);
		CHECK_RS2();

		rs2_set_option((rs2_options *)sensor, RS2_OPTION_ENABLE_POSE_JUMPING,
		               rs->enable_pose_jumping ? 1.0f : 0.0f, &e);
		CHECK_RS2();
	}

	rsc->profile = rs2_pipeline_start_with_config(rsc->pipeline, rsc->config, &e);
	CHECK_RS2();

	rs2_delete_sensor(sensor);
	rs2_delete_sensor_list(sensors);

	return 0;
}

/*!
 * Process a frame as 6DOF data, does not assume ownership of the frame.
 */
static void
process_frame(struct rs_ddev *rs, rs2_frame *frame)
{
	rs2_error *e = NULL;
	int ret = 0;

	ret = rs2_is_frame_extendable_to(frame, RS2_EXTENSION_POSE_FRAME, &e);
	if (check_error(rs, e) != 0 || ret == 0) {
		return;
	}

	rs2_pose camera_pose;
	rs2_pose_frame_get_pose_data(frame, &camera_pose, &e);
	if (check_error(rs, e) != 0) {
		return;
	}

#if 0
	rs2_timestamp_domain domain = rs2_get_frame_timestamp_domain(frame, &e);
	if (check_error(rs, e) != 0) {
		return;
	}
#endif

	double timestamp_milliseconds = rs2_get_frame_timestamp(frame, &e);
	if (check_error(rs, e) != 0) {
		return;
	}

	// Close enough
	uint64_t now_real_ns = os_realtime_get_ns();
	uint64_t now_monotonic_ns = os_monotonic_get_ns();
	uint64_t timestamp_ns = (uint64_t)(timestamp_milliseconds * 1000.0 * 1000.0);

	// How far in the past is it?
	uint64_t diff_ns = now_real_ns - timestamp_ns;

	// Adjust the timestamp to monotonic time.
	timestamp_ns = now_monotonic_ns - diff_ns;

	/*
	 * Transfer the data to the struct.
	 */

	struct xrt_space_relation relation;

	// Rotation/angular
	relation.pose.orientation.x = camera_pose.rotation.x;
	relation.pose.orientation.y = camera_pose.rotation.y;
	relation.pose.orientation.z = camera_pose.rotation.z;
	relation.pose.orientation.w = camera_pose.rotation.w;
	relation.angular_velocity.x = camera_pose.angular_velocity.x;
	relation.angular_velocity.y = camera_pose.angular_velocity.y;
	relation.angular_velocity.z = camera_pose.angular_velocity.z;

	// Position/linear
	relation.pose.position.x = camera_pose.translation.x;
	relation.pose.position.y = camera_pose.translation.y;
	relation.pose.position.z = camera_pose.translation.z;
	relation.linear_velocity.x = camera_pose.velocity.x;
	relation.linear_velocity.y = camera_pose.velocity.y;
	relation.linear_velocity.z = camera_pose.velocity.z;

	// clang-format off
	relation.relation_flags =
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT |
	    XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	    XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
	// clang-format on

	m_relation_history_push(rs->relation_hist, &relation, timestamp_ns);
}

static int
update(struct rs_ddev *rs)
{
	rs2_frame *frames;
	rs2_error *e = NULL;

	frames = rs2_pipeline_wait_for_frames(rs->rsc.pipeline, RS2_DEFAULT_TIMEOUT, &e);
	if (check_error(rs, e) != 0) {
		return 1;
	}

	int num_frames = rs2_embedded_frames_count(frames, &e);
	if (check_error(rs, e) != 0) {
		return 1;
	}

	for (int i = 0; i < num_frames; i++) {
		rs2_frame *frame = rs2_extract_frame(frames, i, &e);
		if (check_error(rs, e) != 0) {
			rs2_release_frame(frames);
			return 1;
		}

		// Does not assume ownership of the frame.
		process_frame(rs, frame);
		rs2_release_frame(frame);

		rs2_release_frame(frames);
	}

	return 0;
}

static void *
rs_run_thread(void *ptr)
{
	struct rs_ddev *rs = (struct rs_ddev *)ptr;

	os_thread_helper_lock(&rs->oth);

	while (os_thread_helper_is_running_locked(&rs->oth)) {

		os_thread_helper_unlock(&rs->oth);

		int ret = update(rs);
		if (ret < 0) {
			return NULL;
		}
	}

	return NULL;
}

static bool
load_config(struct rs_ddev *rs)
{
	struct u_config_json config_json = {0};

	u_config_json_open_or_create_main_file(&config_json);
	if (!config_json.file_loaded) {
		return false;
	}

	const cJSON *realsense_config_json = u_json_get(config_json.root, "config_realsense_ddev");
	if (realsense_config_json == NULL) {
		return false;
	}

	const cJSON *mapping = u_json_get(realsense_config_json, "enable_mapping");
	const cJSON *pose_jumping = u_json_get(realsense_config_json, "enable_pose_jumping");
	const cJSON *relocalization = u_json_get(realsense_config_json, "enable_relocalization");
	const cJSON *pose_prediction = u_json_get(realsense_config_json, "enable_pose_prediction");
	const cJSON *pose_filtering = u_json_get(realsense_config_json, "enable_pose_filtering");

	// if json key isn't in the json, default to true. if it is in there, use json value
	if (mapping != NULL) {
		rs->enable_mapping = cJSON_IsTrue(mapping);
	}
	if (pose_jumping != NULL) {
		rs->enable_pose_jumping = cJSON_IsTrue(pose_jumping);
	}
	if (relocalization != NULL) {
		rs->enable_relocalization = cJSON_IsTrue(relocalization);
	}
	if (pose_prediction != NULL) {
		rs->enable_pose_prediction = cJSON_IsTrue(pose_prediction);
	}
	if (pose_filtering != NULL) {
		rs->enable_pose_filtering = cJSON_IsTrue(pose_filtering);
	}

	cJSON_Delete(config_json.root);

	return true;
}


/*
 *
 * Device functions.
 *
 */

static void
rs_ddev_update_inputs(struct xrt_device *xdev)
{
	// Empty
}

static void
rs_ddev_get_tracked_pose(struct xrt_device *xdev,
                         enum xrt_input_name name,
                         uint64_t at_timestamp_ns,
                         struct xrt_space_relation *out_relation)
{
	struct rs_ddev *rs = rs_ddev(xdev);

	if (name != XRT_INPUT_GENERIC_TRACKER_POSE) {
		U_LOG_E("unknown input name");
		return;
	}

	m_relation_history_get(rs->relation_hist, at_timestamp_ns, out_relation);
}

static void
rs_ddev_get_view_poses(struct xrt_device *xdev,
                       const struct xrt_vec3 *default_eye_relation,
                       uint64_t at_timestamp_ns,
                       uint32_t view_count,
                       struct xrt_space_relation *out_head_relation,
                       struct xrt_fov *out_fovs,
                       struct xrt_pose *out_poses)
{
	assert(false);
}

static void
rs_ddev_destroy(struct xrt_device *xdev)
{
	struct rs_ddev *rs = rs_ddev(xdev);

	// Destroy the thread object.
	os_thread_helper_destroy(&rs->oth);

	close_ddev(rs);

	m_relation_history_destroy(&rs->relation_hist);

	free(rs);
}


/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_device *
rs_ddev_create(int device_idx)
{
	struct rs_ddev *rs = U_DEVICE_ALLOCATE(struct rs_ddev, U_DEVICE_ALLOC_TRACKING_NONE, 1, 0);

	m_relation_history_create(&rs->relation_hist);

	rs->enable_mapping = true;
	rs->enable_pose_jumping = true;
	rs->enable_relocalization = true;
	rs->enable_pose_prediction = true;
	rs->enable_pose_filtering = true;

	if (load_config(rs)) {
		U_LOG_D("Used config file");
	} else {
		U_LOG_D("Did not use config file");
	}

	U_LOG_D("Realsense opts are %i %i %i %i %i\n", rs->enable_mapping, rs->enable_pose_jumping,
	        rs->enable_relocalization, rs->enable_pose_prediction, rs->enable_pose_filtering);
	rs->base.update_inputs = rs_ddev_update_inputs;
	rs->base.get_tracked_pose = rs_ddev_get_tracked_pose;
	rs->base.get_view_poses = rs_ddev_get_view_poses;
	rs->base.destroy = rs_ddev_destroy;
	rs->base.name = XRT_DEVICE_REALSENSE;
	rs->base.tracking_origin->type = XRT_TRACKING_TYPE_EXTERNAL_SLAM;
	rs->base.tracking_origin->offset = (struct xrt_pose)XRT_POSE_IDENTITY;

	// Print name.
	snprintf(rs->base.str, XRT_DEVICE_NAME_LEN, "Intel RealSense Device-SLAM");
	snprintf(rs->base.serial, XRT_DEVICE_NAME_LEN, "Intel RealSense Device-SLAM");

	rs->base.inputs[0].name = XRT_INPUT_GENERIC_TRACKER_POSE;

	int ret = 0;



	// Thread and other state.
	ret = os_thread_helper_init(&rs->oth);
	if (ret != 0) {
		U_LOG_E("Failed to init threading!");
		rs_ddev_destroy(&rs->base);
		return NULL;
	}

	ret = create_ddev(rs, device_idx);
	if (ret != 0) {
		rs_ddev_destroy(&rs->base);
		return NULL;
	}

	ret = os_thread_helper_start(&rs->oth, rs_run_thread, rs);
	if (ret != 0) {
		U_LOG_E("Failed to start thread!");
		rs_ddev_destroy(&rs->base);
		return NULL;
	}

	rs->base.orientation_tracking_supported = true;
	rs->base.position_tracking_supported = true;
	rs->base.device_type = XRT_DEVICE_TYPE_GENERIC_TRACKER;

	return &rs->base;
}
