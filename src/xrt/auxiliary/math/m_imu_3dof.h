// Copyright 2013, Fredrik Hultin.
// Copyright 2013, Jakob Bornecrantz.
// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A IMU fusion specially made for 3dof devices.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_math
 */

#pragma once

#include "xrt/xrt_defines.h"


#ifdef __cplusplus
extern "C" {
#endif


#define M_IMU_3DOF_USE_GRAVITY_DUR_300MS (1 << 0)
#define M_IMU_3DOF_USE_GRAVITY_DUR_20MS (1 << 1)


struct m_ff_vec3_f32;

enum m_imu_3dof_state
{
	M_IMU_3DOF_STATE_START = 0,
	M_IMU_3DOF_STATE_RUNNING = 1,
};

struct m_imu_3dof
{
	struct xrt_quat rot; //!< Orientation

	struct
	{
		uint64_t timestamp_ns;
		struct xrt_vec3 gyro;  //!< Angular velocity
		struct xrt_vec3 accel; //!< Acceleration
		float delta_ms;
	} last;

	enum m_imu_3dof_state state;

	int flags;

	// Filter fifos for accelerometer and gyroscope.
	struct m_ff_vec3_f32 *word_accel_ff;
	struct m_ff_vec3_f32 *gyro_ff;

	// gravity correction
	struct
	{
		uint64_t level_timestamp_ns;
		struct xrt_vec3 error_axis;
		float error_angle;
	} grav;
};

void
m_imu_3dof_init(struct m_imu_3dof *f, int flags);

void
m_imu_3dof_close(struct m_imu_3dof *f);

void
m_imu_3dof_add_vars(struct m_imu_3dof *f, void *root, const char *prefix);

void
m_imu_3dof_update(struct m_imu_3dof *f,
                  uint64_t timepoint_ns,
                  const struct xrt_vec3 *accel,
                  const struct xrt_vec3 *gyro);


#ifdef __cplusplus
}
#endif
