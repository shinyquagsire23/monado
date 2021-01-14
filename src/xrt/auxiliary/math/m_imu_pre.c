// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  IMU pre filter struct.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_math
 */

#include "math/m_api.h"
#include "math/m_imu_pre.h"


void
m_imu_pre_filter_init(struct m_imu_pre_filter *imu, float ticks_to_float_accel, float ticks_to_float_gyro)
{
	imu->accel.gain.x = 1.0;
	imu->accel.gain.y = 1.0;
	imu->accel.gain.z = 1.0;
	imu->gyro._pad = 0.0;
	imu->accel.bias.x = 0.0;
	imu->accel.bias.y = 0.0;
	imu->accel.bias.z = 0.0;
	imu->accel.ticks_to_float = ticks_to_float_accel;

	imu->gyro.gain.x = 1.0;
	imu->gyro.gain.y = 1.0;
	imu->gyro.gain.z = 1.0;
	imu->gyro._pad = 0.0;
	imu->gyro.bias.x = 0.0;
	imu->gyro.bias.y = 0.0;
	imu->gyro.bias.z = 0.0;
	imu->gyro.ticks_to_float = ticks_to_float_gyro;

	imu->transform.v[0] = 1;
	imu->transform.v[1] = 0;
	imu->transform.v[2] = 0;
	imu->transform.v[3] = 0;
	imu->transform.v[4] = 1;
	imu->transform.v[5] = 0;
	imu->transform.v[6] = 0;
	imu->transform.v[7] = 0;
	imu->transform.v[8] = 1;
}

void
m_imu_pre_filter_set_switch_x_and_y(struct m_imu_pre_filter *imu)
{
	imu->transform.v[0] = 0;
	imu->transform.v[1] = 1;
	imu->transform.v[2] = 0;
	imu->transform.v[3] = 1;
	imu->transform.v[4] = 0;
	imu->transform.v[5] = 0;
	imu->transform.v[6] = 0;
	imu->transform.v[7] = 0;
	imu->transform.v[8] = 1;
}

void
m_imu_pre_filter_data(struct m_imu_pre_filter *imu,
                      const struct xrt_vec3_i32 *accel,
                      const struct xrt_vec3_i32 *gyro,
                      struct xrt_vec3 *out_accel,
                      struct xrt_vec3 *out_gyro)
{
	struct m_imu_pre_filter_part fa, fg;
	struct xrt_vec3 a, g;
	struct xrt_matrix_3x3 m;

	fa = imu->accel;
	fg = imu->gyro;
	m = imu->transform;

	a.x = accel->x * fa.ticks_to_float;
	a.y = accel->y * fa.ticks_to_float;
	a.z = accel->z * fa.ticks_to_float;

	g.x = gyro->x * fg.ticks_to_float;
	g.y = gyro->y * fg.ticks_to_float;
	g.z = gyro->z * fg.ticks_to_float;

	a.x = (a.x - fa.bias.x) * fa.gain.x;
	a.y = (a.y - fa.bias.y) * fa.gain.y;
	a.z = (a.z - fa.bias.z) * fa.gain.z;

	g.x = (g.x - fg.bias.x) * fg.gain.x;
	g.y = (g.y - fg.bias.y) * fg.gain.y;
	g.z = (g.z - fg.bias.z) * fg.gain.z;

	math_matrix_3x3_transform_vec3(&m, &a, &a);
	math_matrix_3x3_transform_vec3(&m, &g, &g);

	*out_accel = a;
	*out_gyro = g;
}
