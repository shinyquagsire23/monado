// Copyright 2022, Google, Inc.
// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSD-3-Clause
/*!
 * @file
 * @brief Autodiff-safe rotations for Levenberg-Marquardt kinematic optimizer.
 * Copied out of Ceres's `rotation.h` with some modifications.
 * @author Kier Mierle <kier@google.com>
 * @author Sameer Agarwal <sameeragarwal@google.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors may be
//   used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.


template <typename T>
inline void
QuaternionProduct(const Quat<T> &z, const Quat<T> &w, Quat<T> &zw)
{
	// Inplace product is not supported
	assert(&z != &zw);
	assert(&w != &zw);

	assert_quat_length_1(z);
	assert_quat_length_1(w);


	// clang-format off
  zw.w = z.w * w.w - z.x * w.x - z.y * w.y - z.z * w.z;
  zw.x = z.w * w.x + z.x * w.w + z.y * w.z - z.z * w.y;
  zw.y = z.w * w.y - z.x * w.z + z.y * w.w + z.z * w.x;
  zw.z = z.w * w.z + z.x * w.y - z.y * w.x + z.z * w.w;
	// clang-format on
}


template <typename T>
inline void
UnitQuaternionRotatePoint(const Quat<T> &q, const Vec3<T> &pt, Vec3<T> &result)
{
	// clang-format off
  T uv0 = q.y * pt.z - q.z * pt.y;
  T uv1 = q.z * pt.x - q.x * pt.z;
  T uv2 = q.x * pt.y - q.y * pt.x;
  uv0 += uv0;
  uv1 += uv1;
  uv2 += uv2;
  result.x = pt.x + q.w * uv0;
  result.y = pt.y + q.w * uv1;
  result.z = pt.z + q.w * uv2;
  result.x += q.y * uv2 - q.z * uv1;
  result.y += q.z * uv0 - q.x * uv2;
  result.z += q.x * uv1 - q.y * uv0;
	// clang-format on
}

template <typename T>
inline void
UnitQuaternionRotateAndScalePoint(const Quat<T> &q, const Vec3<T> &pt, const T scale, Vec3<T> &result)
{
	T uv0 = q.y * pt.z - q.z * pt.y;
	T uv1 = q.z * pt.x - q.x * pt.z;
	T uv2 = q.x * pt.y - q.y * pt.x;
	uv0 += uv0;
	uv1 += uv1;
	uv2 += uv2;
	result.x = pt.x + q.w * uv0;
	result.y = pt.y + q.w * uv1;
	result.z = pt.z + q.w * uv2;
	result.x += q.y * uv2 - q.z * uv1;
	result.y += q.z * uv0 - q.x * uv2;
	result.z += q.x * uv1 - q.y * uv0;

	result.x *= scale;
	result.y *= scale;
	result.z *= scale;
}


template <typename T>
inline void
AngleAxisToQuaternion(const Vec3<T> angle_axis, Quat<T> &result)
{
	const T &a0 = angle_axis.x;
	const T &a1 = angle_axis.y;
	const T &a2 = angle_axis.z;
	const T theta_squared = a0 * a0 + a1 * a1 + a2 * a2;

	// For points not at the origin, the full conversion is numerically stable.
	if (likely(theta_squared > T(0.0))) {
		const T theta = sqrt(theta_squared);
		const T half_theta = theta * T(0.5);
		const T k = sin(half_theta) / theta;
		result.w = cos(half_theta);
		result.x = a0 * k;
		result.y = a1 * k;
		result.z = a2 * k;
	} else {
		// At the origin, sqrt() will produce NaN in the derivative since
		// the argument is zero.  By approximating with a Taylor series,
		// and truncating at one term, the value and first derivatives will be
		// computed correctly when Jets are used.
		const T k(0.5);
		result.w = T(1.0);
		result.x = a0 * k;
		result.y = a1 * k;
		result.z = a2 * k;
	}
}

