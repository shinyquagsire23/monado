// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  IPC Client device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_client
 */

#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "xrt/xrt_device.h"

#include "os/os_time.h"

#include "math/m_api.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"

#include "ipc_client.h"
#include "ipc_client_generated.h"


/*
 *
 * Structs and defines.
 *
 */

/*!
 * An IPC client proxy for an @ref xrt_device.
 * @implements xrt_device
 */
struct ipc_client_device
{
	struct xrt_device base;

	ipc_connection_t *ipc_c;

	uint32_t device_id;
};


/*
 *
 * Functions
 *
 */

static inline struct ipc_client_device *
ipc_client_device(struct xrt_device *xdev)
{
	return (struct ipc_client_device *)xdev;
}

static void
ipc_client_device_destroy(struct xrt_device *xdev)
{
	struct ipc_client_device *icd = ipc_client_device(xdev);

	// Remove the variable tracking.
	u_var_remove_root(icd);

	// We do not own these, so don't free them.
	icd->base.inputs = NULL;
	icd->base.outputs = NULL;

	// Free this device with the helper.
	u_device_free(&icd->base);
}

static void
ipc_client_device_update_inputs(struct xrt_device *xdev)
{
	struct ipc_client_device *icd = ipc_client_device(xdev);

	xrt_result_t r =
	    ipc_call_device_update_input(icd->ipc_c, icd->device_id);
	if (r != XRT_SUCCESS) {
		IPC_DEBUG(icd->ipc_c, "IPC: Error sending input update!");
	}
}

static void
ipc_client_device_get_tracked_pose(struct xrt_device *xdev,
                                   enum xrt_input_name name,
                                   uint64_t at_timestamp_ns,
                                   uint64_t *out_relation_timestamp_ns,
                                   struct xrt_space_relation *out_relation)
{
	struct ipc_client_device *icd = ipc_client_device(xdev);

	xrt_result_t r = ipc_call_device_get_tracked_pose(
	    icd->ipc_c, icd->device_id, name, at_timestamp_ns,
	    out_relation_timestamp_ns, out_relation);
	if (r != XRT_SUCCESS) {
		IPC_DEBUG(icd->ipc_c, "IPC: Error sending input update!");
	}
}

static void
ipc_client_device_get_view_pose(struct xrt_device *xdev,
                                struct xrt_vec3 *eye_relation,
                                uint32_t view_index,
                                struct xrt_pose *out_pose)
{
	// Empty
}

static void
ipc_client_device_set_output(struct xrt_device *xdev,
                             enum xrt_output_name name,
                             union xrt_output_value *value)
{
	struct ipc_client_device *icd = ipc_client_device(xdev);

	xrt_result_t r =
	    ipc_call_device_set_output(icd->ipc_c, icd->device_id, name, value);
	if (r != XRT_SUCCESS) {
		IPC_DEBUG(icd->ipc_c, "IPC: Error sending set output!");
	}
}

/*!
 * @public @memberof ipc_client_device
 */
struct xrt_device *
ipc_client_device_create(ipc_connection_t *ipc_c,
                         struct xrt_tracking_origin *xtrack,
                         uint32_t device_id)
{
	// Helpers.
	struct ipc_shared_memory *ism = ipc_c->ism;
	struct ipc_shared_device *idev = &ism->idevs[device_id];

	// Allocate and setup the basics.
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD);
	struct ipc_client_device *icd =
	    U_DEVICE_ALLOCATE(struct ipc_client_device, flags, 0, 0);
	icd->ipc_c = ipc_c;
	icd->base.update_inputs = ipc_client_device_update_inputs;
	icd->base.get_tracked_pose = ipc_client_device_get_tracked_pose;
	icd->base.get_view_pose = ipc_client_device_get_view_pose;
	icd->base.set_output = ipc_client_device_set_output;
	icd->base.destroy = ipc_client_device_destroy;

	// Start copying the information from the idev.
	icd->base.tracking_origin = xtrack;
	icd->base.name = idev->name;
	icd->device_id = device_id;

	// Print name.
	snprintf(icd->base.str, XRT_DEVICE_NAME_LEN, "%s", idev->str);

	// Setup inputs, by pointing directly to the shared memory.
	assert(idev->num_inputs > 0);
	icd->base.inputs = &ism->inputs[idev->first_input_index];
	icd->base.num_inputs = idev->num_inputs;

	// Setup outputs, if any point directly into the shared memory.
	icd->base.num_outputs = idev->num_outputs;
	if (idev->num_outputs > 0) {
		icd->base.outputs = &ism->outputs[idev->first_output_index];
	} else {
		icd->base.outputs = NULL;
	}

	// Setup variable tracker.
	u_var_add_root(icd, icd->base.str, true);
	u_var_add_ro_u32(icd, &icd->device_id, "device_id");

	return &icd->base;
}
