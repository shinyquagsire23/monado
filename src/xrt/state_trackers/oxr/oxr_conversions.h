// Copyright 2018-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Smaller helper functions to convert between xrt and OpenXR things.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#pragma once

#include "xrt/xrt_defines.h"
#include "xrt/xrt_vulkan_includes.h"
#include "xrt/xrt_openxr_includes.h"


static inline XrSpaceLocationFlags
xrt_to_xr_space_location_flags(enum xrt_space_relation_flags relation_flags)
{
	// clang-format off
	bool valid_ori = (relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0;
	bool tracked_ori = (relation_flags & XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT) != 0;
	bool valid_pos = (relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) != 0;
	bool tracked_pos = (relation_flags & XRT_SPACE_RELATION_POSITION_TRACKED_BIT) != 0;

	bool linear_vel = (relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) != 0;
	bool angular_vel = (relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) != 0;
	// clang-format on

	XrSpaceLocationFlags location_flags = (XrSpaceLocationFlags)0;
	if (valid_ori) {
		location_flags |= XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	}
	if (tracked_ori) {
		location_flags |= XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
	}
	if (valid_pos) {
		location_flags |= XR_SPACE_LOCATION_POSITION_VALID_BIT;
	}
	if (tracked_pos) {
		location_flags |= XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	}
	if (linear_vel) {
		location_flags |= XR_SPACE_VELOCITY_LINEAR_VALID_BIT;
	}
	if (angular_vel) {
		location_flags |= XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
	}

	return location_flags;
}

static inline XrReferenceSpaceType
oxr_ref_space_to_xr(enum oxr_space_type space_type)
{
	switch (space_type) {
	case OXR_SPACE_TYPE_REFERENCE_VIEW: return XR_REFERENCE_SPACE_TYPE_VIEW;
	case OXR_SPACE_TYPE_REFERENCE_LOCAL: return XR_REFERENCE_SPACE_TYPE_LOCAL;
	case OXR_SPACE_TYPE_REFERENCE_LOCAL_FLOOR: return XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT;
	case OXR_SPACE_TYPE_REFERENCE_STAGE: return XR_REFERENCE_SPACE_TYPE_STAGE;
	case OXR_SPACE_TYPE_REFERENCE_UNBOUNDED_MSFT: return XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT;
	case OXR_SPACE_TYPE_REFERENCE_COMBINED_EYE_VARJO: return XR_REFERENCE_SPACE_TYPE_COMBINED_EYE_VARJO;

	case OXR_SPACE_TYPE_ACTION: return XR_REFERENCE_SPACE_TYPE_MAX_ENUM;
	}
	return XR_REFERENCE_SPACE_TYPE_MAX_ENUM;
}

static inline enum oxr_space_type
xr_ref_space_to_oxr(XrReferenceSpaceType space_type)
{
	switch (space_type) {
	case XR_REFERENCE_SPACE_TYPE_VIEW: return OXR_SPACE_TYPE_REFERENCE_VIEW;
	case XR_REFERENCE_SPACE_TYPE_LOCAL: return OXR_SPACE_TYPE_REFERENCE_LOCAL;
	case XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT: return OXR_SPACE_TYPE_REFERENCE_LOCAL_FLOOR;
	case XR_REFERENCE_SPACE_TYPE_STAGE: return OXR_SPACE_TYPE_REFERENCE_STAGE;
	case XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT: return OXR_SPACE_TYPE_REFERENCE_UNBOUNDED_MSFT;
	case XR_REFERENCE_SPACE_TYPE_COMBINED_EYE_VARJO: return OXR_SPACE_TYPE_REFERENCE_COMBINED_EYE_VARJO;

	case XR_REFERENCE_SPACE_TYPE_MAX_ENUM: return (enum oxr_space_type) - 1;
	}

	// wrap around or negative depending on enum data type, invalid value either way.
	return (enum oxr_space_type) - 1;
}
