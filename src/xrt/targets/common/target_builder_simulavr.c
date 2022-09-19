// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Builder for SimulaVR devices
 * @author Moses Turner <moses@collabora.com>
 * @ingroup xrt_iface
 */

#include "multi_wrapper/multi.h"
#include "realsense/rs_interface.h"
#include "tracking/t_hand_tracking.h"
#include "tracking/t_tracking.h"

#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_prober.h"

#include "util/u_builders.h"
#include "util/u_config_json.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_sink.h"
#include "util/u_system_helpers.h"
#include "util/u_file.h"

#include "target_builder_interface.h"

#include "simula/svr_interface.h"
#include "v4l2/v4l2_interface.h"

#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_results.h"
#include "xrt/xrt_tracking.h"

#include <assert.h>

DEBUG_GET_ONCE_OPTION(simula_config_path, "SIMULA_CONFIG_PATH", NULL)
DEBUG_GET_ONCE_LOG_OPTION(svr_log, "SIMULA_LOG", U_LOGGING_WARN)


#define SVR_TRACE(...) U_LOG_IFL_T(debug_get_log_option_svr_log(), __VA_ARGS__)
#define SVR_DEBUG(...) U_LOG_IFL_D(debug_get_log_option_svr_log(), __VA_ARGS__)
#define SVR_INFO(...) U_LOG_IFL_I(debug_get_log_option_svr_log(), __VA_ARGS__)
#define SVR_WARN(...) U_LOG_IFL_W(debug_get_log_option_svr_log(), __VA_ARGS__)
#define SVR_ERROR(...) U_LOG_IFL_E(debug_get_log_option_svr_log(), __VA_ARGS__)

static const char *driver_list[] = {
    "simula",
};

struct simula_builder
{
	struct xrt_builder base;
	struct svr_two_displays_distortion display_distortion;
};

bool
process_poly_values(const cJSON *values, struct svr_display_distortion_polynomial_values *out_values)
{
	bool good = true;
	good = good && u_json_get_float(u_json_get(values, "k1"), &out_values->k1);
	good = good && u_json_get_float(u_json_get(values, "k3"), &out_values->k3);
	good = good && u_json_get_float(u_json_get(values, "k5"), &out_values->k5);
	good = good && u_json_get_float(u_json_get(values, "k7"), &out_values->k7);
	good = good && u_json_get_float(u_json_get(values, "k9"), &out_values->k9);
	return good;
}

static bool
process_config(const char *config_path, struct svr_two_displays_distortion *out_dist)
{
	char *file_content = u_file_read_content_from_path(config_path);
	if (file_content == NULL) {
		U_LOG_E("The file at \"%s\" was unable to load. Either there wasn't a file there or it was empty.",
		        config_path);
		return false;
	}

	cJSON *config_json = cJSON_Parse(file_content);



	if (config_json == NULL) {
		const char *error_ptr = cJSON_GetErrorPtr();
		U_LOG_E("The JSON file at path \"%s\" was unable to parse", config_path);
		if (error_ptr != NULL) {
			U_LOG_E("because of an error before %s", error_ptr);
		}
		free((void *)file_content);
		return false;
	}
	free((void *)file_content);

	bool good = true;


	const cJSON *dd = u_json_get(config_json, "display_distortion");

	if (dd == NULL) {
		good = false;
		goto end;
	}

	// struct svr_two_displays_distortion distortion = {0};

	const char *eye_names[] = {"left_eye", "right_eye"};
	for (int eye = 0; eye < 2; eye++) {
		const cJSON *this_eye = u_json_get(dd, eye_names[eye]);
		if (this_eye == NULL) {
			good = false;
			goto end;
		}
		// u_json does its own null checking from here on out

		good = good && u_json_get_float(u_json_get(this_eye, "half_fov"), &out_dist->views[eye].half_fov);
		good = good && u_json_get_float(u_json_get(this_eye, "display_size_mm_x"),
		                                &out_dist->views[eye].display_size_mm.x);
		good = good && u_json_get_float(u_json_get(this_eye, "display_size_mm_y"),
		                                &out_dist->views[eye].display_size_mm.y);

		good = good && process_poly_values(u_json_get(this_eye, "params_red"), &out_dist->views[eye].red);
		good = good && process_poly_values(u_json_get(this_eye, "params_green"), &out_dist->views[eye].green);
		good = good && process_poly_values(u_json_get(this_eye, "params_blue"), &out_dist->views[eye].blue);
	}

end:



	cJSON_Delete(config_json);

	return good;
}

static xrt_result_t
svr_estimate_system(struct xrt_builder *xb, cJSON *config, struct xrt_prober *xp, struct xrt_builder_estimate *estimate)
{
	struct simula_builder *sb = (struct simula_builder *)xb;
	U_ZERO(estimate);

	const char *config_path = debug_get_option_simula_config_path();

	if (config_path == NULL) {
		// No failure occurred - the user just didn't ask for Simula
		return XRT_SUCCESS;
	}

	bool config_valid = process_config(config_path, &sb->display_distortion);

	if (!config_valid) {
		U_LOG_E("Failed to parse SimulaVR config");
		return XRT_SUCCESS;
	}

	struct xrt_prober_device **xpdevs = NULL;
	size_t xpdev_count = 0;
	xrt_result_t xret = XRT_SUCCESS;

	// Lock the device list
	xret = xrt_prober_lock_list(xp, &xpdevs, &xpdev_count);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	bool movidius = u_builder_find_prober_device(xpdevs, xpdev_count, REALSENSE_MOVIDIUS_VID,
	                                             REALSENSE_MOVIDIUS_PID, XRT_BUS_TYPE_USB);
	bool tm2 =
	    u_builder_find_prober_device(xpdevs, xpdev_count, REALSENSE_TM2_VID, REALSENSE_TM2_PID, XRT_BUS_TYPE_USB);

	if (!movidius && !tm2) {
		U_LOG_E("Simula enabled but couldn't find realsense device!");
		return XRT_SUCCESS;
	}

	// I think that ideally we want `movidius` - in that case I think when we grab the device, it reboots to
	// `tm2`


	estimate->maybe.head = true;
	estimate->certain.head = true;


	return XRT_SUCCESS;
}

static xrt_result_t
svr_open_system(struct xrt_builder *xb, cJSON *config, struct xrt_prober *xp, struct xrt_system_devices **out_xsysd)
{
	struct simula_builder *sb = (struct simula_builder *)xb;
	struct u_system_devices *usysd = u_system_devices_allocate();
	xrt_result_t result = XRT_SUCCESS;

	if (out_xsysd == NULL || *out_xsysd != NULL) {
		SVR_ERROR("Invalid output system pointer");
		result = XRT_ERROR_DEVICE_CREATION_FAILED;
		goto end;
	}

	struct xrt_device *t265_dev = rs_create_tracked_device_internal_slam(xp);

	struct xrt_device *svr_dev = svr_hmd_create(&sb->display_distortion);

	struct xrt_pose ident = XRT_POSE_IDENTITY;


	struct xrt_device *head_device = multi_create_tracking_override(
	    XRT_TRACKING_OVERRIDE_ATTACHED, svr_dev, t265_dev, XRT_INPUT_GENERIC_TRACKER_POSE, &ident);

	usysd->base.roles.head = head_device;
	usysd->base.xdevs[0] = usysd->base.roles.head;
	usysd->base.xdev_count = 1;


end:
	if (result == XRT_SUCCESS) {
		*out_xsysd = &usysd->base;
	} else {
		u_system_devices_destroy(&usysd);
	}

	return result;
}

static void
svr_destroy(struct xrt_builder *xb)
{
	free(xb);
}

/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_builder *
t_builder_simula_create(void)
{
	struct simula_builder *sb = U_TYPED_CALLOC(struct simula_builder);
	sb->base.estimate_system = svr_estimate_system;
	sb->base.open_system = svr_open_system;
	sb->base.destroy = svr_destroy;
	sb->base.identifier = "simula";
	sb->base.name = "SimulaVR headset";
	sb->base.driver_identifiers = driver_list;
	sb->base.driver_identifier_count = ARRAY_SIZE(driver_list);

	return &sb->base;
}
