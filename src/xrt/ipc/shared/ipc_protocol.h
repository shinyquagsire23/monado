// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common protocol definition.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_shared
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_results.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_system.h"
#include "xrt/xrt_instance.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_tracking.h"

#include <sys/types.h>

#define IPC_MSG_SOCK_FILE "monado_comp_ipc"
#define IPC_MAX_SWAPCHAIN_HANDLES 8
#define IPC_CRED_SIZE 1    // auth not implemented
#define IPC_BUF_SIZE 512   // must be >= largest message length in bytes
#define IPC_MAX_VIEWS 8    // max views we will return configs for
#define IPC_MAX_FORMATS 32 // max formats our server-side compositor supports
#define IPC_MAX_DEVICES 8  // max number of devices we will map using shared mem
#define IPC_MAX_LAYERS 16
#define IPC_MAX_SLOTS 128
#define IPC_MAX_CLIENTS 8
#define IPC_EVENT_QUEUE_SIZE 32

#define IPC_SHARED_MAX_INPUTS 1024
#define IPC_SHARED_MAX_OUTPUTS 128
#define IPC_SHARED_MAX_BINDINGS 64

// example: v21.0.0-560-g586d33b5
#define IPC_VERSION_NAME_LEN 64

/*
 *
 * Shared memory structs.
 *
 */

/*!
 * A tracking in the shared memory area.
 *
 * @ingroup ipc
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

/*!
 * A binding in the shared memory area.
 *
 * @ingroup ipc
 */
struct ipc_shared_binding_profile
{
	enum xrt_device_name name;

	//! Number of inputs.
	uint32_t input_count;
	//! Offset into the array of pairs where this input bindings starts.
	uint32_t first_input_index;

	//! Number of outputs.
	uint32_t output_count;
	//! Offset into the array of pairs where this output bindings starts.
	uint32_t first_output_index;
};

/*!
 * A device in the shared memory area.
 *
 * @ingroup ipc
 */
struct ipc_shared_device
{
	//! Enum identifier of the device.
	enum xrt_device_name name;
	enum xrt_device_type device_type;

	//! Which tracking system origin is this device attached to.
	uint32_t tracking_origin_index;

	//! A string describing the device.
	char str[XRT_DEVICE_NAME_LEN];

	//! Number of bindings.
	uint32_t binding_profile_count;
	//! 'Offset' into the array of bindings where the bindings starts.
	uint32_t first_binding_profile_index;

	//! Number of inputs.
	uint32_t input_count;
	//! 'Offset' into the array of inputs where the inputs starts.
	uint32_t first_input_index;

	//! Number of outputs.
	uint32_t output_count;
	//! 'Offset' into the array of outputs where the outputs starts.
	uint32_t first_output_index;

	bool orientation_tracking_supported;
	bool position_tracking_supported;
	bool hand_tracking_supported;
	bool force_feedback_supported;
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
	uint32_t swapchain_ids[4];

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
	uint64_t display_time_ns;
	enum xrt_blend_mode env_blend_mode;
	uint32_t layer_count;
	struct ipc_layer_entry layers[IPC_MAX_LAYERS];
};

/*!
 * A big struct that contains all data that is shared to a client, no pointers
 * allowed in this. To get the inputs of a device you go:
 *
 * ```C++
 * struct xrt_input *
 * helper(struct ipc_shared_memory *ism, uint32_t device_id, uint32_t input)
 * {
 * 	uint32_t index = ism->isdevs[device_id]->first_input_index + input;
 * 	return &ism->inputs[index];
 * }
 * ```
 *
 * @ingroup ipc
 */
struct ipc_shared_memory
{
	/*!
	 * The git revision of the service, used by clients to detect version mismatches.
	 */
	char u_git_tag[IPC_VERSION_NAME_LEN];

	/*!
	 * Number of elements in @ref itracks that are populated/valid.
	 */
	uint32_t itrack_count;

	/*!
	 * @brief Array of shared tracking origin data.
	 *
	 * Only @ref itrack_count elements are populated/valid.
	 */
	struct ipc_shared_tracking_origin itracks[XRT_SYSTEM_MAX_DEVICES];

	/*!
	 * Number of elements in @ref isdevs that are populated/valid.
	 */
	uint32_t isdev_count;

	/*!
	 * @brief Array of shared data per device.
	 *
	 * Only @ref isdev_count elements are populated/valid.
	 */
	struct ipc_shared_device isdevs[XRT_SYSTEM_MAX_DEVICES];

	/*!
	 * Various roles for the devices.
	 */
	struct
	{
		int32_t head;
		int32_t left;
		int32_t right;
		int32_t gamepad;

		struct
		{
			int32_t left;
			int32_t right;
		} hand_tracking;
	} roles;

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
			 * xrt_view::display::h_pixels become the recommended
			 * image size for this view.
			 *
			 * @todo doesn't account for overfill for timewarp or
			 * distortion?
			 */
			struct
			{
				uint32_t w_pixels;
				uint32_t h_pixels;
			} display;
		} views[2];

		enum xrt_blend_mode blend_modes[XRT_MAX_DEVICE_BLEND_MODES];
		uint32_t blend_mode_count;
	} hmd;

	struct xrt_input inputs[IPC_SHARED_MAX_INPUTS];

	struct xrt_output outputs[IPC_SHARED_MAX_OUTPUTS];

	struct ipc_shared_binding_profile binding_profiles[IPC_SHARED_MAX_BINDINGS];
	struct xrt_binding_input_pair input_pairs[IPC_SHARED_MAX_INPUTS];
	struct xrt_binding_output_pair output_pairs[IPC_SHARED_MAX_OUTPUTS];

	struct ipc_layer_slot slots[IPC_MAX_SLOTS];

	uint64_t startup_timestamp;
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
	bool io_active;
	uint32_t z_order;
	pid_t pid;
	struct xrt_instance_info info;
};


/*!
 * Arguments for creating swapchains from native images.
 */
struct ipc_arg_swapchain_from_native
{
	uint32_t sizes[IPC_MAX_SWAPCHAIN_HANDLES];
};

/*!
 * Arguments for xrt_device::get_view_poses with two views.
 */
struct ipc_info_get_view_poses_2
{
	struct xrt_fov fovs[2];
	struct xrt_pose poses[2];
	struct xrt_space_relation head_relation;
};
