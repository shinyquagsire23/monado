// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common protocol definition.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_results.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_instance.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_tracking.h"


#define IPC_MSG_SOCK_FILE "/tmp/monado_comp_ipc"
#define IPC_MAX_SWAPCHAIN_FDS 8
#define IPC_CRED_SIZE 1    // auth not implemented
#define IPC_BUF_SIZE 512   // must be >= largest message length in bytes
#define IPC_MAX_VIEWS 8    // max views we will return configs for
#define IPC_MAX_FORMATS 32 // max formats our server-side compositor supports
#define IPC_MAX_DEVICES 8  // max number of devices we will map via shared mem
#define IPC_MAX_LAYERS 16
#define IPC_MAX_SLOTS 128
#define IPC_MAX_CLIENTS 8
#define IPC_EVENT_QUEUE_SIZE 32

#define IPC_SHARED_MAX_DEVICES 8
#define IPC_SHARED_MAX_INPUTS 1024
#define IPC_SHARED_MAX_OUTPUTS 128

/*
 *
 * Shared memory structs.
 *
 */

struct ipc_shared_tracking_origin
{
	//! For debugging.
	char name[XRT_TRACKING_NAME_LEN];

	//! What can the state tracker expect from this tracking system.
	enum xrt_tracking_type type;

	//! Initial offset of the tracking origin.
	struct xrt_pose offset;
};

struct ipc_shared_device
{
	//! Enum identifier of the device.
	enum xrt_device_name name;

	//! Which tracking system origin is this device attached to.
	uint32_t tracking_origin_index;

	//! A string describing the device.
	char str[XRT_DEVICE_NAME_LEN];

	//! Number of inputs.
	uint32_t num_inputs;
	//! 'Offset' into the array of inputs where this devices inputs starts.
	uint32_t first_input_index;

	//! Number of outputs.
	uint32_t num_outputs;
	//! 'Offset' into the array of outputs where this devices outputs
	//! starts.
	uint32_t first_output_index;
};

/*!
 * Data for a single composition layer.
 *
 * Similar in function to @ref comp_layer
 *
 * @ingroup ipc
 */
struct ipc_layer_entry
{
	//! @todo what is this used for?
	uint32_t xdev_id;

	/*!
	 * Up to two indices of swapchains to use.
	 *
	 * How many are actually used depends on the value of @p data.type
	 */
	uint32_t swapchain_ids[2];

	/*!
	 * All basic (trivially-serializable) data associated with a layer,
	 * aside from which swapchain(s) are used.
	 */
	struct xrt_layer_data data;
};

/*!
 * Render state for a single client, including all layers.
 *
 * @ingroup ipc
 */
struct ipc_layer_slot
{
	enum xrt_blend_mode env_blend_mode;
	uint32_t num_layers;
	struct ipc_layer_entry layers[IPC_MAX_LAYERS];
};

/*!
 * A big struct that contains all data that is shared to a client, no pointers
 * allowed in this. To get the inputs of a device you go:
 *
 * ```C++
 * struct xrt_input *
 * helper(struct ipc_shared_memory *ism, uin32_t device_id, size_t input)
 * {
 * 	size_t index = ism->idevs[device_id]->first_input_index + input;
 * 	return &ism->inputs[index];
 * }
 * ```
 */
struct ipc_shared_memory
{
	// This array may be sparse.
	size_t num_itracks;
	struct ipc_shared_tracking_origin itracks[IPC_SHARED_MAX_DEVICES];
	size_t num_idevs;
	struct ipc_shared_device idevs[IPC_SHARED_MAX_DEVICES];

	struct
	{
		struct
		{
			/*!
			 * Pixel properties of this display, not in absolute
			 * screen coordinates that the compositor sees. So
			 * before any rotation is applied by xrt_view::rot.
			 *
			 * The xrt_view::display::w_pixels &
			 * xrt_view::display::h_pixels become the recommdnded
			 * image size for this view.
			 */
			struct
			{
				uint32_t w_pixels;
				uint32_t h_pixels;
			} display;

			/*!
			 * Fov expressed in OpenXR.
			 */
			struct xrt_fov fov;
		} views[2];
	} hmd;

	struct xrt_input inputs[IPC_SHARED_MAX_INPUTS];

	struct xrt_output outputs[IPC_SHARED_MAX_OUTPUTS];

	struct ipc_layer_slot slots[IPC_MAX_SLOTS];
};

struct ipc_client_list
{
	int32_t ids[IPC_MAX_CLIENTS];
};

/*!
 * State for a connected application.
 *
 * @ingroup ipc
 */
struct ipc_app_state
{
	bool primary_application;
	bool session_active;
	bool session_visible;
	bool session_focused;
	bool session_overlay;
	uint32_t z_order;
	pid_t pid;
	struct xrt_instance_info info;
};
