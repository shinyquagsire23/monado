// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief SimulaVR driver interface.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_svr
 */

#pragma once

#include "xrt/xrt_defines.h"
#ifdef __cplusplus
extern "C" {
#endif

struct svr_display_distortion_polynomial_values
{
	float k1;
	float k3;
	float k5;
	float k7;
	float k9;
};

struct svr_one_display_distortion
{
	float half_fov;
	struct xrt_vec2 display_size_mm;

	struct svr_display_distortion_polynomial_values red, green, blue;
};

struct svr_two_displays_distortion
{
	struct svr_one_display_distortion views[2]; // left, right
};

// Doesn't take possession of *distortion - feel free to free it after.
struct xrt_device *
svr_hmd_create(struct svr_two_displays_distortion *distortion);



#ifdef __cplusplus
}
#endif
