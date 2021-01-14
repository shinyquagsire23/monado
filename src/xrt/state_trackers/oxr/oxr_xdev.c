// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Various helpers for accessing @ref xrt_device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "os/os_time.h"

#include "math/m_api.h"
#include "math/m_space.h"

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
oxr_xdev_find_input(struct xrt_device *xdev, enum xrt_input_name name, struct xrt_input **out_input)
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
oxr_xdev_find_output(struct xrt_device *xdev, enum xrt_output_name name, struct xrt_output **out_output)
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
oxr_xdev_get_space_graph(struct oxr_logger *log,
                         struct oxr_instance *inst,
                         struct xrt_device *xdev,
                         enum xrt_input_name name,
                         XrTime at_time,
                         struct xrt_space_graph *xsg)
{
	// Convert at_time to monotonic and give to device.
	uint64_t at_timestamp_ns = time_state_ts_to_monotonic_ns(inst->timekeeping, at_time);

	struct xrt_space_relation *rel = m_space_graph_reserve(xsg);
	xrt_device_get_tracked_pose(xdev, name, at_timestamp_ns, rel);

	// Add in the offset from the tracking system.
	m_space_graph_add_pose(xsg, &xdev->tracking_origin->offset);
}

void
oxr_xdev_get_hand_tracking_at(struct oxr_logger *log,
                              struct oxr_instance *inst,
                              struct xrt_device *xdev,
                              enum xrt_input_name name,
                              XrTime at_time,
                              struct xrt_hand_joint_set *out_value)
{
	//! Convert at_time to monotonic and give to device.
	uint64_t at_timestamp_ns = time_state_ts_to_monotonic_ns(inst->timekeeping, at_time);

	struct xrt_hand_joint_set value;

	xrt_device_get_hand_tracking(xdev, name, at_timestamp_ns, &value);

	*out_value = value;
}
void
oxr_xdev_get_space_relation(struct oxr_logger *log,
                            struct oxr_instance *inst,
                            struct xrt_device *xdev,
                            enum xrt_input_name name,
                            XrTime at_time,
                            struct xrt_space_relation *out_relation)
{
	struct xrt_space_graph xsg = {0};
	oxr_xdev_get_space_graph(log, inst, xdev, name, at_time, &xsg);
	m_space_graph_resolve(&xsg, out_relation);
}
