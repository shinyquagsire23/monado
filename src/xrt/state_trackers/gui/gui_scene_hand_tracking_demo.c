// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Small hand-tracking demo scene
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "xrt/xrt_prober.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_results.h"
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_config_drivers.h"

#include "math/m_api.h"
#include "math/m_space.h"
#include "multi_wrapper/multi.h"
#include "realsense/rs_interface.h"
#include "tracking/t_hand_tracking.h"
#include "tracking/t_tracking.h"

#include "util/u_file.h"
#include "util/u_sink.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_builders.h"
#include "util/u_config_json.h"
#include "util/u_pretty_print.h"
#include "util/u_system_helpers.h"

#include "gui_common.h"


#ifdef XRT_BUILD_DRIVER_DEPTHAI
#include "depthai/depthai_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
#include "ht/ht_interface.h"
#endif


#if defined(XRT_BUILD_DRIVER_DEPTHAI) && defined(XRT_BUILD_DRIVER_HANDTRACKING)


void
gui_scene_hand_tracking_demo(struct gui_program *p)
{
	struct u_system_devices *usysd = u_system_devices_allocate();
	struct xrt_system_devices *xsysd = &usysd->base;
	struct depthai_slam_startup_settings settings = {0};
	struct xrt_device *ht_dev;

	settings.frames_per_second = 60;
	settings.half_size_ov9282 = false;
	settings.want_cameras = true;
	settings.want_imu = false;

	struct xrt_fs *the_fs = depthai_fs_slam(&usysd->xfctx, &settings);

	if (the_fs == NULL) {
		xrt_system_devices_destroy(&xsysd);
		return;
	}

	struct t_stereo_camera_calibration *calib = NULL;
	depthai_fs_get_stereo_calibration(the_fs, &calib);


	struct xrt_slam_sinks *hand_sinks = NULL;

	struct t_camera_extra_info extra_camera_info = XRT_STRUCT_INIT;
	extra_camera_info.views[0].boundary_type = HT_IMAGE_BOUNDARY_NONE;
	extra_camera_info.views[0].camera_orientation = CAMERA_ORIENTATION_0;
	extra_camera_info.views[1].boundary_type = HT_IMAGE_BOUNDARY_NONE;
	extra_camera_info.views[1].camera_orientation = CAMERA_ORIENTATION_0;

	int create_status = ht_device_create( //
	    &usysd->xfctx,                    //
	    calib,                            //
	    extra_camera_info,                //
	    &hand_sinks,                      //
	    &ht_dev);                         //
	t_stereo_camera_calibration_reference(&calib, NULL);
	if (create_status != 0) {
		xrt_system_devices_destroy(&xsysd);
		return;
	}

	xsysd->xdevs[xsysd->xdev_count++] = ht_dev;

	struct xrt_slam_sinks gen_lock = {0};
	u_sink_force_genlock_create( //
	    &usysd->xfctx,           //
	    hand_sinks->cams[0],     //
	    hand_sinks->cams[1],     //
	    &gen_lock.cams[0],       //
	    &gen_lock.cams[1]);      //

	xrt_fs_slam_stream_start(the_fs, &gen_lock);

	p->xsysd = xsysd;

	gui_scene_debug(p);
}


#else /* XRT_BUILD_DRIVER_DEPTHAI */


void
gui_scene_hand_tracking_demo(struct gui_program *p)
{
	// No-op
}


#endif /* XRT_BUILD_DRIVER_DEPTHAI */
