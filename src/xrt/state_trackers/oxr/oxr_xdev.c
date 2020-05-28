// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Various helpers for accessing @ref xrt_device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "os/os_time.h"
#include "math/m_api.h"
#include "util/u_time.h"
#include "util/u_misc.h"

#include "oxr_objects.h"

#include <assert.h>


void
oxr_xdev_destroy(struct xrt_device **xdev_ptr)
{
	struct xrt_device *xdev = *xdev_ptr;

	if (xdev == NULL) {
		return;
	}

	xdev->destroy(xdev);
	*xdev_ptr = NULL;
}

void
oxr_xdev_update(struct xrt_device *xdev)
{
	if (xdev != NULL) {
		xdev->update_inputs(xdev);
	}
}

bool
oxr_xdev_find_input(struct xrt_device *xdev,
                    enum xrt_input_name name,
                    struct xrt_input **out_input)
{
	*out_input = NULL;
	if (xdev == NULL) {
		return false;
	}

	for (uint32_t i = 0; i < xdev->num_inputs; i++) {
		if (xdev->inputs[i].name != name) {
			continue;
		}

		*out_input = &xdev->inputs[i];
		return true;
	}
	return false;
}

bool
oxr_xdev_find_output(struct xrt_device *xdev,
                     enum xrt_output_name name,
                     struct xrt_output **out_output)
{
	if (xdev == NULL) {
		return false;
	}

	for (uint32_t i = 0; i < xdev->num_outputs; i++) {
		if (xdev->outputs[i].name != name) {
			continue;
		}

		*out_output = &xdev->outputs[i];
		return true;
	}
	return false;
}

static void
ensure_valid_position_and_orientation(struct xrt_space_relation *relation,
                                      const struct xrt_pose *fallback)
{
	// clang-format off
	bool valid_pos = (relation->relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) != 0;
	bool valid_ori = (relation->relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0;
	// clang-format on

	if (!valid_ori) {
		relation->pose.orientation = fallback->orientation;
	}

	if (!valid_pos) {
		relation->pose.position = fallback->position;
	}
}

void
oxr_xdev_get_relation_at(struct oxr_logger *log,
                         struct oxr_instance *inst,
                         struct xrt_device *xdev,
                         enum xrt_input_name name,
                         XrTime at_time,
                         uint64_t *out_relation_timestamp_ns,
                         struct xrt_space_relation *out_relation)
{
	struct xrt_pose *offset = &xdev->tracking_origin->offset;

	//! @todo Convert at_time to monotonic and give to device.
	uint64_t at_timestamp_ns = os_monotonic_get_ns();
	(void)at_time;

	uint64_t relation_timestamp_ns = 0;

	struct xrt_space_relation relation;
	U_ZERO(&relation);

	xrt_device_get_tracked_pose(xdev, name, at_timestamp_ns,
	                            &relation_timestamp_ns, &relation);

	// Add in the offset from the tracking system.
	math_relation_apply_offset(offset, &relation);

	// Always make those to base things valid.
	ensure_valid_position_and_orientation(&relation, offset);

	*out_relation_timestamp_ns = time_state_monotonic_to_ts_ns(
	    inst->timekeeping, relation_timestamp_ns);

	*out_relation = relation;
}

void
oxr_xdev_get_pose_at(struct oxr_logger *log,
                     struct oxr_instance *inst,
                     struct xrt_device *xdev,
                     enum xrt_input_name name,
                     XrTime at_time,
                     uint64_t *out_pose_timestamp_ns,
                     struct xrt_space_relation *out_relation)
{
	struct xrt_space_relation relation;
	U_ZERO(&relation);

	oxr_xdev_get_relation_at(log, inst, xdev, name, at_time,
	                         out_pose_timestamp_ns, &relation);

	*out_relation = relation;
}
