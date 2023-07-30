// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Small hand-tracking demo scene
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "xrt/xrt_results.h"
#include "xrt/xrt_system.h"
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_config_drivers.h"


#include "math/m_api.h"
#include "math/m_space.h"
#include "tracking/t_hand_tracking.h"
#include "tracking/t_tracking.h"
#include "tracking/t_euroc_recorder.h"

#include "util/u_var.h"
#include "util/u_sink.h"
#include "util/u_debug.h"
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
gui_scene_record_euroc(struct gui_program *p)
{
	// Dummy so that gui_scene_debug doesn't try to probe.
	struct u_system_devices *usysd = u_system_devices_allocate();
	struct xrt_system_devices *xsysd = &usysd->base;
	p->xsysd = xsysd;

	struct xrt_frame_context xfctx;
	struct depthai_slam_startup_settings settings = {0};

	settings.frames_per_second = 60;
	// This is not what we use for HT/SLAM, but it's better here because it lets us see calibration patterns in more
	// detail.
	// For now, if you use this you will have to manually multiply all fx, fy, cx, cy by 0.5. No distortion
	// values, just camera projection values.
	settings.half_size_ov9282 = false;
	settings.want_cameras = true;
	settings.want_imu = true;

	struct xrt_fs *the_fs = depthai_fs_slam(&xfctx, &settings);

	if (the_fs == NULL) {
		xrt_system_devices_destroy(&xsysd);
		return;
	}


	struct xrt_slam_sinks *slam_sinks = NULL;
	slam_sinks = euroc_recorder_create(&xfctx, NULL, 2, false);

	u_var_add_root(usysd, "DepthAI Euroc recorder", 0);
	euroc_recorder_add_ui(slam_sinks, usysd, "");


	struct xrt_slam_sinks gen_lock = {0};
	u_sink_force_genlock_create( //
	    &xfctx,                  //
	    slam_sinks->cams[0],     //
	    slam_sinks->cams[1],     //
	    &gen_lock.cams[0],       //
	    &gen_lock.cams[1]);      //

	gen_lock.imu = slam_sinks->imu;

	xrt_fs_slam_stream_start(the_fs, &gen_lock);

	gui_scene_debug(p);
}


#else /* XRT_BUILD_DRIVER_DEPTHAI */


void
gui_scene_record_euroc(struct gui_program *p)
{
	// No-op
}


#endif /* XRT_BUILD_DRIVER_DEPTHAI */
