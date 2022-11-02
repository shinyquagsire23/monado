// Copyright 2022, Collabora, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Test for change-of-basis transformations between left-handed and right-handed coordinate sytems for
 * quaternions.
 * @author Moses Turner <moses@collabora.com>
 */
#include "math/m_api.h"
#include "xrt/xrt_defines.h"
#include <util/u_worker.hpp>
#include <util/u_logging.h>
#include <math/m_space.h>
#include <math/m_vec3.h>


#include "catch/catch.hpp"

#include <random>


#include "math/m_eigen_interop.hpp"
#include <Eigen/Core>


// https://stackoverflow.com/questions/28673777/convert-quaternion-from-right-handed-to-left-handed-coordinate-system


// unity: x=0.3, w=0.95 . results in +40 rotation, which I think is wrong probably, should probably be -40
// openxr, according to us:
void
log_quat(xrt_quat q)
{
	U_LOG_E("xyzw: %f %f %f %f", q.x, q.y, q.z, q.w);
}

float
quat_difference(xrt_quat q1, xrt_quat q2)
{
	// https://math.stackexchange.com/a/90098
	// d(q1,q2)=1−⟨q1,q2⟩2

	float inner_product = (q1.w * q2.w) + (q1.x * q2.x) + (q1.y * q2.y) + (q1.z * q2.z);
	return 1.0 - (inner_product * inner_product);
}

xrt_quat
random_quat()
{
	std::random_device dev;

	auto mt = std::mt19937(dev());
	auto rd = std::normal_distribution<float>(0, 1);

	struct xrt_quat quat = {rd(mt), rd(mt), rd(mt), rd(mt)};

	math_quat_normalize(&quat);
	return quat;
}

//! https://stackoverflow.com/questions/28673777/convert-quaternion-from-right-handed-to-left-handed-coordinate-system
// Same as zldtt_ori_right in lm_main.
void
slow_change_of_basis_lh_to_rh(xrt_quat *in, xrt_quat *out)
{

	xrt_vec3 x = XRT_VEC3_UNIT_X;
	xrt_vec3 z = XRT_VEC3_UNIT_Z;

	math_quat_rotate_vec3(in, &x, &x);
	math_quat_rotate_vec3(in, &z, &z);

	// This is a very squashed change-of-basis from left-handed coordinate systems to right-handed coordinate
	// systems: you multiply everything by (-1 0 0) then negate the X axis.

	x.y *= -1;
	x.z *= -1;

	z.x *= -1;

	math_quat_from_plus_x_z(&x, &z, out);
}

void
fast_change_of_basis_lh_to_rh(xrt_quat *in, xrt_quat *out)
{
	out->x = -in->x;
	out->y = in->y;
	out->z = in->z;
	out->w = -in->w;
}

/*
OpenXR is +X right, +Y up, -Z forward
Unity is X+ Right,  Y+ Up, Z+ Forward

So we're not swapping axes, we're just flipping them. It's the same change of basis as "left hand" to "right hand"
(indeed we chould have implemented left vs right in our optical hand tracking that way), just the "flip" is on the XY
plane not the YZ plane.

Vaguely based on `make_joint_at_matrix_right_hand` from ccdik_main but rotated
*/

void
slow_change_of_basis_unity_to_oxr(xrt_quat *in, xrt_quat *out)
{

	Eigen::Quaternionf q;

	q.w() = in->w;
	q.x() = in->x;
	q.y() = in->y;
	q.z() = in->z;

	Eigen::Matrix3f unity_rotation(q);

	Eigen::Matrix3f mirror_unity_to_openxr = Eigen::Matrix3f::Identity();
	mirror_unity_to_openxr(2, 2) = -1;

	Eigen::Matrix3f intermediate = mirror_unity_to_openxr * unity_rotation;

	intermediate(0, 2) *= -1;
	intermediate(1, 2) *= -1;
	intermediate(2, 2) *= -1;

	Eigen::Quaternionf q_new;

	q_new = intermediate;


	out->w = q_new.w();
	out->x = q_new.x();
	out->y = q_new.y();
	out->z = q_new.z();
}

void
fast_change_of_basis_unity_to_oxr(xrt_quat *in, xrt_quat *out)
{
	out->x = in->x;
	out->y = in->y;
	out->z = -in->z;
	out->w = -in->w;
}

TEST_CASE("QuaternionChangeOfBasis")

{
	U_LOG_E("LH to RH!");
	for (int i = 0; i < 3; i++) {
		xrt_quat q = random_quat();
		xrt_quat q_prime;
		xrt_quat q_prime_fast;
		slow_change_of_basis_lh_to_rh(&q, &q_prime);
		fast_change_of_basis_lh_to_rh(&q, &q_prime_fast);
		log_quat(q);
		log_quat(q_prime);
		log_quat(q_prime_fast);
		// xrt_quat factors;
		// factors.w = q_prime.w / q.w;
		// factors.x = q_prime.x / q.x;
		// factors.y = q_prime.y / q.y;
		// factors.z = q_prime.z / q.z;
		// log_quat(factors);

		CHECK(quat_difference(q_prime, q_prime_fast) < 0.01);
	}

	U_LOG_E("Unity to OpenXR!");
	for (int i = 0; i < 3; i++) {
		xrt_quat q = random_quat();
		if (i == 0) {
			q.x = 0.31;
			q.y = 0;
			q.z = 0;
			q.w = 0.95;
			math_quat_normalize(&q);
		}
		xrt_quat q_prime;
		xrt_quat q_prime_fast;
		slow_change_of_basis_unity_to_oxr(&q, &q_prime);
		fast_change_of_basis_unity_to_oxr(&q, &q_prime_fast);
		log_quat(q);
		log_quat(q_prime);
		log_quat(q_prime_fast);
		// xrt_quat factors;
		// factors.w = q_prime.w / q.w;
		// factors.x = q_prime.x / q.x;
		// factors.y = q_prime.y / q.y;
		// factors.z = q_prime.z / q.z;
		// log_quat(factors);

		CHECK(quat_difference(q_prime, q_prime_fast) < 0.01);
	}
}
