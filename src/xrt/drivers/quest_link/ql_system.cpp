/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 *
 */

/*!
 * @file
 * @brief  Meta Quest Link headset tracking system
 *
 * The Quest Link system provides the HID/USB polling thread
 * and dispatches incoming packets to the HMD and controller
 * implementations.
 *
 * Ported from OpenHMD
 *
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_quest_link
 */

/* Meta Quest Link Driver - HID/USB Driver Implementation */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <inttypes.h>

#include "math/m_api.h"
#include "math/m_vec3.h"

#include "os/os_time.h"

#include "util/u_device.h"
#include "util/u_distortion_mesh.h"
#include "util/u_trace_marker.h"
#include "util/u_var.h"

#include "xrt/xrt_device.h"

#include "ql_system.h"
#include "ql_controller.h"
#include "ql_hmd.h"
#include "ql_utils.h"
#include "ql_xrsp.h"

enum u_logging_level ql_log_level;

static void *
ql_run_thread(void *ptr);
static void
ql_system_free(struct ql_system *sys);

struct ql_system *
ql_system_create(struct xrt_prober *xp,
						 struct xrt_prober_device *dev,
                     	 const unsigned char *hmd_serial_no,
                     	 int if_num)
{
	int ret;
	 struct ql_hmd *hmd;
	struct ql_controller *ctrl_left, *ctrl_right;

	DRV_TRACE_MARKER();

	struct ql_system *sys = U_TYPED_CALLOC(struct ql_system);
	sys->base.type = XRT_TRACKING_TYPE_NONE;
	sys->base.offset.orientation.w = 1.0f;

	/* Init refcount */
	sys->ref.count = 1;

	ret = os_mutex_init(&sys->dev_mutex);
	if (ret != 0) {
		QUEST_LINK_ERROR("Failed to init device mutex");
		goto cleanup;
	}

	/* Create the HMD now. Controllers are created in the
     * ql_system_get_controller() call later */
    hmd = ql_hmd_create(sys, hmd_serial_no, &sys->hmd_config);
    if (hmd == NULL) {
        QUEST_LINK_ERROR("Failed to create Meta Quest Link device.");
        goto cleanup;
    }

    sys->hmd = hmd;

    /* Create the HMD now. Controllers are created in the
     * ql_system_get_controller() call later */
    ctrl_left = ql_controller_create(sys, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER);
    if (ctrl_left == NULL) {
        QUEST_LINK_ERROR("Failed to create Meta Quest Link controller.");
        goto cleanup;
    }

    ctrl_right = ql_controller_create(sys, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER);
    if (ctrl_left == NULL) {
        QUEST_LINK_ERROR("Failed to create Meta Quest Link controller.");
        goto cleanup;
    }

    sys->controllers[0] = ctrl_left;
    sys->controllers[1] = ctrl_right;

	ret = ql_xrsp_host_create(&sys->xrsp_host, dev->vendor_id, dev->product_id, if_num);
	if (ret != 0) {
		QUEST_LINK_ERROR("Failed to init XRSP");
		goto cleanup;
	}

	sys->xrsp_host.sys = sys;

	/* Turn on the headset and display connection */

	QUEST_LINK_DEBUG("Meta Quest Link driver ready");

	return sys;

cleanup:
	if (sys->hmd != NULL) {
		xrt_device_destroy((struct xrt_device **)&sys->hmd);
	}
	ql_system_reference(&sys, NULL);
	return NULL;
}

static void
ql_system_free(struct ql_system *sys)
{
	/* Stop the packet reading thread */
	os_thread_helper_destroy(&sys->oth);

	/* Stop all the frame processing (has to happen before the cameras
	 * and tracker are destroyed */
	xrt_frame_context_destroy_nodes(&sys->xfctx);

	/* Close USB */
	ql_xrsp_host_destroy(&sys->xrsp_host);

	/* Free the camera */
	
	/* Free tracker */

	os_mutex_destroy(&sys->dev_mutex);

	free(sys);
}

/* Reference count handling for ql_system */
void
ql_system_reference(struct ql_system **dst, struct ql_system *src)
{
	struct ql_system *old_dst = *dst;

	if (old_dst == src) {
		return;
	}

	if (src) {
		xrt_reference_inc(&src->ref);
	}

	*dst = src;

	if (old_dst) {
		if (xrt_reference_dec(&old_dst->ref)) {
			ql_system_free(old_dst);
		}
	}
}

struct os_hid_device *
ql_system_hid_handle(struct ql_system *sys)
{
	return sys->handles[HMD_HID];
}

struct xrt_device *
ql_system_get_hmd(struct ql_system *sys)
{
	return (struct xrt_device *)sys->hmd;
}

void
ql_system_remove_hmd(struct ql_system *sys)
{
	os_mutex_lock(&sys->dev_mutex);
	sys->hmd = NULL;
	os_mutex_unlock(&sys->dev_mutex);
}

static bool handle_packets(struct ql_system *sys)
{

	return true;
}

static void *
ql_run_thread(void *ptr)
{
	DRV_TRACE_MARKER();

	struct ql_system *sys = (struct ql_system *)ptr;

	os_thread_helper_lock(&sys->oth);
	while (os_thread_helper_is_running_locked(&sys->oth)) {
		os_thread_helper_unlock(&sys->oth);

		printf(".\n");
		bool success = handle_packets(sys);

		if (success) {
			//radio
			os_mutex_lock(&sys->dev_mutex);
			//cam
			os_mutex_unlock(&sys->dev_mutex);
		}

		os_thread_helper_lock(&sys->oth);

		if (!success) {
			break;
		}

		if (os_thread_helper_is_running_locked(&sys->oth)) {
			os_nanosleep(U_TIME_1MS_IN_NS / 2);
		}
	}
	os_thread_helper_unlock(&sys->oth);

	QUEST_LINK_DEBUG("Exiting packet reading thread");

	return NULL;
}
