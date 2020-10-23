// Copyright 2013, Fredrik Hultin.
// Copyright 2013, Jakob Bornecrantz.
// Copyright 2015, Joey Ferwerda.
// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Android sensors driver code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_android
 */

#include "android_sensors.h"

#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_var.h"

// 60 events per second (in us).
#define POLL_RATE_USEC (1000L / 60) * 1000


DEBUG_GET_ONCE_LOG_OPTION(android_log, "ANDROID_SENSORS_LOG", U_LOGGING_WARN)

static inline struct android_device *
android_device(struct xrt_device *xdev)
{
	return (struct android_device *)xdev;
}

// Callback for the Android sensor event queue
static int
android_sensor_callback(int fd, int events, void *data)
{
	struct android_device *d = (struct android_device *)data;

	if (d->accelerometer == NULL || d->gyroscope == NULL)
		return 1;

	ASensorEvent event;
	struct xrt_vec3 gyro;
	struct xrt_vec3 accel;
	while (ASensorEventQueue_getEvents(d->event_queue, &event, 1) > 0) {

		switch (event.type) {
		case ASENSOR_TYPE_ACCELEROMETER: {
			accel.x = event.acceleration.y;
			accel.y = -event.acceleration.x;
			accel.z = event.acceleration.z;

			ANDROID_TRACE(d, "accel %ld %.2f %.2f %.2f",
			              event.timestamp, accel.x, accel.y,
			              accel.z);
			break;
		}
		case ASENSOR_TYPE_GYROSCOPE: {
			gyro.x = -event.data[1];
			gyro.y = event.data[0];
			gyro.z = event.data[2];

			ANDROID_TRACE(d, "gyro %ld %.2f %.2f %.2f",
			              event.timestamp, gyro.x, gyro.y, gyro.z);

			// TODO: Make filter handle accelerometer
			struct xrt_vec3 null_accel;

			// Lock last and the fusion.
			os_mutex_lock(&d->lock);

			m_imu_3dof_update(&d->fusion, event.timestamp,
			                  &null_accel, &gyro);

			// Now done.
			os_mutex_unlock(&d->lock);
		}
		default:
			ANDROID_TRACE(d, "Unhandled event type %d", event.type);
		}
	}

	return 1;
}

static void *
android_run_thread(void *ptr)
{
	struct android_device *d = (struct android_device *)ptr;

	d->sensor_manager = ASensorManager_getInstance();
	d->accelerometer = ASensorManager_getDefaultSensor(
	    d->sensor_manager, ASENSOR_TYPE_ACCELEROMETER);
	d->gyroscope = ASensorManager_getDefaultSensor(d->sensor_manager,
	                                               ASENSOR_TYPE_GYROSCOPE);

	ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);

	d->event_queue = ASensorManager_createEventQueue(
	    d->sensor_manager, looper, ALOOPER_POLL_CALLBACK,
	    android_sensor_callback, (void *)d);

	// Start sensors in case this was not done already.
	if (d->accelerometer != NULL) {
		ASensorEventQueue_enableSensor(d->event_queue,
		                               d->accelerometer);
		ASensorEventQueue_setEventRate(d->event_queue, d->accelerometer,
		                               POLL_RATE_USEC);
	}
	if (d->gyroscope != NULL) {
		ASensorEventQueue_enableSensor(d->event_queue, d->gyroscope);
		ASensorEventQueue_setEventRate(d->event_queue, d->gyroscope,
		                               POLL_RATE_USEC);
	}

	int ret = 0;
	while (ret != ALOOPER_POLL_ERROR) {
		ret = ALooper_pollAll(0, NULL, NULL, NULL);
	}

	return NULL;
}


/*
 *
 * Device functions.
 *
 */

static void
android_device_destroy(struct xrt_device *xdev)
{
	struct android_device *android = android_device(xdev);

	// Destroy the thread object.
	os_thread_helper_destroy(&android->oth);

	// Now that the thread is not running we can destroy the lock.
	os_mutex_destroy(&android->lock);

	// Destroy the fusion.
	m_imu_3dof_close(&android->fusion);

	// Remove the variable tracking.
	u_var_remove_root(android);

	free(android);
}

static void
android_device_update_inputs(struct xrt_device *xdev)
{
	// Empty
}

static void
android_device_get_tracked_pose(struct xrt_device *xdev,
                                enum xrt_input_name name,
                                uint64_t at_timestamp_ns,
                                struct xrt_space_relation *out_relation)
{
	(void)at_timestamp_ns;

	struct android_device *d = android_device(xdev);
	out_relation->pose.orientation = d->fusion.rot;

	//! @todo assuming that orientation is actually currently tracked.
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
}

static void
android_device_get_view_pose(struct xrt_device *xdev,
                             struct xrt_vec3 *eye_relation,
                             uint32_t view_index,
                             struct xrt_pose *out_pose)
{
	struct xrt_pose pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
	bool adjust = view_index == 0;

	pose.position.x = eye_relation->x / 2.0f;
	pose.position.y = eye_relation->y / 2.0f;
	pose.position.z = eye_relation->z / 2.0f;

	// Adjust for left/right while also making sure there aren't any -0.f.
	if (pose.position.x > 0.0f && adjust) {
		pose.position.x = -pose.position.x;
	}
	if (pose.position.y > 0.0f && adjust) {
		pose.position.y = -pose.position.y;
	}
	if (pose.position.z > 0.0f && adjust) {
		pose.position.z = -pose.position.z;
	}

	*out_pose = pose;
}

/*
 *
 * Prober functions.
 *
 */

struct android_device *
android_device_create()
{
	enum u_device_alloc_flags flags = (enum u_device_alloc_flags)(
	    U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct android_device *d =
	    U_DEVICE_ALLOCATE(struct android_device, flags, 1, 0);

	d->base.name = XRT_DEVICE_ANDROID;
	d->base.destroy = android_device_destroy;
	d->base.update_inputs = android_device_update_inputs;
	d->base.get_tracked_pose = android_device_get_tracked_pose;
	d->base.get_view_pose = android_device_get_view_pose;
	d->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;
	d->base.device_type = XRT_DEVICE_TYPE_HMD;

	d->ll = debug_get_log_option_android_log();

	m_imu_3dof_init(&d->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);

	// Everything done, finally start the thread.
	int ret = os_thread_helper_start(&d->oth, android_run_thread, d);
	if (ret != 0) {
		ANDROID_ERROR(d, "Failed to start thread!");
		android_device_destroy(&d->base);
		return NULL;
	}

	// Setup info.
	struct u_device_simple_info info;
	info.display.w_pixels = 1280;
	info.display.h_pixels = 720;
	info.display.w_meters = 0.13f;
	info.display.h_meters = 0.07f;
	info.lens_horizontal_separation_meters = 0.13f / 2.0f;
	info.lens_vertical_position_meters = 0.07f / 2.0f;
	info.views[0].fov = 85.0f * (M_PI / 180.0f);
	info.views[1].fov = 85.0f * (M_PI / 180.0f);

	if (!u_device_setup_split_side_by_side(&d->base, &info)) {
		ANDROID_ERROR(d, "Failed to setup basic device info");
		android_device_destroy(&d->base);
		return NULL;
	}

	u_var_add_root(d, "Android phone", true);
	u_var_add_ro_vec3_f32(d, &d->fusion.last.accel, "last.accel");
	u_var_add_ro_vec3_f32(d, &d->fusion.last.gyro, "last.gyro");

	d->base.orientation_tracking_supported = true;
	d->base.position_tracking_supported = false;

	ANDROID_DEBUG(d, "Created device!");

	return d;
}
