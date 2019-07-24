// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Various helpers for accessing @ref xrt_device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "math/m_api.h"
#include "util/u_misc.h"

#include "oxr_objects.h"


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
oxr_xdev_update(struct xrt_device *xdev, struct time_state *timekeeping)
{
	if (xdev != NULL) {
		xdev->update_inputs(xdev, timekeeping);
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

void
oxr_xdev_get_pose_at(struct oxr_logger *log,
                     struct oxr_instance *inst,
                     struct xrt_device *xdev,
                     enum xrt_input_name name,
                     struct xrt_pose *pose,
                     int64_t *timestamp)
{
	struct xrt_pose *offset = &xdev->tracking->offset;

	struct xrt_space_relation relation;
	U_ZERO(&relation);
	xdev->get_tracked_pose(xdev, name, inst->timekeeping, timestamp,
	                       &relation);

	// Add in the offset from the tracking system.
	math_relation_accumulate_transform(offset, &relation);

	// clang-format off
	bool valid_pos = (relation.relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) != 0;
	bool valid_ori = (relation.relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0;
	// clang-format on

	if (valid_ori) {
		pose->orientation = relation.pose.orientation;
	} else {
		// If the orientation is not valid just use the offset.
		pose->orientation = offset->orientation;
	}

	if (valid_pos) {
		pose->position = relation.pose.position;
	} else {
		// If the position is not valid just use the offset.
		pose->position = offset->position;
	}
}
