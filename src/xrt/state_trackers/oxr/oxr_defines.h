// Copyright 2018-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared internal defines and enums in the state tracker.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#pragma once

#include "xrt/xrt_compiler.h"


// For corruption and layer checking.
// clang-format off
#define OXR_XR_DEBUG_INSTANCE  (*(uint64_t *)"oxrinst\0")
#define OXR_XR_DEBUG_SESSION   (*(uint64_t *)"oxrsess\0")
#define OXR_XR_DEBUG_SPACE     (*(uint64_t *)"oxrspac\0")
#define OXR_XR_DEBUG_PATH      (*(uint64_t *)"oxrpath\0")
#define OXR_XR_DEBUG_ACTION    (*(uint64_t *)"oxracti\0")
#define OXR_XR_DEBUG_SWAPCHAIN (*(uint64_t *)"oxrswap\0")
#define OXR_XR_DEBUG_ACTIONSET (*(uint64_t *)"oxraset\0")
#define OXR_XR_DEBUG_MESSENGER (*(uint64_t *)"oxrmess\0")
#define OXR_XR_DEBUG_SOURCESET (*(uint64_t *)"oxrsrcs\0")
#define OXR_XR_DEBUG_SOURCE    (*(uint64_t *)"oxrsrc_\0")
#define OXR_XR_DEBUG_HTRACKER  (*(uint64_t *)"oxrhtra\0")
// clang-format on

/*!
 * State of a handle base, to reduce likelihood of going "boom" on
 * out-of-order destruction or other unsavory behavior.
 *
 * @ingroup oxr_main
 */
enum oxr_handle_state
{
	/*! State during/before oxr_handle_init, or after failure */
	OXR_HANDLE_STATE_UNINITIALIZED = 0,

	/*! State after successful oxr_handle_init */
	OXR_HANDLE_STATE_LIVE,

	/*! State after successful oxr_handle_destroy */
	OXR_HANDLE_STATE_DESTROYED,
};

/*!
 * Sub action paths.
 *
 * @ingroup oxr_main
 */
enum oxr_subaction_path
{
	OXR_SUB_ACTION_PATH_USER,
	OXR_SUB_ACTION_PATH_HEAD,
	OXR_SUB_ACTION_PATH_LEFT,
	OXR_SUB_ACTION_PATH_RIGHT,
	OXR_SUB_ACTION_PATH_GAMEPAD,
};

/*!
 * Region of a dpad binding that an input is mapped to
 *
 * @ingroup oxr_main
 */
enum oxr_dpad_region
{
	OXR_DPAD_REGION_CENTER = 0u,
	OXR_DPAD_REGION_UP = (1u << 0u),
	OXR_DPAD_REGION_DOWN = (1u << 1u),
	OXR_DPAD_REGION_LEFT = (1u << 2u),
	OXR_DPAD_REGION_RIGHT = (1u << 3u),
};

/*!
 * Tracks the state of a image that belongs to a @ref oxr_swapchain.
 *
 * @ingroup oxr_main
 */
enum oxr_image_state
{
	OXR_IMAGE_STATE_READY,
	OXR_IMAGE_STATE_ACQUIRED,
	OXR_IMAGE_STATE_WAITED,
};

/*!
 * Internal enum for the type of space, lets us reason about action spaces.
 *
 * @ingroup oxr_main
 */
enum oxr_space_type
{
	OXR_SPACE_TYPE_REFERENCE_VIEW,
	OXR_SPACE_TYPE_REFERENCE_LOCAL,
	OXR_SPACE_TYPE_REFERENCE_LOCAL_FLOOR,
	OXR_SPACE_TYPE_REFERENCE_STAGE,
	OXR_SPACE_TYPE_REFERENCE_UNBOUNDED_MSFT,
	OXR_SPACE_TYPE_REFERENCE_COMBINED_EYE_VARJO,

	OXR_SPACE_TYPE_ACTION,
};
