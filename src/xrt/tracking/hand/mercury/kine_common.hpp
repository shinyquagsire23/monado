// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Random common stuff for Mercury kinematic optimizers
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */

#pragma once
#include <array>
#include "xrt/xrt_defines.h"
namespace xrt::tracking::hand::mercury {

// Changing this to double should work but you might need to fix some things.
// Float is faster, and nothing (should) be too big or too small to require double.

// Different from `Scalar` in lm_* templates - those can be `Ceres::Jet`s too.
typedef float HandScalar;

// Used for "2.5D" joint locations, with a 2D ray direction in stereographic-space and a 1D depth relative to the
// middle-proximal joint.
struct vec2_5
{
	xrt_vec2 pos_2d;
	float depth_relative_to_midpxm;

	float confidence_xy;
	float confidence_depth;
};

// Using the compiler to stop me from getting 2D space mixed up with 3D space.
using MLOutput2D = std::array<vec2_5, 21>;

struct one_curl
{
	float value;
	float variance;
};

// Inputs to kinematic optimizers
//!@todo Ask Ryan if adding `= {}` only does something if we do one_frame_one_view bla = {}.
struct one_frame_one_view
{
	bool active = true;
	xrt_quat look_dir;
	float stereographic_radius;
	MLOutput2D keypoints_in_scaled_stereographic;
	one_curl curls[5];
};

struct one_frame_input
{
	one_frame_one_view views[2] = {};
};

namespace Joint21 {
	enum Joint21
	{
		WRIST = 0,

		THMB_MCP = 1,
		THMB_PXM = 2,
		THMB_DST = 3,
		THMB_TIP = 4,

		INDX_PXM = 5,
		INDX_INT = 6,
		INDX_DST = 7,
		INDX_TIP = 8,

		MIDL_PXM = 9,
		MIDL_INT = 10,
		MIDL_DST = 11,
		MIDL_TIP = 12,

		RING_PXM = 13,
		RING_INT = 14,
		RING_DST = 15,
		RING_TIP = 16,

		LITL_PXM = 17,
		LITL_INT = 18,
		LITL_DST = 19,
		LITL_TIP = 20
	};
}

//!@todo These are not backed up by real anthropometry data; they are just guesstimates. Patches welcome!
constexpr HandScalar MIN_HAND_SIZE = 0.06;
constexpr HandScalar STANDARD_HAND_SIZE = 0.09;
constexpr HandScalar MAX_HAND_SIZE = 0.12;

} // namespace xrt::tracking::hand::mercury
