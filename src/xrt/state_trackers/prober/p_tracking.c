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

#include "targets_enabled_drivers.h"
#ifdef XRT_BUILD_PSMV
#include "tracking/t_tracking.h"
#endif

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

struct p_factory
{
	//! Base struct.
	struct xrt_tracking_factory base;

	// Owning prober.
	struct prober *p;

	//! Shared tracking origin.
	struct xrt_tracking_origin origin;

	//! For destruction of the node graph.
	struct xrt_frame_context xfctx;

#if defined(XRT_BUILD_PSMV) && defined(XRT_BUILD_PSVR)
	//! Keep track of how many psmv trackers that has been handed out.
	size_t num_xtmv;

	//! Pre-created psmv trackers.
	struct xrt_tracked_psmv *xtmv[2];

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

XRT_MAYBE_UNUSED static struct p_factory *
p_factory(struct xrt_tracking_factory *xfact)
{
	return (struct p_factory *)xfact;
}

#if defined(XRT_BUILD_PSMV) && defined(XRT_BUILD_PSVR)
static void
on_video_device(struct xrt_prober *xp,
                struct xrt_prober_device *pdev,
                const char *name,
                void *ptr)
{
	struct p_factory *fact = (struct p_factory *)ptr;

	if (fact->xfs != NULL || name == NULL) {
		return;
	}

	// Hardcoded to PS4 camera.
	if (strcmp(name, "USB Camera-OV580") != 0) {
		return;
	}

	xrt_prober_open_video_device(&fact->p->base, pdev, &fact->xfctx,
	                             &fact->xfs);
}

static void
p_factory_ensure_frameserver(struct p_factory *fact)
{
	// Already created.
	if (fact->xfs != NULL) {
		return;
	}

	//! @todo This is the place where we read the config from file.

	xrt_prober_list_video_devices(&fact->p->base, on_video_device, fact);

	if (fact->xfs == NULL) {
		return;
	}

	struct xrt_frame_sink *xsink = NULL;
	struct xrt_frame_sink *xsinks[4] = {0};
	struct xrt_colour_rgb_f32 rgb[2] = {{1.f, 0.f, 1.f}, {1.f, 0.f, 0.f}};

	// We create the two psmv trackers up front, but don't start them.
	t_psmv_create(&fact->xfctx, &rgb[0], &fact->xtmv[0], &xsinks[0]);
	t_psmv_create(&fact->xfctx, &rgb[1], &fact->xtmv[1], &xsinks[1]);
	t_psvr_create(&fact->xfctx, &fact->xtvr, &xsinks[2]);

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

	// Start the stream now.
	xrt_fs_stream_start(fact->xfs, xsink, 1);
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
#if defined(XRT_BUILD_PSMV) && defined(XRT_BUILD_PSVR)
	struct p_factory *fact = p_factory(xfact);
	struct xrt_tracked_psmv *xtmv = NULL;

	p_factory_ensure_frameserver(fact);

	if (fact->num_xtmv < ARRAY_SIZE(fact->xtmv)) {
		xtmv = fact->xtmv[fact->num_xtmv++];
	}

	if (xtmv == NULL) {
		return -1;
	}

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
#if defined(XRT_BUILD_PSMV) && defined(XRT_BUILD_PSVR)
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
	fact->origin.type = XRT_TRACKING_TYPE_RGB;
	fact->origin.offset.orientation.w = 1.0f;
	fact->p = p;

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

	// Drop any references to objects in the node graph.
	fact->xfs = NULL;
#if defined(XRT_BUILD_PSMV) && defined(XRT_BUILD_PSVR)
	fact->xtmv[0] = NULL;
	fact->xtmv[1] = NULL;
#endif

	// Take down the node graph.
	xrt_frame_context_destroy_nodes(&fact->xfctx);

	free(fact);
	p->base.tracking = NULL;
}
