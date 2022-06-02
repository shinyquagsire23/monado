// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Test C++ quatexpmap interface.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 */

#include "catch/catch.hpp"
#include "math/m_api.h"
#include "math/m_vec3.h"
#include <vector>

using std::vector;

TEST_CASE("m_quatexpmap")
{
	xrt_vec3 axis1 = m_vec3_normalize({4, -7, 3});
	xrt_vec3 axis2 = m_vec3_normalize({-1, -2, -3});
	xrt_vec3 axis3 = m_vec3_normalize({1, -1, 1});
	xrt_vec3 axis4 = m_vec3_normalize({-11, 23, 91});
	SECTION("Test integrate velocity and finite difference mappings")
	{
		vector<xrt_vec3> q1_axes{{axis1, axis2}};
		float q1_angle = (float)GENERATE(M_PI, -M_PI / 6);
		vector<xrt_vec3> vel_axes{{axis3, axis4}};
		float vel_angle = (float)GENERATE(-M_PI, M_PI / 5);
		float dt = (float)GENERATE(0.01, 0.1, 1);

		for (xrt_vec3 q1_axis : q1_axes) {
			for (xrt_vec3 vel_axis : vel_axes) {
				// First orientation q1
				xrt_quat q1{};
				math_quat_from_angle_vector(q1_angle, &q1_axis, &q1);

				// Second orientation q2: q1 rotated by vel_angle*dt radians around its local vel_axis
				xrt_quat q2{};
				xrt_vec3 vel = vel_axis * vel_angle;
				math_quat_integrate_velocity(&q1, &vel, dt, &q2);

				// Global velocity vector from q1 to q2
				xrt_vec3 new_global_vel{};
				math_quat_finite_difference(&q1, &q2, dt, &new_global_vel);

				// Adjust global velocity back to local (w.r.t. q1)
				xrt_quat inv_q1{};
				xrt_vec3 new_vel{};
				math_quat_invert(&q1, &inv_q1);
				math_quat_rotate_derivative(&inv_q1, &new_global_vel, &new_vel);

				INFO("vel=" << vel.x << ", " << vel.y << ", " << vel.z);
				INFO("new_vel=" << new_vel.x << ", " << new_vel.y << ", " << new_vel.z);
				CHECK(m_vec3_len(new_vel - vel) <= 0.001);
			}
		}
	}

	SECTION("Test quat_exp and quat_ln are inverses")
	{
		// We use rotations with less than PI radians as quat_ln will return the negative rotation otherwise
		vector<xrt_vec3> aas = {
		    {0, 0, 0},
		    axis1 * (float)M_PI * 0.01f,
		    axis2 * (float)M_PI * 0.5f,
		    axis3 * (float)M_PI * 0.99f,
		};

		for (xrt_vec3 aa : aas) {
			xrt_quat quat{};
			math_quat_exp(&aa, &quat);

			xrt_vec3 expected_aa{};
			math_quat_ln(&quat, &expected_aa);

			CHECK(m_vec3_len(expected_aa - aa) <= 0.001);
		}
	}

//! @todo Fix quat_exp
#if 0
	SECTION("Test quat_exp(angle_axis) returns the appropriate quaternion")
	{
		float angle = M_PI_2;
		xrt_vec3 axis = axis4;
		xrt_vec3 aa = axis * angle;
		xrt_quat q{};
		math_quat_exp(&aa, &q);

		CHECK(q.x == Approx(axis.x * sin(angle / 2)));
		CHECK(q.y == Approx(axis.y * sin(angle / 2)));
		CHECK(q.z == Approx(axis.z * sin(angle / 2)));
		CHECK(q.w == Approx(cos(angle / 2)));
	}
#endif
}
