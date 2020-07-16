// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal result type for XRT.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

typedef enum xrt_result
{
	XRT_SUCCESS = 0,
	XRT_ERROR_IPC_FAILURE = -1,
	XRT_ERROR_NO_IMAGE_AVAILABLE = -2,
	XRT_ERROR_VULKAN = -3,
	XRT_ERROR_OPENGL = -4,
	XRT_ERROR_FAILED_TO_SUBMIT_VULKAN_COMMANDS = -5,
} xrt_result_t;
