// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal result type for XRT.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

/*!
 * Result type used across Monado.
 *
 * 0 is @ref XRT_SUCCESS, positive values are "special" non-error return codes (like timeout), negative values are
 * errors.
 *
 * @see u_pp_xrt_result
 * @ingroup xrt_iface
 */
typedef enum xrt_result
{
	/*!
	 * The operation succeeded
	 */
	XRT_SUCCESS = 0,

	/*!
	 * The operation was given a timeout and timed out.
	 *
	 * The value 2 picked so it matches VK_TIMEOUT.
	 */
	XRT_TIMEOUT = 2,

	/*!
	 * A problem occurred either with the IPC transport itself, with invalid commands from the client, or with
	 * invalid responses from the server.
	 */
	XRT_ERROR_IPC_FAILURE = -1,

	/*!
	 * Returned when trying to acquire or release an image and there is no image left to acquire/no space in the
	 * queue left to release to
	 */
	XRT_ERROR_NO_IMAGE_AVAILABLE = -2,

	/*!
	 * Other unspecified error related to Vulkan
	 */
	XRT_ERROR_VULKAN = -3,

	/*!
	 * Other unspecified error related to OpenGL
	 */
	XRT_ERROR_OPENGL = -4,

	/*!
	 * The function tried to submit Vulkan commands but received an error.
	 */
	XRT_ERROR_FAILED_TO_SUBMIT_VULKAN_COMMANDS = -5,

	/*!
	 *
	 * Returned when a swapchain create flag is passed that is valid, but
	 * not supported by the main compositor (and lack of support is also
	 * valid).
	 *
	 * For use when e.g. the protected content image flag is requested but
	 * isn't supported.
	 */
	XRT_ERROR_SWAPCHAIN_FLAG_VALID_BUT_UNSUPPORTED = -6,

	/*!
	 * Could not allocate native image buffer(s).
	 */
	XRT_ERROR_ALLOCATION = -7,

	/*!
	 * The pose is no longer active, this happens when the application
	 * tries to get a pose that is no longer active.
	 */
	XRT_ERROR_POSE_NOT_ACTIVE = -8,

	/*!
	 * Creating a fence failed.
	 */
	XRT_ERROR_FENCE_CREATE_FAILED = -9,

	/*!
	 * Getting or giving the native fence handle caused a error.
	 */
	XRT_ERROR_NATIVE_HANDLE_FENCE_ERROR = -10,

	/*!
	 * Multiple not supported on this layer level (IPC, compositor).
	 */
	XRT_ERROR_MULTI_SESSION_NOT_IMPLEMENTED = -11,

	/*!
	 * The requested format is not supported by Monado.
	 */
	XRT_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED = -12,

	/*!
	 * The given config was EGL_NO_CONFIG_KHR and EGL_KHR_no_config_context
	 * is not supported by the display.
	 */
	XRT_ERROR_EGL_CONFIG_MISSING = -13,

	/*!
	 * Failed to initialize threading components.
	 */
	XRT_ERROR_THREADING_INIT_FAILURE = -14,

	/*!
	 * The client has not created a session on this IPC connection,
	 * which is needed for the given command.
	 */
	XRT_ERROR_IPC_SESSION_NOT_CREATED = -15,

	/*!
	 * The client has already created a session on this IPC connection.
	 */
	XRT_ERROR_IPC_SESSION_ALREADY_CREATED = -16,
	/*!
	 * The prober list has not been locked before this call.
	 */
	XRT_ERROR_PROBER_NOT_SUPPORTED = -17,
	/*!
	 * Creating the @ref xrt_prober failed.
	 */
	XRT_ERROR_PROBER_CREATION_FAILED = -18,
	/*!
	 * The prober list is locked (already).
	 */
	XRT_ERROR_PROBER_LIST_LOCKED = -19,
	/*!
	 * The prober list has not been locked before this call.
	 */
	XRT_ERROR_PROBER_LIST_NOT_LOCKED = -20,
	/*!
	 * The probring failed.
	 */
	XRT_ERROR_PROBING_FAILED = -21,
	/*!
	 * Creating a @ref xrt_device failed.
	 */
	XRT_ERROR_DEVICE_CREATION_FAILED = -22,
	/*!
	 * Some D3D error, from code shared between D3D11 and D3D12
	 */
	XRT_ERROR_D3D = -23,
	/*!
	 * Some D3D11 error
	 */
	XRT_ERROR_D3D11 = -24,
	/*!
	 * Some D3D12 error
	 */
	XRT_ERROR_D3D12 = -25,
} xrt_result_t;
