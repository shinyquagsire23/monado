// Copyright 2018, Philipp Zabel.
// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  WMR and MS HoloLens protocol helpers implementation.
 * @author Philipp Zabel <philipp.zabel@gmail.com>
 * @author nima01 <nima_zero_one@protonmail.com>
 * @ingroup drv_wmr
 */

#include "wmr_protocol.h"


/*
 *
 * WMR and MS HoloLens Sensors protocol helpers
 *
 */

void
vec3_from_hololens_accel(int32_t sample[3][4], int i, struct xrt_vec3 *out_vec)
{
	out_vec->x = (float)sample[0][i] * 0.001f * 1.0f;
	out_vec->y = (float)sample[1][i] * 0.001f * 1.0f;
	out_vec->z = (float)sample[2][i] * 0.001f * 1.0f;
}

void
vec3_from_hololens_gyro(int16_t sample[3][32], int i, struct xrt_vec3 *out_vec)
{
	out_vec->x = (float)(sample[0][8 * i + 0] + //
	                     sample[0][8 * i + 1] + //
	                     sample[0][8 * i + 2] + //
	                     sample[0][8 * i + 3] + //
	                     sample[0][8 * i + 4] + //
	                     sample[0][8 * i + 5] + //
	                     sample[0][8 * i + 6] + //
	                     sample[0][8 * i + 7]) *
	             0.001f * 0.125f;
	out_vec->y = (float)(sample[1][8 * i + 0] + //
	                     sample[1][8 * i + 1] + //
	                     sample[1][8 * i + 2] + //
	                     sample[1][8 * i + 3] + //
	                     sample[1][8 * i + 4] + //
	                     sample[1][8 * i + 5] + //
	                     sample[1][8 * i + 6] + //
	                     sample[1][8 * i + 7]) *
	             0.001f * 0.125f;
	out_vec->z = (float)(sample[2][8 * i + 0] + //
	                     sample[2][8 * i + 1] + //
	                     sample[2][8 * i + 2] + //
	                     sample[2][8 * i + 3] + //
	                     sample[2][8 * i + 4] + //
	                     sample[2][8 * i + 5] + //
	                     sample[2][8 * i + 6] + //
	                     sample[2][8 * i + 7]) *
	             0.001f * 0.125f;
}
