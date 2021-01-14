// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Camera based hand tracking driver code.
 * @author Christtoph Haag <christtoph.haag@collabora.com>
 * @ingroup drv_ht
 */

#include "ht_driver.h"
#include "util/u_device.h"
#include "util/u_var.h"
#include "util/u_debug.h"
#include <string.h>

struct ht_device
{
	struct xrt_device base;

	struct xrt_tracked_hand *tracker;

	struct xrt_space_relation hand_relation[2];
	struct u_hand_tracking u_tracking[2];

	struct xrt_tracking_origin tracking_origin;

	enum u_logging_level ll;
};

DEBUG_GET_ONCE_LOG_OPTION(ht_log, "HT_LOG", U_LOGGING_WARN)

#define HT_TRACE(htd, ...) U_LOG_XDEV_IFL_T(&htd->base, htd->ll, __VA_ARGS__)
#define HT_DEBUG(htd, ...) U_LOG_XDEV_IFL_D(&htd->base, htd->ll, __VA_ARGS__)
#define HT_INFO(htd, ...) U_LOG_XDEV_IFL_I(&htd->base, htd->ll, __VA_ARGS__)
#define HT_WARN(htd, ...) U_LOG_XDEV_IFL_W(&htd->base, htd->ll, __VA_ARGS__)
#define HT_ERROR(htd, ...) U_LOG_XDEV_IFL_E(&htd->base, htd->ll, __VA_ARGS__)

static inline struct ht_device *
ht_device(struct xrt_device *xdev)
{
	return (struct ht_device *)xdev;
}

static void
ht_device_update_inputs(struct xrt_device *xdev)
{
	// Empty
}

static void
ht_device_get_hand_tracking(struct xrt_device *xdev,
                            enum xrt_input_name name,
                            uint64_t at_timestamp_ns,
                            struct xrt_hand_joint_set *out_value)
{
	struct ht_device *htd = ht_device(xdev);

	enum xrt_hand hand;
	int index;

	if (name == XRT_INPUT_GENERIC_HAND_TRACKING_LEFT) {
		HT_TRACE(htd, "Get left hand tracking data");
		index = 0;
		hand = XRT_HAND_LEFT;
	} else if (name == XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
		HT_TRACE(htd, "Get right hand tracking data");
		index = 1;
		hand = XRT_HAND_RIGHT;
	} else {
		HT_ERROR(htd, "unknown input name for hand tracker");
		return;
	}



	htd->tracker->get_tracked_joints(htd->tracker, name, at_timestamp_ns, &htd->u_tracking[index].joints,
	                                 &htd->hand_relation[index]);
	htd->u_tracking[index].timestamp_ns = at_timestamp_ns;

	struct xrt_pose identity = {.orientation = {.x = 0, .y = 0, .z = 0, .w = 1},
	                            .position = {.x = 0, .y = 0, .z = 0}};

	u_hand_joints_set_out_data(&htd->u_tracking[index], hand, &htd->hand_relation[index], &identity, out_value);
}

static void
ht_device_destroy(struct xrt_device *xdev)
{
	struct ht_device *htd = ht_device(xdev);

	// Remove the variable tracking.
	u_var_remove_root(htd);

	u_device_free(&htd->base);
}

struct xrt_device *
ht_device_create(struct xrt_auto_prober *xap, cJSON *attached_data, struct xrt_prober *xp)
{
	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_NO_FLAGS;

	//! @todo 2 hands hardcoded
	int num_hands = 2;

	struct ht_device *htd = U_DEVICE_ALLOCATE(struct ht_device, flags, num_hands, 0);


	htd->base.tracking_origin = &htd->tracking_origin;
	htd->base.tracking_origin->type = XRT_TRACKING_TYPE_RGB;
	htd->base.tracking_origin->offset.position.x = 0.0f;
	htd->base.tracking_origin->offset.position.y = 0.0f;
	htd->base.tracking_origin->offset.position.z = 0.0f;
	htd->base.tracking_origin->offset.orientation.w = 1.0f;

	htd->ll = debug_get_log_option_ht_log();

	htd->base.update_inputs = ht_device_update_inputs;
	htd->base.get_hand_tracking = ht_device_get_hand_tracking;
	htd->base.destroy = ht_device_destroy;

	strncpy(htd->base.str, "Camera based Hand Tracker", XRT_DEVICE_NAME_LEN);

	htd->base.inputs[0].name = XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;
	htd->base.inputs[1].name = XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;

	htd->base.name = XRT_DEVICE_HAND_TRACKER;

	if (xp->tracking->create_tracked_hand(xp->tracking, &htd->base, &htd->tracker) < 0) {
		HT_ERROR(htd, "Failed to create hand tracker module");
		return NULL;
	}

	u_hand_joints_init_default_set(&htd->u_tracking[XRT_HAND_LEFT], XRT_HAND_LEFT, XRT_HAND_TRACKING_MODEL_CAMERA,
	                               1.0);
	u_hand_joints_init_default_set(&htd->u_tracking[XRT_HAND_RIGHT], XRT_HAND_RIGHT, XRT_HAND_TRACKING_MODEL_CAMERA,
	                               1.0);

	u_var_add_root(htd, "Camera based Hand Tracker", true);
	u_var_add_ro_text(htd, htd->base.str, "Name");

	HT_DEBUG(htd, "Hand Tracker initialized!");

	return &htd->base;
}
