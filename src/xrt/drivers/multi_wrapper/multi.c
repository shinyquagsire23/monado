// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: Apache-2.0
/*!
 * @file
 * @brief  Combination of multiple @xrt_device.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup drv_multi
 */

#include "multi.h"
#include "util/u_device.h"
#include "util/u_debug.h"

#include "math/m_api.h"
#include "math/m_space.h"

DEBUG_GET_ONCE_LOG_OPTION(multi_log, "MULTI_LOG", U_LOGGING_WARN)

#define MULTI_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->ll, __VA_ARGS__)
#define MULTI_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->ll, __VA_ARGS__)
#define MULTI_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->ll, __VA_ARGS__)
#define MULTI_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->ll, __VA_ARGS__)
#define MULTI_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->ll, __VA_ARGS__)

struct multi_device
{
	struct xrt_device base;
	enum u_logging_level ll;

	struct
	{
		struct xrt_device *target;
		struct xrt_device *tracker;
		enum xrt_input_name input_name;
		struct xrt_pose offset_inv;
	} tracking_override;
};


static void
get_tracked_pose(struct xrt_device *xdev,
                 enum xrt_input_name name,
                 uint64_t at_timestamp_ns,
                 struct xrt_space_relation *out_relation)
{
	struct multi_device *d = (struct multi_device *)xdev;
	struct xrt_device *tracker = d->tracking_override.tracker;
	enum xrt_input_name input_name = d->tracking_override.input_name;

	tracker->get_tracked_pose(tracker, input_name, at_timestamp_ns, out_relation);

	struct xrt_space_graph xsg = {0};
	m_space_graph_add_pose_if_not_identity(&xsg, &d->tracking_override.offset_inv);
	m_space_graph_add_relation(&xsg, out_relation);
	m_space_graph_resolve(&xsg, out_relation);
}

static void
destroy(struct xrt_device *xdev)
{
	struct multi_device *d = (struct multi_device *)xdev;

	xrt_device_destroy(&d->tracking_override.target);

	// we replaced the target device with us, but no the tracker
	// xrt_device_destroy(&d->tracking_override.tracker);

	free(d);
}

static void
get_hand_tracking(struct xrt_device *xdev,
                  enum xrt_input_name name,
                  uint64_t at_timestamp_ns,
                  struct xrt_hand_joint_set *out_value)
{
	struct multi_device *d = (struct multi_device *)xdev;
	struct xrt_device *target = d->tracking_override.target;
	xrt_device_get_hand_tracking(target, name, at_timestamp_ns, out_value);
}

static void
set_output(struct xrt_device *xdev, enum xrt_output_name name, union xrt_output_value *value)
{
	struct multi_device *d = (struct multi_device *)xdev;
	struct xrt_device *target = d->tracking_override.target;
	xrt_device_set_output(target, name, value);
}

static void
get_view_pose(struct xrt_device *xdev, struct xrt_vec3 *eye_relation, uint32_t view_index, struct xrt_pose *out_pose)
{
	struct multi_device *d = (struct multi_device *)xdev;
	struct xrt_device *target = d->tracking_override.target;
	xrt_device_get_view_pose(target, eye_relation, view_index, out_pose);
}

static bool
compute_distortion(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	struct multi_device *d = (struct multi_device *)xdev;
	struct xrt_device *target = d->tracking_override.target;
	return target->compute_distortion(target, view, u, v, result);
}

static void
update_inputs(struct xrt_device *xdev)
{
	struct multi_device *d = (struct multi_device *)xdev;
	struct xrt_device *target = d->tracking_override.target;
	xrt_device_update_inputs(target);
}


struct xrt_device *
multi_create_tracking_override(struct xrt_device *tracking_override_target,
                               struct xrt_device *tracking_override_tracker,
                               enum xrt_input_name tracking_override_input_name,
                               struct xrt_pose *offset)
{
	struct multi_device *d = U_TYPED_CALLOC(struct multi_device);

	if (!d) {
		return NULL;
	}

	d->ll = debug_get_log_option_multi_log();

	// mimic the tracking override target
	d->base = *tracking_override_target;

	// but take orientation and position tracking capabilities from tracker
	d->base.orientation_tracking_supported = tracking_override_target->orientation_tracking_supported;
	d->base.position_tracking_supported = tracking_override_target->position_tracking_supported;

	// because we use the tracking data of the tracker, we use its tracking origin instead
	d->base.tracking_origin = tracking_override_tracker->tracking_origin;

	// The offset describes the physical pose of the tracker in the space of the thing we want to track.
	// For a tracker that is physically attached at y=.1m to the tracked thing, when querying the pose for the
	// tracked thing, we want to transform its pose by y-=.1m relative to the tracker. Multiple target devices may
	// share a single tracker, therefore we can not simply adjust the tracker's tracking origin.
	math_pose_invert(offset, &d->tracking_override.offset_inv);

	d->tracking_override.target = tracking_override_target;
	d->tracking_override.tracker = tracking_override_tracker;
	d->tracking_override.input_name = tracking_override_input_name;

	d->base.get_tracked_pose = get_tracked_pose;
	d->base.destroy = destroy;
	d->base.get_hand_tracking = get_hand_tracking;
	d->base.set_output = set_output;
	d->base.update_inputs = update_inputs;
	d->base.compute_distortion = compute_distortion;
	d->base.get_view_pose = get_view_pose;

	return &d->base;
}
