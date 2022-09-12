// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Realsense prober code.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_realsense
 */

#include <stdio.h>
#include <stdlib.h>

#include "xrt/xrt_config_build.h"
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "rs_driver.h"

#include <librealsense2/rs.h>

#define INFO(...) U_LOG(U_LOGGING_INFO, __VA_ARGS__);
#define WARN(...) U_LOG(U_LOGGING_WARN, __VA_ARGS__);
#define ERROR(...) U_LOG(U_LOGGING_ERROR, __VA_ARGS__);

//! Utility for realsense API calls that can produce errors
#define DO(call, ...)                                                                                                  \
	call(__VA_ARGS__, &e);                                                                                         \
	check_error(e, __FILE__, __LINE__);

/*!
 * @brief Specifies which realsense tracking to use
 * -1 for DISABLED, will not create any RealSense device
 * 0 for UNSPECIFIED, will decide based on what's available
 * 1 for DEVICE_SLAM, will only try to use in-device SLAM tracking
 * 2 for HOST_SLAM, will only try to use external SLAM tracking
 */
DEBUG_GET_ONCE_NUM_OPTION(rs_tracking, "RS_TRACKING", RS_TRACKING_UNSPECIFIED)

static bool
check_error(rs2_error *e, const char *file, int line)
{
	if (e == NULL) {
		return false; // No errors
	}

	ERROR("rs_error was raised when calling %s(%s):", rs2_get_failed_function(e), rs2_get_failed_args(e));
	ERROR("%s:%d: %s", file, line, rs2_get_error_message(e));
	exit(EXIT_FAILURE);
}

/*!
 * @implements xrt_auto_prober
 */
struct rs_prober
{
	struct xrt_auto_prober base;
};

//! @private @memberof rs_prober
static inline struct rs_prober *
rs_prober(struct xrt_auto_prober *p)
{
	return (struct rs_prober *)p;
}

//! @public @memberof rs_prober
static void
rs_prober_destroy(struct xrt_auto_prober *p)
{
	struct rs_prober *dp = rs_prober(p);

	free(dp);
}

/*!
 * @brief Explores a realsense device to see what SLAM capabilities it supports
 *
 * @param device_list List in which the device resides.
 * @param dev_idx Index of the device in @p device_list.
 * @param[out] out_hslam Whether it supports host-SLAM tracking (Has camera-imu streams)
 * @param[out] out_dslam Whether it supports device-SLAM tracking (T26x)
 */
static void
check_slam_capabilities(rs2_device_list *device_list, int dev_idx, bool *out_hslam, bool *out_dslam)
{
	//! @todo Consider adding the sensors list to the rs_container
	bool video_sensor_found = false;
	bool imu_sensor_found = false;
	bool pose_sensor_found = false;

	rs2_error *e = NULL;
	rs2_device *device = DO(rs2_create_device, device_list, dev_idx);
	rs2_sensor_list *sensors = DO(rs2_query_sensors, device);
	int sensors_count = DO(rs2_get_sensors_count, sensors);
	for (int i = 0; i < sensors_count; i++) {
		rs2_sensor *sensor = DO(rs2_create_sensor, sensors, i);
		video_sensor_found |= DO(rs2_is_sensor_extendable_to, sensor, RS2_EXTENSION_VIDEO);
		imu_sensor_found |= DO(rs2_is_sensor_extendable_to, sensor, RS2_EXTENSION_MOTION_SENSOR);
		pose_sensor_found |= DO(rs2_is_sensor_extendable_to, sensor, RS2_EXTENSION_POSE_SENSOR);
		rs2_delete_sensor(sensor);
	}

	rs2_delete_sensor_list(sensors);
	rs2_delete_device(device);

	*out_hslam = video_sensor_found && imu_sensor_found;
	*out_dslam = pose_sensor_found;
}

static bool
supports_host_slam(rs2_device_list *device_list, int index)
{
	bool supported, _;
	check_slam_capabilities(device_list, index, &supported, &_);
	return supported;
}

static bool
supports_device_slam(rs2_device_list *device_list, int index)
{
	bool supported, _;
	check_slam_capabilities(device_list, index, &_, &supported);
	return supported;
}

//! @return index of the first device in device_list that has the requested
//! capability or -1 if none.
static int
find_capable_device(int capability, rs2_device_list *device_list)
{
	rs2_error *e = NULL;
	int device_count = DO(rs2_get_device_count, device_list);

	// Determine predicate to check if a device supports the capability
	bool (*supports_capability)(rs2_device_list *, int);
	if (capability == RS_TRACKING_DEVICE_SLAM) {
		supports_capability = supports_device_slam;
	} else if (capability == RS_TRACKING_HOST_SLAM) {
		supports_capability = supports_host_slam;
	} else {
		ERROR("Invalid capability=%d requested", capability);
		return false;
	}

	// Find the first device that support the capability
	int cdev_idx = -1; // cdev means capable device
	for (int i = 0; i < device_count; i++) {
		if (supports_capability(device_list, i)) {
			cdev_idx = i;
			break;
		}
	}

	return cdev_idx;
}

//! Implements the conditional flow to decide on how to pick which tracking to use
static struct xrt_device *
create_tracked_rs_device(struct xrt_prober *xp)
{
	rs2_error *e = NULL;
	struct rs_container rsc = {0};
	int expected_tracking = debug_get_num_option_rs_tracking();
#ifdef XRT_FEATURE_SLAM
	bool external_slam_supported = true;
#else
	bool external_slam_supported = false;
#endif

	rsc.context = DO(rs2_create_context, RS2_API_VERSION);
	rsc.device_list = DO(rs2_query_devices, rsc.context);
	rsc.device_count = DO(rs2_get_device_count, rsc.device_list);

	if (rsc.device_count == 0) {
		if (expected_tracking != RS_TRACKING_UNSPECIFIED) {
			WARN("RS_TRACKING=%d provided but no RealSense devices found", expected_tracking);
		}
		rs_container_cleanup(&rsc);
		return 0;
	}

	int ddev_idx = find_capable_device(RS_TRACKING_DEVICE_SLAM, rsc.device_list);
	bool has_ddev = ddev_idx != -1;

	int hdev_idx = find_capable_device(RS_TRACKING_HOST_SLAM, rsc.device_list);
	bool has_hdev = hdev_idx != -1;

	rs_container_cleanup(&rsc); // We got ddev_idx and hdev_idx, release realsense resources

	struct xrt_device *dev = NULL;
	if (expected_tracking == RS_TRACKING_HOST_SLAM) {
		if (!external_slam_supported) {
			ERROR("No external SLAM systems built, unable to produce host SLAM tracking");
		} else if (has_hdev) {
			dev = rs_hdev_create(xp, hdev_idx);
		} else {
			ERROR("No RealSense devices that support external SLAM tracking were found");
		}
	} else if (expected_tracking == RS_TRACKING_DEVICE_SLAM) {
		if (has_ddev) {
			dev = rs_ddev_create(ddev_idx);
		} else {
			WARN("No RealSense devices that support in-device SLAM tracking were found");
		}
	} else if (expected_tracking == RS_TRACKING_UNSPECIFIED) {
		if (has_ddev) {
			dev = rs_ddev_create(ddev_idx);
		} else if (has_hdev && external_slam_supported) {
			dev = rs_hdev_create(xp, hdev_idx);
		} else {
			INFO("No RealSense devices that can be tracked were found");
		}
	} else if (expected_tracking == RS_TRACKING_DISABLED) {
		INFO("RS_TRACKING=%d (DISABLED) so skipping any RealSense device", RS_TRACKING_DISABLED);
	} else {
		ERROR("Invalid RS_TRACKING=%d", expected_tracking);
	}

	return dev;
}


//! Basically just for T265
struct xrt_device *
rs_create_tracked_device_internal_slam()
{
	rs2_error *e = NULL;
	struct rs_container rsc = {0};

	rsc.context = DO(rs2_create_context, RS2_API_VERSION);
	rsc.device_list = DO(rs2_query_devices, rsc.context);
	rsc.device_count = DO(rs2_get_device_count, rsc.device_list);


	int ddev_idx = find_capable_device(RS_TRACKING_DEVICE_SLAM, rsc.device_list);



	rs_container_cleanup(&rsc); // We got ddev_idx and hdev_idx, release realsense resources

	struct xrt_device *dev = NULL;

	dev = rs_ddev_create(ddev_idx);


	return dev;
}

//! @public @memberof rs_prober
static int
rs_prober_autoprobe(struct xrt_auto_prober *xap,
                    cJSON *attached_data,
                    bool no_hmds,
                    struct xrt_prober *xp,
                    struct xrt_device **out_xdevs)
{
	struct rs_prober *dp = rs_prober(xap);
	(void)dp;

	struct xrt_device *dev = create_tracked_rs_device(xp);
	if (!dev) {
		return 0;
	}

	out_xdevs[0] = dev;
	return 1;
}

struct xrt_auto_prober *
rs_create_auto_prober()
{
	struct rs_prober *dp = U_TYPED_CALLOC(struct rs_prober);
	dp->base.name = "Realsense";
	dp->base.destroy = rs_prober_destroy;
	dp->base.lelo_dallas_autoprobe = rs_prober_autoprobe;

	return &dp->base;
}
