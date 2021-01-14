// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tracking integration code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup st_prober
 */

#include "xrt/xrt_frame.h"
#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_tracking.h"

#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_drivers.h"
#ifdef XRT_HAVE_OPENCV
#include "tracking/t_tracking.h"
#endif

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_sink.h"
#include "p_prober.h"

#include <stdio.h>
#include <string.h>


/*
 *
 * Structs and defines.
 *
 */

/*!
 * @implements xrt_tracking_factory
 * @extends xrt_tracking_origin
 */
struct p_factory
{
	//! Base struct.
	struct xrt_tracking_factory base;

	// Owning prober.
	struct prober *p;

	// Have we tried to load the settings.
	bool tried_settings;

	// Settings for this tracking system.
	struct xrt_settings_tracking settings;

	//! Shared tracking origin.
	struct xrt_tracking_origin origin;

	//! For destruction of the node graph.
	struct xrt_frame_context xfctx;

#ifdef XRT_HAVE_OPENCV
	//! Data to be given to the trackers.
	struct t_stereo_camera_calibration *data;

	//! Keep track of how many psmv trackers have been handed out.
	size_t num_xtmv;

	//! Pre-created psmv trackers.
	struct xrt_tracked_psmv *xtmv[2];

	//! Have we handed out the hand tracker.
	bool started_xth;

	//! Pre-created hand trackers.
	struct xrt_tracked_hand *xth;

	//! Have we handed out the psvr tracker.
	bool started_xtvr;

	//! Pre-created psvr trackers.
	struct xrt_tracked_psvr *xtvr;
#endif

	// Frameserver.
	struct xrt_fs *xfs;
};


/*
 *
 * Functions.
 *
 */

static inline struct p_factory *
p_factory(struct xrt_tracking_factory *xfact)
{
	return (struct p_factory *)xfact;
}

#ifdef XRT_HAVE_OPENCV
static void
on_video_device(struct xrt_prober *xp,
                struct xrt_prober_device *pdev,
                const char *product,
                const char *manufacturer,
                const char *serial,
                void *ptr)
{
	struct p_factory *fact = (struct p_factory *)ptr;

	if (fact->xfs != NULL || product == NULL) {
		return;
	}

	if (strcmp(product, fact->settings.camera_name) != 0 && strcmp(product, "Video File") != 0) {
		return;
	}

	xrt_prober_open_video_device(&fact->p->base, pdev, &fact->xfctx, &fact->xfs);
}

static void
p_factory_ensure_frameserver(struct p_factory *fact)
{
	// Already created.
	if (fact->xfs != NULL) {
		return;
	}

	// We have already tried to load the settings.
	if (fact->tried_settings) {
		return;
	}

	// We have no tried the settings.
	fact->tried_settings = true;

	if (!p_json_get_tracking_settings(fact->p, &fact->settings)) {
		U_LOG_E(
		    "Could not setup PSVR and/or PSMV tracking, "
		    "see above.");
		return;
	}

	//! @todo This is the place where we read the config from file.

	xrt_prober_list_video_devices(&fact->p->base, on_video_device, fact);

	if (fact->xfs == NULL) {
		return;
	}

	// Open the calibration file.
	FILE *file = fopen(fact->settings.calibration_path, "rb");
	if (file == NULL) {
		return;
	}

	// Parse the calibration data from the file.
	if (!t_stereo_camera_calibration_load_v1(file, &fact->data)) {
		fclose(file);
		return;
	}
	fclose(file);

	struct xrt_frame_sink *xsink = NULL;
	struct xrt_frame_sink *xsinks[4] = {0};
	struct xrt_colour_rgb_f32 rgb[2] = {{1.f, 0.f, 0.f}, {1.f, 0.f, 1.f}};

	// We create the two psmv trackers up front, but don't start them.
	// clang-format off
	t_psmv_create(&fact->xfctx, &rgb[0], fact->data, &fact->xtmv[0], &xsinks[0]);
	t_psmv_create(&fact->xfctx, &rgb[1], fact->data, &fact->xtmv[1], &xsinks[1]);
	t_psvr_create(&fact->xfctx, fact->data, &fact->xtvr, &xsinks[2]);
	// clang-format on

	// Setup origin to the common one.
	fact->xtvr->origin = &fact->origin;
	fact->xtmv[0]->origin = &fact->origin;
	fact->xtmv[1]->origin = &fact->origin;

	// We create the default multi-channel hsv filter.
	struct t_hsv_filter_params params = T_HSV_DEFAULT_PARAMS();
	t_hsv_filter_create(&fact->xfctx, &params, xsinks, &xsink);

	// The filter only supports yuv or yuyv formats.
	u_sink_create_to_yuv_or_yuyv(&fact->xfctx, xsink, &xsink);

	// Put a queue before it to multi-thread the filter.
	u_sink_queue_create(&fact->xfctx, xsink, &xsink);

	struct xrt_frame_sink *ht_sink = NULL;
	t_hand_create(&fact->xfctx, fact->data, &fact->xth, &ht_sink);
	u_sink_create_to_r8g8b8_or_l8(&fact->xfctx, ht_sink, &ht_sink);
	u_sink_split_create(&fact->xfctx, xsink, ht_sink, &xsink);

	// Hardcoded quirk sink.
	struct u_sink_quirk_params qp;
	U_ZERO(&qp);

	switch (fact->settings.camera_type) {
	case XRT_SETTINGS_CAMERA_TYPE_REGULAR_MONO:
		qp.stereo_sbs = false;
		qp.ps4_cam = false;
		qp.leap_motion = false;
		break;
	case XRT_SETTINGS_CAMERA_TYPE_REGULAR_SBS:
		qp.stereo_sbs = true;
		qp.ps4_cam = false;
		qp.leap_motion = false;
		break;
	case XRT_SETTINGS_CAMERA_TYPE_PS4:
		qp.stereo_sbs = true;
		qp.ps4_cam = true;
		qp.leap_motion = false;
		break;
	case XRT_SETTINGS_CAMERA_TYPE_LEAP_MOTION:
		qp.stereo_sbs = true;
		qp.ps4_cam = false;
		qp.leap_motion = true;
		break;
	}

	u_sink_quirk_create(&fact->xfctx, xsink, &qp, &xsink);

	// Start the stream now.
	xrt_fs_stream_start(fact->xfs, xsink, XRT_FS_CAPTURE_TYPE_TRACKING, fact->settings.camera_mode);
}
#endif


/*
 *
 * Tracking factory functions.
 *
 */

static int
p_factory_create_tracked_psmv(struct xrt_tracking_factory *xfact,
                              struct xrt_device *xdev,
                              struct xrt_tracked_psmv **out_xtmv)
{
#ifdef XRT_HAVE_OPENCV
	struct p_factory *fact = p_factory(xfact);

	struct xrt_tracked_psmv *xtmv = NULL;

	p_factory_ensure_frameserver(fact);

	if (fact->num_xtmv < ARRAY_SIZE(fact->xtmv)) {
		xtmv = fact->xtmv[fact->num_xtmv];
	}

	if (xtmv == NULL) {
		return -1;
	}

	fact->num_xtmv++;

	t_psmv_start(xtmv);
	*out_xtmv = xtmv;

	return 0;
#else
	return -1;
#endif
}

static int
p_factory_create_tracked_psvr(struct xrt_tracking_factory *xfact,
                              struct xrt_device *xdev,
                              struct xrt_tracked_psvr **out_xtvr)
{
#ifdef XRT_HAVE_OPENCV
	struct p_factory *fact = p_factory(xfact);

	struct xrt_tracked_psvr *xtvr = NULL;

	p_factory_ensure_frameserver(fact);

	if (!fact->started_xtvr) {
		xtvr = fact->xtvr;
	}

	if (xtvr == NULL) {
		return -1;
	}

	fact->started_xtvr = true;
	t_psvr_start(xtvr);
	*out_xtvr = xtvr;

	return 0;
#else
	return -1;
#endif
}

static int
p_factory_create_tracked_hand(struct xrt_tracking_factory *xfact,
                              struct xrt_device *xdev,
                              struct xrt_tracked_hand **out_xth)
{
#ifdef XRT_HAVE_OPENCV
	struct p_factory *fact = p_factory(xfact);

	struct xrt_tracked_hand *xth = NULL;

	p_factory_ensure_frameserver(fact);

	if (!fact->started_xth) {
		xth = fact->xth;
	}

	if (xth == NULL) {
		return -1;
	}

	fact->started_xth = true;
	t_hand_start(xth);
	*out_xth = xth;

	return 0;
#else
	return -1;
#endif
}

/*
 *
 * "Exported" prober functions.
 *
 */

int
p_tracking_init(struct prober *p)
{
	struct p_factory *fact = U_TYPED_CALLOC(struct p_factory);

	fact->base.xfctx = &fact->xfctx;
	fact->base.create_tracked_psmv = p_factory_create_tracked_psmv;
	fact->base.create_tracked_psvr = p_factory_create_tracked_psvr;
	fact->base.create_tracked_hand = p_factory_create_tracked_hand;
	fact->origin.type = XRT_TRACKING_TYPE_RGB;
	fact->origin.offset.orientation.y = 1.0f;
	fact->origin.offset.position.z = -2.0f;
	fact->origin.offset.position.y = 1.0f;
	fact->p = p;

	snprintf(fact->origin.name, sizeof(fact->origin.name), "PSVR & PSMV tracking");

	u_var_add_root(fact, "Tracking Factory", false);
	u_var_add_pose(fact, &fact->origin.offset, "offset");

	// Finally set us as the tracking factory.
	p->base.tracking = &fact->base;

	return 0;
}

void
p_tracking_teardown(struct prober *p)
{
	if (p->base.tracking == NULL) {
		return;
	}

	struct p_factory *fact = p_factory(p->base.tracking);

	// Remove root
	u_var_remove_root(fact);

	// Drop any references to objects in the node graph.
	fact->xfs = NULL;
#ifdef XRT_HAVE_OPENCV
	fact->xtmv[0] = NULL;
	fact->xtmv[1] = NULL;
	fact->xtvr = NULL;
#endif

	// Take down the node graph.
	xrt_frame_context_destroy_nodes(&fact->xfctx);

#ifdef XRT_HAVE_OPENCV
	/*
	 * Needs to be done after the trackers has been destroyed.
	 *
	 * Does null checking and sets to null.
	 */
	t_stereo_camera_calibration_reference(&fact->data, NULL);
#endif

	free(fact);
	p->base.tracking = NULL;
}
