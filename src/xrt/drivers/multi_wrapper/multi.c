// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Combination of multiple @ref xrt_device.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup drv_multi
 */

#include "math/m_api.h"
#include "math/m_space.h"

#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"

#include "multi.h"


DEBUG_GET_ONCE_LOG_OPTION(multi_log, "MULTI_LOG", U_LOGGING_WARN)

#define MULTI_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->log_level, __VA_ARGS__)
#define MULTI_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->log_level, __VA_ARGS__)
#define MULTI_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->log_level, __VA_ARGS__)
#define MULTI_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->log_level, __VA_ARGS__)
#define MULTI_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->log_level, __VA_ARGS__)

struct multi_device
{
	struct xrt_device base;
	enum u_logging_level log_level;

	struct
	{
		struct xrt_device *target;
		struct xrt_device *tracker;
		enum xrt_input_name input_name;
		struct xrt_pose offset_inv;
	} tracking_override;

	enum xrt_tracking_override_type override_type;
};

static void
direct_override(struct multi_device *d,
                struct xrt_space_relation *tracker_relation,
                struct xrt_space_relation *out_relation)
{
	struct xrt_relation_chain xrc = {0};
	m_relation_chain_push_pose_if_not_identity(&xrc, &d->tracking_override.offset_inv);
	m_relation_chain_push_relation(&xrc, tracker_relation);
	m_relation_chain_resolve(&xrc, out_relation);
}

static void
attached_override(struct multi_device *d,
                  struct xrt_space_relation *target_relation,
                  struct xrt_pose *target_offset,
                  struct xrt_space_relation *tracker_relation,
                  struct xrt_pose *tracker_offset,
                  struct xrt_space_relation *in_target_space,
                  struct xrt_space_relation *out_relation)
{
	/* Example:
	 * - target: hand tracking xrt_device
	 * - tracker: positional tracker that the target is physically attached to
	 * - in_target_space: a tracked hand, relative to target's tracking origin
	 */

	// XXX TODO tracking origin offsets
	// m_relation_chain_push_inverted_pose_if_not_identity(&xrc, tracker_offset);
	// m_relation_chain_push_pose_if_not_identity(&xrc, target_offset);

	struct xrt_relation_chain xrc = {0};
	m_relation_chain_push_relation(&xrc, target_relation);
	m_relation_chain_push_pose_if_not_identity(&xrc, &d->tracking_override.offset_inv);
	m_relation_chain_push_relation(&xrc, tracker_relation);
	m_relation_chain_push_relation(&xrc, in_target_space);
	m_relation_chain_resolve(&xrc, out_relation);
}

static void
get_tracked_pose(struct xrt_device *xdev,
                 enum xrt_input_name name,
                 uint64_t at_timestamp_ns,
                 struct xrt_space_relation *out_relation)
{
	struct multi_device *d = (struct multi_device *)xdev;
	struct xrt_device *tracker = d->tracking_override.tracker;
	enum xrt_input_name tracker_input_name = d->tracking_override.input_name;

	struct xrt_space_relation tracker_relation;

	xrt_device_get_tracked_pose(tracker, tracker_input_name, at_timestamp_ns, &tracker_relation);

	switch (d->override_type) {
	case XRT_TRACKING_OVERRIDE_DIRECT: {
		direct_override(d, &tracker_relation, out_relation);
	} break;
	case XRT_TRACKING_OVERRIDE_ATTACHED: {
		struct xrt_device *target = d->tracking_override.target;

		struct xrt_space_relation target_relation;
		xrt_device_get_tracked_pose(target, name, at_timestamp_ns, &target_relation);


		// just use the origin of the tracker space as reference frame
		struct xrt_space_relation in_target_space;
		m_space_relation_ident(&in_target_space);
		in_target_space.relation_flags = tracker_relation.relation_flags;

		struct xrt_pose *target_offset = &d->tracking_override.target->tracking_origin->offset;
		struct xrt_pose *tracker_offset = &d->tracking_override.tracker->tracking_origin->offset;

		attached_override(d, &target_relation, target_offset, &tracker_relation, tracker_offset,
		                  &in_target_space, out_relation);
	} break;
	}
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
                  struct xrt_hand_joint_set *out_value,
                  uint64_t *out_timestamp_ns)
{
	struct multi_device *d = (struct multi_device *)xdev;
	struct xrt_device *target = d->tracking_override.target;
	xrt_device_get_hand_tracking(target, name, at_timestamp_ns, out_value, out_timestamp_ns);
	if (!out_value->is_active) {
		return;
	}

	struct xrt_device *tracker = d->tracking_override.tracker;
	struct xrt_space_relation tracker_relation;
	xrt_device_get_tracked_pose(tracker, d->tracking_override.input_name, *out_timestamp_ns, &tracker_relation);


	switch (d->override_type) {
	case XRT_TRACKING_OVERRIDE_DIRECT: direct_override(d, &tracker_relation, &out_value->hand_pose); break;
	case XRT_TRACKING_OVERRIDE_ATTACHED: {

		// struct xrt_space_relation target_relation;
		// xrt_device_get_tracked_pose(target, name, at_timestamp_ns, &target_relation);


		// just use the origin of the tracker space as reference frame
		struct xrt_space_relation in_target_space;
		m_space_relation_ident(&in_target_space);
		in_target_space.relation_flags = tracker_relation.relation_flags;

		struct xrt_pose *target_offset = &d->tracking_override.target->tracking_origin->offset;
		struct xrt_pose *tracker_offset = &d->tracking_override.tracker->tracking_origin->offset;

		attached_override(d, &out_value->hand_pose, target_offset, &tracker_relation, tracker_offset,
		                  &in_target_space, &out_value->hand_pose);
	} break;
	}
}

static void
set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
	struct multi_device *d = (struct multi_device *)xdev;
	struct xrt_device *target = d->tracking_override.target;
	xrt_device_set_output(target, name, value);
}

static void
get_view_poses(struct xrt_device *xdev,
               const struct xrt_vec3 *default_eye_relation,
               uint64_t at_timestamp_ns,
               uint32_t view_count,
               struct xrt_space_relation *out_head_relation,
               struct xrt_fov *out_fovs,
               struct xrt_pose *out_poses)
{
	struct multi_device *d = (struct multi_device *)xdev;
	struct xrt_device *target = d->tracking_override.target;
	xrt_device_get_view_poses(target, default_eye_relation, at_timestamp_ns, view_count, out_head_relation,
	                          out_fovs, out_poses);

	/*
	 * Use xrt_device_ function to be sure it is exactly
	 * like if the state-tracker called this function.
	 */
	xrt_device_get_tracked_pose(xdev, XRT_INPUT_GENERIC_HEAD_POSE, at_timestamp_ns, out_head_relation);
}

static bool
compute_distortion(struct xrt_device *xdev, uint32_t view, float u, float v, struct xrt_uv_triplet *result)
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
multi_create_tracking_override(enum xrt_tracking_override_type override_type,
                               struct xrt_device *tracking_override_target,
                               struct xrt_device *tracking_override_tracker,
                               enum xrt_input_name tracking_override_input_name,
                               struct xrt_pose *offset)
{
	struct multi_device *d = U_TYPED_CALLOC(struct multi_device);

	if (!d) {
		return NULL;
	}

	d->log_level = debug_get_log_option_multi_log();
	d->override_type = override_type;

	// mimic the tracking override target
	d->base = *tracking_override_target;

	// but take orientation and position tracking capabilities from tracker
	d->base.orientation_tracking_supported = tracking_override_tracker->orientation_tracking_supported;
	d->base.position_tracking_supported = tracking_override_tracker->position_tracking_supported;

	// because we use the tracking data of the tracker, we use its tracking origin instead
	d->base.tracking_origin = tracking_override_tracker->tracking_origin;

	// The offset describes the physical pose of the tracker in the space of the thing we want to track.
	// For a tracker that is physically attached at y=.1m to the tracked thing, when querying the pose for the
	// tracked thing, we want to transform its pose by y-=.1m relative to the tracker. Multiple target devices may
	// share a single tracker, therefore we cannot simply adjust the tracker's tracking origin.
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
	d->base.get_view_poses = get_view_poses;

	return &d->base;
}
