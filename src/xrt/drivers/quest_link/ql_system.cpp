/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * Copyright 2022-2023 Max Thomas
 * SPDX-License-Identifier: BSL-1.0
 *
 */
/*!
 * @file
 * @brief  Meta Quest Link headset tracking system
 *
 * The Quest Link system instantiates the HMD, controller,
 * and hand devices, and manages refcounts
 *
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

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
#include "ql_hands.h"
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

	// Create the HMD
    hmd = ql_hmd_create(sys, hmd_serial_no);
    if (hmd == NULL) {
        QUEST_LINK_ERROR("Failed to create Meta Quest Link device.");
        goto cleanup;
    }

    // Assign HMD here if controller/hands need it
    sys->hmd = hmd;

    // Create the two controllers
    ctrl_left = ql_controller_create(sys, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER);
    if (ctrl_left == NULL) {
        QUEST_LINK_ERROR("Failed to create Meta Quest Link controller.");
        goto cleanup;
    }
    sys->controllers[0] = ctrl_left;

    ctrl_right = ql_controller_create(sys, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER);
    if (ctrl_right == NULL) {
        QUEST_LINK_ERROR("Failed to create Meta Quest Link controller.");
        goto cleanup;
    }
    sys->controllers[1] = ctrl_right;

    sys->hands = ql_hands_create(sys);
    if (sys->hands == NULL) {
        QUEST_LINK_ERROR("Failed to create Meta Quest Link hands.");
        goto cleanup;
    }

	ret = ql_xrsp_host_create(&sys->xrsp_host, dev->vendor_id, dev->product_id, if_num);
	if (ret != 0) {
		QUEST_LINK_ERROR("Failed to init XRSP");
		goto cleanup;
	}

	sys->xrsp_host.sys = sys;

	while (!hmd->sys->xrsp_host.ready_to_send_frames) {
		os_nanosleep(U_TIME_1MS_IN_NS * 10);
	}

	QUEST_LINK_DEBUG("Meta Quest Link driver ready");

	return sys;

cleanup:
	if (sys->hmd != NULL) {
		xrt_device_destroy((struct xrt_device **)&sys->hmd);
	}
	if (sys->controllers[0] != NULL) {
		xrt_device_destroy((struct xrt_device **)&sys->controllers[0]);
	}
	if (sys->controllers[1] != NULL) {
		xrt_device_destroy((struct xrt_device **)&sys->controllers[1]);
	}
	if (sys->hands != NULL) {
		xrt_device_destroy((struct xrt_device **)&sys->hands);
	}
	ql_system_reference(&sys, NULL);
	return NULL;
}

static void
ql_system_free(struct ql_system *sys)
{
	// Close USB
	ql_xrsp_host_destroy(&sys->xrsp_host);
	
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
