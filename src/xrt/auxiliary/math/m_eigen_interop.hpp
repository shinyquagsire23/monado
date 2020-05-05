// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interoperability helpers connecting internal math types and Eigen.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 */

#pragma once

#ifndef __cplusplus
#error "This header only usable from C++"
#endif

#include "math/m_api.h"

#include <Eigen/Core>
#include <Eigen/Geometry>


/*!
 * @brief Wrap an internal quaternion struct in an Eigen type, const overload.
 *
 * Permits zero-overhead manipulation of `const xrt_quat&` by Eigen routines as
 * if it were a `const Eigen::Quaternionf&`.
 */
static inline Eigen::Map<const Eigen::Quaternionf>
map_quat(const struct xrt_quat &q)
{
	return Eigen::Map<const Eigen::Quaternionf>{&q.x};
}

/*!
 * @brief Wrap an internal quaternion struct in an Eigen type, non-const
 * overload.
 *
 * Permits zero-overhead manipulation of `xrt_quat&` by Eigen routines as if it
 * were a `Eigen::Quaternionf&`.
 */
static inline Eigen::Map<Eigen::Quaternionf>
map_quat(struct xrt_quat &q)
{
	return Eigen::Map<Eigen::Quaternionf>{&q.x};
}


/*!
 * @brief Wrap an internal 3D vector struct in an Eigen type, const overload.
 *
 * Permits zero-overhead manipulation of `const xrt_vec3&` by Eigen routines as
 * if it were a `const Eigen::Vector3f&`.
 */
static inline Eigen::Map<const Eigen::Vector3f>
map_vec3(const struct xrt_vec3 &v)
{
	return Eigen::Map<const Eigen::Vector3f>{&v.x};
}

/*!
 * @brief Wrap an internal 3D vector struct in an Eigen type, non-const
 * overload.
 *
 * Permits zero-overhead manipulation of `xrt_vec3&` by Eigen routines as
 * if it were a `Eigen::Vector3f&`.
 */
static inline Eigen::Map<Eigen::Vector3f>
map_vec3(struct xrt_vec3 &v)
{
	return Eigen::Map<Eigen::Vector3f>{&v.x};
}

/*!
 * @brief Wrap an internal 4x4 matrix struct in an Eigen type, non-const
 * overload.
 *
 * Permits zero-overhead manipulation of `xrt_matrix_4x4&` by Eigen routines as
 * if it were a `Eigen::Matrix4f&`.
 */
static inline Eigen::Map<Eigen::Matrix4f>
map_matrix_4x4(struct xrt_matrix_4x4 &m)
{
	return Eigen::Map<Eigen::Matrix4f>(m.v);
}

/*
 *
 * Pose deconstruction helpers.
 *
 */

/*!
 * Return a Eigen type wrapping a pose's orientation (const).
 */
static inline Eigen::Map<const Eigen::Quaternionf>
orientation(const struct xrt_pose &pose)
{
	return map_quat(pose.orientation);
}

/*!
 * Return a Eigen type wrapping a pose's orientation.
 */
static inline Eigen::Map<Eigen::Quaternionf>
orientation(struct xrt_pose &pose)
{
	return map_quat(pose.orientation);
}

/*!
 * Return a Eigen type wrapping a pose's position (const).
 */
static inline Eigen::Map<const Eigen::Vector3f>
position(const struct xrt_pose &pose)
{
	return map_vec3(pose.position);
}

/*!
 * Return a Eigen type wrapping a pose's position.
 */
static inline Eigen::Map<Eigen::Vector3f>
position(struct xrt_pose &pose)
{
	return map_vec3(pose.position);
}
