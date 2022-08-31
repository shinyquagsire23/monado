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
#include "util/u_distortion_mesh.h"

#include "android/android_globals.h"
#include "android/android_custom_surface.h"

#include <xrt/xrt_config_android.h>

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

			ANDROID_TRACE(d, "accel %ld %.2f %.2f %.2f", event.timestamp, accel.x, accel.y, accel.z);
			break;
		}
		case ASENSOR_TYPE_GYROSCOPE: {
			gyro.x = -event.data[1];
			gyro.y = event.data[0];
			gyro.z = event.data[2];

			ANDROID_TRACE(d, "gyro %ld %.2f %.2f %.2f", event.timestamp, gyro.x, gyro.y, gyro.z);

			// TODO: Make filter handle accelerometer
			struct xrt_vec3 null_accel;

			// Lock last and the fusion.
			os_mutex_lock(&d->lock);

			m_imu_3dof_update(&d->fusion, event.timestamp, &null_accel, &gyro);

			// Now done.
			os_mutex_unlock(&d->lock);
		}
		default: ANDROID_TRACE(d, "Unhandled event type %d", event.type);
		}
	}

	return 1;
}

static inline int32_t
android_get_sensor_poll_rate(const struct android_device *d)
{
	const float freq_multiplier = 1.0f / 3.0f;
	return (d == NULL) ? POLL_RATE_USEC
	                   : (int32_t)(d->base.hmd->screens[0].nominal_frame_interval_ns * freq_multiplier * 0.001f);
}

static void *
android_run_thread(void *ptr)
{
	struct android_device *d = (struct android_device *)ptr;
	const int32_t poll_rate_usec = android_get_sensor_poll_rate(d);

#if __ANDROID_API__ >= 26
	d->sensor_manager = ASensorManager_getInstanceForPackage(XRT_ANDROID_PACKAGE);
#else
	d->sensor_manager = ASensorManager_getInstance();
#endif

	d->accelerometer = ASensorManager_getDefaultSensor(d->sensor_manager, ASENSOR_TYPE_ACCELEROMETER);
	d->gyroscope = ASensorManager_getDefaultSensor(d->sensor_manager, ASENSOR_TYPE_GYROSCOPE);

	ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);

	d->event_queue = ASensorManager_createEventQueue(d->sensor_manager, looper, ALOOPER_POLL_CALLBACK,
	                                                 android_sensor_callback, (void *)d);

	// Start sensors in case this was not done already.
	if (d->accelerometer != NULL) {
		ASensorEventQueue_enableSensor(d->event_queue, d->accelerometer);
		ASensorEventQueue_setEventRate(d->event_queue, d->accelerometer, poll_rate_usec);
	}
	if (d->gyroscope != NULL) {
		ASensorEventQueue_enableSensor(d->event_queue, d->gyroscope);
		ASensorEventQueue_setEventRate(d->event_queue, d->gyroscope, poll_rate_usec);
	}

	int ret = 0;
	while (d->oth.running && ret != ALOOPER_POLL_ERROR) {
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
	out_relation->relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
}

static void
android_device_get_view_poses(struct xrt_device *xdev,
                              const struct xrt_vec3 *default_eye_relation,
                              uint64_t at_timestamp_ns,
                              uint32_t view_count,
                              struct xrt_space_relation *out_head_relation,
                              struct xrt_fov *out_fovs,
                              struct xrt_pose *out_poses)
{
	u_device_get_view_poses(xdev, default_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs,
	                        out_poses);
}


/*
 *
 * Prober functions.
 *
 */

static bool
android_device_compute_distortion(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	struct android_device *d = android_device(xdev);
	return u_compute_distortion_cardboard(&d->cardboard.values[view], u, v, result);
}


struct android_device *
android_device_create()
{
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct android_device *d = U_DEVICE_ALLOCATE(struct android_device, flags, 1, 0);

	d->base.name = XRT_DEVICE_GENERIC_HMD;
	d->base.destroy = android_device_destroy;
	d->base.update_inputs = android_device_update_inputs;
	d->base.get_tracked_pose = android_device_get_tracked_pose;
	d->base.get_view_poses = android_device_get_view_poses;
	d->base.compute_distortion = android_device_compute_distortion;
	d->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;
	d->base.device_type = XRT_DEVICE_TYPE_HMD;
	snprintf(d->base.str, XRT_DEVICE_NAME_LEN, "Android Sensors");
	snprintf(d->base.serial, XRT_DEVICE_NAME_LEN, "Android Sensors");

	d->log_level = debug_get_log_option_android_log();

	m_imu_3dof_init(&d->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);

	int ret = os_mutex_init(&d->lock);
	if (ret != 0) {
		U_LOG_E("Failed to init mutex!");
		android_device_destroy(&d->base);
		return 0;
	}

	struct xrt_android_display_metrics metrics;
	if (!android_custom_surface_get_display_metrics(android_globals_get_vm(), android_globals_get_context(),
	                                                &metrics)) {
		U_LOG_E("Could not get Android display metrics.");
		/* Fallback to default values (Pixel 3) */
		metrics.width_pixels = 2960;
		metrics.height_pixels = 1440;
		metrics.density_dpi = 572;
		metrics.refresh_rate = 60.0f;
	}

	d->base.hmd->screens[0].nominal_frame_interval_ns = time_s_to_ns(1.0f / metrics.refresh_rate);

	// Everything done, finally start the thread.
	os_thread_helper_init(&d->oth);
	ret = os_thread_helper_start(&d->oth, android_run_thread, d);
	if (ret != 0) {
		ANDROID_ERROR(d, "Failed to start thread!");
		android_device_destroy(&d->base);
		return NULL;
	}

	const uint32_t w_pixels = metrics.width_pixels;
	const uint32_t h_pixels = metrics.height_pixels;
	const uint32_t ppi = metrics.density_dpi;

	const float angle = 45 * M_PI / 180.0; // 0.698132; // 40Deg in rads
	const float w_meters = ((float)w_pixels / (float)ppi) * 0.0254f;
	const float h_meters = ((float)h_pixels / (float)ppi) * 0.0254f;

	struct u_cardboard_distortion_arguments args = {
	    .distortion_k = {0.441f, 0.156f, 0.f, 0.f, 0.f},
	    .screen =
	        {
	            .w_pixels = w_pixels,
	            .h_pixels = h_pixels,
	            .w_meters = w_meters,
	            .h_meters = h_meters,
	        },
	    .inter_lens_distance_meters = 0.06f,
	    .lens_y_center_on_screen_meters = h_meters / 2.0f,
	    .screen_to_lens_distance_meters = 0.042f,
	    .fov =
	        {
	            .angle_left = -angle,
	            .angle_right = angle,
	            .angle_up = angle,
	            .angle_down = -angle,
	        },
	};

	u_distortion_cardboard_calculate(&args, d->base.hmd, &d->cardboard);


	u_var_add_root(d, "Android phone", true);
	u_var_add_ro_vec3_f32(d, &d->fusion.last.accel, "last.accel");
	u_var_add_ro_vec3_f32(d, &d->fusion.last.gyro, "last.gyro");

	d->base.orientation_tracking_supported = true;
	d->base.position_tracking_supported = false;

	// Distortion information.
	u_distortion_mesh_fill_in_compute(&d->base);

	ANDROID_DEBUG(d, "Created device!");

	return d;
}
