// Copyright 2019, Collabora, Ltd.
// Copyright 2016, Sensics, Inc.
// SPDX-License-Identifier: Apache-2.0
/*!
 * @file
 * @brief  Base implementations for math library.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 *
 * Based in part on inc/osvr/Util/EigenQuatExponentialMap.h in OSVR-Core
 */

#include "math/m_api.h"
#include "math/m_eigen_interop.hpp"

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <assert.h>


// anonymous namespace for internal types
namespace {
template <typename Scalar> struct FourthRootMachineEps;
template <> struct FourthRootMachineEps<double>
{
	/// machine epsilon is 1e-53, so fourth root is roughly 1e-13
	static double
	get()
	{
		return 1.e-13;
	}
};
template <> struct FourthRootMachineEps<float>
{
	/// machine epsilon is 1e-24, so fourth root is 1e-6
	static float
	get()
	{
		return 1.e-6f;
	}
};
/// Computes the "historical" (un-normalized) sinc(Theta)
/// (sine(theta)/theta for theta != 0, defined as the limit value of 0
/// at theta = 0)
template <typename Scalar>
inline Scalar
sinc(Scalar theta)
{
	/// fourth root of machine epsilon is recommended cutoff for taylor
	/// series expansion vs. direct computation per
	/// Grassia, F. S. (1998). Practical Parameterization of Rotations
	/// Using the Exponential Map. Journal of Graphics Tools, 3(3),
	/// 29-48. http://doi.org/10.1080/10867651.1998.10487493
	Scalar ret;
	if (theta < FourthRootMachineEps<Scalar>::get()) {
		// taylor series expansion.
		ret = Scalar(1.f) - theta * theta / Scalar(6.f);
		return ret;
	}
	// direct computation.
	ret = std::sin(theta) / theta;
	return ret;
}

/// fully-templated free function for quaternion expontiation
template <typename Derived>
inline Eigen::Quaternion<typename Derived::Scalar>
quat_exp(Eigen::MatrixBase<Derived> const &vec)
{
	EIGEN_STATIC_ASSERT_VECTOR_SPECIFIC_SIZE(Derived, 3);
	using Scalar = typename Derived::Scalar;
	/// Implementation inspired by
	/// Grassia, F. S. (1998). Practical Parameterization of Rotations
	/// Using the Exponential Map. Journal of Graphics Tools, 3(3),
	/// 29–48. http://doi.org/10.1080/10867651.1998.10487493
	///
	/// However, that work introduced a factor of 1/2 which I could not
	/// derive from the definition of quaternion exponentiation and
	/// whose absence thus distinguishes this implementation. Without
	/// that factor of 1/2, the exp and ln functions successfully
	/// round-trip and match other implementations.
	Scalar theta = vec.norm();
	Scalar vecscale = sinc(theta);
	Eigen::Quaternion<Scalar> ret;
	ret.vec() = vecscale * vec;
	ret.w() = std::cos(theta);
	return ret.normalized();
}

/// Taylor series expansion of theta over sin(theta), aka cosecant, for
/// use near 0 when you want continuity and validity at 0.
template <typename Scalar>
inline Scalar
cscTaylorExpansion(Scalar theta)
{
	return Scalar(1) +
	       // theta ^ 2 / 6
	       (theta * theta) / Scalar(6) +
	       // 7 theta^4 / 360
	       (Scalar(7) * theta * theta * theta * theta) / Scalar(360) +
	       // 31 theta^6/15120
	       (Scalar(31) * theta * theta * theta * theta * theta * theta) / Scalar(15120);
}

/// fully-templated free function for quaternion log map.
///
/// Assumes a unit quaternion.
template <typename Scalar>
inline Eigen::Matrix<Scalar, 3, 1>
quat_ln(Eigen::Quaternion<Scalar> const &quat)
{
	// ln q = ( (phi)/(norm of vec) vec, ln(norm of quat))
	// When we assume a unit quaternion, ln(norm of quat) = 0
	// so then we just scale the vector part by phi/sin(phi) to get the
	// result (i.e., ln(qv, qw) = (phi/sin(phi)) * qv )
	Scalar vecnorm = quat.vec().norm();

	// "best for numerical stability" vs asin or acos
	Scalar phi = std::atan2(vecnorm, quat.w());

	// Here is where we compute the coefficient to scale the vector part
	// by, which is nominally phi / std::sin(phi).
	// When the angle approaches zero, we compute the coefficient
	// differently, since it gets a bit like sinc in that we want it
	// continuous but 0 is undefined.
	Scalar phiOverSin = vecnorm < 1e-4 ? cscTaylorExpansion<Scalar>(phi) : (phi / std::sin(phi));
	return quat.vec() * phiOverSin;
}

} // namespace

extern "C" void
math_quat_integrate_velocity(const struct xrt_quat *quat,
                             const struct xrt_vec3 *ang_vel,
                             const float dt,
                             struct xrt_quat *result)
{
	assert(quat != NULL);
	assert(ang_vel != NULL);
	assert(result != NULL);
	assert(dt != 0);


	Eigen::Quaternionf q = map_quat(*quat);
	Eigen::Quaternionf incremental_rotation = quat_exp(map_vec3(*ang_vel) * dt * 0.5f).normalized();
	map_quat(*result) = q * incremental_rotation;
}

extern "C" void
math_quat_finite_difference(const struct xrt_quat *quat0,
                            const struct xrt_quat *quat1,
                            const float dt,
                            struct xrt_vec3 *out_ang_vel)
{
	assert(quat0 != NULL);
	assert(quat1 != NULL);
	assert(out_ang_vel != NULL);
	assert(dt != 0);


	Eigen::Quaternionf inc_quat = map_quat(*quat1) * map_quat(*quat0).conjugate();
	map_vec3(*out_ang_vel) = 2.f * quat_ln(inc_quat);
}
