// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  An @ref xrt_frame_sink that does gst things.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Aaron Boxer <aaron.boxer@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "gstreamer/gst_internal.h"
#include "gstreamer/gst_pipeline.h"


/*
 *
 * Internal pipeline functions.
 *
 */

static void
break_apart(struct xrt_frame_node *node)
{
	struct gstreamer_pipeline *gp = container_of(node, struct gstreamer_pipeline, node);

	/*
	 * This function is called when we are shutting down, after returning
	 * from this function you are not allowed to call any other nodes in the
	 * graph. But it must be safe for other nodes to call any normal
	 * functions on us. Once the context is done calling break_aprt on all
	 * objects it will call destroy on them.
	 */

	(void)gp;
}

static void
destroy(struct xrt_frame_node *node)
{
	struct gstreamer_pipeline *gp = container_of(node, struct gstreamer_pipeline, node);

	/*
	 * All of the nodes has been broken apart and none of our functions will
	 * be called, it's now safe to destroy and free ourselves.
	 */

	free(gp);
}


/*
 *
 * Exported functions.
 *
 */

void
gstreamer_pipeline_play(struct gstreamer_pipeline *gp)
{
	U_LOG_D("Starting pipeline");

	gst_element_set_state(gp->pipeline, GST_STATE_PLAYING);
}

void
gstreamer_pipeline_stop(struct gstreamer_pipeline *gp)
{
	U_LOG_D("Stopping pipeline");

	// Settle the pipeline.
	U_LOG_T("Sending EOS");
	gst_element_send_event(gp->pipeline, gst_event_new_eos());

	// Wait for EOS message on the pipeline bus.
	U_LOG_T("Waiting for EOS");
	GstMessage *msg = NULL;
	msg = gst_bus_timed_pop_filtered(GST_ELEMENT_BUS(gp->pipeline), GST_CLOCK_TIME_NONE,
	                                 GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
	//! @todo Should check if we got an error message here or an eos.
	(void)msg;

	// Completely stop the pipeline.
	U_LOG_T("Setting to NULL");
	gst_element_set_state(gp->pipeline, GST_STATE_NULL);
}

void
gstreamer_pipeline_create_from_string(struct xrt_frame_context *xfctx,
                                      const char *pipeline_string,
                                      struct gstreamer_pipeline **out_gp)
{
	gst_init(NULL, NULL);

	struct gstreamer_pipeline *gp = U_TYPED_CALLOC(struct gstreamer_pipeline);
	gp->node.break_apart = break_apart;
	gp->node.destroy = destroy;
	gp->xfctx = xfctx;

	// Setup pipeline.
	gp->pipeline = gst_parse_launch(pipeline_string, NULL);

	/*
	 * Add ourselves to the context so we are destroyed.
	 * This is done once we know everything is completed.
	 */
	xrt_frame_context_add(xfctx, &gp->node);

	*out_gp = gp;
}

void
gstreamer_pipeline_create_autovideo_sink(struct xrt_frame_context *xfctx,
                                         const char *appsrc_name,
                                         struct gstreamer_pipeline **out_gp)
{
	gst_init(NULL, NULL);

	struct gstreamer_pipeline *gp = U_TYPED_CALLOC(struct gstreamer_pipeline);
	gp->node.break_apart = break_apart;
	gp->node.destroy = destroy;
	gp->xfctx = xfctx;

	// Setup pipeline.
	gp->pipeline = gst_pipeline_new("pipeline");
	GstElement *appsrc = gst_element_factory_make("appsrc", appsrc_name);
	GstElement *conv = gst_element_factory_make("videoconvert", "conv");
	GstElement *scale = gst_element_factory_make("videoscale", "scale");
	GstElement *videosink = gst_element_factory_make("autovideosink", "videosink");

	gst_bin_add_many(GST_BIN(gp->pipeline), //
	                 appsrc,                //
	                 conv,                  //
	                 scale,                 //
	                 videosink,             //
	                 NULL);
	gst_element_link_many(appsrc,    //
	                      conv,      //
	                      scale,     //
	                      videosink, //
	                      NULL);

	/*
	 * Add ourselves to the context so we are destroyed.
	 * This is done once we know everything is completed.
	 */
	xrt_frame_context_add(xfctx, &gp->node);

	*out_gp = gp;
}
